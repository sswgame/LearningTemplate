/**
 * @file CommandStack.h
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
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    /** @brief 되돌리기와 다시 하기가 오브젝트에 한 일입니다(`CommandStack::ObjectEditNotice`). */
    enum class ObjectEditKind : uint8
    {
        Modified = 0, ///< 상태를 다시 읽었다
        Recreated,    ///< 원래 id 로 복원했다
        Destroyed     ///< 없앤다(지연 파괴이며, 알림은 없애기 직전에 보낸다)
    };

    /**
     * @class CommandStack
     * @brief 변경에 대한 Push, Undo, Redo 스택입니다. 엔진(`Engine`)이 소유해 모듈 핫 리로드를 넘어 삽니다.
     * @details 명령은 둘로 나뉜다.
     *          - **데이터 명령**: 코드는 엔진에 있고 나머지는 값(오브젝트 id, 직렬화한 상태)인 명령이다(`ObjectUndoUtil`). 모듈이 언로드되어도 남는다.
     *          - **모듈 명령**: undo 와 redo 가 모듈 라이브러리의 코드(람다, 패널 메서드)인 명령이다. 그 모듈이 언로드되기 전에 `removeCodeWithin` 이 분리한다.
     *          언리얼의 `FTransaction`(오브젝트 상태를 직렬화해 기록)과 유니티의 `Undo`(직렬화한 객체 상태)가 코드 리로드를 넘는 것과 같은 구조다.
     */
    class SW_API CommandStack final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. 초기화 · 종료할 것이 없고 수명(서비스 범위 — 엔진 범위보다 먼저 서고 나중에 사라진다)만 컬렉션이 정한다. */
        static constexpr const utf8* kSubsystemName = "CommandStack";

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /**
         * @struct Command
         * @brief 레이블과 undo/redo 델리게이트를 담는 한 명령입니다.
         * @details 트랜잭션으로 묶인 명령은 `_listChild` 에 안쪽 명령을 그대로 가진다(undo 와 redo 는 비어 있고 스택이 차례로 부른다). 안쪽이 보여야
         *          모듈 코드를 가진 명령을 가려낼 수 있다.
         */
        struct Command
        {
            string                            _label;
            Delegate<void()>                  _undo;
            Delegate<void()>                  _redo;
            shared_ptr<const vector<Command>> _listChild; ///< 트랜잭션의 안쪽 명령이다(앞에서 뒤로 redo, 뒤에서 앞으로 undo). 묶음이 아니면 nullptr
        };

        /**
         * @struct ObjectEditNotice
         * @brief 데이터 명령이 오브젝트에 무엇을 했는지 알리는 내용입니다.
         */
        struct ObjectEditNotice
        {
            uint64         _objectID{ 0 };
            ObjectEditKind _kind{ ObjectEditKind::Modified };
        };
        /** @brief 에디터가 등록하는 알림 처리기입니다(선택 갱신, 씬 dirty). 모듈 코드라 그 모듈이 언로드될 때 분리됩니다. */
        using ObjectEditListener = Delegate<void( const ObjectEditNotice& )>;

        /** @brief 빈 스택으로 시작합니다. 생성자를 .cpp 에 두는 이유는 `ModuleLibraryUtil::registerUnloadHandler` 의 주의(vtable 의 집)입니다. */
        CommandStack();

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
        /** @brief 동일한 coalesceKey로 연속 push될 때 최초 undo를 보존하고 최신 redo로 병합합니다. */
        void pushCoalesce( string_view coalesceKey, Command cmd );

        /** @brief 실행 취소 가능 여부를 반환합니다. */
        bool canUndo() const;
        /** @brief 다시 실행 가능 여부를 반환합니다. */
        bool canRedo() const;
        /** @brief 이전 명령을 취소합니다. */
        void undo();
        /** @brief 취소한 명령을 다시 실행합니다. */
        void redo();
        /** @brief 스택을 비웁니다. 알림 처리기는 그대로 둡니다. 보관해 둔 내역(`parkHistory`)도 그대로입니다. */
        void clear();
        /**
         * @brief 현재 내역(명령과 위치)을 보관해 두고 스택을 비웁니다. 이미 보관한 것이 있으면 버리고 새로 보관합니다.
         * @details 에디터가 Play 를 시작할 때 부른다. 플레이 중 편집은 Stop 이 스냅샷으로 되돌리므로 그 동안의 기록은 쓰지 않지만, Play 전 편집은
         *          Stop 뒤에도 되돌릴 수 있어야 한다(유니티, 언리얼과 같다). 보관한 명령도 모듈을 언로드할 때 같은 규칙(`removeCodeWithin`)으로 정리한다.
         */
        void parkHistory();
        /** @brief 보관한 내역을 되돌려 놓습니다(현재 내역은 버립니다). 보관한 것이 없으면 비우기만 합니다. Stop 이 씬을 되돌린 뒤 부릅니다. */
        void unparkHistory();
        /** @brief 보관한 내역이 있으면 true 입니다. */
        bool hasParkedHistory() const { return _bHistoryParked; }
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
        /** @brief 특정 인덱스 위치로 연속 undo/redo를 실행하여 점프합니다. */
        void jumpTo( size_t targetIndex );

        /** @brief 데이터 명령의 알림 처리기를 겁니다. 칸 주인은 하나다(이미 걸려 있으면 단언) — 구독이 풀리면 빈다. */
        [[nodiscard]] Subscription bindObjectEditListener( ObjectEditListener listener ) { return _objectEditListener.bind( std::move( listener ) ); }
        /** @brief 알림 처리기가 걸려 있으면 true 입니다(모듈 언로드 훑기가 비웠으면 false — 새 모듈이 다시 건다). */
        bool hasObjectEditListener() const { return _objectEditListener.isBound(); }
        /** @brief 데이터 명령이 오브젝트에 한 일을 처리기에 알립니다. 처리기가 없으면 아무것도 하지 않습니다. */
        void notifyObjectEdit( const ObjectEditNotice& notice ) const;

        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 명령만 분리하고, 분리한 명령 수를 반환합니다. 알림 처리기도 그 범위면 분리합니다(수에는 넣지 않습니다).
         * @details 모듈 라이브러리를 언로드하기 전에 부른다(그 모듈 스스로, 핫 리로드의 안전망). 트랜잭션 묶음은 안쪽에 하나라도 걸리면 **통째로** 분리한다
         *          (트랜잭션의 반쪽만 되돌리지 않는다). 남은 명령의 순서와 현재 위치(되돌린 것과 아직 되돌리지 않은 것의 경계)는 그대로다. 데이터 명령은 대상
         *          오브젝트의 상태 전체를 가지고 있어, 사이의 명령이 빠져도 그 오브젝트는 같은 상태로 돌아간다.
         */
        uint32 removeCodeWithin( const void* pBegin, const void* pEnd );

        /** @brief `removeCodeWithin` 입니다. 명령은 분리하면 그만이라 이미지를 붙들지 않습니다. */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepLibraryMapped );

    private:
        /** @brief undo · redo 하나를 실행합니다. 트랜잭션 묶음이면 안쪽 명령을 차례로(undo 는 거꾸로) 실행합니다. */
        static void execute( const Command& cmd, bool bUndo );
        /** @brief @p cmd(와 그 안쪽)가 [@p pBegin, @p pEnd) 의 코드를 가지고 있는지 확인합니다. */
        static bool holdsCodeWithin( const Command& cmd, const void* pBegin, const void* pEnd );
        /** @brief 실행할 것이 있는 명령인지 확인합니다(undo 와 redo 가 둘 다 있거나 트랜잭션 묶음). */
        static bool isExecutable( const Command& cmd );
        /**
         * @brief @p inoutListCommand 에서 [@p pBegin, @p pEnd) 의 코드를 가진 명령을 분리하고 @p inoutIndex(되돌린 것과 아직 되돌리지 않은 것의 경계)를 맞춥니다. 분리한 수를 반환합니다.
         */
        static uint32 dropCommandsWithin( vector<Command>& inoutListCommand, size_t& inoutIndex, const void* pBegin, const void* pEnd );

    private:
        vector<Command>                  _listCommand;
        vector<Command>                  _listPendingTransactionCommand;
        vector<Command>                  _listParkedCommand; ///< 보관한 내역(`parkHistory`)
        DelegateSlot<ObjectEditListener> _objectEditListener;
        string                           _transactionLabel;
        string                           _lastCoalesceKey;
        string                           _empty;
        size_t                           _index;
        size_t                           _parkedIndex; ///< 보관한 내역의 위치
        /** @brief 중첩 트랜잭션 깊이입니다. 가장 바깥(0 으로 돌아올 때)에서만 하나의 복합 커맨드로 커밋합니다. */
        uint32 _transactionDepth;
        bool   _bIsExecuting;
        bool   _bHistoryParked; ///< 보관한 내역이 있다

    private:
        /** @brief 모듈 언로드 처리기 구독(`ModuleLibraryUtil::registerUnloadHandler`)입니다. 맨 끝 멤버라 먼저 사라져, 다른 멤버가 사라지는 동안 불리지 않는다. */
        SW_MODULE_UNLOAD_HANDLER( &CommandStack::onModuleUnloading, "undo commands" );
    };
} // namespace sw
