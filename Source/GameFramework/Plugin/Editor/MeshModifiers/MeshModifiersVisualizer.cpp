/**
 * @file MeshModifiersVisualizer.cpp
 * @brief 뷰포트 시각화 meshmodifiers.preview(툴바 "Modifiers")의 화면 도구 View 입니다 — 고른 모디파이어의 범위 · 기즈모 손잡이 · 격자 제어점 값을 그리고 마우스를 명령으로 냅니다.
 * @details 값 모으기 · 집기 판정 · 명령 순서 · 적용(끌기 하나가 되돌리기 하나)은 Model `MeshModifierHandleEdit`(파일 머리말 참고), 변형 결과 선은 모으기(`MeshModifierPreviewGatherer`)가
 *          도형 값으로 내고 `EditorViewportVisualizer::drawAll` 이 그린다(실시간 GPU 미리보기는 2 단계 — 컴퓨트 셰이더).
 *          meshmodifiers.stage(툴바 "Stage")는 고른 모디파이어까지만 건 결과를 다른 색으로 겹쳐 그린다(변형 영향 미리보기).
 *          뷰포트는 시각화를 그린 뒤에 클릭 선택을 처리하므로 잡기 명령이 같은 프레임에 마우스를 가져가 첫 클릭도 새지 않는다(`EditorViewportMouseClaim` 헤더의 프레임 순서 계약).
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Localization/LocText.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Model/MeshModifierHandleEdit.h"

#include <cmath>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct MeshModifiersVisualizerInternal
        {
            static constexpr uint32  kRingSegmentCount = 48;
            static constexpr float32 kMarkHalfSize     = 4.0f;

            /** @brief 테마 색 토큰을 그리기 목록 색으로 바꿉니다. */
            static ImU32 themeColor( const Color& color ) { return EditorColorUtil::toImU32( color ); }
            /** @brief 손잡이 축(0 X · 1 Y · 2 Z)의 색입니다. */
            static ImU32 axisColor( uint32 axis )
            {
                switch ( axis )
                {
                    case 0:
                        return themeColor( SW_THEME_COLOR( "Viewport", "AxisX" ) );
                    case 1:
                        return themeColor( SW_THEME_COLOR( "Viewport", "AxisY" ) );
                    default:
                        return themeColor( SW_THEME_COLOR( "Viewport", "AxisZ" ) );
                }
            }

            static void drawSegment( const EditorViewportVisualizerArgs& args, const float3& from, const float3& to, ImU32 color, float32 thickness )
            {
                ImVec2 screenFrom;
                ImVec2 screenTo;
                if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, from, to, args._canvasPos, args._canvasSize, screenFrom, screenTo ) )
                    args._pDrawList->AddLine( screenFrom, screenTo, color, thickness );
            }

            /** @brief 화살표(선 + 화면 공간 머리)를 그립니다. */
            static void drawArrow( const EditorViewportVisualizerArgs& args, const float3& from, const float3& to, ImU32 color, float32 thickness )
            {
                ImVec2 screenFrom;
                ImVec2 screenTo;
                if ( EditorViewportProjectionUtil::projectSegment( *args._pViewProj, from, to, args._canvasPos, args._canvasSize, screenFrom, screenTo ) == false )
                    return;
                args._pDrawList->AddLine( screenFrom, screenTo, color, thickness );
                const float32 dx     = screenTo.x - screenFrom.x;
                const float32 dy     = screenTo.y - screenFrom.y;
                const float32 length = ::sqrtf( dx * dx + dy * dy );
                if ( length < 4.0f )
                    return;
                const float32 unitX = dx / length;
                const float32 unitY = dy / length;
                const float32 head  = MathUtil::min( 10.0f, length * 0.3f );
                const ImVec2  left{ screenTo.x - unitX * head - unitY * head * 0.5f, screenTo.y - unitY * head + unitX * head * 0.5f };
                const ImVec2  right{ screenTo.x - unitX * head + unitY * head * 0.5f, screenTo.y - unitY * head - unitX * head * 0.5f };
                args._pDrawList->AddTriangleFilled( screenTo, left, right, color );
            }

            /** @brief @p center 둘레 원을 @p axisU · @p axisV(단위, 서로 수직) 평면에 그립니다. */
            static void drawCircle( const EditorViewportVisualizerArgs& args, const float3& center, const float3& axisU, const float3& axisV, float32 radius, ImU32 color,
                                    float32 thickness )
            {
                float3 previous = center + axisU * radius;
                for ( uint32 segment = 1; segment <= kRingSegmentCount; ++segment )
                {
                    const float32 angle = MathUtil::kTwoPi * static_cast<float32>( segment ) / static_cast<float32>( kRingSegmentCount );
                    const float3  next  = center + axisU * ( ::cosf( angle ) * radius ) + axisV * ( ::sinf( angle ) * radius );
                    drawSegment( args, previous, next, color, thickness );
                    previous = next;
                }
            }

            static void drawMarker( const EditorViewportVisualizerArgs& args, const float3& point, ImU32 color, float32 thickness )
            {
                ImVec2 screen;
                if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, point, args._canvasPos, args._canvasSize, screen ) == false )
                    return;
                args._pDrawList->AddLine( ImVec2{ screen.x - 6.0f, screen.y }, ImVec2{ screen.x + 6.0f, screen.y }, color, thickness );
                args._pDrawList->AddLine( ImVec2{ screen.x, screen.y - 6.0f }, ImVec2{ screen.x, screen.y + 6.0f }, color, thickness );
            }

            /** @brief 범위 도형을 그립니다. */
            static void drawRanges( const EditorViewportVisualizerArgs& args, const vector<MeshModifierRangeShape>& listRange )
            {
                const ImU32 color = themeColor( SW_THEME_COLOR( "Viewport", "ModifierRange" ) );
                for ( const MeshModifierRangeShape& shape : listRange )
                {
                    switch ( shape._kind )
                    {
                        case MeshModifierRangeShapeKind::Segment:
                            drawSegment( args, shape._from, shape._to, color, shape._thickness );
                            break;
                        case MeshModifierRangeShapeKind::Arrow:
                            drawArrow( args, shape._from, shape._to, color, shape._thickness );
                            break;
                        case MeshModifierRangeShapeKind::Marker:
                            drawMarker( args, shape._from, color, shape._thickness );
                            break;
                    }
                }
            }

            /** @brief 손잡이를 그립니다(이동은 화살표, 회전은 고리). */
            static void drawHandle( const EditorViewportVisualizerArgs& args, const MeshModifierHandle& handle, bool bHighlight )
            {
                const ImU32 color = bHighlight ? themeColor( SW_THEME_COLOR( "Viewport", "ModifierHover" ) ) : axisColor( handle._axis );
                if ( handle._kind == MeshModifierHandleKind::GizmoRotate )
                {
                    // 고리 평면의 두 방향을 법선에서 만든다
                    const float3 helper = MathUtil::abs( handle._direction._y ) < 0.9f ? float3{ 0.0f, 1.0f, 0.0f } : float3{ 1.0f, 0.0f, 0.0f };
                    const float3 axisU  = handle._direction.cross( helper ).normalize();
                    const float3 axisV  = handle._direction.cross( axisU ).normalize();
                    drawCircle( args, handle._origin, axisU, axisV, handle._radius, color, bHighlight ? 2.5f : 1.5f );
                    return;
                }
                drawArrow( args, handle._origin, handle._origin + handle._direction, color, bHighlight ? 3.0f : 2.0f );
            }

            /**
             * @brief 자동화가 켜졌으면 이동 화살표마다 화면 사각형 이름(`meshmodifiers.handle.move.<축>`)를 남깁니다 — 시나리오가 손잡이를 끌어 양보를 검증한다.
             * @details 화살표 끝 쪽(길이의 90 %)을 고른다. 가운데 쪽은 다른 축 회전 고리(반경 80 %)와 겹친다.
             */
            static void recordMarks( const EditorViewportVisualizerArgs& args, const vector<MeshModifierHandle>& listHandle )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                for ( const MeshModifierHandle& handle : listHandle )
                {
                    ImVec2 screen;
                    if ( handle._kind != MeshModifierHandleKind::GizmoMove )
                        continue;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, handle._origin + handle._direction * 0.9f, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    const string key = string( "meshmodifiers.handle.move." ) + to_string( handle._axis );
                    EditorSelfTestMarks::recordRect( key.c_str(), float2{ screen.x - kMarkHalfSize, screen.y - kMarkHalfSize }, float2{ screen.x + kMarkHalfSize, screen.y + kMarkHalfSize } );
                }
            }

            /** @brief 격자 선과 제어점을 그립니다(고른 점 · 호버 점은 강조). */
            static void drawPoints( const EditorViewportVisualizerArgs& args, const MeshModifierHandleViewState& state, int32 selectedPoint, optional<int32> hoveredPoint )
            {
                const vector<float3>& listWorld = state._listPoint;
                const ImU32           lineColor = themeColor( SW_THEME_COLOR( "Viewport", "LatticeLine" ) );
                for ( int32 indexZ = 0; indexZ < state._countZ; ++indexZ )
                {
                    for ( int32 indexY = 0; indexY < state._countY; ++indexY )
                    {
                        for ( int32 indexX = 0; indexX < state._countX; ++indexX )
                        {
                            const int32 index = indexX + state._countX * ( indexY + state._countY * indexZ );
                            if ( static_cast<size_t>( index ) >= listWorld.size() )
                                continue;
                            const float3& point = listWorld[static_cast<size_t>( index )];
                            if ( indexX + 1 < state._countX )
                                drawSegment( args, point, listWorld[static_cast<size_t>( index + 1 )], lineColor, 1.0f );
                            if ( indexY + 1 < state._countY )
                                drawSegment( args, point, listWorld[static_cast<size_t>( index + state._countX )], lineColor, 1.0f );
                            if ( indexZ + 1 < state._countZ )
                                drawSegment( args, point, listWorld[static_cast<size_t>( index + state._countX * state._countY )], lineColor, 1.0f );
                            ImVec2 screen;
                            if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, point, args._canvasPos, args._canvasSize, screen ) == false )
                                continue;
                            if ( index == selectedPoint )
                            {
                                args._pDrawList->AddCircleFilled( screen, 6.0f, themeColor( SW_THEME_COLOR( "Viewport", "LatticeSelected" ) ) );
                                args._pDrawList->AddCircle( screen, 8.0f, themeColor( SW_THEME_COLOR( "Viewport", "ModifierHover" ) ), 0, 1.5f );
                            }
                            else
                            {
                                const bool bHovered = hoveredPoint == index;
                                args._pDrawList->AddCircleFilled( screen, bHovered ? 5.0f : 3.5f,
                                                                  bHovered ? themeColor( SW_THEME_COLOR( "Viewport", "ModifierHover" ) ) : themeColor( SW_THEME_COLOR( "Viewport", "LatticePoint" ) ) );
                            }
                        }
                    }
                }
            }

            /** @brief 모으기 → 끄는 중의 명령 → 적용 → 집기 · 누름 명령 → 적용 → 범위 · 손잡이 · 제어점 그리기(`meshmodifiers.preview` 의 화면 도구). */
            static void process( const EditorViewportVisualizerArgs& args )
            {
                const MeshModifierHandleViewState& state = MeshModifierHandleViewState::gather();
                vector<MeshModifierHandleCommand>  listCommand; // 잡기 · 끄는 중에만 찬다(빈 벡터는 할당하지 않는다)

                const ImVec2 mouse        = ImGui::GetMousePos();
                const bool   bMouseInside = mouse.x >= args._canvasPos._x && mouse.y >= args._canvasPos._y && mouse.x <= args._canvasPos._x + args._canvasSize._x &&
                                          mouse.y <= args._canvasPos._y + args._canvasSize._y && ImGui::IsWindowHovered();
                Ray        ray{};
                const bool bHasRay   = state._bAvailable && EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray );
                const bool bDragging = MeshModifierHandleCommand::step( state, ImGui::IsMouseDown( ImGuiMouseButton_Left ), bHasRay ? &ray : nullptr, listCommand );
                if ( listCommand.empty() == false )
                    MeshModifierHandleCommand::apply( listCommand );
                if ( state._bAvailable == false )
                    return;

                optional<int32> hoveredHandle;
                optional<int32> hoveredPoint;
                int32           selectedPoint = state._selectedPoint;
                const bool      bCanHover     = bDragging == false && bMouseInside && bHasRay && args._bGizmoDragging == false;
                if ( bCanHover )
                {
                    state.pick( ray, hoveredHandle, hoveredPoint );
                    if ( EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) )
                    {
                        listCommand.clear();
                        MeshModifierHandleCommand::press( state, hoveredHandle, hoveredPoint, ray, listCommand );
                        if ( listCommand.empty() == false )
                            MeshModifierHandleCommand::apply( listCommand );
                        if ( hoveredHandle.has_value() == false && hoveredPoint.has_value() )
                            selectedPoint = *hoveredPoint;
                    }
                }

                drawSegment( args, state._axisFrom, state._axisTo, themeColor( SW_THEME_COLOR( "Viewport", "ModifierAxis" ) ), 1.0f );
                drawRanges( args, state._listRange );
                if ( state._bPointSet )
                    drawPoints( args, state, selectedPoint, hoveredPoint );
                for ( size_t index = 0; index < state._listHandle.size(); ++index )
                {
                    const MeshModifierHandle& handle  = state._listHandle[index];
                    const bool                bActive = bDragging && state._active.isSameAs( handle );
                    drawHandle( args, handle, bActive || static_cast<int32>( index ) == hoveredHandle );
                }
                recordMarks( args, state._listHandle );
                ImVec2 centerScreen;
                if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, state._centerWorld, args._canvasPos, args._canvasSize, centerScreen ) )
                    args._pDrawList->AddCircle( centerScreen, 6.0f, themeColor( SW_THEME_COLOR( "Viewport", "ModifierAxis" ) ), 0, 2.0f );
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER_TOOL( MeshModifiers, "meshmodifiers.preview", 910, SW_EDITOR_ICON_NAME( "Cube" ), SW_LOCKEYREF( "EdPluginVisualizer", "MeshModifiersLabel" ),
                               SW_LOCKEYREF( "EdPluginVisualizer", "MeshModifiersTip" ), true, &MeshModifiersVisualizerInternal::process,
                               &MeshModifierPreviewGatherer::gatherPreview );
    SW_EDITOR_VISUALIZER_GATHER( MeshModifiersStage, "meshmodifiers.stage", 911, ::sw::editor::EditorIconName{}, SW_LOCKEYREF( "EdPluginVisualizer", "MeshModifiersStageLabel" ),
                                 SW_LOCKEYREF( "EdPluginVisualizer", "MeshModifiersStageTip" ), false, &MeshModifierPreviewGatherer::gatherStage );
} // namespace sw::editor
