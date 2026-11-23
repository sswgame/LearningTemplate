#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsHistory.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/EditorStateTextHistory.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsPage.h"

namespace sw::editor
{
    namespace
    {
        struct ProjectSettingsHistoryInternal
        {
            static void finishMove( ProjectSettingsPage& page, const ProjectSettingsMovedDelegate& onMoved )
            {
                page.refreshDirty();
                if ( onMoved.isBound() )
                    onMoved( page );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ProjectSettingsHistory::ProjectSettingsHistory()
        : _pHistory{ make_unique<EditorStateTextHistory>( EditorUndoHistory::readConfiguredLimit( getEditorToolDefaults() ) ) }
    {
    }

    ProjectSettingsHistory::~ProjectSettingsHistory() = default; // Ctrl+Z 의 스택은 패널 id 로 찾는다 — 사라질 스택을 풀 일이 없다

    CommandStack& ProjectSettingsHistory::getStack()
    {
        return _pHistory->getStack();
    }

    void ProjectSettingsHistory::selectPage( ProjectSettingsPage& page, const ProjectSettingsMovedDelegate& onMoved )
    {
        ProjectSettingsPage* const pPage = &page;
        EditorStateTextSource      source;
        if ( page._entry.isTable() == false && page._document.isOpen() )
        {
            source._capture = SW_DELEGATE_LAMBDA( Delegate<string()>, [pPage]()
            { return pPage->_document.captureText(); } );
            source._restore = SW_DELEGATE_LAMBDA( Delegate<void( string_view )>, [pPage, onMoved]( string_view text )
            {
                if ( pPage->_document.applyText( text ) )
                    ProjectSettingsHistoryInternal::finishMove( *pPage, onMoved );
            } );
        }
        _pHistory->selectSource( source );
    }

    void ProjectSettingsHistory::notifyEdited( string_view label, bool bInteracting )
    {
        _pHistory->notifyEdited( label, bInteracting );
    }

    void ProjectSettingsHistory::flush( bool bInteracting )
    {
        _pHistory->flush( bInteracting );
    }

    void ProjectSettingsHistory::pushTableStep( ProjectSettingsPage& page, const ProjectSettingsMovedDelegate& onMoved )
    {
        ProjectSettingsPage* const pPage    = &page;
        const auto                 makeMove = [pPage, onMoved]( bool bUndo )
        {
            return SW_DELEGATE_LAMBDA( Delegate<void()>, [pPage, onMoved, bUndo]()
            {
                const bool bMoved = bUndo ? pPage->_tableDocument.undo() : pPage->_tableDocument.redo();
                if ( bMoved )
                    ProjectSettingsHistoryInternal::finishMove( *pPage, onMoved );
            } );
        };
        StringBuilder<constant::buffer::kMax256> label;
        label.appendFormat( "Edit %#", page._entry._page );
        _pHistory->pushStep( label.view(), makeMove( true ), makeMove( false ), 0u ); // 자료는 표 문서의 기록이 쥔다
    }

    void ProjectSettingsHistory::clear()
    {
        _pHistory->clear();
    }
} // namespace sw::editor
