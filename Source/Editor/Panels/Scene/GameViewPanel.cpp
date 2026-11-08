#include "pch.h"

#include "Editor/Panels/Scene/GameViewPanel.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "DevTools/Client/DebugDraw/DebugDrawCanvas.h"

#include "Editor/Common/Backend/EditorUICapture.h"
#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/Backend/ImGuiRenderer.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorDebugHUDControls.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 런타임 UI 가 고른 OS 커서 모양을 ImGui 커서 모양으로 바꿉니다(에디터에서는 ImGui 가 OS 커서를 쥔다). */
        ImGuiMouseCursor toImGuiCursor( CursorShape shape )
        {
            switch ( shape )
            {
                case CursorShape::Arrow:
                    return ImGuiMouseCursor_Arrow;
                case CursorShape::Hand:
                    return ImGuiMouseCursor_Hand;
                case CursorShape::TextBeam:
                    return ImGuiMouseCursor_TextInput;
                case CursorShape::ResizeHorizontal:
                    return ImGuiMouseCursor_ResizeEW;
                case CursorShape::ResizeVertical:
                    return ImGuiMouseCursor_ResizeNS;
                case CursorShape::NotAllowed:
                    return ImGuiMouseCursor_NotAllowed;
            }
            return ImGuiMouseCursor_Arrow;
        }
    } // namespace

    SW_EDITOR_PANEL( GameViewPanel, GameViewPanel::kPanelID, EditorPanelCategory::Core, 350 );

    GameViewPanel::GameViewPanel()
        : _model{}
        , _pDevOverlayTextureID{ nullptr }
        , _devOverlayTexture{ 0 }
    {
    }

    void GameViewPanel::shutdown()
    {
        releaseDevOverlayTexture();
    }

    void GameViewPanel::releaseDevOverlayTexture()
    {
        if ( _pDevOverlayTextureID == nullptr )
            return;
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        if ( pContext != nullptr )
        {
            pContext->getSubsystem<EditorUICapture>().setTextureExcluded( _pDevOverlayTextureID, false ); // ID 칸은 다른 등록이 다시 받는다
            if ( pContext->getSubsystem<EditorRenderViews>().getRenderer() != nullptr )
                pContext->getSubsystem<EditorRenderViews>().getRenderer()->unregisterTexture( _pDevOverlayTextureID );
        }
        _pDevOverlayTextureID = nullptr;
        _devOverlayTexture    = 0;
    }

    void GameViewPanel::syncDevOverlayTexture()
    {
        AssetManager* const    pAssets  = editor::findSubsystem<AssetManager>();
        const Texture2D* const pTexture = pAssets != nullptr ? pAssets->getAssetCache<TextureCache>().find( CanvasFrameData::kDevOverlayTargetPath ) : nullptr;
        const uint64           texture  = pTexture != nullptr && pTexture->isRHIValid() ? pTexture->getHandle() : 0;
        if ( texture == _devOverlayTexture && ( _pDevOverlayTextureID != nullptr || texture == 0 ) )
            return;
        releaseDevOverlayTexture();
        EditorContext&     editorContext = editor::getSubsystem<EditorContext>();
        EditorRenderViews& renderViews   = editorContext.getSubsystem<EditorRenderViews>();
        if ( texture == 0 || renderViews.getRenderer() == nullptr )
            return; // 겹치기가 없거나 렌더 스레드가 아직 만들지 않았다(다음 프레임)
        _pDevOverlayTextureID = renderViews.getRenderer()->registerTexture( texture );
        _devOverlayTexture    = _pDevOverlayTextureID != nullptr ? texture : 0;
        editorContext.getSubsystem<EditorUICapture>().setTextureExcluded( _pDevOverlayTextureID, true );
    }

    void GameViewPanel::drawDevOverlay( const float2& imagePos, const float2& imageSize )
    {
        // 렌더 텍스처는 겹치기가 처음 실린 뒤 캐시에 남는다(내용이 빈 프레임도 지운 채로 남는다) — 겹치기가 없으면 투명이라 그려도 보이지 않는다.
        syncDevOverlayTexture();
        if ( _pDevOverlayTextureID == nullptr || imageSize._x <= 1.0f || imageSize._y <= 1.0f )
            return;
        ImGui::GetWindowDrawList()->AddImage( reinterpret_cast<ImTextureID>( _pDevOverlayTextureID ), ImVec2{ imagePos._x, imagePos._y },
                                              ImVec2{ imagePos._x + imageSize._x, imagePos._y + imageSize._y } );
    }

    void GameViewPanel::forwardDetachedInput()
    {
        const ImGuiViewport* pMainViewport = ImGui::GetMainViewport();
        const bool           bDetached     = ImGui::GetWindowViewport() != pMainViewport;
        // 포인터 상태는 어댑터 한 곳에서 읽는다(패널 티어는 ImGui 입력 함수를 부르지 않는다 — EditorInputBan.h).
        _model.forwardDetachedPointer( bDetached, ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows ), EditorInputGUI::readPointer(),
                                       float2{ pMainViewport->Pos.x, pMainViewport->Pos.y } );
        const bool            bForwardKeys = bDetached && ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        vector<RawInputEvent> listEvent;
        if ( bForwardKeys )
        {
            ImGuiKeyForward::appendKeyChanges( listEvent );
            ImGuiKeyForward::appendTextInput( listEvent );
        }
        _model.forwardDetachedKeyboard( bForwardKeys, listEvent );
    }

    void GameViewPanel::onPanelCollapsed()
    {
        EditorContext* pClosedContext = editor::findSubsystem<EditorContext>();
        if ( pClosedContext != nullptr )
        {
            pClosedContext->getSubsystem<EditorRenderViews>().setGameViewFocused( false );
            pClosedContext->getSubsystem<EditorRenderViews>().setGameViewHovered( false );
        }
    }

    void GameViewPanel::drawContent()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        // Play 중 게임 입력은 이 패널 위에서만 게임으로 간다(`ImGuiEditor::processEvent`). 씬 뷰 위의 입력은 에디터 카메라 담당이다.
        editorContext.getSubsystem<EditorRenderViews>().setGameViewFocused( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) );
        if ( editorContext.getSubsystem<EditorRenderViews>().isGameViewFocused() )
            EditorScreenshotCommands::recordFocusedView( EditorViewKind::Game );
        editorContext.getSubsystem<EditorRenderViews>().setGameViewHovered( ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows ) );

        drawToolbar();

        const ImVec2         available = ImGui::GetContentRegionAvail();
        const EditorViewRect rect      = EditorViewTargetUtil::fitViewImage( float2{ available.x, available.y }, _model.getAspect() );
        const uint32         width     = static_cast<uint32>( rect._size._x );
        const uint32         height    = static_cast<uint32>( rect._size._y );
        if ( width > 1 && height > 1 )
        {
            const EditorViewTarget& view = editorContext.getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Game );
            if ( EditorViewTargetUtil::needsResize( view.getRequestedWidth(), view.getRequestedHeight(), width, height ) )
                editorContext.getSubsystem<EditorRenderViews>().ensureViewTargetSize( EditorViewKind::Game, width, height );
        }
        // 레터박스 — 남는 쪽은 비워 둔다. 이미지는 RT 크기 그대로(늘이지 않는다).
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 imagePos{ origin.x + rect._offset._x, origin.y + rect._offset._y };
        const ImVec2 imageSize{ rect._size._x, rect._size._y };
        // 이 패널이 보이는 프레임만 게임 뷰를 그린다(접힘 · 닫힘 · 다른 탭이면 drawContent 가 불리지 않는다 — 게임 뷰 RT 요청 0).
        // 이미지 자리도 함께 알린다 — 런타임 UI 가 창 픽셀 포인터를 게임 뷰 픽셀로 옮긴다(`ViewportTarget::_windowX`).
        editorContext.getSubsystem<EditorRenderViews>().markViewDrawn( EditorViewKind::Game, float2{ imagePos.x - ImGui::GetMainViewport()->Pos.x, imagePos.y - ImGui::GetMainViewport()->Pos.y } );
        forwardDetachedInput();
        ImGui::SetCursorScreenPos( imagePos );
        const void* pTextureID        = editorContext.getSubsystem<EditorRenderViews>().getViewTarget( EditorViewKind::Game )._pTextureID;
        const bool  bCanDrawGameImage = pTextureID != nullptr && imageSize.x > 1.0f && imageSize.y > 1.0f;
        if ( bCanDrawGameImage )
        {
            ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), imageSize );
            // 에디터에서는 ImGui 가 OS 커서를 쥐어 엔진이 창에 건 커서가 덮이므로, 게임 UI 가 호버한 위젯의 모양(손 · 글 상자 …)을 ImGui 커서로 낸다.
            const InputManager* pCursorInput = findSubsystem<InputManager>();
            if ( pCursorInput != nullptr && ImGui::IsItemHovered() )
                ImGui::SetMouseCursor( toImGuiCursor( pCursorInput->getCursorShape() ) );
        }
        else
            ImGui::Dummy( ImVec2{ MathUtil::max( imageSize.x, 1.0f ), MathUtil::max( imageSize.y, 1.0f ) } );
        if ( bCanDrawGameImage )
            drawDevOverlay( float2{ imagePos.x, imagePos.y }, float2{ imageSize.x, imageSize.y } );
        EditorSelfTestMarks::record( "gameView.canvas" );

        // 게임 카메라가 없으면(꺼짐 · 지움) 그릴 눈이 없다 — 지난 이미지 위에 안내를 띄운다(유니티 "No cameras rendering").
        _model.refreshNoCameraHint();
        ImDrawList* pDrawList         = ImGui::GetWindowDrawList();
        const bool  bShowNoCameraHint = _model.isNoCameraHintShown() && imageSize.x > 1.0f && imageSize.y > 1.0f;
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
            InputManager& input = editor::getSubsystem<InputManager>();
            editor::getSubsystem<ViewportInputOverlaySettings>()._overlay.draw( pDrawList, imagePos, imageSize, &input, &input.getInputMap() );
        }
    }

    void GameViewPanel::drawToolbar()
    {
        if ( EditorChrome::beginToolbar( "##GameViewToolbar" ) )
        {
            const utf8* pCurrentLabel = EditorViewTargetUtil::getAspectLabel( _model.getAspect() );
            ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldComboEm" ) );
            if ( ImGui::BeginCombo( "##GameViewAspect", pCurrentLabel ) )
            {
                for ( uint32 aspectIndex = 0; aspectIndex < static_cast<uint32>( EditorGameViewAspect::Count ); ++aspectIndex )
                {
                    const EditorGameViewAspect aspect = static_cast<EditorGameViewAspect>( aspectIndex );
                    if ( ImGui::Selectable( EditorViewTargetUtil::getAspectLabel( aspect ), aspect == _model.getAspect() ) )
                        _model.setAspect( aspect );
                }
                ImGui::EndCombo();
            }
            EditorSelfTestMarks::record( "gameView.aspect" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "GameViewAspect" ) );

            // 유니티 게임 뷰의 Maximize On Play 에 해당 — 값은 환경설정(Editor/Play)이고 바꾸면 바로 저장한다.
            EditorPlayPreferences& playPreferences = editPreferences<EditorPlayPreferences>();
            EditorChrome::sameLineIfFits( EditorChrome::measureCheckbox( SW_LOCKEY( "EdLabel", "Max" ) ) );
            if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "Max" ), &playPreferences._bMaximizeOnPlay ) && EditorPreferencesStore::saveOpened() == false )
                SW_LOG_WARNING_ID( "GameViewPanel.EditorPreferencesCouldNot", "Editor preferences could not be saved" );
            EditorSelfTestMarks::record( "gameView.maximizeOnPlay" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "MaximizeOnPlay" ) );

            ImGui::SameLine();
            // 게임 HUD(`DebugHUD`) — 런타임 UI 라 게임 뷰에 그대로 그려진다(멈춤 · 일시정지에는 숨는다). 게임 값(`DebugOverlayState`)은 HUD 의 overlay 섹션이다.
            EditorDebugHUDControls::drawMasterToggle( "gameView.hud" );

            ImGui::SameLine();
            // 디버그 드로우 겹치기(서버 디버깅 S6) — 이 에디터의 게임 뷰와, 멀티 프로세스 재생이 띄운 자식 프로세스(클라이언트의 권위 고스트) 모두에 같은 값을 건다.
            const bool bDebugDrawShown = DebugDrawCanvasUtil::isGameViewOverlayEnabled();
            if ( EditorWidgets::drawIconToggle( "##gameViewDebugDraw", SW_EDITOR_ICON( "Bug" ), bDebugDrawShown,
                                                "Debug draw in the game view - lines, spheres and text from DebugDrawQueue (authority ghosts too). Sent to net play children" ) )
                GameViewModel::setDebugDrawShown( bDebugDrawShown == false );
            EditorSelfTestMarks::record( "gameView.debugDraw" );

            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Camera" ), "##GameViewScreenshot" ) ) );
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( SW_EDITOR_ICON( "Camera" ), "##GameViewScreenshot" ) ) )
                EditorScreenshotCommands::captureGameView();
            EditorSelfTestMarks::record( "gameView.screenshot" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorScene", "GameViewScreenshot" ) );
        }
        EditorChrome::endToolbar();
    }
} // namespace sw::editor
