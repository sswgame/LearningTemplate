/**
 * @file BoxCollider2DVisualizer.cpp
 * @brief BoxCollider2D 사각형을 뷰포트에 와이어프레임으로 그립니다
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct BoxCollider2DVisualizerInternal
        {
            /** @brief BoxCollider2D 사각형을 와이어프레임으로 그립니다. */
            static void draw( const EditorViewportVisualizerArgs& args )
            {
                constexpr ImU32 colorWire = IM_COL32( 60, 230, 80, 220 );

                for ( GameObject* pObj : *args._pListObject )
                {
                    if ( pObj == nullptr || pObj->isActive() == false )
                        continue;
                    BoxCollider2DComponent* pBox = pObj->getComponent<BoxCollider2DComponent>();
                    if ( pBox == nullptr || pBox->isActive() == false )
                        continue;

                    const float2  offsetPos = pBox->getOffsetPosition();
                    const float2  offsetScl = pBox->getOffsetScale();
                    const float3  center    = pBox->getWorldPosition() + float3{ offsetPos._x, offsetPos._y, 0.0f };
                    const float32 hx        = MathUtil::max( offsetScl._x * 0.5f, 0.05f );
                    const float32 hy        = MathUtil::max( offsetScl._y * 0.5f, 0.05f );

                    const float3 p0{ center._x - hx, center._y - hy, center._z };
                    const float3 p1{ center._x + hx, center._y - hy, center._z };
                    const float3 p2{ center._x + hx, center._y + hy, center._z };
                    const float3 p3{ center._x - hx, center._y + hy, center._z };

                    ImVec2 s0, s1, s2, s3;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, p0, args._canvasPos, args._canvasSize, s0 ) &&
                         EditorViewportProjectionUtil::projectPoint( *args._pViewProj, p1, args._canvasPos, args._canvasSize, s1 ) &&
                         EditorViewportProjectionUtil::projectPoint( *args._pViewProj, p2, args._canvasPos, args._canvasSize, s2 ) &&
                         EditorViewportProjectionUtil::projectPoint( *args._pViewProj, p3, args._canvasPos, args._canvasSize, s3 ) )
                    {
                        args._pDrawList->AddLine( s0, s1, colorWire, 1.5f );
                        args._pDrawList->AddLine( s1, s2, colorWire, 1.5f );
                        args._pDrawList->AddLine( s2, s3, colorWire, 1.5f );
                        args._pDrawList->AddLine( s3, s0, colorWire, 1.5f );
                    }
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( BoxCollider2D, "box_collider_2d", 100, "Col", "BoxCollider2D 사각형을 와이어프레임으로 표시합니다", true,
                          &BoxCollider2DVisualizerInternal::draw );
} // namespace sw::editor
