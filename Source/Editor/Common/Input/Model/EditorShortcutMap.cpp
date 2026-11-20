/**
 * @file EditorShortcutMap.cpp
 * @brief 에디터 입력 맵의 소유 · 다시 만들기 · 포커스 레이어 · 발화 판정 · 사용자 재배치입니다(ImGui 없음).
 */
#include "pch.h"

#include "Editor/Common/Input/Model/EditorShortcutMap.h"

#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Input/DebugKeyActions.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputRemapSerializer.h"

namespace sw::editor
{
    EditorShortcutMap::EditorShortcutMap()
        : _map{}
        , _focusedPanelLayer{}
        , _syncedMapGeneration{ constant::invalid_index::kUint32 }
    {
    }

    bool EditorShortcutMap::connect( InputManager& input )
    {
        _map.setInputManager( &input );
        // 게임 뷰가 포커스여도(F8 플레이 빠져나오기 · F9 스크린샷) 에디터 도구 창이 포커스여도 키를 본다. 셸 · UI 맵은 도구 포커스 동안 키를 보지 않는다.
        _map.setKeyboardFocusIgnored( true );
        _map.setToolKeyboardReader( true );
        if ( rebuildMap() )
            return true;
        _map.setInputManager( nullptr );
        return false;
    }

    void EditorShortcutMap::disconnect()
    {
        _map.setInputManager( nullptr );
    }

    void EditorShortcutMap::rebuildIfExtensionsChanged()
    {
        if ( EditorExtensionRegistry<EditorInputMapRegistration>::getList().getGeneration() != _syncedMapGeneration && rebuildMap() == false )
            SW_LOG_ERROR_ID( "Editor.EditorInputActionsEditorInputMap2", "EditorInputActions: the editor input map could not be rebuilt after an extension module changed - see above" );
    }

    bool EditorShortcutMap::rebuildMap()
    {
        using MapRegistry    = EditorExtensionRegistry<EditorInputMapRegistration>;
        _syncedMapGeneration = MapRegistry::getList().getGeneration();
        _focusedPanelLayer   = {};

        const string& basePath = getEditorToolDefaults()._editorInputMap.get();
        if ( _map.loadFromAsset( AssetID{ basePath } ) == false )
        {
            SW_LOG_ERROR_ID( "Editor.EditorInputActionsEditorInputMap", "EditorInputActions: the editor input map '%#' could not be read - the editor has no shortcuts", basePath.c_str() );
            return false;
        }
        bool bValid = true;
        for ( uint32 index = 0; index < MapRegistry::getCount(); ++index )
        {
            const EditorInputMapRegistration& row = MapRegistry::getAt( index );
            if ( _map.appendFromAsset( AssetID{ row._pAssetPath } ) == false )
            {
                SW_LOG_ERROR_ID( "Editor.EditorInputActionsExtensionInputMap", "EditorInputActions: the extension input map '%#' (%#) could not be appended (listed above)", row._pAssetPath, row._pID );
                bValid = false;
            }
        }
        // 기본 맵 안의 겹침도 데이터 오류다 — 한 번 눌러 두 명령이 돈다. 더한 파일의 겹침은 `appendFromAsset` 가 이미 알렸다.
        for ( const hashed_string& action : _map.getActionNames() )
        {
            for ( uint32 bindIndex = 0; bindIndex < _map.getBindingCount( action ); ++bindIndex )
            {
                hashed_string other{};
                if ( _map.findSameLayerInputConflict( action, bindIndex, other ) == false )
                    continue;
                SW_LOG_ERROR_ID( "Editor.EditorInputActionsShareInputLayer", "EditorInputActions: '%#' and '%#' share an input in layer '%#' - fix %#", action.c_str(), other.c_str(),
                                 _map.findBindingLayer( action, bindIndex ).c_str(), basePath.c_str() );
                bValid = false;
            }
        }
        if ( bValid == false )
            return false;
        // 사용자 재배치(옛 `Shortcuts.json` 은 읽지 않는다). 낡은 줄은 경고하고 건너뛴다.
        if ( DebugKeyActions::loadUserBindings( _map, getUserBindingsPath() ) == false )
            SW_LOG_WARNING_ID( "Editor.EditorInputActionsUserBindingsCould", "EditorInputActions: the user bindings '%#' could not be read - the default shortcuts are used", getUserBindingsPath().c_str() );
        return true;
    }

    void EditorShortcutMap::setFocusedPanel( string_view panelID )
    {
        hashed_string focusedLayer{};
        if ( panelID.empty() == false )
        {
            fixed_string<constant::buffer::kMax256> layerName;
            formatstring( layerName.data(), layerName.capacity(), "%#%#", kPanelLayerPrefix, panelID );
            const hashed_string layer( layerName.c_str() );
            if ( _map.hasLayer( layer ) )
                focusedLayer = layer;
        }
        if ( focusedLayer == _focusedPanelLayer )
            return;
        if ( _focusedPanelLayer.empty() == false && _map.hasLayer( _focusedPanelLayer ) )
            _map.setLayerEnabled( _focusedPanelLayer, false );
        if ( focusedLayer.empty() == false )
            _map.setLayerEnabled( focusedLayer, true );
        _focusedPanelLayer = focusedLayer;
    }

    void EditorShortcutMap::update( float32 deltaSeconds )
    {
        _map.update( deltaSeconds );
    }

    void EditorShortcutMap::collectTriggeredCommands( const vector<EditorCommandDesc>& listCommand, vector<string>& outListCommandID ) const
    {
        outListCommandID.clear();
        vector<string> listOtherCommand;
        for ( const EditorCommandDesc& desc : listCommand )
        {
            if ( desc._inputAction.empty() )
                continue;
            const hashed_string action( desc._inputAction.c_str() );
            if ( _map.wasActionTriggered( action ) == false )
                continue;
            bool bPanelLayer = false;
            for ( uint32 bindIndex = 0; _focusedPanelLayer.empty() == false && bindIndex < _map.getBindingCount( action ); ++bindIndex )
            {
                bPanelLayer = bPanelLayer || _map.findBindingLayer( action, bindIndex ) == _focusedPanelLayer;
            }
            ( bPanelLayer ? outListCommandID : listOtherCommand ).push_back( desc._id );
        }
        if ( outListCommandID.empty() )
            outListCommandID.swap( listOtherCommand );
    }

    bool EditorShortcutMap::wasActionTriggered( string_view action ) const
    {
        return _map.wasActionTriggered( hashed_string( string( action ).c_str() ) );
    }

    bool EditorShortcutMap::isActionDown( string_view action ) const
    {
        return _map.isActionDown( hashed_string( string( action ).c_str() ) );
    }

    void EditorShortcutMap::setModeLayerEnabled( string_view layer, bool bEnabled )
    {
        const hashed_string layerName( string( layer ).c_str() );
        SW_ASSERT( _map.hasLayer( layerName ) ); // 모드 레이어는 editor.input.xml 에 있다 — 없으면 이름을 잘못 적은 것이다
        if ( _map.isLayerEnabled( layerName ) != bEnabled )
            _map.setLayerEnabled( layerName, bEnabled );
    }

    void EditorShortcutMap::formatShortcutLabel( string_view action, string& outLabel ) const
    {
        outLabel.clear();
        if ( action.empty() )
            return;
        const hashed_string actionName( string( action ).c_str() );
        for ( uint32 bindIndex = 0; bindIndex < _map.getBindingCount( actionName ); ++bindIndex )
        {
            const ActionBinding* pBinding       = _map.getBinding( actionName, bindIndex );
            const bool           bKeyOrShortcut = pBinding != nullptr && ( pBinding->_kind == BindingKind::SingleSlot || pBinding->_kind == BindingKind::Shortcut );
            if ( bKeyOrShortcut == false || pBinding->_arrSlot[0]._deviceKind != InputDeviceKind::Keyboard )
                continue;
            if ( outLabel.empty() == false )
                outLabel += " / ";
            const string modifierText = InputRemapSerializer::formatModifierNames( pBinding->_modifierMask );
            if ( modifierText.empty() == false )
            {
                outLabel += modifierText;
                outLabel += "+";
            }
            outLabel += KeyCodeUtil::toName( static_cast<Key>( pBinding->_arrSlot[0]._controlIndex ) );
        }
    }

    bool EditorShortcutMap::findKeyboardBinding( const hashed_string& action, uint32& outBindIndex ) const
    {
        for ( uint32 bindIndex = 0; bindIndex < _map.getBindingCount( action ); ++bindIndex )
        {
            const ActionBinding* pBinding = _map.getBinding( action, bindIndex );
            if ( pBinding != nullptr && pBinding->_arrSlot[0]._deviceKind == InputDeviceKind::Keyboard )
            {
                outBindIndex = bindIndex;
                return true;
            }
        }
        return false;
    }

    bool EditorShortcutMap::rebindShortcut( string_view action, Key key, uint8 modifierMask, hashed_string& outConflictAction )
    {
        const hashed_string actionName( string( action ).c_str() );
        uint32              bindIndex = 0;
        if ( findKeyboardBinding( actionName, bindIndex ) == false )
            return false;
        const ActionBinding before = *_map.getBinding( actionName, bindIndex );
        if ( _map.rebindKeyShortcut( actionName, bindIndex, key, modifierMask ) == false )
            return false;
        if ( _map.findSameLayerInputConflict( actionName, bindIndex, outConflictAction ) )
        {
            // 겹치면 바꾸지 않는다 — 창이 바꾸기 · 취소를 묻는다.
            (void)_map.rebindKeyShortcut( actionName, bindIndex, static_cast<Key>( before._arrSlot[0]._controlIndex ), before._modifierMask ); // 방금 있던 조합이라 늘 된다
            return false;
        }
        saveUserBindings();
        return true;
    }

    bool EditorShortcutMap::swapShortcut( string_view action, string_view otherAction, Key key, uint8 modifierMask )
    {
        const hashed_string actionName( string( action ).c_str() );
        const hashed_string otherName( string( otherAction ).c_str() );
        uint32              bindIndex = 0;
        if ( findKeyboardBinding( actionName, bindIndex ) == false || _map.swapKeyShortcut( actionName, bindIndex, otherName, key, modifierMask ) == false )
            return false;
        saveUserBindings();
        return true;
    }

    void EditorShortcutMap::resetShortcut( string_view action )
    {
        const hashed_string actionName( string( action ).c_str() );
        (void)_map.resetActionToDefault( actionName ); // 없는 액션이면 되돌릴 것도 없다
        saveUserBindings();
        // 되돌린 기본 조합을 사용자가 다른 액션에 줬을 수 있다 - 한 번 눌러 둘 다 발화하므로 알린다(어느 쪽을 바꿀지는 사용자 몫이라 저절로 고치지 않는다).
        for ( uint32 bindIndex = 0; bindIndex < _map.getBindingCount( actionName ); ++bindIndex )
        {
            hashed_string conflictAction;
            if ( _map.findSameLayerInputConflict( actionName, bindIndex, conflictAction ) )
                SW_LOG_WARNING_ID( "EditorInputActions.ResetShortcutConflict", "The default shortcut of '%#' is also bound to '%#' - both fire on one press", actionName.c_str(), conflictAction.c_str() );
        }
    }

    void EditorShortcutMap::resetAllShortcuts()
    {
        _map.resetAllBindingsToDefault();
        saveUserBindings();
    }

    bool EditorShortcutMap::isShortcutModified( string_view action ) const
    {
        const hashed_string  actionName( string( action ).c_str() );
        const ActionBinding* pCurrent = _map.getBinding( actionName, 0 );
        const ActionBinding* pDefault = _map.getDefaultBinding( actionName, 0 );
        if ( pCurrent == nullptr || pDefault == nullptr )
            return false;
        return pCurrent->_kind != pDefault->_kind || pCurrent->_modifierMask != pDefault->_modifierMask || ( pCurrent->_arrSlot[0] == pDefault->_arrSlot[0] ) == false;
    }

    uint32 EditorShortcutMap::countModifiedShortcuts() const
    {
        uint32 count = 0;
        for ( const hashed_string& action : _map.getActionNames() )
        {
            if ( isShortcutModified( action.view() ) )
                ++count;
        }
        return count;
    }

    string EditorShortcutMap::getUserBindingsPath()
    {
        return EditorPaths::resolveEditorStateFile( kUserBindingsFileName );
    }

    void EditorShortcutMap::saveUserBindings() const
    {
        if ( DebugKeyActions::saveUserBindings( _map, getUserBindingsPath() ) == false )
            SW_LOG_WARNING_ID( "Editor.EditorInputActionsUserBindingsCould2", "EditorInputActions: the user bindings '%#' could not be written", getUserBindingsPath().c_str() );
    }

    // 이 종류의 등록 목록 — EditorModule 이미지에 하나(확장 DLL 의 등록자도 내보낸 이 함수로 오른다).
    SW_DEFINE_EDITOR_EXTENSION_KIND( EditorInputMapRegistration )
} // namespace sw::editor
