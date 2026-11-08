#include "pch.h"

#include "Editor/Panels/Settings/Model/ShortcutsViewModel.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    EditorInputActions& ShortcutsViewModel::getInputActions()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>();
    }

    const EditorCommandRegistry& ShortcutsViewModel::getCommandRegistry()
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
    }

    uint32 ShortcutsViewModel::countModifiedShortcuts()
    {
        return static_cast<uint32>( getInputActions().countModifiedShortcuts() );
    }

    bool ShortcutsViewModel::rebindShortcut( string_view action, Key key, uint8 modifierMask, hashed_string& outConflictAction )
    {
        return getInputActions().rebindShortcut( action, key, modifierMask, outConflictAction );
    }

    bool ShortcutsViewModel::swapShortcut( string_view action, string_view otherAction, Key key, uint8 modifierMask )
    {
        return getInputActions().swapShortcut( action, otherAction, key, modifierMask );
    }

    void ShortcutsViewModel::resetShortcut( string_view action )
    {
        getInputActions().resetShortcut( action );
    }

    void ShortcutsViewModel::resetAllShortcuts()
    {
        getInputActions().resetAllShortcuts();
    }

    void ShortcutsViewModel::setCommandsSuspended( bool bSuspended )
    {
        EditorContext* pContext = editor::findSubsystem<EditorContext>(); // 선택: 종료 경로에서 컨텍스트가 먼저 사라진다
        if ( pContext != nullptr )
            pContext->getSubsystem<EditorInputActions>().setCommandsSuspended( bSuspended );
    }
} // namespace sw::editor
