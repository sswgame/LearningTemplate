#include "pch.h"

#include "Engine/Utility/CommandStack.h"

namespace sw
{
	void CommandStack::push( Command cmd )
	{
		if ( cmd._undo.isBound() == false || cmd._redo.isBound() == false )
			return;

		if ( _bInsideTransaction != 0 )
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
		_bInsideTransaction = 1;
		_transactionLabel	= label;
		_listPendingTransactionCommand.clear();
	}

	void CommandStack::endTransaction()
	{
		if ( _bInsideTransaction == 0 )
			return;

		_bInsideTransaction = 0;
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

		auto listMergedCommands = sw::make_shared<vector<Command>>( std::move( _listPendingTransactionCommand ) );
		_listPendingTransactionCommand.clear();

		compoundCmd._redo = SW_DELEGATE_LAMBDA( Delegate<void()>, [listMergedCommands]()
		{
			for ( size_t cmdIndex = 0; cmdIndex < listMergedCommands->size(); ++cmdIndex )
			{
				if ( ( *listMergedCommands )[cmdIndex]._redo.isBound() )
					( *listMergedCommands )[cmdIndex]._redo();
			}
		} );

		compoundCmd._undo = SW_DELEGATE_LAMBDA( Delegate<void()>, [listMergedCommands]()
		{
			for ( size_t cmdIndex = listMergedCommands->size(); cmdIndex > 0; --cmdIndex )
			{
				if ( ( *listMergedCommands )[cmdIndex - 1]._undo.isBound() )
					( *listMergedCommands )[cmdIndex - 1]._undo();
			}
		} );

		push( std::move( compoundCmd ) );
	}

	void CommandStack::cancelTransaction()
	{
		_bInsideTransaction = 0;
		_transactionLabel.clear();
		_listPendingTransactionCommand.clear();
	}

	void CommandStack::pushCoalesce( string_view coalesceKey, Command cmd )
	{
		if ( cmd._undo.isBound() == false || cmd._redo.isBound() == false )
			return;

		if ( _bInsideTransaction != 0 )
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
		if ( canUndo() == false )
			return;
		--_index;
		if ( _listCommand[_index]._undo.isBound() )
			_listCommand[_index]._undo();
	}

	void CommandStack::redo()
	{
		_lastCoalesceKey.clear();
		if ( canRedo() == false )
			return;
		if ( _listCommand[_index]._redo.isBound() )
			_listCommand[_index]._redo();
		++_index;
	}

	void CommandStack::clear()
	{
		_listCommand.clear();
		_listPendingTransactionCommand.clear();
		_transactionLabel.clear();
		_lastCoalesceKey.clear();
		_index				= 0;
		_bInsideTransaction = 0;
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
} // namespace sw
