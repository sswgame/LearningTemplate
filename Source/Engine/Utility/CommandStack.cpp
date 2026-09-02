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
        // 중첩 호출은 최외곽 트랜잭션에 합류시킨다. 여기서 목록을 비우면 바깥이 쌓아둔
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

        // 최외곽이 끝날 때만 실제로 커밋한다.
        --_transactionDepth;
        if ( _transactionDepth != 0 )
            return;

        if ( _listPendingTransactionCommand.empty() )
            return;

        if ( _listPendingTransactionCommand.size() == 1 )
        {
            push( std::move( _listPendingTransactionCommand[0] ) );
            _listPendingTransactionCommand.clear();
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
        if ( cmd._undo.isBound() == false || cmd._redo.isBound() == false )
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
            // 첫 실행 시점의 undo는 보존하고, 최신 redo와 레이블만 교체
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
        _lastCoalesceKey.clear();
        if ( targetIndex > _listCommand.size() )
            targetIndex = _listCommand.size();

        while ( _index > targetIndex && canUndo() )
            undo();
        while ( _index < targetIndex && canRedo() )
            redo();
    }
} // namespace sw
