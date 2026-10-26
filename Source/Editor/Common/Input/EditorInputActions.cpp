/**
 * @file EditorInputActions.cpp
 * @brief 에디터 입력 맵의 소유 · 포커스 레이어 · 키보드 주인 · 액션 → 명령 실행 · 사용자 재배치입니다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorInputActions.h"

#include "Core/Container/vector.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/ShellInputActions.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "Editor" );

    EditorInputActions::EditorInputActions()
        : _map{}
        , _listQueuedCommand{}
        , _focusedPanelLayer{}
    {
    }

    EditorInputActions::~EditorInputActions() = default;

    span<const utf8* const> EditorInputActions::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { EditorCommandRegistry::kSubsystemName, EditorPanelManager::kSubsystemName };
        return kArrDependency;
    }

    SubsystemInitResult EditorInputActions::initializeSubsystem()
    {
        // 엔진 입력 매니저는 클라이언트 호스트에 늘 있다(필수) — 에디터 키도 같은 장치 · 같은 맵 규칙으로 판정한다.
        _map.setInputManager( &editor::getSubsystem<InputManager>() );
        // 게임 뷰가 포커스여도(F8 플레이 빠져나오기 · F9 스크린샷) 에디터 도구 창이 포커스여도 키를 본다. 셸 · UI 맵은 도구 포커스 동안 키를 보지 않는다.
        _map.setKeyboardFocusIgnored( true );
        _map.setToolKeyboardReader( true );
        if ( rebuildMap() == false )
        {
            _map.setInputManager( nullptr );
            return SubsystemInitResult::Failed;
        }
        return SubsystemInitResult::Succeeded;
    }

    void EditorInputActions::shutdownSubsystem()
    {
        // 입력 매니저는 에디터보다 오래 산다 — 키보드 주인과 넣은 키를 돌려 두고, 맵이 그것을 가리킨 채 남지 않게 끊는다(끊긴 맵의 `update` 는 아무것도 하지 않는다).
        InputManager& input = editor::getSubsystem<InputManager>();
        releaseForwardedKeys( input );
        if ( _bToolOwnsKeyboard && input.getKeyboardFocus() == InputKeyboardFocus::Tool )
            input.setKeyboardFocus( _focusBeforeTool );
        _bToolOwnsKeyboard = false;
        _map.setInputManager( nullptr );
    }

    bool EditorInputActions::rebuildMap()
    {
        using MapRegistry    = EditorExtensionRegistry<EditorInputMapRegistration>;
        _syncedMapGeneration = MapRegistry::getList().getGeneration();
        _focusedPanelLayer   = {};

        const string& basePath = getEditorToolDefaults()._editorInputMap.get();
        if ( _map.loadFromResource( basePath ) == false )
        {
            SW_LOG_ERROR( "EditorInputActions: the editor input map '%#' could not be read - the editor has no shortcuts", basePath.c_str() );
            return false;
        }
        bool bValid = true;
        for ( uint32 index = 0; index < MapRegistry::getCount(); ++index )
        {
            const EditorInputMapRegistration& row = MapRegistry::getAt( index );
            if ( _map.appendFromResource( row._pResourcePath ) == false )
            {
                SW_LOG_ERROR( "EditorInputActions: the extension input map '%#' (%#) could not be appended (listed above)", row._pResourcePath, row._pID );
                bValid = false;
            }
        }
        // 기본 맵 안의 겹침도 데이터 오류다 — 한 번 눌러 두 명령이 돈다. 더한 파일의 겹침은 `appendFromResource` 가 이미 알렸다.
        for ( const hashed_string& action : _map.getActionNames() )
        {
            for ( uint32 bindIndex = 0; bindIndex < _map.getBindingCount( action ); ++bindIndex )
            {
                hashed_string other{};
                if ( _map.findSameLayerInputConflict( action, bindIndex, other ) == false )
                    continue;
                SW_LOG_ERROR( "EditorInputActions: '%#' and '%#' share an input in layer '%#' - fix %#", action.c_str(), other.c_str(),
                              _map.findBindingLayer( action, bindIndex ).c_str(), basePath.c_str() );
                bValid = false;
            }
        }
        if ( bValid == false )
            return false;
        // 사용자 재배치(옛 `Shortcuts.json` 은 읽지 않는다 — 결정 기록 5-11). 낡은 줄은 경고하고 건너뛴다.
        if ( ShellInputActions::loadUserBindings( _map, getUserBindingsPath() ) == false )
            SW_LOG_WARNING( "EditorInputActions: the user bindings '%#' could not be read - the default shortcuts are used", getUserBindingsPath().c_str() );
        return true;
    }

    void EditorInputActions::processFrame( float32 deltaSeconds )
    {
        // 확장 모듈이 맵을 더하거나(로드) 뺐으면(언로드) 맵을 처음부터 다시 만든다 — 빠진 모듈의 액션이 남지 않고, 다시 올린 모듈의 액션이 두 번 더해지지 않는다.
        if ( EditorExtensionRegistry<EditorInputMapRegistration>::getList().getGeneration() != _syncedMapGeneration && rebuildMap() == false )
            SW_LOG_ERROR( "EditorInputActions: the editor input map could not be rebuilt after an extension module changed - see above" );
        // 지난 프레임 메뉴에서 고른 명령부터 — 패널을 그리기 전이라 그리는 중인 목록이 없다. 실행이 다시 넣은 것은 다음 프레임이다.
        vector<QueuedCommand> listRun;
        listRun.swap( _listQueuedCommand );
        EditorCommandRegistry& registry = editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
        for ( const QueuedCommand& queued : listRun )
        {
            (void)registry.execute( queued._commandID, queued._target ); // 그 사이 꺼졌으면 아무 일도 없다(메뉴의 회색과 같은 조건)
        }
        forwardEditorKeyboard( syncKeyboardOwner() );
        syncFocusedPanelLayer();
        _map.update( deltaSeconds );
        // ImGui 의 클릭 · 끌기 · 더블클릭 판정(항목 단추 · 맥락 메뉴 · 끌어 놓기)도 입력 맵의 값 하나를 쓴다 — 두 곳에 숫자를 두지 않는다.
        ImGuiIO& io                = ImGui::GetIO();
        io.MouseDragThreshold      = _map.getDragThreshold();
        io.MouseDoubleClickTime    = _map.getDoubleClickTime();
        io.MouseDoubleClickMaxDist = _map.getDoubleClickMaxDistance();
        // 글을 치는 중에는 Ctrl+S 같은 단축키도 그 글 칸의 것이다(예전 처리와 같다). 재배치 창이 키를 받는 중에도 실행하지 않는다.
        if ( areActionsBlocked() )
            return;
        executeTriggeredCommands();
    }

    void EditorInputActions::queueCommand( string_view commandID, const EditorCommandTarget& target )
    {
        _listQueuedCommand.push_back( QueuedCommand{ string( commandID ), target } );
    }

    bool EditorInputActions::areActionsBlocked() const
    {
        return _bCommandsSuspended || ImGui::GetIO().WantTextInput;
    }

    bool EditorInputActions::wasActionTriggered( string_view action ) const
    {
        return areActionsBlocked() == false && _map.wasActionTriggered( hashed_string( string( action ).c_str() ) );
    }

    bool EditorInputActions::isActionDown( string_view action ) const
    {
        return areActionsBlocked() == false && _map.isActionDown( hashed_string( string( action ).c_str() ) );
    }

    void EditorInputActions::executeTriggeredCommands()
    {
        const EditorCommandRegistry& registry = editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
        vector<string>               listPanelCommand;
        vector<string>               listOtherCommand;
        for ( const EditorCommandDesc& desc : registry.getCommands() )
        {
            if ( desc._inputAction.empty() )
                continue;
            const hashed_string action( desc._inputAction.c_str() );
            if ( _map.wasActionTriggered( action ) == false )
                continue;
            // 한 액션이 여러 패널 레이어에 바인딩을 가질 수 있다(Delete 가 계층 창 · 씬 뷰에 각자) — 포커스 레이어의 바인딩이 하나라도 있으면 패널 쪽이다.
            bool bPanelLayer = false;
            for ( uint32 bindIndex = 0; _focusedPanelLayer.empty() == false && bindIndex < _map.getBindingCount( action ); ++bindIndex )
            {
                bPanelLayer = bPanelLayer || _map.findBindingLayer( action, bindIndex ) == _focusedPanelLayer;
            }
            ( bPanelLayer ? listPanelCommand : listOtherCommand ).push_back( desc._id );
        }
        // 실행은 순회가 끝난 뒤에 한다 — 명령이 등록부를 다시 만들어도(모듈 리로드) 순회 중인 목록을 읽지 않는다.
        EditorCommandRegistry& writableRegistry = editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
        const vector<string>&  listRun          = listPanelCommand.empty() ? listOtherCommand : listPanelCommand;
        for ( const string& commandID : listRun )
        {
            (void)writableRegistry.execute( commandID, EditorCommandTarget::none() ); // 꺼진 명령이면 아무 일도 없다(메뉴의 회색과 같은 조건)
        }
    }

    bool EditorInputActions::syncKeyboardOwner()
    {
        const bool               bGameViewFocused = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().isGameViewFocused();
        const bool               bEditorOwns      = EditorPlaySession::isSimulating() || ( ImGui::GetIO().WantCaptureKeyboard && bGameViewFocused == false );
        InputManager&            input            = editor::getSubsystem<InputManager>();
        const InputKeyboardFocus current          = input.getKeyboardFocus();
        if ( bEditorOwns && current != InputKeyboardFocus::Tool )
        {
            _focusBeforeTool   = current;
            _bToolOwnsKeyboard = true;
            input.setKeyboardFocus( InputKeyboardFocus::Tool );
            return true;
        }
        if ( bEditorOwns == false && _bToolOwnsKeyboard )
        {
            // 그동안 다른 주인(콘솔)이 포커스를 바꿨으면 그쪽을 존중하고 돌리지 않는다.
            if ( current == InputKeyboardFocus::Tool )
                input.setKeyboardFocus( _focusBeforeTool );
            _bToolOwnsKeyboard = false;
        }
        return bEditorOwns;
    }

    void EditorInputActions::forwardEditorKeyboard( bool bEditorOwnsKeyboard )
    {
        InputManager& input = editor::getSubsystem<InputManager>();
        if ( bEditorOwnsKeyboard == false )
        {
            releaseForwardedKeys( input );
            return;
        }
        vector<RawInputEvent> listEvent;
        ImGuiKeyForward::appendKeyChanges( listEvent );
        for ( const RawInputEvent& event : listEvent )
        {
            if ( input.postRawEvent( event ) == false )
                continue; // 큐가 차면 이 사건만 잃는다
            const size_t keyIndex = static_cast<size_t>( event._payload._keyData._key );
            if ( event._type == RawInputEventType::KeyDown )
                _arrForwardedKey[keyIndex] = true;
            else if ( event._type == RawInputEventType::KeyUp )
                _arrForwardedKey[keyIndex] = false;
        }
    }

    void EditorInputActions::releaseForwardedKeys( InputManager& input )
    {
        for ( size_t keyIndex = 0; keyIndex < std::size( _arrForwardedKey ); ++keyIndex )
        {
            // 큐가 차서 못 넣은 뗌은 다음 프레임에 다시 시도한다.
            if ( _arrForwardedKey[keyIndex] && input.postRawEvent( RawInputEvent::makeKeyUp( static_cast<Key>( keyIndex ) ) ) )
                _arrForwardedKey[keyIndex] = false;
        }
    }

    void EditorInputActions::syncFocusedPanelLayer()
    {
        const EditorPanelManager& panelManager = editor::getSubsystem<EditorContext>().getSubsystem<EditorPanelManager>();
        hashed_string             focusedLayer{};
        for ( const EditorPanelEntry& entry : panelManager.getPanels() )
        {
            const bool bFocused = entry._pInstance != nullptr && entry._pInstance->isOpen() && entry._pInstance->isWindowFocused();
            if ( bFocused == false )
                continue;
            const hashed_string layer( ( string( kPanelLayerPrefix ) + entry._id ).c_str() );
            if ( _map.hasLayer( layer ) )
                focusedLayer = layer;
            break;
        }
        if ( focusedLayer == _focusedPanelLayer )
            return;
        if ( _focusedPanelLayer.empty() == false && _map.hasLayer( _focusedPanelLayer ) )
            _map.setLayerEnabled( _focusedPanelLayer, false );
        if ( focusedLayer.empty() == false )
            _map.setLayerEnabled( focusedLayer, true );
        _focusedPanelLayer = focusedLayer;
    }

    void EditorInputActions::formatShortcutLabel( string_view action, string& outLabel ) const
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
            const string modifierText = InputMap::formatModifierNames( pBinding->_modifierMask );
            if ( modifierText.empty() == false )
            {
                outLabel += modifierText;
                outLabel += "+";
            }
            outLabel += KeyCodeUtil::toName( static_cast<Key>( pBinding->_arrSlot[0]._controlIndex ) );
        }
    }

    bool EditorInputActions::findKeyboardBinding( const hashed_string& action, uint32& outBindIndex ) const
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

    bool EditorInputActions::rebindShortcut( string_view action, Key key, uint8 modifierMask, hashed_string& outConflictAction )
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

    bool EditorInputActions::swapShortcut( string_view action, string_view otherAction, Key key, uint8 modifierMask )
    {
        const hashed_string actionName( string( action ).c_str() );
        const hashed_string otherName( string( otherAction ).c_str() );
        uint32              bindIndex      = 0;
        uint32              otherBindIndex = 0;
        if ( findKeyboardBinding( actionName, bindIndex ) == false || findKeyboardBinding( otherName, otherBindIndex ) == false )
            return false;
        const ActionBinding before  = *_map.getBinding( actionName, bindIndex );
        const bool          bOther  = _map.rebindKeyShortcut( otherName, otherBindIndex, static_cast<Key>( before._arrSlot[0]._controlIndex ), before._modifierMask );
        const bool          bAction = bOther && _map.rebindKeyShortcut( actionName, bindIndex, key, modifierMask );
        if ( bAction )
            saveUserBindings();
        return bAction;
    }

    void EditorInputActions::resetShortcut( string_view action )
    {
        (void)_map.resetActionToDefault( hashed_string( string( action ).c_str() ) ); // 없는 액션이면 되돌릴 것도 없다
        saveUserBindings();
    }

    void EditorInputActions::resetAllShortcuts()
    {
        _map.resetAllBindingsToDefault();
        saveUserBindings();
    }

    bool EditorInputActions::isShortcutModified( string_view action ) const
    {
        const hashed_string  actionName( string( action ).c_str() );
        const ActionBinding* pCurrent = _map.getBinding( actionName, 0 );
        const ActionBinding* pDefault = _map.getDefaultBinding( actionName, 0 );
        if ( pCurrent == nullptr || pDefault == nullptr )
            return false;
        return pCurrent->_kind != pDefault->_kind || pCurrent->_modifierMask != pDefault->_modifierMask || ( pCurrent->_arrSlot[0] == pDefault->_arrSlot[0] ) == false;
    }

    uint32 EditorInputActions::countModifiedShortcuts() const
    {
        uint32 count = 0;
        for ( const hashed_string& action : _map.getActionNames() )
        {
            if ( isShortcutModified( action.view() ) )
                ++count;
        }
        return count;
    }

    string EditorInputActions::getUserBindingsPath()
    {
        return EditorPaths::resolveEditorStateFile( kUserBindingsFileName );
    }

    void EditorInputActions::saveUserBindings() const
    {
        if ( ShellInputActions::saveUserBindings( _map, getUserBindingsPath() ) == false )
            SW_LOG_WARNING( "EditorInputActions: the user bindings '%#' could not be written", getUserBindingsPath().c_str() );
    }
} // namespace sw::editor
