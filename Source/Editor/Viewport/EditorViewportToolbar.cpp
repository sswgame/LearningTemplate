#include "pch.h"

#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Renderer/Frame/FrameRenderer.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportToolbarInternal
        {
            static void drawSnapToggleCombo( const utf8* pButtonLabel, const utf8* pComboID, bool& bEnabled, float32& value,
                                             const float32* arrValue, const utf8* const* arrLabel, int32 valueCount,
                                             float32 comboWidth, int32 fallbackIndex )
            {
                if ( EditorWidgets::drawToggleButton( pButtonLabel, bEnabled ) )
                    bEnabled = ( bEnabled == false );

                ImGui::SameLine();
                ImGui::SetNextItemWidth( comboWidth );

                int32 currentIndex = fallbackIndex;
                for ( int32 index = 0; index < valueCount; ++index )
                {
                    if ( MathUtil::nearEqual( value, arrValue[index], 0.001f ) )
                    {
                        currentIndex = index;
                        break;
                    }
                }

                if ( ImGui::Combo( pComboID, &currentIndex, arrLabel, valueCount ) )
                {
                    if ( 0 <= currentIndex && currentIndex < valueCount )
                        value = arrValue[currentIndex];
                }
            }

            /**
             * @brief 호스트가 내준 FrameRenderer 입니다(없으면 nullptr).
             * @details 렌더러는 **호스트 서비스**입니다(`EngineServiceList.xxx` 의 선택 행) — 씬 계층은 렌더러를 모릅니다
             *          (Scene::tick 주석). 테스트 하네스처럼 렌더러가 없는 호스트에서는 nullptr 이고, 그때 콤보는
             *          비활성입니다.
             */
            static FrameRenderer* findFrameRenderer()
            {
                return editor::getService<FrameRenderer>();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorViewportToolbar::draw( ViewportToolbarSettings& settings, float32 viewportWidth )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2{ 6.0f, 2.0f } );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.18f, 0.18f, 0.22f, 0.85f } );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4{ 0.28f, 0.28f, 0.32f, 1.0f } );

        {
            // 씬 뷰의 보기 모드다(`FrameRenderer::setSceneViewMode`) — 게임 뷰는 늘 주 출력 모드(Lit)로 그린다(언리얼 뷰포트마다의 View Mode).
            // 값이 아니라 **렌더러 상태**가 정본이므로 프레임마다 렌더러에서 읽어 표시한다. 항목 목록은 보기 모드 표(kArrRenderViewModeInfo)를 돈다.
            FrameRenderer*       pRenderer   = EditorViewportToolbarInternal::findFrameRenderer();
            const RenderViewMode currentMode = ( pRenderer != nullptr ) ? pRenderer->getSceneViewMode() : RenderViewMode::Lit;

            ImGui::BeginDisabled( pRenderer == nullptr );
            ImGui::SetNextItemWidth( 95.0f * EditorThemeUtil::getDpiScale() );
            const bool bOpen = ImGui::BeginCombo( "##ViewMode", getRenderViewModeInfo( currentMode )._pName );
            EditorSelfTestMarks::note( "viewport.viewMode" ); // 시나리오가 콤보를 열고 항목(`viewport.viewMode.<이름>`)을 누른다
            if ( bOpen )
            {
                for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
                {
                    const RenderViewMode mode  = static_cast<RenderViewMode>( modeIndex );
                    const utf8*          pName = getRenderViewModeInfo( mode )._pName;
                    if ( ImGui::Selectable( pName, mode == currentMode ) && pRenderer != nullptr )
                        pRenderer->setSceneViewMode( mode );
                    const string markKey = string( "viewport.viewMode." ) + StringUtil::toLower( pName );
                    EditorSelfTestMarks::note( markKey.c_str() );
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( pRenderer != nullptr ? "씬 뷰 보기 모드 — Lit / Unlit / Wireframe / Normals / Depth / Overdraw (게임 뷰는 늘 Lit)"
                                                             : "렌더러가 아직 붙지 않았습니다" );
        }

        EditorWidgets::drawToolbarSeparator();

        {
            const bool b2D = settings._bIs2DMode;
            if ( EditorWidgets::drawToggleIconButton( "##viewDimension", b2D, editoricon::kSprite, editoricon::kCube, "2D view (XY plane grid) - click for 3D",
                                                      "3D view (XZ plane grid) - click for 2D" ) )
                settings._bIs2DMode = ( settings._bIs2DMode == false );
            EditorSelfTestMarks::note( "viewport.dimension" );
        }

        EditorWidgets::drawToolbarSeparator();

        {
            ImGui::TextDisabled( "Cam:" );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 70.0f * EditorThemeUtil::getDpiScale() );
            ImGui::SliderFloat( "##CamSpeed", &settings._cameraSpeed, 0.5f, 20.0f, "%.1f" );
        }

        // 숨김 문턱은 DPI 배율을 받는다(아이콘 단추의 한 변이 글꼴 높이를 따른다). 아이콘 단추라 글자 체크박스보다 좁아 문턱을 낮췄다.
        const float32 dpiScale = EditorThemeUtil::getDpiScale();
        if ( viewportWidth > 220.0f * dpiScale )
        {
            EditorWidgets::drawToolbarSeparator();
            if ( EditorWidgets::drawIconToggle( "##stats", editoricon::kChart, settings._bShowStats, "Stats overlay (FPS, objects, resolution)" ) )
                settings._bShowStats = ( settings._bShowStats == false );
            ImGui::SameLine();
            if ( EditorWidgets::drawIconToggle( "##grid", editoricon::kGrid, settings._bShowGrid, "Grid" ) )
                settings._bShowGrid = ( settings._bShowGrid == false );
            ImGui::SameLine();
            if ( EditorWidgets::drawIconToggle( "##cube", editoricon::kAxes, settings._bShowOrientationCube, "Orientation cube" ) )
                settings._bShowOrientationCube = ( settings._bShowOrientationCube == false );

            // 컴포넌트 시각화 토글은 시각화 등록부에서 만든다. 시각화를 더해도 여기는 그대로다.
            for ( uint32 index = 0; index < EditorViewportVisualizer::getCount(); ++index )
            {
                const EditorVisualizerRegistration& visualizer = EditorViewportVisualizer::getAt( index );
                const bool                          bOn        = settings._visualizerToggles.isOn( visualizer );

                ImGui::SameLine();
                ImGui::PushID( visualizer._pID );
                fixed_string<constant::kMaxBuffer256> tooltip;
                formatstring( tooltip.data(), tooltip.capacity(), "%#: %#", visualizer._pToggleLabel, visualizer._pTooltip );
                const bool bPressed = visualizer._pIcon != nullptr ? EditorWidgets::drawIconToggle( "##visualizer", visualizer._pIcon, bOn, tooltip.c_str() )
                                                                   : EditorWidgets::drawToggleButton( visualizer._pToggleLabel, bOn );
                if ( bPressed )
                    settings._visualizerToggles.setOn( visualizer, bOn == false );
                fixed_string<constant::kMaxBuffer64> mark;
                formatstring( mark.data(), mark.capacity(), "viewport.visualizer.%#", visualizer._pID );
                EditorSelfTestMarks::note( mark.c_str() );
                if ( visualizer._pIcon == nullptr )
                    EditorWidgets::drawTooltip( tooltip.c_str() );
                ImGui::PopID();
            }

            ImGui::SameLine();
            if ( EditorWidgets::drawIconToggle( "##surfaceSnap", editoricon::kMagnet, settings._bSurfaceSnap, "Surface snap - dragged objects land on surfaces" ) )
                settings._bSurfaceSnap = ( settings._bSurfaceSnap == false );
        }

        if ( viewportWidth > 270.0f * dpiScale )
        {
            EditorWidgets::drawToolbarSeparator();
            if ( ImGui::Button( editoricon::kBookmark, ImVec2{ ImGui::GetFrameHeight(), ImGui::GetFrameHeight() } ) )
                ImGui::OpenPopup( "##ViewportBookmarksPopup" );
            EditorWidgets::drawTooltip( "Camera bookmarks (Ctrl+1~9)" );

            if ( ImGui::BeginPopup( "##ViewportBookmarksPopup" ) )
            {
                ImGui::Text( "Camera Bookmarks (Ctrl+1~9)" );
                ImGui::Separator();
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                {
                    EditorWorkspace& ws = pContext->getWorkspace();
                    for ( uint32 slot = 0; slot < 9; ++slot )
                    {
                        const bool                           bHas = ws.hasCameraBookmark( slot );
                        fixed_string<constant::kMaxBuffer64> arrLabel;
                        formatstring( arrLabel.data(), arrLabel.capacity(), "Slot %u: %s", slot + 1,
                                      bHas ? ws.getCameraBookmark( slot )->_name.c_str() : "<Empty>" );
                        if ( ImGui::Selectable( arrLabel.c_str(), false ) && bHas )
                            settings._requestedBookmarkSlot = static_cast<int32>( slot );
                        if ( ImGui::IsItemHovered() && bHas )
                        {
                            const CameraBookmark* pBm = ws.getCameraBookmark( slot );
                            if ( pBm != nullptr )
                            {
                                fixed_string<constant::kMaxBuffer128> tooltipText;
                                formatstring( tooltipText.data(), tooltipText.capacity(), "Pos: (%.1f, %.1f, %.1f)",
                                              static_cast<float64>( pBm->_position._x ),
                                              static_cast<float64>( pBm->_position._y ),
                                              static_cast<float64>( pBm->_position._z ) );
                                EditorWidgets::drawTooltip( tooltipText.c_str() );
                            }
                        }
                    }
                }
                ImGui::EndPopup();
            }
        }

        if ( viewportWidth > 300.0f * dpiScale )
        {
            EditorContext* pContext = EditorContext::get();
            if ( pContext != nullptr && pContext->getEditorSelection().getSelectedObjectCount() >= 2 )
            {
                EditorWidgets::drawToolbarSeparator();
                if ( ImGui::Button( editoricon::kAlign, ImVec2{ ImGui::GetFrameHeight(), ImGui::GetFrameHeight() } ) )
                    ImGui::OpenPopup( "##ViewportAlignPopup" );
                EditorWidgets::drawTooltip( "Align and distribute the selected objects" );

                if ( ImGui::BeginPopup( "##ViewportAlignPopup" ) )
                {
                    ImGui::Text( "Multi-Object Alignment" );
                    ImGui::Separator();
                    // 항목 · 구분선은 커맨드 표의 메뉴 경로 칸(`commandmenu::kViewportAlign`)에서 나온다.
                    EditorCommandGUI::drawMenuItems( commandmenu::kViewportAlign );
                    ImGui::EndPopup();
                }
            }
        }

        ImGui::PopStyleColor( 2 );
        ImGui::PopStyleVar( 2 );
    }

    void EditorViewportToolbar::drawTransformBar( ViewportToolbarSettings& settings, const float2& anchorPos, float32 maxWidth,
                                                  bool bEnabled )
    {
        editor::EditorFloatingBarDesc barDesc{};
        barDesc._pID       = "##EditorTransformBar";
        barDesc._anchorPos = anchorPos;
        barDesc._pivot     = float2{ 0.5f, 0.0f };
        barDesc._maxWidth  = maxWidth;
        barDesc._bEnabled  = bEnabled;

        if ( EditorChrome::beginFloatingBar( barDesc ) == false )
        {
            EditorChrome::endFloatingBar();
            return;
        }

        EditorWidgets::drawGizmoOperationControls();

        EditorWidgets::drawToolbarSeparator();

        const float32 arrSnapValue[] = { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
        const utf8*   arrSnapLabel[] = { "0.1", "0.5", "1.0", "5.0", "10.0" };
        EditorViewportToolbarInternal::drawSnapToggleCombo( "Grid Snap", "##GridSnapVal", settings._bGridSnap, settings._gridSnapValue, arrSnapValue,
                                                            arrSnapLabel, 5, 65.0f, 2 );

        EditorWidgets::drawToolbarSeparator();

        const float32 arrRotValue[] = { 5.0f, 15.0f, 45.0f, 90.0f };
        const utf8*   arrRotLabel[] = { "5 deg", "15 deg", "45 deg", "90 deg" };
        EditorViewportToolbarInternal::drawSnapToggleCombo( "Rot Snap", "##RotSnapVal", settings._bRotationSnap, settings._rotationSnapValue,
                                                            arrRotValue, arrRotLabel, 4, 60.0f, 1 );

        EditorChrome::endFloatingBar();
    }
} // namespace sw::editor
