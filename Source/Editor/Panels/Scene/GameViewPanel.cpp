#include "pch.h"

#include "Editor/Panels/Scene/GameViewPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Commands/EditorScreenshotCommands.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorDebugHUDControls.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/ViewportInputOverlay.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Scene/Scene.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( GameViewPanel, "game_view", EditorPanelCategory::Core, 350 );

    GameViewPanel::GameViewPanel()
        : _aspect{ EditorGameViewAspect::Free }
        , _bNoCameraHintShown{ false }
    {
    }

    void GameViewPanel::onPanelCollapsed()
    {
        EditorContext* pClosedContext = EditorContext::get();
        if ( pClosedContext != nullptr )
        {
            pClosedContext->setGameViewFocused( false );
            pClosedContext->setGameViewHovered( false );
        }
    }

    void GameViewPanel::drawContent()
    {
        EditorContext* pEditorContext = EditorContext::get();
        if ( pEditorContext == nullptr )
            return;

        // Play 중 게임 입력은 이 패널 위에서만 게임으로 간다(`ImGuiEditor::processEvent`). 씬 뷰 위의 입력은 에디터 카메라 몫이다.
        pEditorContext->setGameViewFocused( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) );
        if ( pEditorContext->isGameViewFocused() )
            EditorScreenshotCommands::noteFocusedView( EditorViewKind::Game );
        pEditorContext->setGameViewHovered( ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows ) );

        drawToolbar();

        const ImVec2         available = ImGui::GetContentRegionAvail();
        const EditorViewRect rect      = EditorViewTargetUtil::fitViewImage( float2{ available.x, available.y }, _aspect );
        const uint32         width     = static_cast<uint32>( rect._size._x );
        const uint32         height    = static_cast<uint32>( rect._size._y );
        if ( width > 1 && height > 1 )
        {
            const EditorViewTarget& view = pEditorContext->getViewTarget( EditorViewKind::Game );
            if ( EditorViewTargetUtil::needsResize( view._width, view._height, width, height ) )
                pEditorContext->ensureViewTargetSize( EditorViewKind::Game, width, height );
        }
        // 이 패널이 보이는 프레임만 게임 뷰를 그린다(접힘 · 닫힘 · 다른 탭이면 drawContent 가 불리지 않는다 — 게임 뷰 RT 요청 0).
        pEditorContext->markViewDrawn( EditorViewKind::Game );

        // 레터박스 — 남는 쪽은 비워 둔다. 이미지는 RT 크기 그대로(늘이지 않는다).
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 imagePos{ origin.x + rect._offset._x, origin.y + rect._offset._y };
        const ImVec2 imageSize{ rect._size._x, rect._size._y };
        ImGui::SetCursorScreenPos( imagePos );
        const void* pTextureID = pEditorContext->getViewTarget( EditorViewKind::Game )._pTextureID;
        if ( pTextureID != nullptr && imageSize.x > 1.0f && imageSize.y > 1.0f )
            ImGui::Image( reinterpret_cast<ImTextureID>( pTextureID ), imageSize );
        else
            ImGui::Dummy( ImVec2{ MathUtil::max( imageSize.x, 1.0f ), MathUtil::max( imageSize.y, 1.0f ) } );
        EditorSelfTestMarks::note( "gameView.canvas" );

        // 게임 카메라가 없으면(꺼짐 · 지움) 그릴 눈이 없다 — 지난 그림 위에 안내를 띄운다(유니티 "No cameras rendering").
        Scene*                 pScene      = editor::getActiveScene();
        const CameraComponent* pGameCamera = pScene != nullptr ? pScene->getActiveGameCamera() : nullptr;
        _bNoCameraHintShown                = CameraRegistry::isUsableCamera( pGameCamera ) == false;
        ImDrawList* pDrawList              = ImGui::GetWindowDrawList();
        if ( _bNoCameraHintShown && imageSize.x > 1.0f && imageSize.y > 1.0f )
        {
            constexpr const utf8* kHint    = "No camera rendering";
            const ImVec2          hintSize = ImGui::CalcTextSize( kHint );
            pDrawList->AddRectFilled( imagePos, ImVec2{ imagePos.x + imageSize.x, imagePos.y + imageSize.y }, IM_COL32( 0, 0, 0, 255 ) );
            pDrawList->AddText( ImVec2{ imagePos.x + ( imageSize.x - hintSize.x ) * 0.5f, imagePos.y + ( imageSize.y - hintSize.y ) * 0.5f },
                                IM_COL32( 220, 220, 220, 255 ), kHint );
        }

        if ( imageSize.x > 1.0f && imageSize.y > 1.0f )
        {
            InputManager* pInput = getService<InputManager>();
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
            EditorWidgets::drawTooltip( SW_LOCTEXT( "EditorScene", "GameViewAspect", "Game view aspect ratio - Free (whole panel) or 16:9 (letterboxed on the spare side)" ) );

            ImGui::SameLine();
            // 유니티 게임 뷰의 Maximize On Play 자리 — 값은 환경설정(Editor/Play)이고 바꾸면 바로 저장한다.
            EditorPlayPreferences& playPreferences = *static_cast<EditorPlayPreferences*>( EditorSettingsInstance<EditorPlayPreferences>::getInstance() );
            if ( ImGui::Checkbox( "Max", &playPreferences._bMaximizeOnPlay ) && EditorPreferencesStore::saveAll( EditorPreferencesStore::getDefaultFilePath() ) == false )
                SW_LOG_WARNING( "Editor preferences could not be saved" );
            EditorSelfTestMarks::note( "gameView.maximizeOnPlay" );
            EditorWidgets::drawTooltip( SW_LOCTEXT( "EditorScene", "MaximizeOnPlay", "Maximize On Play - keep only the game view while playing and restore the layout on Stop" ) );

            ImGui::SameLine();
            // 게임 HUD(`DebugHUD`) — 런타임 UI 라 게임 뷰에 그대로 그려진다(멈춤 · 일시정지에는 숨는다). 게임 값(`DebugOverlayState`)은 HUD 의 overlay 섹션이다.
            EditorDebugHUDControls::drawMasterToggle( "gameView.hud" );

            ImGui::SameLine();
            if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kCamera, "##GameViewScreenshot" ) ) )
                EditorScreenshotCommands::captureGameView();
            EditorSelfTestMarks::note( "gameView.screenshot" );
            EditorWidgets::drawTooltip( SW_LOCTEXT( "EditorScene", "GameViewScreenshot", "Save the game view image to Saved/Screenshots/<time>.png (F9 - the focused view)" ) );
        }
        EditorChrome::endToolbar();
    }
} // namespace sw::editor
