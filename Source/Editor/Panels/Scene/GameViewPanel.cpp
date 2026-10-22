#include "pch.h"

#include "Editor/Panels/Scene/GameViewPanel.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorDebugHUDControls.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Engine/Graphics/Canvas/DebugDrawCanvas.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/KeyCodeUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 보조 창에서 게임으로 넘기는 단추입니다(ImGui 번호와 게임 단추의 짝). 순서가 `_forwardedButtonMask` 의 비트다. */
        struct ForwardedButton
        {
            ImGuiMouseButton _imguiButton;
            MouseButton      _button;
        };
        constexpr ForwardedButton kForwardedButtons[] = {
            {  ImGuiMouseButton_Left,   MouseButton::Left},
            { ImGuiMouseButton_Right,  MouseButton::Right},
            {ImGuiMouseButton_Middle, MouseButton::Middle}
        };
    } // namespace

    SW_EDITOR_PANEL( GameViewPanel, GameViewPanel::kPanelID, EditorPanelCategory::Core, 350 );

    GameViewPanel::GameViewPanel()
        : _aspect{ EditorGameViewAspect::Free }
        , _bNoCameraHintShown{ false }
        , _forwardedButtonMask{ 0 }
        , _listForwardedKey{}
    {
    }

    void GameViewPanel::forwardDetachedKeyboard()
    {
        InputManager* pInput = findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const bool bDetached = ImGui::GetWindowViewport() != ImGui::GetMainViewport();
        const bool bForward  = bDetached && ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        if ( bForward == false )
        {
            // 포커스를 잃었거나 패널을 주 창에 되돌렸다 — 누른 채 남은 키를 놓는다. 큐가 차서 못 넣은 키는 다음 프레임에 다시 시도한다.
            for ( size_t index = _listForwardedKey.size(); index > 0; --index )
            {
                if ( pInput->postRawEvent( RawInputEvent::makeKeyUp( _listForwardedKey[index - 1] ) ) )
                    VectorUtil::removeAtSwap( _listForwardedKey, index - 1 );
            }
            return;
        }

        vector<RawInputEvent> listEvent;
        ImGuiKeyForward::appendKeyChanges( listEvent );
        ImGuiKeyForward::appendTextInput( listEvent );
        for ( const RawInputEvent& event : listEvent )
        {
            if ( pInput->postRawEvent( event ) == false )
                continue; // 큐가 차면 이 사건만 잃는다
            if ( event._type == RawInputEventType::KeyDown && VectorUtil::contains( _listForwardedKey, event._payload._keyData._key ) == false )
                _listForwardedKey.push_back( event._payload._keyData._key );
            else if ( event._type == RawInputEventType::KeyUp )
                VectorUtil::removeValue( _listForwardedKey, event._payload._keyData._key );
        }
    }

    void GameViewPanel::forwardDetachedPointer()
    {
        InputManager* pInput = findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const ImGuiViewport* pMainViewport = ImGui::GetMainViewport();
        const bool           bDetached     = ImGui::GetWindowViewport() != pMainViewport;
        const bool           bHovered      = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        if ( bDetached == false && _forwardedButtonMask == 0 )
            return; // 주 창에 있으면 주 창의 메시지가 게임으로 간다(`ImGuiEditor::processEvent`)
        const ImGuiIO& io       = ImGui::GetIO();
        const int32    x        = static_cast<int32>( MathUtil::round( io.MousePos.x - pMainViewport->Pos.x ) );
        const int32    y        = static_cast<int32>( MathUtil::round( io.MousePos.y - pMainViewport->Pos.y ) );
        const bool     bForward = bDetached && bHovered;
        if ( bForward && ( io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ) )
            (void)pInput->postRawEvent( RawInputEvent::makeMouseMove( x, y ) ); // 큐가 차면 이 프레임의 이동만 잃는다 — 다음 이동이 위치를 다시 준다
        for ( uint32 index = 0; index < static_cast<uint32>( std::size( kForwardedButtons ) ); ++index )
        {
            const ForwardedButton& button       = kForwardedButtons[index];
            const uint8            bit          = static_cast<uint8>( 1u << index );
            const bool             bPressedHere = bForward && ImGui::IsMouseClicked( button._imguiButton );
            if ( bPressedHere && pInput->postRawEvent( RawInputEvent::makeMouseButtonDown( button._button, x, y ) ) )
                _forwardedButtonMask = static_cast<uint8>( _forwardedButtonMask | bit );
            // 뗌은 넣은 누름의 짝이면 패널 밖이어도 넣는다 — 안 넣으면 게임에서 단추가 눌린 채 남는다.
            const bool bReleasedForwarded = ( _forwardedButtonMask & bit ) != 0 && ImGui::IsMouseReleased( button._imguiButton );
            if ( bReleasedForwarded && pInput->postRawEvent( RawInputEvent::makeMouseButtonUp( button._button, x, y ) ) )
                _forwardedButtonMask = static_cast<uint8>( _forwardedButtonMask & ~bit );
        }
        if ( bForward && io.MouseWheel != 0.0f )
            (void)pInput->postRawEvent( RawInputEvent::makeMouseWheel( io.MouseWheel ) );
    }

    void GameViewPanel::onPanelCollapsed()
    {
        EditorContext* pClosedContext = EditorContext::get();
        if ( pClosedContext != nullptr )
        {
            pClosedContext->getSubsystem<EditorRenderViews>().setGameViewFocused( false );
            pClosedContext->getSubsystem<EditorRenderViews>().setGameViewHovered( false );
        }
    }

    void GameViewPanel::drawContent()
    {
        EditorContext* pEditorContext = EditorContext::get();
        if ( pEditorContext == nullptr )
            return;

        // Play 중 게임 입력은 이 패널 위에서만 게임으로 간다(`ImGuiEditor::processEvent`). 씬 뷰 위의 입력은 에디터 카메라 담당이다.
        pEditorContext->getSubsystem<EditorRenderViews>().setGameViewFocused( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) );
        if ( pEditorContext->getSubsystem<EditorRenderViews>().isGameViewFocused() )
            EditorScreenshotCommands::noteFocusedView( EditorViewKind::Game );
        pEditorContext->getSubsystem<EditorRenderViews>().setGameViewHovered( ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows ) );

        drawToolbar();

        const ImVec2         available = ImGui::GetContentRegionAvail();
        const EditorViewRect rect      = EditorViewTargetUtil::fitViewImage( float2{ available.x, available.y }, _aspect );
        const uint32         width     = static_cast<uint32>( rect._size._x );
        const uint32         height    = static_cast<uint32>( rect._size._y );
        if ( width > 1 && height > 1 )
        {
            const EditorViewTarget& view = pEditorContext->getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Game );
            if ( EditorViewTargetUtil::needsResize( view._width, view._height, width, height ) )
                pEditorContext->getSubsystem<EditorRenderViews>().ensureViewTargetSize( EditorViewKind::Game, width, height );
        }
        // 레터박스 — 남는 쪽은 비워 둔다. 이미지는 RT 크기 그대로(늘이지 않는다).
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 imagePos{ origin.x + rect._offset._x, origin.y + rect._offset._y };
        const ImVec2 imageSize{ rect._size._x, rect._size._y };
        // 이 패널이 보이는 프레임만 게임 뷰를 그린다(접힘 · 닫힘 · 다른 탭이면 drawContent 가 불리지 않는다 — 게임 뷰 RT 요청 0).
        // 이미지 자리도 함께 알린다 — 런타임 UI 가 창 픽셀 포인터를 게임 뷰 픽셀로 옮긴다(`HostViewTarget::_windowX`).
        pEditorContext->getSubsystem<EditorRenderViews>().markViewDrawn( EditorViewKind::Game, float2{ imagePos.x - ImGui::GetMainViewport()->Pos.x, imagePos.y - ImGui::GetMainViewport()->Pos.y } );
        forwardDetachedPointer();
        forwardDetachedKeyboard();
        ImGui::SetCursorScreenPos( imagePos );
        const void* pTextureID        = pEditorContext->getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Game )._pTextureID;
        const bool  bCanDrawGameImage = pTextureID != nullptr && imageSize.x > 1.0f && imageSize.y > 1.0f;
        if ( bCanDrawGameImage )
            ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), imageSize );
        else
            ImGui::Dummy( ImVec2{ MathUtil::max( imageSize.x, 1.0f ), MathUtil::max( imageSize.y, 1.0f ) } );
        EditorSelfTestMarks::note( "gameView.canvas" );

        // 게임 카메라가 없으면(꺼짐 · 지움) 그릴 눈이 없다 — 지난 이미지 위에 안내를 띄운다(유니티 "No cameras rendering").
        Scene*                 pScene      = editor::getActiveScene();
        const CameraComponent* pGameCamera = pScene != nullptr ? pScene->getActiveGameCamera() : nullptr;
        _bNoCameraHintShown                = CameraRegistry::isUsableCamera( pGameCamera ) == false;
        ImDrawList* pDrawList              = ImGui::GetWindowDrawList();
        const bool  bShowNoCameraHint      = _bNoCameraHintShown && imageSize.x > 1.0f && imageSize.y > 1.0f;
        if ( bShowNoCameraHint )
        {
            constexpr const utf8*  kHint = "No camera rendering";
            static constexpr Color kHintBackground{ Color::fromBytes( 0, 0, 0 ) };
            static constexpr Color kHintText{ Color::fromBytes( 220, 220, 220 ) };
            const ImVec2           hintSize = ImGui::CalcTextSize( kHint );
            pDrawList->AddRectFilled( imagePos, ImVec2{ imagePos.x + imageSize.x, imagePos.y + imageSize.y }, EditorColorUtil::toImU32( kHintBackground ) );
            pDrawList->AddText( ImVec2{ imagePos.x + ( imageSize.x - hintSize.x ) * 0.5f, imagePos.y + ( imageSize.y - hintSize.y ) * 0.5f },
                                EditorColorUtil::toImU32( kHintText ), kHint );
        }

        if ( imageSize.x > 1.0f && imageSize.y > 1.0f )
        {
            InputManager* pInput = findSubsystem<InputManager>();
            if ( pInput != nullptr && ViewportInputOverlay::getConfig()._bEnabled == SW_TRUE )
                ViewportInputOverlay::draw( pDrawList, imagePos, imageSize, pInput, &pInput->getInputMap() );
        }
    }

    void GameViewPanel::drawToolbar()
    {
        if ( EditorChrome::beginToolbar( "##GameViewToolbar" ) )
        {
            const utf8* pCurrentLabel = EditorViewTargetUtil::getAspectLabel( _aspect );
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * 8.0f );
            if ( ImGui::BeginCombo( "##GameViewAspect", pCurrentLabel ) )
            {
                for ( uint32 aspectIndex = 0; aspectIndex < static_cast<uint32>( EditorGameViewAspect::Count ); ++aspectIndex )
                {
                    const EditorGameViewAspect aspect = static_cast<EditorGameViewAspect>( aspectIndex );
                    if ( ImGui::Selectable( EditorViewTargetUtil::getAspectLabel( aspect ), aspect == _aspect ) )
                        _aspect = aspect;
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::note( "gameView.aspect" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "GameViewAspect" ) );

            ImGui::SameLine();
            // 유니티 게임 뷰의 Maximize On Play 에 해당 — 값은 환경설정(Editor/Play)이고 바꾸면 바로 저장한다.
            EditorPlayPreferences& playPreferences = *static_cast<EditorPlayPreferences*>( EditorSettingsInstance<EditorPlayPreferences>::getInstance() );
            if ( ImGui::Checkbox( "Max", &playPreferences._bMaximizeOnPlay ) && EditorPreferencesStore::saveAll( EditorPreferencesStore::getDefaultFilePath() ) == false )
                SW_LOG_WARNING( "Editor preferences could not be saved" );
            EditorSelfTestMarks::note( "gameView.maximizeOnPlay" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "MaximizeOnPlay" ) );

            ImGui::SameLine();
            // 게임 HUD(`DebugHUD`) — 런타임 UI 라 게임 뷰에 그대로 그려진다(멈춤 · 일시정지에는 숨는다). 게임 값(`DebugOverlayState`)은 HUD 의 overlay 섹션이다.
            EditorDebugHUDControls::drawMasterToggle( "gameView.hud" );

            ImGui::SameLine();
            // 디버그 드로우 겹치기(서버 디버깅 S6) — 이 에디터의 게임 뷰와, 멀티 프로세스 재생이 띄운 자식 프로세스(클라이언트의 권위 고스트) 모두에 같은 값을 건다.
            const bool bDebugDrawShown = DebugDrawCanvasUtil::isGameViewOverlayEnabled();
            if ( EditorWidgets::drawIconToggle( "##gameViewDebugDraw", editoricon::kBug, bDebugDrawShown,
                                                "Debug draw in the game view - lines, spheres and text from DebugDrawQueue (authority ghosts too). Sent to net play children" ) )
            {
                DebugDrawCanvasUtil::setGameViewOverlayEnabled( bDebugDrawShown == false );
                EditorContext* pContext = EditorContext::get();
                const utf8*    pCommand = bDebugDrawShown ? "gv_debugDrawGameView 0" : "gv_debugDrawGameView 1";
                if ( pContext != nullptr )
                {
                    EditorNetPlaySession& session = pContext->getSubsystem<EditorNetPlaySession>();
                    for ( int32 processIndex = 0; processIndex < session.getProcessCount(); ++processIndex )
                    {
                        (void)session.sendProcessCommand( processIndex, pCommand ); // 끝난 프로세스는 false — 받을 쪽이 없다
                    }
                }
            }
            EditorSelfTestMarks::note( "gameView.debugDraw" );

            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kCamera, "##GameViewScreenshot" ) ) )
                EditorScreenshotCommands::captureGameView();
            EditorSelfTestMarks::note( "gameView.screenshot" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "GameViewScreenshot" ) );
        }
        EditorChrome::endToolbar();
    }
} // namespace sw::editor
