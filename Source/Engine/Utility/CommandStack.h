/**
 * @file Utility/CommandStack.h
 * @brief 실행 취소/다시 실행 명령 스택 (에디터·툴 공용)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleUnloadListener.h"

namespace sw
{
    /** @brief 되돌리기 · 다시 하기가 오브젝트에 한 일입니다(`CommandStack::ObjectEditNotice`). */
    enum class ObjectEditKind : uint8
    {
        Modified = 0, ///< 상태를 다시 읽었다
        Recreated,    ///< 원래 id 로 되살렸다
        Destroyed     ///< 없앤다(지연 파괴 — 알림은 없애기 직전)
    };

    /**
     * @class CommandStack
     * @brief 변경에 대한 Push / Undo / Redo 스택입니다. 엔진(`EngineLoop`)이 소유해 모듈 핫 리로드를 넘어 삽니다.
     * @details 명령은 둘로 나뉩니다.
     *          - **데이터 명령** — 코드가 엔진에 있고 나머지는 값(오브젝트 id · 직렬화한 상태)인 명령(`ObjectUndoUtil`). 모듈이 내려가도 남습니다.
     *          - **모듈 명령** — undo · redo 가 모듈 이미지의 코드(람다 · 패널 메서드)인 명령. 그 모듈이 내려가기 전에 `releaseCodeWithin` 이 떼어 냅니다.
     *          UE `FTransaction`(오브젝트 상태를 직렬화해 기록) · Unity `Undo`(직렬화한 객체 상태)가 코드 리로드를 넘는 것과 같은 모양입니다.
     */
    class SW_API CommandStack final : public IModuleUnloadListener
    {
    public:
        /**
         * @struct Command
         * @brief 레이블과 undo/redo 델리게이트를 담는 한 명령입니다.
         * @details 트랜잭션으로 묶인 명령은 `_listChild` 에 안쪽 명령을 그대로 듭니다(undo · redo 는 비어 있고 스택이 차례로 부른다). 안쪽이 보여야
         *          모듈 코드를 쥔 명령을 가릴 수 있습니다.
         */
        struct Command
        {
            string                            _label;
            Delegate<void()>                  _undo;
            Delegate<void()>                  _redo;
            shared_ptr<const vector<Command>> _listChild; ///< 트랜잭션의 안쪽 명령(앞에서 뒤로 redo, 뒤에서 앞으로 undo). 묶음이 아니면 nullptr
        };

        /**
         * @struct ObjectEditNotice
         * @brief 데이터 명령이 오브젝트에 무엇을 했는지 알리는 내용입니다.
         */
        struct ObjectEditNotice
        {
            uint64         _objectId{ 0 };
            ObjectEditKind _kind{ ObjectEditKind::Modified };
        };
        /** @brief 에디터가 다는 알림 처리기입니다(선택 갱신 · 씬 dirty). 모듈 코드라 그 모듈이 내려갈 때 떼어집니다. */
        using ObjectEditListener = Delegate<void( const ObjectEditNotice& )>;

        /** @brief 빈 스택으로 시작합니다. 생성자를 .cpp 에 두는 이유는 `IModuleUnloadListener` 의 주의(vtable 의 집)입니다. */
        CommandStack();

        /** @brief 명령 스택에 새로운 명령을 추가합니다. undo · redo 가 비었거나(트랜잭션 묶음 제외) 실행 중이면 버립니다. */
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
        /** @brief 스택을 비웁니다. 알림 처리기는 그대로 둡니다. 맡긴 내역(`parkHistory`)은 그대로입니다. */
        void clear();
        /**
         * @brief 지금 내역(명령 · 위치)을 맡겨 두고 스택을 비웁니다. 이미 맡긴 것이 있으면 버리고 새로 맡깁니다.
         * @details 에디터가 Play 를 시작할 때 부릅니다 — 플레이 중 편집은 Stop 이 스냅샷으로 되돌리므로 그 동안의 기록은 쓰지 않지만, Play 전 편집은
         *          Stop 뒤에도 되돌릴 수 있어야 합니다(유니티 · 언리얼과 같다). 맡긴 명령도 모듈을 내릴 때 같은 규칙(`releaseCodeWithin`)으로 정리합니다.
         */
        void parkHistory();
        /** @brief 맡긴 내역을 되돌려 놓습니다(지금 내역은 버린다). 맡긴 것이 없으면 비우기만 합니다. Stop 이 씬을 되돌린 뒤 부릅니다. */
        void unparkHistory();
        /** @brief 맡긴 내역이 있으면 true 입니다. */
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

        /** @brief 데이터 명령의 알림 처리기를 답니다. 빈 델리게이트를 넘기면 뗍니다. */
        void setObjectEditListener( ObjectEditListener listener );
        /** @brief 데이터 명령이 오브젝트에 한 일을 처리기에 알립니다. 처리기가 없으면 아무것도 하지 않습니다. */
        void notifyObjectEdit( const ObjectEditNotice& notice ) const;

        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 명령만 떼어 내고, 뗀 명령 수를 반환합니다. 알림 처리기도 그 범위면 뗍니다(수에는 넣지 않는다).
         * @details 모듈 이미지를 내리기 전에 부릅니다(그 모듈 스스로 · 핫 리로드의 안전망). 트랜잭션 묶음은 안쪽에 하나라도 걸리면 **통째로** 뗍니다
         *          (트랜잭션의 반쪽만 되돌리지 않는다). 남은 명령의 순서와 현재 위치(되돌린 것 · 아직인 것의 경계)는 그대로입니다. 데이터 명령은 대상
         *          오브젝트의 상태 전체를 들고 있어, 사이의 명령이 빠져도 그 오브젝트는 같은 상태로 돌아갑니다.
         */
        uint32 releaseCodeWithin( const void* pBegin, const void* pEnd );

        /** @brief 언로드 리스너 목록의 이름입니다. */
        const utf8* getModuleUnloadListenerName() const override { return "undo commands"; }
        /** @brief `releaseCodeWithin` 입니다. 명령은 떼면 그만이라 이미지를 붙들지 않습니다. */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

    private:
        /** @brief undo · redo 하나를 실행합니다. 트랜잭션 묶음이면 안쪽 명령을 차례로(undo 는 거꾸로) 실행합니다. */
        static void execute( const Command& cmd, bool bUndo );
        /** @brief @p cmd(와 그 안쪽)가 [@p pBegin, @p pEnd) 의 코드를 쥐었는지 봅니다. */
        static bool holdsCodeWithin( const Command& cmd, const void* pBegin, const void* pEnd );
        /** @brief 실행할 것이 있는 명령인지 봅니다(undo · redo 둘 다 있거나 트랜잭션 묶음). */
        static bool isExecutable( const Command& cmd );
        /**
         * @brief @p inoutListCommand 에서 [@p pBegin, @p pEnd) 의 코드를 쥔 명령을 떼고 @p inoutIndex(되돌린 것 · 아직인 것의 경계)를 맞춥니다. 뗀 수를 돌려줍니다.
         */
        static uint32 dropCommandsWithin( vector<Command>& inoutListCommand, size_t& inoutIndex, const void* pBegin, const void* pEnd );

    private:
        vector<Command>    _listCommand;
        vector<Command>    _listPendingTransactionCommand;
        vector<Command>    _listParkedCommand; ///< 맡긴 내역(`parkHistory`)
        ObjectEditListener _objectEditListener;
        string             _transactionLabel;
        string             _lastCoalesceKey;
        string             _empty;
        size_t             _index;
        size_t             _parkedIndex; ///< 맡긴 내역의 위치
        /** @brief 중첩 트랜잭션 깊이입니다. 가장 바깥(0 으로 돌아올 때)에서만 하나의 복합 커맨드로 커밋합니다. */
        uint32 _transactionDepth;
        bool   _bIsExecuting;
        bool   _bHistoryParked; ///< 맡긴 내역이 있다
    };
} // namespace sw
