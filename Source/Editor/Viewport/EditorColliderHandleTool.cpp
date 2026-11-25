#include "pch.h"

#include "Editor/Viewport/EditorColliderHandleTool.h"

#include "Core/Math/Color.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Ray.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorColliderHandleEdit.h"
#include "Editor/Viewport/Model/Visualizers/PhysicsShapeVisualizer.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorColliderHandleToolInternal
        {
            /** @brief 손잡이 사각형의 반 변(픽셀)입니다. 마우스가 올라오면 키운다. */
            static constexpr float32 kHandleHalfSize = 4.0f;
            /** @brief 물리 셰이프 시각화 토글의 툴바 순서입니다. */
            static constexpr int32 kPhysicsShapesOrder = 110;
            /** @brief 손잡이를 집을 수 있는 마우스 거리(픽셀)입니다. */
            static constexpr float32 kPickRadius = 9.0f;

            /** @brief 시나리오가 손잡이를 누를 수 있도록 화면 사각형을 기록합니다(`collider.handle.<셰이프>.<축><부호>`). */
            static void recordHandle( const ColliderHandle& handle, const ImVec2& screen )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                static const utf8* const                 arrAxisName[3] = { "x", "y", "z" };
                StringBuilder<constant::buffer::kMax256> keyBuilder;
                keyBuilder.appendFormat( "collider.handle.%#.%#%#", handle._shapeIndex, arrAxisName[handle._axis], ( handle._sign > 0.0f ? "+" : "-" ) );
                const string key{ keyBuilder.view() };
                EditorSelfTestMarks::recordRect( key.c_str(), float2{ screen.x - kHandleHalfSize, screen.y - kHandleHalfSize }, float2{ screen.x + kHandleHalfSize, screen.y + kHandleHalfSize } );
            }

            /** @brief 모으기 → 끄는 중의 명령 → 적용 → 손잡이 그리기 · 집기 → 잡기 적용. 끌기가 셰이프를 바꾸면 다시 모아 이번 프레임의 손잡이를 새 크기로 그린다. */
            static void process( const EditorViewportVisualizerArgs& args )
            {
                EditorColliderHandleSession& session = EditorColliderHandleSession::get();
                session._bCapturing                  = false;
                session._frame                       = ImGui::GetFrameCount();
                session._drawnHandleCount            = 0;
                session._listCommand.clear();

                EditorColliderHandleDragTracker& tracker = session._tracker;
                EditorColliderHandleEdit::gather( tracker.isDragging() ? tracker.getObject() : GameObjectHandle{}, session._state );
                const bool bAvailable = session._state._bAvailable && args._pViewProj != nullptr && args._pDrawList != nullptr;
                Ray        ray{};
                const bool bRay   = bAvailable && EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray );
                const bool bDrawn = tracker.step( bAvailable, ImGui::IsMouseDown( ImGuiMouseButton_Left ), bRay ? &ray : nullptr, session._listCommand );
                if ( session._listCommand.empty() == false )
                {
                    session._edit.apply( session._listCommand );
                    if ( bDrawn )
                        EditorColliderHandleEdit::gather( tracker.getObject(), session._state );
                }
                if ( bDrawn == false )
                    return;
                session._bCapturing = tracker.isDragging();

                const ImVec2 mouse        = ImGui::GetMousePos();
                const bool   bMouseInside = mouse.x >= args._canvasPos._x && mouse.y >= args._canvasPos._y && mouse.x <= args._canvasPos._x + args._canvasSize._x &&
                                          mouse.y <= args._canvasPos._y + args._canvasSize._y && ImGui::IsWindowHovered();
                const ColliderHandle& active          = tracker.getActive();
                const ColliderHandle* pHovered        = nullptr;
                float32               hoveredDistance = kPickRadius;
                for ( const ColliderHandle& handle : session._state._listHandle )
                {
                    ImVec2 screen;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, handle._position, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    ++session._drawnHandleCount;
                    recordHandle( handle, screen );
                    const bool    bActive                = tracker.isDragging() && handle._shapeIndex == active._shapeIndex && handle._axis == active._axis && handle._sign == active._sign;
                    const float32 dx                     = mouse.x - screen.x;
                    const float32 dy                     = mouse.y - screen.y;
                    const float32 distance               = ::sqrtf( dx * dx + dy * dy );
                    const bool    bNearestHoverCandidate = tracker.isDragging() == false && bMouseInside && args._bGizmoDragging == false && distance <= hoveredDistance;
                    if ( bNearestHoverCandidate )
                    {
                        hoveredDistance = distance;
                        pHovered        = &handle;
                    }
                    const bool   bHighlight = bActive || pHovered == &handle;
                    const float2 half{ kHandleHalfSize + ( bHighlight ? 2.0f : 0.0f ), kHandleHalfSize + ( bHighlight ? 2.0f : 0.0f ) };
                    const ImVec2 min{ screen.x - half._x, screen.y - half._y };
                    const ImVec2 max{ screen.x + half._x, screen.y + half._y };
                    args._pDrawList->AddRectFilled( min, max, EditorColorUtil::toImU32( bHighlight ? SW_THEME_COLOR( "Viewport", "SelectionOutline" ) : SW_THEME_COLOR( "Base", "Text" ).withAlpha( 0.95f ) ) );
                    args._pDrawList->AddRect( min, max, EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "HandleOutline" ) ) );
                }

                if ( tracker.isDragging() || pHovered == nullptr )
                    return;
                session._bCapturing = true;
                if ( EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) == false )
                    return;
                session._listCommand.clear();
                tracker.press( session._state._object, *pHovered, session._listCommand );
                session._edit.apply( session._listCommand );
            }
        };
    } // namespace

    void EditorColliderHandleTool::process( const EditorViewportVisualizerArgs& args )
    {
        EditorColliderHandleToolInternal::process( args );
    }

    bool EditorColliderHandleTool::isCapturingMouse()
    {
        const EditorColliderHandleSession& session = EditorColliderHandleSession::get();
        return session._bCapturing && session._frame == ImGui::GetFrameCount();
    }

    // 물리 셰이프 시각화와 같은 토글이다 — 셰이프 선은 Model 의 모으기(`PhysicsShapeGatherer`), 손잡이는 이 화면 도구.
    SW_EDITOR_VISUALIZER_TOOL( PhysicsShapes, "physics_shapes", EditorColliderHandleToolInternal::kPhysicsShapesOrder, SW_EDITOR_ICON_NAME( "Collider" ), SW_LOCKEYREF( "EdVisualizer", "PhysicsShapesLabel" ),
                               SW_LOCKEYREF( "EdVisualizer", "PhysicsShapesTip" ), false, &EditorColliderHandleTool::process, &PhysicsShapeGatherer::gather );
} // namespace sw::editor
