/**
 * @file EditorViewportVisualizer.cpp
 * @brief 컴포넌트 종류별 시각화 표와 그리기 구현
 */
#include "pch.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/EditorViewportProjection.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorViewportVisualizerInternal
        {
            /** @brief BoxCollider2D 사각형을 와이어프레임으로 그립니다. */
            static void drawColliders( const EditorViewportVisualizerArgs& args )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;

                constexpr ImU32 colWire = IM_COL32( 60, 230, 80, 220 );

                for ( GameObject* pObj : pManager->getAllGameObjects() )
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
                        args._pDrawList->AddLine( s0, s1, colWire, 1.5f );
                        args._pDrawList->AddLine( s1, s2, colWire, 1.5f );
                        args._pDrawList->AddLine( s2, s3, colWire, 1.5f );
                        args._pDrawList->AddLine( s3, s0, colWire, 1.5f );
                    }
                }
            }

            /** @brief 활성 카메라를 제외한 CameraComponent 의 프러스텀을 그립니다. */
            static void drawCameraFrustums( const EditorViewportVisualizerArgs& args )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                    return;

                constexpr ImU32 colCamWire = IM_COL32( 60, 200, 255, 200 );

                for ( GameObject* pObj : pManager->getAllGameObjects() )
                {
                    if ( pObj == nullptr || pObj->isActive() == false )
                        continue;
                    CameraComponent* pCam = pObj->getComponent<CameraComponent>();
                    if ( pCam == nullptr || pCam == args._pActiveCamera || pCam->isActive() == false )
                        continue;

                    const float4x4 camWorld = pCam->getWorldMatrix();
                    const float3   eye      = float3{ camWorld._41, camWorld._42, camWorld._43 };
                    const float3   rgt      = float3{ camWorld._11, camWorld._12, camWorld._13 };
                    const float3   up       = float3{ camWorld._21, camWorld._22, camWorld._23 };
                    const float3   fwd      = float3{ camWorld._31, camWorld._32, camWorld._33 };

                    const float3 nearCenter = eye + fwd * 1.0f;
                    const float3 p0         = nearCenter - rgt * 0.6f - up * 0.4f;
                    const float3 p1         = nearCenter + rgt * 0.6f - up * 0.4f;
                    const float3 p2         = nearCenter + rgt * 0.6f + up * 0.4f;
                    const float3 p3         = nearCenter - rgt * 0.6f + up * 0.4f;

                    ImVec2 sEye, s0, s1, s2, s3;
                    if ( EditorViewportProjectionUtil::projectPoint( *args._pViewProj, eye, args._canvasPos, args._canvasSize, sEye ) &&
                         EditorViewportProjectionUtil::projectPoint( *args._pViewProj, p0, args._canvasPos, args._canvasSize, s0 ) &&
                         EditorViewportProjectionUtil::projectPoint( *args._pViewProj, p1, args._canvasPos, args._canvasSize, s1 ) &&
                         EditorViewportProjectionUtil::projectPoint( *args._pViewProj, p2, args._canvasPos, args._canvasSize, s2 ) &&
                         EditorViewportProjectionUtil::projectPoint( *args._pViewProj, p3, args._canvasPos, args._canvasSize, s3 ) )
                    {
                        args._pDrawList->AddLine( sEye, s0, colCamWire, 1.2f );
                        args._pDrawList->AddLine( sEye, s1, colCamWire, 1.2f );
                        args._pDrawList->AddLine( sEye, s2, colCamWire, 1.2f );
                        args._pDrawList->AddLine( sEye, s3, colCamWire, 1.2f );
                        args._pDrawList->AddLine( s0, s1, colCamWire, 1.2f );
                        args._pDrawList->AddLine( s1, s2, colCamWire, 1.2f );
                        args._pDrawList->AddLine( s2, s3, colCamWire, 1.2f );
                        args._pDrawList->AddLine( s3, s0, colCamWire, 1.2f );
                    }
                }
            }

            /** @brief 시각화 정본 — 새 시각화는 여기 한 줄이다. 툴바 체크박스도 이 표에서 나온다. */
            inline static const EditorViewportVisualizer::Row _s_arrRow[] = {
                {"Col",  "BoxCollider2D 사각형을 와이어프레임으로 표시합니다", true,      &drawColliders},
                {"Cam", "활성 카메라를 제외한 카메라의 프러스텀을 표시합니다", true, &drawCameraFrustums}
            };

            static constexpr uint32 kRowCount = static_cast<uint32>( sizeof( _s_arrRow ) / sizeof( _s_arrRow[0] ) );

            static_assert( kRowCount <= 32, "시각화 마스크가 uint32 라 시각화는 최대 32개입니다" );
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    const EditorViewportVisualizer::Row* EditorViewportVisualizer::getRows( uint32& outCount )
    {
        outCount = EditorViewportVisualizerInternal::kRowCount;
        return EditorViewportVisualizerInternal::_s_arrRow;
    }

    uint32 EditorViewportVisualizer::getDefaultMask()
    {
        uint32 mask{ 0 };
        for ( uint32 index = 0; index < EditorViewportVisualizerInternal::kRowCount; ++index )
        {
            if ( EditorViewportVisualizerInternal::_s_arrRow[index]._bDefaultOn )
                mask |= getMaskBit( index );
        }
        return mask;
    }

    void EditorViewportVisualizer::drawAll( const EditorViewportVisualizerArgs& args, uint32 visualizerMask )
    {
        if ( args._pDrawList == nullptr || args._pViewProj == nullptr )
            return;

        for ( uint32 index = 0; index < EditorViewportVisualizerInternal::kRowCount; ++index )
        {
            const Row& row = EditorViewportVisualizerInternal::_s_arrRow[index];
            if ( ( visualizerMask & getMaskBit( index ) ) == 0 )
                continue;
            if ( row._pDraw != nullptr )
                row._pDraw( args );
        }
    }
} // namespace sw::editor
