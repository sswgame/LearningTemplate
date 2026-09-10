#include "pch.h"

#include "Editor/Viewport/EditorViewportToolbar.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Gui/EditorCommandGui.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportToolbarInternal
        {
            static void drawSnapToggleCombo( const utf8* pButtonLabel, const utf8* pComboId, bool& bEnabled, float32& value,
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

                if ( ImGui::Combo( pComboId, &currentIndex, arrLabel, valueCount ) )
                {
                    if ( 0 <= currentIndex && currentIndex < valueCount )
                        value = arrValue[currentIndex];
                }
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
            // 이 콤보는 **아직 아무 일도 하지 않는다.** `_renderMode` 를 읽는 곳이 렌더러에 없다 —
            // 고르면 값만 바뀌고 화면은 그대로였다. 거짓 컨트롤보다 비활성 컨트롤이 정직하다.
            // 연결하려면 렌더러 쪽 작업이 필요하다: 파이프라인 리소스에 채우기 모드를 노출하고
            // 와이어프레임 PSO 변형을 만들고, 프레임 렌더러가 뷰 모드를 골라야 한다.
            // (RHI 는 네 백엔드 모두 RHIFillMode::Wireframe 을 지원한다.) 자세한 것은 백로그.
            ImGui::BeginDisabled();
            ImGui::SetNextItemWidth( 85.0f );
            const utf8* arrModeLabel[] = { "Lit", "Unlit", "Wireframe" };
            int32       modeIndex      = static_cast<int32>( settings._renderMode );
            ImGui::Combo( "##ViewMode", &modeIndex, arrModeLabel, 3 );
            ImGui::EndDisabled();
            EditorWidgets::drawTooltip( "뷰 모드 전환은 아직 렌더러에 연결되지 않았습니다 (백로그 참고)" );
        }

        EditorWidgets::drawToolbarSeparator();

        {
            const bool b2D = settings._bIs2DMode;
            if ( EditorWidgets::drawToggleButton( b2D ? "2D Mode" : "3D Mode", b2D ) )
                settings._bIs2DMode = ( settings._bIs2DMode == false );

            EditorWidgets::drawTooltip( "Toggle 2D (XY Plane Grid) / 3D (XZ Plane Grid) View Mode" );
        }

        EditorWidgets::drawToolbarSeparator();

        {
            ImGui::TextDisabled( "Cam:" );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 70.0f );
            ImGui::SliderFloat( "##CamSpeed", &settings._cameraSpeed, 0.5f, 20.0f, "%.1f" );
        }

        if ( viewportWidth > 320.0f )
        {
            EditorWidgets::drawToolbarSeparator();
            ImGui::Checkbox( "Stats", &settings._bShowStats );
            ImGui::SameLine();
            ImGui::Checkbox( "Grid", &settings._bShowGrid );
            ImGui::SameLine();
            ImGui::Checkbox( "Cube", &settings._bShowOrientationCube );

            // 컴포넌트 시각화 체크박스는 시각화 표에서 만든다 — 시각화를 더해도 여기는 그대로다.
            uint32                                     visualizerCount{ 0 };
            const EditorViewportVisualizer::Row* const pVisualizerRow = EditorViewportVisualizer::getRows( visualizerCount );
            for ( uint32 index = 0; index < visualizerCount; ++index )
            {
                const uint32 maskBit = EditorViewportVisualizer::getMaskBit( index );
                bool         bOn     = ( settings._visualizerMask & maskBit ) != 0;

                ImGui::SameLine();
                ImGui::PushID( static_cast<int32>( index ) );
                if ( ImGui::Checkbox( pVisualizerRow[index]._pToggleLabel, &bOn ) )
                {
                    if ( bOn )
                        settings._visualizerMask |= maskBit;
                    else
                        settings._visualizerMask &= ~maskBit;
                }
                EditorWidgets::drawTooltip( pVisualizerRow[index]._pTooltip );
                ImGui::PopID();
            }

            ImGui::SameLine();
            ImGui::Checkbox( "Surf", &settings._bSurfaceSnap );
        }

        if ( viewportWidth > 420.0f )
        {
            EditorWidgets::drawToolbarSeparator();
            if ( ImGui::Button( "Bookmarks" ) )
                ImGui::OpenPopup( "##ViewportBookmarksPopup" );

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
                        {
                            settings._requestedBookmarkSlot = static_cast<int32>( slot );
                        }
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

        if ( viewportWidth > 520.0f )
        {
            EditorContext* pContext = EditorContext::get();
            if ( pContext != nullptr && pContext->getSelectionManager().getSelectedObjectCount() >= 2 )
            {
                EditorWidgets::drawToolbarSeparator();
                if ( ImGui::Button( "Align..." ) )
                    ImGui::OpenPopup( "##ViewportAlignPopup" );

                if ( ImGui::BeginPopup( "##ViewportAlignPopup" ) )
                {
                    ImGui::Text( "Multi-Object Alignment" );
                    ImGui::Separator();
                    EditorCommandGui::drawMenuItem( "transform.snapToGround" );
                    ImGui::Separator();
                    EditorCommandGui::drawMenuItem( "transform.alignX" );
                    EditorCommandGui::drawMenuItem( "transform.alignY" );
                    EditorCommandGui::drawMenuItem( "transform.alignZ" );
                    ImGui::Separator();
                    EditorCommandGui::drawMenuItem( "transform.distributeX" );
                    EditorCommandGui::drawMenuItem( "transform.distributeY" );
                    EditorCommandGui::drawMenuItem( "transform.distributeZ" );
                    ImGui::EndPopup();
                }
            }
        }

        ImGui::PopStyleColor( 2 );
        ImGui::PopStyleVar( 2 );
    }

    void EditorViewportToolbar::drawTransformBar( ViewportToolbarSettings& settings, const float2& anchorPos,
                                                  bool bEnabled )
    {
        editor::EditorFloatingBarDesc barDesc{};
        barDesc._pId       = "##EditorTransformBar";
        barDesc._anchorPos = anchorPos;
        barDesc._pivot     = float2{ 0.5f, 0.0f };
        barDesc._bEnabled  = bEnabled;

        if ( EditorChrome::beginFloatingBar( barDesc ) == false )
        {
            EditorChrome::endFloatingBar();
            return;
        }

        int32 op = EditorContext::get()->getWorkspace().getGizmoOperation();
        ImGui::RadioButton( "Translate", &op, 0 );
        ImGui::SameLine();
        ImGui::RadioButton( "Rotate", &op, 1 );
        ImGui::SameLine();
        ImGui::RadioButton( "Scale", &op, 2 );
        ImGui::SameLine();
        EditorContext::get()->getWorkspace().setGizmoOperation( op );

        bool bLocal = EditorContext::get()->getWorkspace().isGizmoLocalSpace();
        if ( ImGui::Checkbox( "Local", &bLocal ) )
            EditorContext::get()->getWorkspace().setGizmoLocalSpace( bLocal );

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
