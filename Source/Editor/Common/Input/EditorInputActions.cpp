/**
 * @file EditorInputActions.cpp
 * @brief 에디터 입력 맵의 소유 · 포커스 레이어 · 액션 → 명령 실행입니다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorInputActions.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Input/RawInputEvent.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "Editor" );

    EditorInputActions::EditorInputActions()
        : _map{}
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
        const string& mapPath = getEditorToolDefaults()._editorInputMap.get();
        if ( _map.loadFromResource( mapPath ) == false )
        {
            SW_LOG_ERROR( "EditorInputActions: the editor input map '%#' could not be read - the editor has no shortcuts", mapPath.c_str() );
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

    void EditorInputActions::processFrame( float32 deltaSeconds )
    {
        syncKeyboardOwner();
        forwardDetachedKeyboard();
        syncFocusedPanelLayer();
        _map.update( deltaSeconds );
    }

    void EditorInputActions::syncKeyboardOwner()
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
            return;
        }
        if ( bEditorOwns == false && _bToolOwnsKeyboard )
        {
            // 그동안 다른 주인(콘솔)이 포커스를 바꿨으면 그쪽을 존중하고 돌리지 않는다.
            if ( current == InputKeyboardFocus::Tool )
                input.setKeyboardFocus( _focusBeforeTool );
            _bToolOwnsKeyboard = false;
        }
    }

    void EditorInputActions::forwardDetachedKeyboard()
    {
        InputManager&          input      = editor::getSubsystem<InputManager>();
        const ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        const ImGuiViewport*   pMain      = ImGui::GetMainViewport();
        bool                   bDetached  = false;
        for ( const ImGuiViewport* pViewport : platformIO.Viewports )
        {
            bDetached = bDetached || ( pViewport != pMain && ( pViewport->Flags & ImGuiViewportFlags_IsFocused ) != 0 );
        }
        // 떼어 낸 게임 뷰의 키는 `GameViewPanel` 이 넘긴다 — 같은 키를 두 번 넣지 않는다.
        const bool bGameViewFocused = editor::getSubsystem<EditorContext>().getSubsystem<EditorRenderViews>().isGameViewFocused();
        if ( bDetached == false || bGameViewFocused )
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
        if ( _focusedPanelLayer.empty() == false )
            _map.setLayerEnabled( _focusedPanelLayer, false );
        if ( focusedLayer.empty() == false )
            _map.setLayerEnabled( focusedLayer, true );
        _focusedPanelLayer = focusedLayer;
    }
} // namespace sw::editor
