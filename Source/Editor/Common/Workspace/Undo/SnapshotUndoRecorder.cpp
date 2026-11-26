#include "pch.h"

#include "Editor/Common/Workspace/Undo/SnapshotUndoRecorder.h"

#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

namespace sw::editor
{
    SnapshotUndoRecorder::SnapshotUndoRecorder()
        : _pOwnStack{}
        , _pStack{ nullptr }
        , _pOwner{ make_unique<UndoStepOwner>() }
        , _lastCoalesceKey{}
        , _lastIndex{ 0 }
    {
    }

    SnapshotUndoRecorder::~SnapshotUndoRecorder()
    {
        _pOwner.reset(); // 주인이 먼저 사라져 스택이 이 기록기의 명령을 뺀다 — 자기 스택은 그 뒤에 사라진다
    }

    void SnapshotUndoRecorder::bindStack( CommandStack& stack )
    {
        if ( _pStack == &stack )
            return;
        clear();
        _pStack = &stack;
        _pOwnStack.reset();
    }

    CommandStack& SnapshotUndoRecorder::getStack()
    {
        if ( _pStack == nullptr )
        {
            _pOwnStack = make_unique<CommandStack>( EditorUndoHistory::readConfiguredLimit( getEditorToolDefaults() ) );
            _pStack    = _pOwnStack.get();
        }
        return *_pStack;
    }

    bool SnapshotUndoRecorder::canCoalesce( string_view coalesceKey )
    {
        if ( coalesceKey.empty() || _pStack == nullptr || string_view( _lastCoalesceKey ) != coalesceKey )
            return false;
        return _pStack->getCurrentIndex() == _lastIndex && _pStack->getCommandCount() == _lastIndex && _lastIndex != 0;
    }

    void SnapshotUndoRecorder::push( string_view label, string_view coalesceKey, const Delegate<void()>& undo, const Delegate<void()>& redo, uint64 heldBytes )
    {
        CommandStack&         stack = getStack();
        CommandStack::Command command;
        command._label = string{ label };
        command._undo  = UndoStep::makeTransient( undo, *_pOwner, heldBytes );
        command._redo  = UndoStep::makeTransient( redo, *_pOwner, heldBytes );
        stack.push( std::move( command ) );
        _lastCoalesceKey = string{ coalesceKey };
        _lastIndex       = stack.getCurrentIndex();
    }

    void SnapshotUndoRecorder::endCoalescing()
    {
        _lastCoalesceKey.clear();
    }

    void SnapshotUndoRecorder::clear()
    {
        unique_ptr<UndoStepOwner> pReleased = std::move( _pOwner );
        _pOwner                             = make_unique<UndoStepOwner>();
        pReleased.reset(); // 앞 주인이 사라지며 그 명령이 스택에서 빠진다(다른 모델의 명령은 남는다)
        _lastCoalesceKey.clear();
        _lastIndex = 0;
    }

    void SnapshotUndoRecorder::cancelLast()
    {
        if ( _pStack == nullptr || _pStack->getCurrentIndex() != _lastIndex || _lastIndex == 0 || _pStack->undo() == false )
            return;
        _pStack->discardRedo();
        _lastCoalesceKey.clear();
        _lastIndex = 0;
    }

    bool SnapshotUndoRecorder::undo()
    {
        _lastCoalesceKey.clear();
        return _pStack != nullptr && _pStack->undo();
    }

    bool SnapshotUndoRecorder::redo()
    {
        _lastCoalesceKey.clear();
        return _pStack != nullptr && _pStack->redo();
    }

    bool SnapshotUndoRecorder::canUndo() const
    {
        return _pStack != nullptr && _pStack->canUndo();
    }

    bool SnapshotUndoRecorder::canRedo() const
    {
        return _pStack != nullptr && _pStack->canRedo();
    }
} // namespace sw::editor
