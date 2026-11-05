#include "pch.h"

#include "Editor/Panels/Settings/Model/ShortcutCaptureModel.h"

#include "Core/Log/Logger.h"

#include "Engine/Input/RawInputEvent.h"

namespace sw::editor
{
    ShortcutCaptureModel::ShortcutCaptureModel()
        : _captureAction{}
        , _conflictAction{}
        , _pendingKey{ Key::Unknown }
        , _pendingModifier{ ModifierKey::None }
    {
    }

    void ShortcutCaptureModel::beginCapture( IShortcutBindings& bindings, const string& action )
    {
        SW_ASSERT( action.empty() == false && "only a command with an input action has a shortcut row" );
        _captureAction   = action;
        _conflictAction  = {};
        _pendingKey      = Key::Unknown;
        _pendingModifier = ModifierKey::None;
        bindings.setCommandsSuspended( true );
    }

    void ShortcutCaptureModel::submitChord( IShortcutBindings& bindings, Key key, uint8 modifierMask )
    {
        if ( isWaitingForChord() == false )
            return;
        if ( key == Key::Escape && modifierMask == ModifierKey::None )
        {
            finish( bindings );
            return;
        }
        hashed_string conflictAction{};
        if ( bindings.rebindShortcut( _captureAction, key, modifierMask, conflictAction ) || conflictAction.empty() )
        {
            finish( bindings );
            return;
        }
        _conflictAction  = conflictAction.c_str();
        _pendingKey      = key;
        _pendingModifier = modifierMask;
    }

    void ShortcutCaptureModel::confirmReplace( IShortcutBindings& bindings )
    {
        if ( hasConflict() == false )
            return;
        if ( bindings.swapShortcut( _captureAction, _conflictAction, _pendingKey, _pendingModifier ) == false )
        {
            SW_LOG_WARNING_ID( "ShortcutsPanel.CouldNotSwapShortcut", "Could not swap the shortcuts of '%#' and '%#'", _captureAction.c_str(), _conflictAction.c_str() );
            return;
        }
        finish( bindings );
    }

    void ShortcutCaptureModel::cancel( IShortcutBindings& bindings )
    {
        if ( _captureAction.empty() )
            return;
        finish( bindings );
    }

    void ShortcutCaptureModel::resetShortcut( IShortcutBindings& bindings, const string& action )
    {
        cancel( bindings );
        bindings.resetShortcut( action );
    }

    void ShortcutCaptureModel::resetAllShortcuts( IShortcutBindings& bindings )
    {
        cancel( bindings );
        bindings.resetAllShortcuts();
    }

    void ShortcutCaptureModel::finish( IShortcutBindings& bindings )
    {
        _captureAction   = {};
        _conflictAction  = {};
        _pendingKey      = Key::Unknown;
        _pendingModifier = ModifierKey::None;
        bindings.setCommandsSuspended( false );
    }
} // namespace sw::editor
