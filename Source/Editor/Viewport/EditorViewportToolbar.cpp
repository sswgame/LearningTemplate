#include "pch.h"

#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorCommandGUI.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorDebugHUDControls.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
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
                                             float32 comboWidth, int32 fallbackIndex, bool bVertical, const utf8* pToggleTooltip,
                                             const utf8* pValueTooltip )
            {
                if ( EditorWidgets::drawToggleButton( pButtonLabel, bEnabled ) )
                    bEnabled = ( bEnabled == false );
                EditorWidgets::drawTooltip( pToggleTooltip );

                EditorViewportToolbar::nextItem( bVertical );
                ImGui::SetNextItemWidth( comboWidth * EditorThemeUtil::getDPIScale() );

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
                EditorWidgets::drawTooltip( pValueTooltip );
            }

            /**
             * @brief 호스트가 내준 FrameRenderer 입니다(없으면 nullptr).
             * @details 렌더러는 **호스트 서비스**입니다(`EngineServiceList.xxx` 의 선택 행) — 씬 계층은 렌더러를 모릅니다
             *          (Scene::update 주석). 테스트 하네스처럼 렌더러가 없는 호스트에서는 nullptr 이고, 그때 콤보는
             *          비활성입니다.
             */
            static FrameRenderer* findFrameRenderer()
            {
                return editor::findSubsystem<FrameRenderer>();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorViewportToolbar::nextItem( bool bVertical )
    {
        if ( bVertical == false )
            ImGui::SameLine();
    }

    void EditorViewportToolbar::drawViewBar( ViewportToolbarSettings& settings, bool bVertical )
    {
        {
            // 씬 뷰의 보기 모드다(`FrameRenderer::setSceneViewMode`) — 게임 뷰는 늘 주 출력 모드(Lit)로 그린다(언리얼 뷰포트마다의 View Mode).
            // 값이 아니라 **렌더러 상태**가 원본이므로 프레임마다 렌더러에서 읽어 표시한다. 항목 목록은 보기 모드 표(kArrRenderViewModeInfo)를 돈다.
            FrameRenderer*       pRenderer   = EditorViewportToolbarInternal::findFrameRenderer();
            const RenderViewMode currentMode = ( pRenderer != nullptr ) ? pRenderer->getSceneViewMode() : RenderViewMode::Lit;

            ImGui::BeginDisabled( pRenderer == nullptr );
            EditorChrome::setNextItemWidthColumns( 13.0f );
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
            const utf8* pViewModeTooltip = SW_LOCKEY( "EdCommon", "ViewModeNoRenderer" );
            if ( pRenderer != nullptr )
                pViewModeTooltip = SW_LOCKEY( "EdCommon", "ViewMode" );
            EditorWidgets::drawTooltip( pViewModeTooltip );
        }

        nextItem( bVertical );
        {
            const bool b2D = settings._bIs2DMode;
            if ( EditorWidgets::drawToggleIconButton( "##viewDimension", b2D, editoricon::kSprite, editoricon::kCube,
                                                      SW_LOCKEY( "EdCommon", "View2D" ),
                                                      SW_LOCKEY( "EdCommon", "View3D" ) ) )
                settings._bIs2DMode = ( settings._bIs2DMode == false );
            EditorSelfTestMarks::note( "viewport.dimension" );
        }

        nextItem( bVertical );
        EditorChrome::setNextItemWidthColumns( 9.0f );
        ImGui::SliderFloat( "##CamSpeed", &settings._cameraSpeed, 0.5f, 20.0f, "%.1f" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "CameraSpeed" ) );
    }

    void EditorViewportToolbar::drawDisplayBar( ViewportToolbarSettings& settings, bool bVertical )
    {
        // 씬 뷰 HUD — 게임 HUD 와 같은 섹션(언리얼 뷰포트 stat 에 해당). 켜고 끄기는 뷰마다, 섹션은 오른쪽 클릭 팝업(게임 HUD 와 같은 설정).
        if ( EditorWidgets::drawIconToggle( "##hud", editoricon::kChart, settings._bShowHUD,
                                            SW_LOCKEY( "EdCommon", "ViewportHUD" ) ) )
            settings._bShowHUD = ( settings._bShowHUD == false );
        EditorSelfTestMarks::note( "viewport.hud" );
        EditorDebugHUDControls::drawSectionPopupOnRightClick( "##sceneHUDSections" );
        nextItem( bVertical );
        if ( EditorWidgets::drawIconToggle( "##grid", editoricon::kGrid, settings._bShowGrid, SW_LOCKEY( "EdCommon", "ViewportGrid" ) ) )
            settings._bShowGrid = ( settings._bShowGrid == false );
        nextItem( bVertical );
        if ( EditorWidgets::drawIconToggle( "##cube", editoricon::kAxes, settings._bShowOrientationCube,
                                            SW_LOCKEY( "EdCommon", "ViewportOrientationCube" ) ) )
            settings._bShowOrientationCube = ( settings._bShowOrientationCube == false );

        // 컴포넌트 시각화 토글은 시각화 레지스트리에서 만든다. 시각화를 더해도 여기는 그대로다.
        for ( uint32 index = 0; index < EditorViewportVisualizer::getCount(); ++index )
        {
            const EditorVisualizerRegistration& visualizer = EditorViewportVisualizer::getAt( index );
            const bool                          bOn        = settings._visualizerToggles.isOn( visualizer );

            nextItem( bVertical );
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

        nextItem( bVertical );
        if ( EditorWidgets::drawIconToggle( "##surfaceSnap", editoricon::kMagnet, settings._bSurfaceSnap,
                                            SW_LOCKEY( "EdCommon", "SurfaceSnap" ) ) )
            settings._bSurfaceSnap = ( settings._bSurfaceSnap == false );
    }

    void EditorViewportToolbar::drawToolsBar( ViewportToolbarSettings& settings, bool bVertical )
    {
        const float32 side = ImGui::GetFrameHeight();
        if ( ImGui::Button( editoricon::kBookmark, ImVec2{ side, side } ) )
            EditorInputGUI::openPopup( "##ViewportBookmarksPopup" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "CameraBookmarks" ) );

        if ( ImGui::BeginPopup( "##ViewportBookmarksPopup" ) )
        {
            ImGui::Text( "Camera Bookmarks (Ctrl+1~9)" );
            ImGui::Separator();
            {
                EditorWorkspace& ws = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
                for ( uint32 slot = 0; slot < 9; ++slot )
                {
                    const bool                           bHas = ws.hasCameraBookmark( slot );
                    fixed_string<constant::kMaxBuffer64> arrLabel;
                    formatstring( arrLabel.data(), arrLabel.capacity(), "Slot %u: %s", slot + 1, bHas ? ws.getCameraBookmark( slot )->_name.c_str() : "<Empty>" );
                    if ( ImGui::Selectable( arrLabel.c_str(), false ) && bHas )
                        settings._requestedBookmarkSlot = static_cast<int32>( slot );
                    if ( ImGui::IsItemHovered() && bHas )
                    {
                        const CameraBookmark* pBm = ws.getCameraBookmark( slot );
                        if ( pBm != nullptr )
                        {
                            fixed_string<constant::kMaxBuffer128> positionText;
                            formatstring( positionText.data(), positionText.capacity(), "(%.1f, %.1f, %.1f)", static_cast<float64>( pBm->_position._x ),
                                          static_cast<float64>( pBm->_position._y ), static_cast<float64>( pBm->_position._z ) );
                            const string tooltipText = SW_LOCKEYFORMAT( "EdCommon", "CameraBookmarkPosition", TextArgumentList().addText( "position", positionText.c_str() ) );
                            EditorWidgets::drawTooltip( tooltipText.c_str() );
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }

        // 정렬은 둘 이상 골랐을 때만 쓴다 — 단추는 늘 두고 막아 둔다(바 크기가 선택마다 바뀌지 않게).
        const bool bCanAlign = editor::getMainSceneSelection().getSelectedObjectCount() >= 2;
        nextItem( bVertical );
        ImGui::BeginDisabled( bCanAlign == false );
        if ( ImGui::Button( editoricon::kAlign, ImVec2{ side, side } ) )
            EditorInputGUI::openPopup( "##ViewportAlignPopup" );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "AlignSelected" ) );
        if ( ImGui::BeginPopup( "##ViewportAlignPopup" ) )
        {
            ImGui::Text( "Multi-Object Alignment" );
            ImGui::Separator();
            // 항목 · 구분선은 커맨드 테이블의 메뉴 경로 필드(`commandmenu::kViewportAlign`)에서 나온다.
            EditorCommandGUI::drawMenuItems( commandmenu::kViewportAlign );
            ImGui::EndPopup();
        }
    }

    void EditorViewportToolbar::drawTransformBar( ViewportToolbarSettings& settings, bool bVertical, bool bEnabled )
    {
        ImGui::BeginDisabled( bEnabled == false );
        EditorWidgets::drawGizmoOperationControls( "gizmo", bVertical );

        nextItem( bVertical );
        const float32 arrSnapValue[] = { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
        const utf8*   arrSnapLabel[] = { "0.1", "0.5", "1.0", "5.0", "10.0" };
        EditorViewportToolbarInternal::drawSnapToggleCombo( "Grid Snap", "##GridSnapVal", settings._bGridSnap, settings._gridSnapValue, arrSnapValue, arrSnapLabel, 5,
                                                            65.0f, 2, bVertical,
                                                            SW_LOCKEY( "EdCommon", "GridSnap" ),
                                                            SW_LOCKEY( "EdCommon", "GridSnapSize" ) );

        nextItem( bVertical );
        const float32 arrRotValue[] = { 5.0f, 15.0f, 45.0f, 90.0f };
        const utf8*   arrRotLabel[] = { "5 deg", "15 deg", "45 deg", "90 deg" };
        EditorViewportToolbarInternal::drawSnapToggleCombo( "Rot Snap", "##RotSnapVal", settings._bRotationSnap, settings._rotationSnapValue, arrRotValue, arrRotLabel,
                                                            4, 60.0f, 1, bVertical,
                                                            SW_LOCKEY( "EdCommon", "RotationSnap" ),
                                                            SW_LOCKEY( "EdCommon", "RotationSnapSize" ) );
        ImGui::EndDisabled();
    }
} // namespace sw::editor
