#include "pch.h"

#include "Editor/Common/Workspace/Undo/EditorStateTextHistory.h"

namespace sw::editor
{
    EditorStateTextHistory::EditorStateTextHistory( const UndoBufferLimit& limit )
        : _owner{}
        , _stack{ limit }
        , _interaction{}
        , _span{}
        , _source{}
    {
    }

    void EditorStateTextHistory::selectSource( const EditorStateTextSource& source )
    {
        flush( false );
        _source = source;
        resetBaseline();
    }

    void EditorStateTextHistory::notifyEdited( string_view label, bool bInteracting )
    {
        if ( _source._capture.isBound() == false )
            return;
        const optional<DocumentEdit> edit = _span.notifyEdited( label, bInteracting, [this]()
        { return _source._capture(); } );
        syncInteraction();
        if ( edit.has_value() )
            record( *edit );
    }

    void EditorStateTextHistory::flush( bool bInteracting )
    {
        if ( bInteracting || _span.isHolding() == false )
            return;
        const optional<DocumentEdit> edit = _span.finish( _source._capture() );
        syncInteraction(); // 기록 전에 닫는다 — 다음 Ctrl+Z 가 이 단계를 되돌린다
        if ( edit.has_value() )
            record( *edit );
    }

    void EditorStateTextHistory::resetBaseline()
    {
        _span.resetBaseline( _source._capture.isBound() ? _source._capture() : string{} );
        syncInteraction();
    }

    void EditorStateTextHistory::pushStep( string_view label, const Delegate<void()>& undo, const Delegate<void()>& redo, uint64 heldBytes )
    {
        CommandStack::Command command;
        command._label = string{ label };
        command._undo  = UndoStep::makeTransient( undo, _owner, heldBytes );
        command._redo  = UndoStep::makeTransient( redo, _owner, heldBytes );
        _stack.push( std::move( command ) );
    }

    void EditorStateTextHistory::clear()
    {
        _source = {};
        resetBaseline();
        _stack.clear();
    }

    void EditorStateTextHistory::record( const DocumentEdit& edit )
    {
        CommandStack::Command command;
        command._label = edit._label;
        command._undo  = makeRestoreStep( edit._pBefore );
        command._redo  = makeRestoreStep( edit._pAfter );
        _stack.push( std::move( command ) );
    }

    UndoStep EditorStateTextHistory::makeRestoreStep( const shared_ptr<const string>& pText )
    {
        const Delegate<void( string_view )> restore = _source._restore;
        return UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [this, restore, pText]()
        {
            restore( *pText );
            resetBaseline(); // 되돌린 뒤의 글이 다음 편집의 "전" 이다
        } ),
                                        _owner, static_cast<uint64>( pText->size() ) );
    }

    void EditorStateTextHistory::syncInteraction()
    {
        const bool bHolding = _span.isHolding();
        if ( bHolding == _interaction.isOpen() )
            return;
        if ( bHolding )
            _interaction = _stack.openInteraction();
        else
            _interaction.reset();
    }
} // namespace sw::editor
