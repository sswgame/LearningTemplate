#pragma once
/**
 * @file Workspace/EditorCommandStack.h
 * @brief Editor undo/redo command stack (function-based commands)
 */
#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

#include <functional>

namespace sw
{
	/** @brief Push / undo / redo stack for editor mutations */
	class EditorCommandStack
	{
	public:
		struct Command
		{
			std::string			  label;
			std::function<void()> undo;
			std::function<void()> redo;
		};

		static EditorCommandStack& get();

		void push( Command cmd );
		bool canUndo() const { return _index > 0; }
		bool canRedo() const { return _index < _commands.size(); }
		void undo();
		void redo();
		void clear();

		const std::string& peekUndoLabel() const;
		const std::string& peekRedoLabel() const;

	private:
		EditorCommandStack() = default;

		std::vector<Command> _commands;
		size_t				 _index = 0;
		std::string			 _empty;
	};
} // namespace sw
