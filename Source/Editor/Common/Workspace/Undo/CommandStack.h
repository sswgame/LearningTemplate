/**
 * @file Editor/Common/Workspace/Undo/CommandStack.h
 * @brief 실행 취소와 다시 실행 명령 스택입니다(에디터와 툴 공용).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/DelegateSlot.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

namespace sw
{
    class Archive;

    /** @brief 되돌리기와 다시 하기가 오브젝트에 한 일입니다(`CommandStack::ObjectEditEvent`). */
    enum class ObjectEditKind : uint8
    {
        Modified = 0, ///< 상태를 다시 읽었다
        Recreated,    ///< 원래 id 로 복원했다
        Destroyed     ///< 없앤다(지연 파괴이며, 알림은 없애기 직전에 보낸다)
    };

    /**
     * @struct UndoBufferLimit
     * @brief 되돌리기 스택 하나의 한도입니다 — 명령 수와 쥔 바이트(넘으면 가장 오래된 명령부터 버린다). 값은 `Config/Editor/editortooldefaults.json` 이다(`EditorUndoHistory::readConfiguredLimit`).
     * @details 예전에는 개수 상수(128)뿐이라 문서 글 · 스냅샷을 쥔 단계가 쌓이면 메모리가 끝없이 늘 수 있었다(감사 E-13). 언리얼 에디터 설정 Undo Buffer Size(MB)와 같은 자리다.
     */
    struct UndoBufferLimit
    {
        uint32 _maxCommandCount; ///< 명령 수 한도(1 이상)
        uint64 _maxBytes;        ///< 쥔 바이트 한도(1 이상 — 명령 이름 · 인자 · 코드 단계가 쥔 자료)
    };

    class CommandStack;

    /**
     * @class CommandStackInteractionScope
     * @brief 스택 하나에 연 상호작용(`CommandStack::openInteraction`)의 범위형 핸들입니다 — 사라지거나 `reset` 하면 닫는다. 옮길 수 있고 복사할 수 없다.
     * @details 예전에는 여닫기(`beginInteraction` · `endInteraction`)를 부르는 쪽이 짝지었다 — 기즈모가 끌기 도중 사라지면(모듈 리로드 · 창 닫기) 닫지 못해 그 스택이
     *          되돌리기를 영영 거절했다. 스택이 먼저 사라지면 핸들은 아무것도 하지 않는다(스택의 생존 표지를 함께 쥔다). 언리얼 `FScopedTransaction` 과 같은 범위형이다.
     */
    class SW_EDITOR_API CommandStackInteractionScope final
    {
    public:
        CommandStackInteractionScope() = default;
        ~CommandStackInteractionScope() { reset(); }
        CommandStackInteractionScope( const CommandStackInteractionScope& )            = delete;
        CommandStackInteractionScope& operator=( const CommandStackInteractionScope& ) = delete;
        CommandStackInteractionScope( CommandStackInteractionScope&& other ) noexcept
            : _pStack{ std::move( other._pStack ) }
        {
        }
        CommandStackInteractionScope& operator=( CommandStackInteractionScope&& other ) noexcept
        {
            if ( this != &other )
            {
                reset();
                _pStack = std::move( other._pStack );
            }
            return *this;
        }

        /** @brief 상호작용을 쥐고 있으면 true 입니다(스택이 사라졌으면 false). */
        bool isOpen() const { return _pStack != nullptr && *_pStack != nullptr; }
        /** @brief 쥔 상호작용을 닫습니다. 쥔 것이 없거나 스택이 사라졌으면 아무것도 하지 않는다. */
        void reset();

    private:
        friend class CommandStack;
        explicit CommandStackInteractionScope( shared_ptr<CommandStack*> pStack )
            : _pStack{ std::move( pStack ) }
        {
        }

    private:
        shared_ptr<CommandStack*> _pStack; ///< 스택의 생존 표지(`CommandStack::_pAlive`) — 스택이 사라지면 가리키는 값이 nullptr 이다
    };

    /**
     * @class CommandStack
     * @brief 변경에 대한 Push, Undo, Redo 스택입니다. 메인 스택은 `EditorUndoHistory`(편집기 범위)가 소유해 범위에 노출하고, 문서 패널은 문서마다 자기 스택을 멤버로 든다(`EditorDocumentPanel::getDocumentCommandStack`).
     * @details 단계는 데이터다(`UndoStep` — 함수 이름 해시 + 인자 바이트). 그래서 편집기 모듈을 리로드해도 기록이 넘어간다. 언리얼의 `FTransaction`
     *          (오브젝트 상태를 직렬화해 기록)과 유니티의 `Undo` 와 같은 구조다. 패널 인스턴스에 묶인 코드 단계를 가진 명령은 맡길 때 버리고, 그 코드가 든
     *          코드 단계는 주인(`UndoStepOwner` — 패널)이 사라질 때 그 단계를 받은 스택에서 빠진다(스택이 주인의 사라짐을 구독한다).
     */
    class SW_EDITOR_API CommandStack final
    {
    public:
        /**
         * @struct Command
         * @brief 레이블과 undo/redo 단계를 담는 한 명령입니다.
         * @details 트랜잭션으로 묶인 명령은 `_listChild` 에 안쪽 명령을 그대로 가진다(undo 와 redo 는 비어 있고 스택이 차례로 부른다). 안쪽이 보여야
         *          모듈 코드를 가진 명령을 가려낼 수 있다.
         */
        struct Command
        {
            string                            _label;
            editor::UndoStep                  _undo;
            editor::UndoStep                  _redo;
            shared_ptr<const vector<Command>> _listChild; ///< 트랜잭션의 안쪽 명령이다(앞에서 뒤로 redo, 뒤에서 앞으로 undo). 묶음이 아니면 nullptr
        };

        /**
         * @struct ObjectEditEvent
         * @brief 데이터 명령이 오브젝트에 무엇을 했는지 알리는 내용입니다.
         */
        struct ObjectEditEvent
        {
            uint64         _objectID{ 0 };
            ObjectEditKind _kind{ ObjectEditKind::Modified };
        };
        /** @brief 에디터가 등록하는 알림 처리기입니다(선택 갱신, 씬 dirty). 모듈 코드라 그 모듈이 언로드될 때 분리됩니다. */
        using ObjectEditListener = Delegate<void( const ObjectEditEvent& )>;

        /** @brief 빈 스택으로 시작합니다. 한도는 필수다(기본값이 없다 — 설정에서 읽는다). */
        explicit CommandStack( const UndoBufferLimit& limit );
        /** @brief 살아 있는 스택 목록에서 내려갑니다. */
        ~CommandStack();
        CommandStack( const CommandStack& )            = delete;
        CommandStack& operator=( const CommandStack& ) = delete;

        /** @brief 명령 스택에 새로운 명령을 추가합니다. undo 와 redo 가 비었거나(트랜잭션 묶음 제외) 실행 중이면 버립니다. */
        void push( Command cmd );
        /** @brief 복합 트랜잭션을 시작합니다. */
        void beginTransaction( string_view label = "" );
        /** @brief 트랜잭션을 종료하고 수집된 명령들을 단일 복합 명령으로 커밋합니다. */
        void endTransaction();
        /** @brief 트랜잭션을 취소하고 수집된 명령들을 버립니다. */
        void cancelTransaction();
        /** @brief 트랜잭션 진행 여부를 반환합니다. */
        bool isInsideTransaction() const { return _transactionDepth != 0; }
        /**
         * @brief 여러 프레임에 걸친 상호작용(기즈모 끌기 · 문서 위젯 끌기 — `DocumentEditSpan`)을 엽니다. 돌려준 핸들이 닫을 때까지 되돌리기 · 다시 하기 · 이동을 거절한다.
         * @details 상호작용은 시작 전 상태를 쥐고 끝에 한 단계를 올린다. 그 사이에 되돌리면 쥔 "전" 이 낡아 끝의 단계가 되돌린 편집을 다시 덮고 다시 하기 기록을 자른다(감사 E-2).
         *          언리얼 `UTransBuffer::CanUndo` 가 열린 트랜잭션이 있으면 거절하는 것과 같은 자리다. 닫기는 핸들만 한다(`CommandStackInteractionScope` — 짝을 잊을 수 없다, 중첩은 센다).
         */
        [[nodiscard]] CommandStackInteractionScope openInteraction();
        /** @brief 트랜잭션이나 상호작용이 열려 있으면 true 입니다 — 이 동안 `undo` · `redo` · `jumpTo` 는 거절한다. */
        bool isEditOpen() const { return _transactionDepth != 0 || _interactionDepth != 0; }

        /** @brief 실행 취소 가능 여부를 반환합니다. 편집이 열려 있으면(`isEditOpen`) false 입니다. */
        bool canUndo() const;
        /** @brief 다시 실행 가능 여부를 반환합니다. 편집이 열려 있으면(`isEditOpen`) false 입니다. */
        bool canRedo() const;
        /** @brief 이전 명령을 취소합니다. 할 것이 없거나 실행 중이거나 편집이 열려 있으면 거절하고 false 입니다. */
        bool undo();
        /** @brief 취소한 명령을 다시 실행합니다. 할 것이 없거나 실행 중이거나 편집이 열려 있으면 거절하고 false 입니다. */
        bool redo();
        /** @brief 지금 위치 뒤의 명령(다시 하기)을 버립니다 — 편집을 거절해 되돌린 단계를 없던 일로 한다(`SnapshotUndoRecorder::cancelLast`). 실행 중이면 아무것도 하지 않는다. */
        void discardRedo();
        /** @brief 스택을 비웁니다. 알림 처리기는 그대로 둡니다. 보관해 둔 내역(`suspendHistory`)도 그대로입니다. */
        void clear();
        /**
         * @brief 현재 내역(명령과 위치)을 보관해 두고 스택을 비웁니다. 이미 보관한 것이 있으면 버리고 새로 보관합니다.
         * @details 에디터가 Play 를 시작할 때 부른다. 플레이 중 편집은 Stop 이 스냅샷으로 되돌리므로 그 동안의 기록은 쓰지 않지만, Play 전 편집은
         *          Stop 뒤에도 되돌릴 수 있어야 한다(유니티, 언리얼과 같다). 보관한 명령도 주인이 사라질 때 같은 규칙(`removeOwnedBy`)으로 정리한다.
         */
        void suspendHistory();
        /** @brief 보관한 내역을 되돌려 놓습니다(현재 내역은 버립니다). 보관한 것이 없으면 비우기만 합니다. Stop 이 씬을 되돌린 뒤 부릅니다. */
        void resumeHistory();
        /** @brief 보관한 내역이 있으면 true 입니다. */
        bool hasSuspendedHistory() const { return _bHistorySuspended; }
        /** @brief 취소할 명령의 레이블을 반환합니다. */
        const string& peekUndoLabel() const;
        /** @brief 다시 실행할 명령의 레이블을 반환합니다. */
        const string& peekRedoLabel() const;

        /** @brief 스택에 기록된 총 명령 수를 반환합니다. */
        size_t getCommandCount() const { return _listCommand.size(); }
        /** @brief 현재 실행 위치 인덱스를 반환합니다 (0..getCommandCount()). */
        size_t getCurrentIndex() const { return _index; }
        /** @brief 특정 인덱스의 명령 정보를 반환합니다. */
        const Command& getCommand( size_t index ) const { return _listCommand[index]; }
        /** @brief @p cmd(와 그 안쪽)가 쥔 바이트입니다(이름 · 단계). 한도(`UndoBufferLimit`)가 세는 값과 같다 — 시나리오가 거래 하나의 크기를 본다(`Editor.LastUndoBytes`). */
        static uint64 measureHeldBytes( const Command& cmd );
        /** @brief 특정 인덱스 위치로 연속 undo/redo를 실행하여 점프합니다. 편집이 열려 있으면(`isEditOpen`) 거절합니다. */
        void jumpTo( size_t targetIndex );

        /** @brief 데이터 명령의 알림 처리기를 겁니다. 칸 주인은 하나다(이미 걸려 있으면 단언) — 구독이 풀리면 빈다. */
        [[nodiscard]] Subscription bindObjectEditListener( ObjectEditListener listener ) { return _objectEditListener.bind( std::move( listener ) ); }
        /** @brief 알림 처리기가 걸려 있으면 true 입니다(모듈 언로드 훑기가 비웠으면 false — 새 모듈이 다시 건다). */
        bool hasObjectEditListener() const { return _objectEditListener.isBound(); }
        /** @brief 데이터 명령이 오브젝트에 한 일을 처리기에 알립니다. 처리기가 없으면 아무것도 하지 않습니다. */
        void notifyObjectEdit( const ObjectEditEvent& event ) const;

        /**
         * @brief 주인이 @p ownerID 인 코드 단계를 가진 명령을 빼고 뺀 수를 반환합니다. 트랜잭션 묶음은 안쪽에 하나라도 있으면 **통째로** 뺀다.
         * @details 남은 명령의 순서와 현재 위치(되돌린 것과 아직 되돌리지 않은 것의 경계)는 그대로다. 데이터 명령은 대상 오브젝트의 상태 전체를 가지고 있어,
         *          사이의 명령이 빠져도 그 오브젝트는 같은 상태로 돌아간다.
         */
        uint32 removeOwnedBy( uint64 ownerID );

        /**
         * @brief 기록(명령 · 위치 · 보관한 내역)을 씁니다. 코드 단계를 가진 명령은 빼고 위치를 맞춘다. 진행 중인 트랜잭션은 쓰지 않는다.
         * @details 편집기 인스턴스가 내려갈 때 호스트의 맡김 칸(`IModuleStateStore`)에 맡긴다 — 리로드 · 백엔드 교체 뒤의 새 인스턴스가 `load` 로 되찾는다.
         */
        void save( Archive& outArchive ) const;
        /** @brief `save` 가 쓴 기록으로 바꿉니다. 읽지 못하면 빈 스택으로 두고 false 입니다. 알림 처리기는 그대로입니다. */
        [[nodiscard]] bool load( Archive& archive );

    private:
        friend class CommandStackInteractionScope;
        /** @brief 핸들이 상호작용 하나를 닫습니다. */
        void closeInteraction();

        /** @brief @p cmd(안쪽 명령까지)의 코드 단계 주인마다 사라짐을 한 번 구독합니다(`push`). */
        void subscribeOwners( const Command& cmd );
        /** @brief 주인이 사라졌다 — 그 명령을 빼고 구독을 놓습니다. */
        void onOwnerReleased( uint64 ownerID );

    private:
        /** @brief undo · redo 하나를 실행합니다. 트랜잭션 묶음이면 안쪽 명령을 차례로(undo 는 거꾸로) 실행합니다. */
        void execute( const Command& cmd, bool bUndo );
        /** @brief @p cmd(와 그 안쪽)가 모두 데이터 단계면 true 입니다(맡길 수 있다). */
        static bool isPersistable( const Command& cmd );
        /** @brief @p cmd(와 그 안쪽)에 주인이 @p ownerID 인 코드 단계가 있는지 확인합니다. */
        static bool isOwnedBy( const Command& cmd, uint64 ownerID );
        /** @brief 한도(`UndoBufferLimit`)를 넘으면 가장 오래된 명령부터 버리고 위치를 맞춥니다. 가장 새 명령 하나는 남긴다. */
        void evictOverLimit();
        /** @brief 실행할 것이 있는 명령인지 확인합니다(undo 와 redo 가 둘 다 있거나 트랜잭션 묶음). */
        static bool isExecutable( const Command& cmd );
        /** @brief @p inoutListCommand 에서 주인이 @p ownerID 인 명령을 빼고 @p inoutIndex 를 맞춥니다. 뺀 수를 반환합니다. */
        static uint32 dropOwnedBy( vector<Command>& inoutListCommand, size_t& inoutIndex, uint64 ownerID );

    private:
        UndoBufferLimit                  _limit;
        vector<Command>                  _listCommand;
        vector<Command>                  _listPendingTransactionCommand;
        vector<Command>                  _listSuspendedCommand; ///< 보관한 내역(`suspendHistory`)
        DelegateSlot<ObjectEditListener> _objectEditListener;
        string                           _transactionLabel;
        string                           _empty;
        size_t                           _index;
        size_t                           _suspendedIndex; ///< 보관한 내역의 위치
        /** @brief 중첩 트랜잭션 깊이입니다. 가장 바깥(0 으로 돌아올 때)에서만 하나의 복합 커맨드로 커밋합니다. */
        uint32 _transactionDepth;
        uint32 _interactionDepth; ///< 열린 상호작용 수(`beginInteraction`)
        uint8  _bIsExecuting      : 1;
        uint8  _bHistorySuspended : 1; ///< 보관한 내역이 있다
        /** @struct OwnerSubscription @brief 코드 단계를 받은 주인 하나의 사라짐 구독입니다. */
        struct OwnerSubscription
        {
            uint64       _ownerID;
            Subscription _subscription;
        };
        shared_ptr<CommandStack*> _pAlive;                ///< 생존 표지 — 상호작용 핸들이 쥐고, 스택이 사라지면 nullptr 로 비운다
        vector<OwnerSubscription> _listOwnerSubscription; ///< 맨 끝 — 먼저 사라져 주인 쪽 알림 목록에서 빠진다
    };
} // namespace sw
