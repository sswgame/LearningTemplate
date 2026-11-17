#include "pch.h"

#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsTableHistory.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/EditorUndoHistory.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"
#include "Editor/Panels/ProjectSettings/Model/ProjectSettingsPage.h"

namespace sw::editor
{
    ProjectSettingsTableHistory::ProjectSettingsTableHistory()
        : _pStack{ make_unique<CommandStack>( EditorUndoHistory::readConfiguredLimit( getEditorToolDefaults() ) ) }
        , _pOwner{ make_unique<UndoStepOwner>() }
    {
    }

    ProjectSettingsTableHistory::~ProjectSettingsTableHistory()
    {
        releaseFocusedCommandStack( _pStack.get() );
    }

    void ProjectSettingsTableHistory::push( ProjectSettingsPage& page, const ProjectSettingsTableMovedDelegate& onMoved )
    {
        ProjectSettingsPage* const pPage    = &page;
        const auto                 makeStep = [this, pPage, onMoved]( bool bUndo )
        {
            return UndoStep::makeTransient( SW_DELEGATE_LAMBDA( Delegate<void()>, [pPage, onMoved, bUndo]()
            {
                const bool bMoved = bUndo ? pPage->_tableDocument.undo() : pPage->_tableDocument.redo();
                if ( bMoved == false )
                    return;
                pPage->refreshDirty();
                if ( onMoved.isBound() )
                    onMoved( *pPage );
            } ),
                                            *_pOwner, 0u ); // 표 글은 페이지의 창 안 기록(`_tableDocument`)이 쥔다 — 이 단계는 가리키기만 한다
        };
        CommandStack::Command command;
        command._label = "Edit " + page._entry._page;
        command._undo  = makeStep( true );
        command._redo  = makeStep( false );
        _pStack->push( std::move( command ) );
    }

    void ProjectSettingsTableHistory::clear()
    {
        _pStack->clear();
    }
} // namespace sw::editor
