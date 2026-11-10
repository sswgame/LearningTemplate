#include "pch.h"

#include "Editor/Viewport/ViewportToolbarSettings.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
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
            /**
             * @brief 스냅 켜기 아이콘 토글과 간격 콤보를 그립니다.
             * @details 켜기 단추는 그 조작(이동 · 회전 · 크기)의 아이콘이다 — 언리얼 뷰포트 툴바의 스냅 묶음(아이콘 + 값)과 같다. 예전 글 단추("Grid Snap" · "Rot Snap")는
             *          크기 스냅을 더하자 기본 배치(1600 폭)에서 막대가 캔버스 밖으로 잘렸다.
             */
            static void drawSnapToggleCombo( const utf8* pToggleID, const utf8* pIcon, const utf8* pComboID, bool& bEnabled, float32& value,
                                             const float32* arrValue, const utf8* const* arrLabel, int32 valueCount,
                                             int32 fallbackIndex, bool bVertical, const utf8* pToggleTooltip,
                                             const utf8* pValueTooltip, const utf8* pToggleMark )
            {
                if ( EditorWidgets::drawIconToggle( pToggleID, pIcon, bEnabled, pToggleTooltip ) )
                    bEnabled = ( bEnabled == false );
                EditorSelfTestMarks::record( pToggleMark );

                ViewportToolbarSettings::nextItem( bVertical );
                // 폭은 가장 긴 값 글자 + 펼침 화살표다 — 고정 픽셀(60 · 65)은 글꼴이 바뀌자 "15 de" 로 잘렸다.
                float32 labelWidth = 0.0f;
                for ( int32 index = 0; index < valueCount; ++index )
                {
                    labelWidth = MathUtil::max( labelWidth, ImGui::CalcTextSize( arrLabel[index] ).x );
                }
                ImGui::SetNextItemWidth( labelWidth + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f );

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
             * @details 렌더러는 **호스트 서비스**입니다(선택 서비스 칸 — `FrameRendererSubsystem.cpp` 의 등록) — 씬 계층은 렌더러를 모릅니다
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
    void ViewportToolbarSettings::nextItem( bool bVertical )
    {
        if ( bVertical == false )
            ImGui::SameLine();
    }

    namespace
    {
        /** @brief 콤보에 보일 보기 모드 이름(지금 언어)입니다. 표의 `_pName` 은 로그 · 테스트 표지가 쓰는 식별자라 그대로 둔다. */
        const utf8* getViewModeDisplayName( RenderViewMode mode )
        {
            switch ( mode )
            {
                case RenderViewMode::Lit:
                    return SW_LOCKEY( "EdViewMode", "Lit" );
                case RenderViewMode::Unlit:
                    return SW_LOCKEY( "EdViewMode", "Unlit" );
                case RenderViewMode::Wireframe:
                    return SW_LOCKEY( "EdViewMode", "Wireframe" );
                case RenderViewMode::Normals:
                    return SW_LOCKEY( "EdViewMode", "Normals" );
                case RenderViewMode::Depth:
                    return SW_LOCKEY( "EdViewMode", "Depth" );
                case RenderViewMode::Overdraw:
                    return SW_LOCKEY( "EdViewMode", "Overdraw" );
                case RenderViewMode::LightingOnly:
                    return SW_LOCKEY( "EdViewMode", "LightingOnly" );
                case RenderViewMode::Roughness:
                    return SW_LOCKEY( "EdViewMode", "Roughness" );
                case RenderViewMode::Metallic:
                    return SW_LOCKEY( "EdViewMode", "Metallic" );
                case RenderViewMode::Mipmaps:
                    return SW_LOCKEY( "EdViewMode", "Mipmaps" );
                case RenderViewMode::LightComplexity:
                    return SW_LOCKEY( "EdViewMode", "LightComplexity" );
                case RenderViewMode::ShadedWireframe:
                    return SW_LOCKEY( "EdViewMode", "ShadedWireframe" );
                case RenderViewMode::LODLevel:
                    return SW_LOCKEY( "EdViewMode", "LODLevel" );
                case RenderViewMode::LODFadeCode:
                    return SW_LOCKEY( "EdViewMode", "LODFadeCode" );
                case RenderViewMode::MarkerSlots:
                    return SW_LOCKEY( "EdViewMode", "MarkerSlots" );
                case RenderViewMode::ShadingPath:
                    return SW_LOCKEY( "EdViewMode", "ShadingPath" );
                case RenderViewMode::Count:
                    break;
            }
            return getRenderViewModeInfo( mode )._pName;
        }
    } // namespace

    void ViewportToolbarSettings::drawViewBar( ViewportToolbarSettings& settings, bool bVertical )
    {
        {
            // 씬 뷰의 보기 모드다(`FrameRenderer::setSceneViewMode`) — 게임 뷰는 늘 주 출력 모드(Lit)로 그린다(언리얼 뷰포트마다의 View Mode).
            // 값이 아니라 **렌더러 상태**가 원본이므로 프레임마다 렌더러에서 읽어 표시한다. 항목 목록은 보기 모드 표(kArrRenderViewModeInfo)를 돈다.
            FrameRenderer*       pRenderer   = EditorViewportToolbarInternal::findFrameRenderer();
            const RenderViewMode currentMode = ( pRenderer != nullptr ) ? pRenderer->getSceneViewMode() : RenderViewMode::Lit;

            ImGui::BeginDisabled( pRenderer == nullptr );
            EditorChrome::setNextItemWidthColumns( 13.0f );
            const bool bOpen = ImGui::BeginCombo( "##ViewMode", getViewModeDisplayName( currentMode ) );
            EditorSelfTestMarks::record( "viewport.viewMode" ); // 시나리오가 콤보를 열고 항목(`viewport.viewMode.<이름>`)을 누른다
            if ( bOpen )
            {
                for ( uint32 modeIndex = 0; modeIndex < static_cast<uint32>( RenderViewMode::Count ); ++modeIndex )
                {
                    const RenderViewMode mode  = static_cast<RenderViewMode>( modeIndex );
                    const utf8*          pName = getRenderViewModeInfo( mode )._pName;
                    if ( ImGui::Selectable( EditorThemeUtil::makeStableLabel( getViewModeDisplayName( mode ), pName ), mode == currentMode ) && pRenderer != nullptr )
                        pRenderer->setSceneViewMode( mode );
                    const string markKey = string( "viewport.viewMode." ) + StringUtil::toLower( pName );
                    EditorSelfTestMarks::record( markKey.c_str() );
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
            if ( EditorWidgets::drawToggleIconButton( "##viewDimension", b2D, SW_EDITOR_ICON( "Sprite" ), SW_EDITOR_ICON( "Cube" ),
                                                      SW_LOCKEY( "EdCommon", "View2D" ),
                                                      SW_LOCKEY( "EdCommon", "View3D" ) ) )
                settings._bIs2DMode = ( settings._bIs2DMode == false );
            EditorSelfTestMarks::record( "viewport.dimension" );
        }

        nextItem( bVertical );
        EditorChrome::setNextItemWidthColumns( 9.0f );
        ImGui::SliderFloat( "##CamSpeed", &settings._cameraSpeed, 0.5f, 20.0f, "%.1f" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "CameraSpeed" ) );
    }

    void ViewportToolbarSettings::drawDisplayBar( ViewportToolbarSettings& settings, bool bVertical )
    {
        // 씬 뷰 HUD — 게임 HUD 와 같은 섹션(언리얼 뷰포트 stat 에 해당). 켜고 끄기는 뷰마다, 섹션은 오른쪽 클릭 팝업(게임 HUD 와 같은 설정).
        if ( EditorWidgets::drawIconToggle( "##hud", SW_EDITOR_ICON( "Chart" ), settings._bShowHUD,
                                            SW_LOCKEY( "EdCommon", "ViewportHUD" ) ) )
            settings._bShowHUD = ( settings._bShowHUD == false );
        EditorSelfTestMarks::record( "viewport.hud" );
        EditorDebugHUDControls::drawSectionPopupOnRightClick( "##sceneHUDSections" );
        nextItem( bVertical );
        if ( EditorWidgets::drawIconToggle( "##grid", SW_EDITOR_ICON( "Grid" ), settings._bShowGrid, SW_LOCKEY( "EdCommon", "ViewportGrid" ) ) )
            settings._bShowGrid = ( settings._bShowGrid == false );
        nextItem( bVertical );
        if ( EditorWidgets::drawIconToggle( "##cube", SW_EDITOR_ICON( "Axes" ), settings._bShowOrientationCube,
                                            SW_LOCKEY( "EdCommon", "ViewportOrientationCube" ) ) )
            settings._bShowOrientationCube = ( settings._bShowOrientationCube == false );

        // 컴포넌트 시각화 토글은 시각화 레지스트리에서 만든다. 시각화를 더해도 여기는 그대로다.
        for ( uint32 index = 0; index < EditorViewportVisualizer::getCount(); ++index )
        {
            const EditorVisualizerRegistration& visualizer = EditorViewportVisualizer::getAt( index );
            const bool                          bOn        = settings._visualizerToggles.isOn( visualizer );

            nextItem( bVertical );
            ImGui::PushID( visualizer._pID );
            fixed_string<constant::buffer::kMax256> tooltip;
            // 등록은 키(`SW_LOCKEYREF`)를 든다 — 지금 언어로 풀어 보이고, 버튼 ID 는 시각화 id 로 고정한다.
            const utf8* const pLabel = LocalizedText::find( visualizer._pToggleLabel, visualizer._pToggleLabel );
            formatstring( tooltip.data(), tooltip.capacity(), "%#: %#", pLabel, LocalizedText::find( visualizer._pTooltip, visualizer._pTooltip ) );
            const bool bPressed = visualizer._pIconName.isEmpty() == false ? EditorWidgets::drawIconToggle( "##visualizer", EditorIconGlyphTable::findGlyphByName( visualizer._pIconName ), bOn, tooltip.c_str() )
                                                                           : EditorWidgets::drawToggleButton( EditorThemeUtil::makeStableLabel( pLabel, visualizer._pID ), bOn );
            if ( bPressed )
                settings._visualizerToggles.setOn( visualizer, bOn == false );
            fixed_string<constant::buffer::kMax64> mark;
            formatstring( mark.data(), mark.capacity(), "viewport.visualizer.%#", visualizer._pID );
            EditorSelfTestMarks::record( mark.c_str() );
            if ( visualizer._pIconName.isEmpty() )
                EditorWidgets::drawTooltip( tooltip.c_str() );
            ImGui::PopID();
        }

        nextItem( bVertical );
        if ( EditorWidgets::drawIconToggle( "##surfaceSnap", SW_EDITOR_ICON( "Magnet" ), settings._bSurfaceSnap,
                                            SW_LOCKEY( "EdCommon", "SurfaceSnap" ) ) )
            settings._bSurfaceSnap = ( settings._bSurfaceSnap == false );
    }

    void ViewportToolbarSettings::drawToolsBar( ViewportToolbarSettings& settings, bool bVertical )
    {
        const float32 side = ImGui::GetFrameHeight();
        if ( ImGui::Button( SW_EDITOR_ICON( "Bookmark" ), ImVec2{ side, side } ) )
            EditorInputGUI::openPopup( "##ViewportBookmarksPopup" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "CameraBookmarks" ) );

        if ( ImGui::BeginPopup( "##ViewportBookmarksPopup" ) )
        {
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "CameraBookmarksCtrl19" ) );
            ImGui::Separator();
            {
                EditorWorkspace& ws = editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>();
                for ( uint32 slot = 0; slot < 9; ++slot )
                {
                    const bool                             bHas = ws.hasCameraBookmark( slot );
                    fixed_string<constant::buffer::kMax64> arrLabel;
                    formatstring( arrLabel.data(), arrLabel.capacity(), "Slot %u: %s", slot + 1, bHas ? ws.getCameraBookmark( slot )->_name.c_str() : "<Empty>" );
                    if ( ImGui::Selectable( arrLabel.c_str(), false ) && bHas )
                        settings._requestedBookmarkSlot = static_cast<uint32>( slot );
                    if ( ImGui::IsItemHovered() && bHas )
                    {
                        const CameraBookmark* pBm = ws.getCameraBookmark( slot );
                        if ( pBm != nullptr )
                        {
                            fixed_string<constant::buffer::kMax128> positionText;
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
        if ( ImGui::Button( SW_EDITOR_ICON( "Align" ), ImVec2{ side, side } ) )
            EditorInputGUI::openPopup( "##ViewportAlignPopup" );
        ImGui::EndDisabled();
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "AlignSelected" ) );
        if ( ImGui::BeginPopup( "##ViewportAlignPopup" ) )
        {
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "MultiObjectAlignment" ) );
            ImGui::Separator();
            // 항목 · 구분선은 커맨드 테이블의 메뉴 경로 필드(`commandmenu::kViewportAlign`)에서 나온다.
            EditorCommandGUI::drawMenuItems( commandmenu::kViewportAlign );
            ImGui::EndPopup();
        }
    }

    void ViewportToolbarSettings::drawTransformBar( ViewportToolbarSettings& settings, bool bVertical, bool bEnabled )
    {
        ImGui::BeginDisabled( bEnabled == false );
        EditorWidgets::drawGizmoOperationControls( "gizmo", bVertical );

        nextItem( bVertical );
        const float32 arrSnapValue[] = { 0.1f, 0.5f, 1.0f, 5.0f, 10.0f };
        const utf8*   arrSnapLabel[] = { "0.1", "0.5", "1.0", "5.0", "10.0" };
        EditorViewportToolbarInternal::drawSnapToggleCombo( "##gridSnap", SW_EDITOR_ICON( "Grid" ), "##GridSnapVal", settings._bGridSnap, settings._gridSnapValue, arrSnapValue, arrSnapLabel, 5, 2, bVertical,
                                                            SW_LOCKEY( "EdCommon", "GridSnap" ),
                                                            SW_LOCKEY( "EdCommon", "GridSnapSize" ), "viewport.gridSnap" );

        nextItem( bVertical );
        const float32 arrRotValue[] = { 5.0f, 15.0f, 45.0f, 90.0f };
        const utf8*   arrRotLabel[] = { "5\xc2\xb0", "15\xc2\xb0", "45\xc2\xb0", "90\xc2\xb0" }; // 5° …(UTF-8 도 기호)
        EditorViewportToolbarInternal::drawSnapToggleCombo( "##rotationSnap", SW_EDITOR_ICON( "Rotate" ), "##RotSnapVal", settings._bRotationSnap, settings._rotationSnapValue, arrRotValue, arrRotLabel,
                                                            4, 1, bVertical,
                                                            SW_LOCKEY( "EdCommon", "RotationSnap" ),
                                                            SW_LOCKEY( "EdCommon", "RotationSnapSize" ), "viewport.rotationSnap" );

        // 크기 스냅 — 설정 · 기즈모는 이미 값을 읽는데 켤 단추가 없었다(언리얼 뷰포트 툴바 · Godot 스냅 설정은 이동 · 회전 · 크기 셋을 나란히 둔다).
        nextItem( bVertical );
        const float32 arrScaleValue[] = { 0.1f, 0.25f, 0.5f, 1.0f };
        const utf8*   arrScaleLabel[] = { "0.1", "0.25", "0.5", "1.0" };
        EditorViewportToolbarInternal::drawSnapToggleCombo( "##scaleSnap", SW_EDITOR_ICON( "Scale" ), "##ScaleSnapVal", settings._bScaleSnap, settings._scaleSnapValue, arrScaleValue,
                                                            arrScaleLabel, 4, 0, bVertical,
                                                            SW_LOCKEY( "EdCommon", "ScaleSnap" ),
                                                            SW_LOCKEY( "EdCommon", "ScaleSnapSize" ), "viewport.scaleSnap" );
        ImGui::EndDisabled();
    }
} // namespace sw::editor
