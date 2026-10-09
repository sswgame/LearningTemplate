#include "pch.h"

#include "Engine/Utility/CommandStack.h"

namespace sw
{
    CommandStack::CommandStack()
        : _listCommand{}
        , _listPendingTransactionCommand{}
        , _listParkedCommand{}
        , _objectEditListener{}
        , _transactionLabel{}
        , _lastCoalesceKey{}
        , _empty{}
        , _index{ 0 }
        , _parkedIndex{ 0 }
        , _transactionDepth{ 0 }
        , _bIsExecuting{ false }
        , _bHistoryParked{ false }
    {
    }

    void CommandStack::push( Command cmd )
    {
        if ( isExecutable( cmd ) == false || _bIsExecuting )
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
        // 중첩 호출은 가장 바깥 트랜잭션에 합친다. 여기서 목록을 비우면 바깥이 쌓아 둔 기록이 사라진다.
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

        // 트랜잭션 레이블은 명령이 몇 개든 그 트랜잭션의 이름이다("Move 3 objects" 로 묶었는데 명령이 하나여도 그 이름).
        if ( _listPendingTransactionCommand.size() == 1 )
        {
            Command singleCmd = std::move( _listPendingTransactionCommand[0] );
            _listPendingTransactionCommand.clear();
            if ( _transactionLabel.empty() == false )
                singleCmd._label = _transactionLabel;
            push( std::move( singleCmd ) );
            return;
        }

        Command compoundCmd;
        compoundCmd._label     = _transactionLabel.empty() == false
                                   ? _transactionLabel
                                   : _listPendingTransactionCommand[0]._label;
        compoundCmd._listChild = sw::make_shared<const vector<Command>>( std::move( _listPendingTransactionCommand ) );
        _listPendingTransactionCommand.clear();
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
        // 재진입(undo/redo 콜백 안의 push)을 여기서도 막는다. 막지 않으면 `push` 는 거절되는데 coalesce 키만 남아, 다음 병합이
        // 상관없는 지난 명령의 redo 를 바꾼다.
        if ( isExecutable( cmd ) == false || _bIsExecuting )
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
        _bIsExecuting = true;
        execute( _listCommand[_index], true );
        _bIsExecuting = false;
    }

    void CommandStack::redo()
    {
        _lastCoalesceKey.clear();
        if ( canRedo() == false || _bIsExecuting )
            return;
        const size_t targetIndex = _index;
        ++_index;
        _bIsExecuting = true;
        execute( _listCommand[targetIndex], false );
        _bIsExecuting = false;
    }

    void CommandStack::setObjectEditListener( ObjectEditListener listener )
    {
        _objectEditListener = std::move( listener );
    }

    void CommandStack::notifyObjectEdit( const ObjectEditNotice& notice ) const
    {
        if ( _objectEditListener.isBound() )
            _objectEditListener( notice );
    }

    uint32 CommandStack::releaseCodeWithin( const void* pBegin, const void* pEnd )
    {
        if ( _objectEditListener.isCodeWithin( pBegin, pEnd ) )
            _objectEditListener = {};

        uint32 droppedCount = dropCommandsWithin( _listCommand, _index, pBegin, pEnd );
        droppedCount += dropCommandsWithin( _listParkedCommand, _parkedIndex, pBegin, pEnd );

        size_t keptPendingCount{ 0 };
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingTransactionCommand.size(); ++pendingIndex )
        {
            if ( holdsCodeWithin( _listPendingTransactionCommand[pendingIndex], pBegin, pEnd ) )
            {
                ++droppedCount;
                continue;
            }
            if ( keptPendingCount != pendingIndex )
                _listPendingTransactionCommand[keptPendingCount] = std::move( _listPendingTransactionCommand[pendingIndex] );
            ++keptPendingCount;
        }
        _listPendingTransactionCommand.resize( keptPendingCount );

        // 병합 대상이 바뀌었을 수 있다 — 다음 병합 push 가 엉뚱한 명령을 고치지 않게 끊는다.
        if ( droppedCount > 0 )
            _lastCoalesceKey.clear();
        return droppedCount;
    }

    uint32 CommandStack::dropCommandsWithin( vector<Command>& inoutListCommand, size_t& inoutIndex, const void* pBegin, const void* pEnd )
    {
        uint32 droppedCount{ 0 };
        size_t keptCount{ 0 };
        size_t keptBeforeIndex{ 0 };
        for ( size_t commandIndex = 0; commandIndex < inoutListCommand.size(); ++commandIndex )
        {
            if ( holdsCodeWithin( inoutListCommand[commandIndex], pBegin, pEnd ) )
            {
                ++droppedCount;
                continue;
            }
            if ( commandIndex < inoutIndex )
                ++keptBeforeIndex;
            if ( keptCount != commandIndex )
                inoutListCommand[keptCount] = std::move( inoutListCommand[commandIndex] );
            ++keptCount;
        }
        inoutListCommand.resize( keptCount );
        inoutIndex = keptBeforeIndex;
        return droppedCount;
    }

    uint32 CommandStack::onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped )
    {
        (void)outKeepImageMapped;
        return releaseCodeWithin( pBegin, pEnd );
    }

    void CommandStack::execute( const Command& cmd, bool bUndo )
    {
        if ( cmd._listChild != nullptr )
        {
            const vector<Command>& listChild = *cmd._listChild;
            if ( bUndo )
            {
                for ( size_t childIndex = listChild.size(); childIndex > 0; --childIndex )
                {
                    execute( listChild[childIndex - 1], true );
                }
            }
            else
            {
                for ( const Command& child : listChild )
                {
                    execute( child, false );
                }
            }
            return;
        }

        const Delegate<void()>& step = bUndo ? cmd._undo : cmd._redo;
        if ( step.isBound() )
            step();
    }

    bool CommandStack::holdsCodeWithin( const Command& cmd, const void* pBegin, const void* pEnd )
    {
        if ( cmd._undo.isCodeWithin( pBegin, pEnd ) || cmd._redo.isCodeWithin( pBegin, pEnd ) )
            return true;
        if ( cmd._listChild == nullptr )
            return false;
        for ( const Command& child : *cmd._listChild )
        {
            if ( holdsCodeWithin( child, pBegin, pEnd ) )
                return true;
        }
        return false;
    }

    bool CommandStack::isExecutable( const Command& cmd )
    {
        const bool bHasBothSteps = cmd._undo.isBound() && cmd._redo.isBound();
        const bool bIsCompound   = cmd._listChild != nullptr && cmd._listChild->empty() == false;
        return bHasBothSteps || bIsCompound;
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

    void CommandStack::parkHistory()
    {
        _listParkedCommand = std::move( _listCommand );
        _parkedIndex       = _index;
        _bHistoryParked    = true;
        clear();
    }

    void CommandStack::unparkHistory()
    {
        clear();
        if ( _bHistoryParked == false )
            return;
        _listCommand = std::move( _listParkedCommand );
        _index       = _parkedIndex;
        _listParkedCommand.clear();
        _parkedIndex    = 0;
        _bHistoryParked = false;
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
        // 재진입 플래그를 여기서도 본다. undo/redo 콜백 안에서 부르면 아래 `undo()` 가 그 플래그 때문에 아무것도 하지 않아 `_index` 가
        // 줄지 않고 `while` 이 끝나지 않는다.
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
