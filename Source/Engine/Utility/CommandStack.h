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

namespace sw
{
    /** @brief 변경에 대한 Push / Undo / Redo 스택 */
    class SW_API CommandStack
    {
    public:
        /** @brief 레이블과 undo/redo 델리게이트를 담는 한 명령 */
        struct Command
        {
            string           _label;
            Delegate<void()> _undo;
            Delegate<void()> _redo;
        };

        /** @brief 빈 스택으로 시작합니다. */
        CommandStack() = default;

        /** @brief 명령 스택에 새로운 명령을 추가합니다. */
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
        /** @brief 스택을 초기화합니다. */
        void clear();
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

    private:
        vector<Command> _listCommand;
        vector<Command> _listPendingTransactionCommand;
        string          _transactionLabel;
        string          _lastCoalesceKey;
        string          _empty;
        size_t          _index{ 0 };
        /** @brief 중첩 트랜잭션 깊이. 최외곽(0 으로 복귀)에서만 하나의 복합 커맨드로 커밋합니다. */
        uint32 _transactionDepth{ 0 };
        bool   _bIsExecuting{ false };
    };
} // namespace sw
