/**
 * @file EditorCommandStack.cpp
 */
#include "Workspace/EditorCommandStack.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	EditorCommandStack& EditorCommandStack::get()
	{
		static EditorCommandStack s_instance;
		return s_instance;
	}

	void EditorCommandStack::push( Command cmd )
	{
		if ( cmd.undo == nullptr || cmd.redo == nullptr )
			return;

		if ( _index < _commands.size() )
			_commands.erase( _commands.begin() + static_cast<std::ptrdiff_t>( _index ), _commands.end() );

		_commands.push_back( std::move( cmd ) );
		++_index;

		// Cap history
		constexpr size_t kMax = 128;
		if ( _commands.size() > kMax )
		{
			const size_t drop = _commands.size() - kMax;
			_commands.erase( _commands.begin(), _commands.begin() + static_cast<std::ptrdiff_t>( drop ) );
			_index -= drop;
		}
	}

	void EditorCommandStack::undo()
	{
		if ( canUndo() == false )
			return;
		--_index;
		if ( _commands[_index].undo )
			_commands[_index].undo();
	}

	void EditorCommandStack::redo()
	{
		if ( canRedo() == false )
			return;
		if ( _commands[_index].redo )
			_commands[_index].redo();
		++_index;
	}

	void EditorCommandStack::clear()
	{
		_commands.clear();
		_index = 0;
	}

	const std::string& EditorCommandStack::peekUndoLabel() const
	{
		if ( canUndo() == false )
			return _empty;
		return _commands[_index - 1].label;
	}

	const std::string& EditorCommandStack::peekRedoLabel() const
	{
		if ( canRedo() == false )
			return _empty;
		return _commands[_index].label;
	}
} // namespace sw
