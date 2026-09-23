#include "pch.h"

#include "Engine/Utility/CommandStack.h"

namespace sw
{
    void CommandStack::push( Command cmd )
    {
        if ( cmd._undo.isBound() == false || cmd._redo.isBound() == false || _bIsExecuting )
            return;

        if ( _transactionDepth != 0 )
        {
            _listPendingTransactionCommand.push_back( std::move( cmd ) );
            return;
        }

        _lastCoalesceKey.clear();

        if ( _index < _listCommand.size() )
            _listCommand.erase( _listCommand.begin() + static_cast<std::ptrdiff_t>( _index ), _listCommand.end() );

        _listCommand.push_back( std::move( cmd ) );
        ++_index;

        constexpr size_t kMax = 128;
        if ( _listCommand.size() > kMax )
        {
            const size_t drop = _listCommand.size() - kMax;
            _listCommand.erase( _listCommand.begin(), _listCommand.begin() + static_cast<std::ptrdiff_t>( drop ) );
            _index -= drop;
        }
    }

    void CommandStack::beginTransaction( string_view label )
    {
        // 중첩 호출은 가장 바깥 트랜잭션에 합친다. 여기서 목록을 비우면 바깥이 쌓아 둔
        // Undo 기록이 통째로 사라진다.
        if ( _transactionDepth == 0 )
        {
            _transactionLabel = label;
            _listPendingTransactionCommand.clear();
        }
        ++_transactionDepth;
    }

    void CommandStack::endTransaction()
    {
        if ( _transactionDepth == 0 )
            return;

        // 가장 바깥이 끝날 때만 실제로 커밋한다.
        --_transactionDepth;
        if ( _transactionDepth != 0 )
            return;

        if ( _listPendingTransactionCommand.empty() )
            return;

        if ( _listPendingTransactionCommand.size() == 1 )
        {
            // 트랜잭션 레이블은 **명령이 몇 개든** 그 트랜잭션의 이름이다. 예전에는 명령 하나로 끝난
            // 트랜잭션만 안쪽 명령의 레이블을 그대로 썼다. "Move 3 objects" 로 묶었는데 실제로
            // 명령이 하나 나오면 실행 취소 메뉴에 "Set position" 이 떴다.
            Command singleCmd = std::move( _listPendingTransactionCommand[0] );
            _listPendingTransactionCommand.clear();
            if ( _transactionLabel.empty() == false )
                singleCmd._label = _transactionLabel;
            push( std::move( singleCmd ) );
            return;
        }

        Command compoundCmd;
        compoundCmd._label = _transactionLabel.empty() == false
                               ? _transactionLabel
                               : _listPendingTransactionCommand[0]._label;

        auto listMergedCommand = sw::make_shared<vector<Command>>( std::move( _listPendingTransactionCommand ) );
        _listPendingTransactionCommand.clear();

        compoundCmd._redo = SW_DELEGATE_LAMBDA( Delegate<void()>, [listMergedCommand]()
        {
            for ( size_t cmdIndex = 0; cmdIndex < listMergedCommand->size(); ++cmdIndex )
            {
                if ( ( *listMergedCommand )[cmdIndex]._redo.isBound() )
                    ( *listMergedCommand )[cmdIndex]._redo();
            }
        } );

        compoundCmd._undo = SW_DELEGATE_LAMBDA( Delegate<void()>, [listMergedCommand]()
        {
            for ( size_t cmdIndex = listMergedCommand->size(); cmdIndex > 0; --cmdIndex )
            {
                if ( ( *listMergedCommand )[cmdIndex - 1]._undo.isBound() )
                    ( *listMergedCommand )[cmdIndex - 1]._undo();
            }
        } );

        push( std::move( compoundCmd ) );
    }

    void CommandStack::cancelTransaction()
    {
        // 취소는 중첩 깊이와 무관하게 전체 트랜잭션을 버린다.
        _transactionDepth = 0;
        _transactionLabel.clear();
        _listPendingTransactionCommand.clear();
    }

    void CommandStack::pushCoalesce( string_view coalesceKey, Command cmd )
    {
        // **`_bIsExecuting` 을 여기서도 본다.** 이 플래그는 undo/redo 콜백이 자기 자신을 새 명령으로
        // 기록하지 못하게 막는 재진입 방지인데, 예전에는 `push` 만 보고 이쪽은 보지 않았다.
        // 그러면 콜백 안에서 병합 push 를 했을 때 `push` 는 거절당하는데 **coalesce 키는 그대로
        // 남아**, 그다음의 정상적인 병합 push 가 같은 키를 보고 `_index - 1` 의 명령(아무
        // 상관 없는 지난 명령)의 redo 를 바꿔 버렸다. 되돌린 뒤 다시 실행하면 다른 일이 일어난다.
        // 여기서 막고 나면 아래 `push` 가 거절될 이유가 남지 않으므로, 키를 적는 것도 안전해진다.
        if ( cmd._undo.isBound() == false || cmd._redo.isBound() == false || _bIsExecuting )
            return;

        if ( _transactionDepth != 0 )
        {
            _listPendingTransactionCommand.push_back( std::move( cmd ) );
            return;
        }

        const bool bCanCoalesce = coalesceKey.empty() == false &&
                                  _lastCoalesceKey == coalesceKey &&
                                  _index > 0 &&
                                  _index <= _listCommand.size();

        if ( bCanCoalesce )
        {
            // 처음 실행할 때의 undo 는 보존하고, 최신 redo 와 레이블만 바꾼다
            _listCommand[_index - 1]._redo = std::move( cmd._redo );
            if ( cmd._label.empty() == false )
                _listCommand[_index - 1]._label = std::move( cmd._label );
            return;
        }

        push( std::move( cmd ) );
        _lastCoalesceKey = coalesceKey;
    }

    bool CommandStack::canUndo() const
    {
        return _index > 0;
    }

    bool CommandStack::canRedo() const
    {
        return _index < _listCommand.size();
    }

    void CommandStack::undo()
    {
        _lastCoalesceKey.clear();
        if ( canUndo() == false || _bIsExecuting )
            return;
        --_index;
        if ( _listCommand[_index]._undo.isBound() )
        {
            _bIsExecuting = true;
            _listCommand[_index]._undo();
            _bIsExecuting = false;
        }
    }

    void CommandStack::redo()
    {
        _lastCoalesceKey.clear();
        if ( canRedo() == false || _bIsExecuting )
            return;
        const size_t targetIndex = _index;
        ++_index;
        if ( _listCommand[targetIndex]._redo.isBound() )
        {
            _bIsExecuting = true;
            _listCommand[targetIndex]._redo();
            _bIsExecuting = false;
        }
    }

    void CommandStack::clear()
    {
        _listCommand.clear();
        _listPendingTransactionCommand.clear();
        _transactionLabel.clear();
        _lastCoalesceKey.clear();
        _index            = 0;
        _transactionDepth = 0;
    }

    const string& CommandStack::peekUndoLabel() const
    {
        if ( canUndo() == false )
            return _empty;
        return _listCommand[_index - 1]._label;
    }

    const string& CommandStack::peekRedoLabel() const
    {
        if ( canRedo() == false )
            return _empty;
        return _listCommand[_index]._label;
    }

    void CommandStack::jumpTo( size_t targetIndex )
    {
        // **재진입 플래그를 여기서도 본다. 여기서는 값이 아니라 진행이 걸려 있다.** `push` ·
        // `pushCoalesce` · `undo` · `redo` 는 모두 `_bIsExecuting` 을 보는데 이 함수만 보지 않았다.
        // undo/redo 콜백 안에서 `jumpTo` 를 부르면 아래의 `undo()` 가 그 플래그 때문에 **아무것도
        // 하지 않고 돌아오고**, `_index` 가 줄지 않으므로 `while` 조건이 영원히 참이다.
        // 틀린 답이 아니라 **멈춘 에디터**가 된다.
        if ( _bIsExecuting )
            return;

        _lastCoalesceKey.clear();
        if ( targetIndex > _listCommand.size() )
            targetIndex = _listCommand.size();

        while ( _index > targetIndex && canUndo() )
            undo();
        while ( _index < targetIndex && canRedo() )
            redo();
    }
} // namespace sw
