/**
 * @file PropertyShapeTool.cpp
 * @brief 선언형 모양 손잡이 도구(MVVM View)입니다 — 값(`PropertyShapeHandleEdit::gather`)으로 손잡이를 그리고 마우스를 명령(잡기 · 끌기 · 놓기)으로 냅니다.
 * @details 모양 선은 Model 의 모으기(`PropertyShapeGatherer`), 적용 · 되돌리기는 Model `PropertyShapeHandleEdit`. 콜라이더 손잡이 도구(`EditorColliderHandleTool`)와 같은 차례다.
 *          ezEngine 조작기 · 언리얼 `MakeEditWidget` · 유니티 Handles 처럼 컴포넌트는 손잡이 코드를 쓰지 않고 프로퍼티 메타만 적는다.
 */
#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Panels/Scene/Model/PropertyShapeEdit.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorViewportMouseCapture.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct PropertyShapeToolInternal
        {
            /** @brief 손잡이 사각형의 반 변(픽셀)입니다. 마우스가 올라오면 키운다. */
            static constexpr float32 kHandleHalfSize = 4.0f;
            /** @brief 강조한 손잡이가 키우는 픽셀입니다. */
            static constexpr float32 kHighlightGrow = 2.0f;
            /** @brief 손잡이를 집을 수 있는 마우스 거리(픽셀)입니다. */
            static constexpr float32 kPickRadius = 9.0f;
            /** @brief 시각화 토글의 툴바 순서입니다(물리 셰이프 110 곁). */
            static constexpr int32 kVisualizerOrder = 115;

            /** @brief 시나리오가 손잡이를 누를 수 있도록 화면 사각형을 기록합니다(`propertyshape.handle.<프로퍼티>.<축><부호>`, 점 목록은 `propertyshape.handle.<프로퍼티>.<자리>`). */
            static void recordHandle( const PropertyShapeHandle& handle, const ImVec2& screen )
            {
                if ( EditorSelfTestMarks::isEnabled() == false )
                    return;
                static const utf8* const                 arrAxisName[3] = { "x", "y", "z" };
                StringBuilder<constant::buffer::kMax256> keyBuilder;
                if ( handle._kind == PropertyShapeHandleKind::Point )
                    keyBuilder.appendFormat( "propertyshape.handle.%#.%#", handle._property.c_str(), handle._elementIndex );
                else
                    keyBuilder.appendFormat( "propertyshape.handle.%#.%#%#", handle._property.c_str(), arrAxisName[handle._axis], ( handle._sign > 0.0f ? "+" : "-" ) );
                const string key{ keyBuilder.view() };
                EditorSelfTestMarks::recordRect( key.c_str(), float2{ screen.x - kHandleHalfSize, screen.y - kHandleHalfSize }, float2{ screen.x + kHandleHalfSize, screen.y + kHandleHalfSize } );
            }

            /** @brief 모으기 → 끄는 중의 명령 → 적용 → 손잡이 그리기 · 집기 → 잡기 적용. 끌기가 값을 바꾸면 다시 모아 이번 프레임의 손잡이를 새 자리에 그린다. */
            static void process( const EditorViewportVisualizerArgs& args )
            {
                PropertyShapeHandleSession& session = PropertyShapeHandleSession::get();
                session._drawnHandleCount           = 0;
                session._listCommand.clear();

                PropertyShapeHandleDragTracker& tracker = session._tracker;
                PropertyShapeHandleEdit::gather( tracker.isDragging() ? tracker.getObject() : GameObjectHandle{}, session._state );
                const bool bAvailable = session._state._bAvailable && args._pViewProj != nullptr && args._pDrawList != nullptr;
                Ray        ray{};
                const bool bRay   = bAvailable && EditorViewportProjectionUtil::makeMouseRay( args._pViewProj->invert(), args._canvasPos, args._canvasSize, ray );
                const bool bDrawn = tracker.step( bAvailable, ImGui::IsMouseDown( ImGuiMouseButton_Left ), bRay ? &ray : nullptr, session._listCommand );
                if ( session._listCommand.empty() == false )
                {
                    session._edit.apply( session._listCommand );
                    if ( bDrawn )
                        PropertyShapeHandleEdit::gather( tracker.getObject(), session._state );
                }
                if ( bDrawn == false )
                {
                    EditorViewportMouseCapture::release( &session );
                    return;
                }
                if ( tracker.isDragging() )
                    EditorViewportMouseCapture::acquire( &session );

                const ImVec2 mouse        = ImGui::GetMousePos();
                const bool   bMouseInside = mouse.x >= args._canvasPos._x && mouse.y >= args._canvasPos._y && mouse.x <= args._canvasPos._x + args._canvasSize._x &&
                                          mouse.y <= args._canvasPos._y + args._canvasSize._y && ImGui::IsWindowHovered();
                const PropertyShapeHandle& active          = tracker.getActive();
                const PropertyShapeHandle* pHovered        = nullptr;
                float32                    hoveredDistance = kPickRadius;
                for ( const PropertyShapeHandle& handle : session._state._listHandle )
                {
                    ImVec2 screen;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, handle._position, args._canvasPos, args._canvasSize, screen ) == false )
                        continue;
                    ++session._drawnHandleCount;
                    recordHandle( handle, screen );
                    const bool    bActive                = tracker.isDragging() && handle.isSameHandle( active );
                    const float32 dx                     = mouse.x - screen.x;
                    const float32 dy                     = mouse.y - screen.y;
                    const float32 distance               = ::sqrtf( dx * dx + dy * dy );
                    const bool    bNearestHoverCandidate = tracker.isDragging() == false && bMouseInside && args._bGizmoDragging == false && distance <= hoveredDistance;
                    if ( bNearestHoverCandidate )
                    {
                        hoveredDistance = distance;
                        pHovered        = &handle;
                    }
                    const bool    bHighlight = bActive || pHovered == &handle;
                    const float32 half       = kHandleHalfSize + ( bHighlight ? kHighlightGrow : 0.0f );
                    const ImVec2  min{ screen.x - half, screen.y - half };
                    const ImVec2  max{ screen.x + half, screen.y + half };
                    args._pDrawList->AddRectFilled( min, max, EditorColorUtil::toImU32( bHighlight ? SW_THEME_COLOR( "Viewport", "SelectionOutline" ) : SW_THEME_COLOR( "Base", "Text" ) ) );
                    args._pDrawList->AddRect( min, max, EditorColorUtil::toImU32( SW_THEME_COLOR( "Viewport", "HandleOutline" ) ) );
                }

                if ( tracker.isDragging() )
                    return;
                if ( pHovered == nullptr )
                {
                    EditorViewportMouseCapture::release( &session );
                    return;
                }
                EditorViewportMouseCapture::acquire( &session ); // 손잡이 위에서는 클릭 선택 · 변환 기즈모가 물러난다
                if ( EditorInputGUI::readPointer().wasPressed( EditorMouseButton::Left ) == false )
                    return;
                session._listCommand.clear();
                tracker.press( session._state._object, *pHovered, session._listCommand );
                session._edit.apply( session._listCommand );
            }
        };
    } // namespace

    // 모양 선은 Model 의 모으기(`PropertyShapeGatherer`), 손잡이는 이 화면 도구 — 같은 토글이다.
    SW_EDITOR_VISUALIZER_TOOL( PropertyShapes, "property_shapes", PropertyShapeToolInternal::kVisualizerOrder, SW_EDITOR_ICON_NAME( "Cube" ), SW_LOCKEYREF( "EdVisualizer", "PropertyShapesLabel" ),
                               SW_LOCKEYREF( "EdVisualizer", "PropertyShapesTip" ), true, &PropertyShapeToolInternal::process, &PropertyShapeGatherer::gather );
} // namespace sw::editor
