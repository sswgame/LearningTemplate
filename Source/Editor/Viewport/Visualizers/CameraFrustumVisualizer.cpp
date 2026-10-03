/**
 * @file CameraFrustumVisualizer.cpp
 * @brief 활성 카메라를 제외한 CameraComponent 의 프러스텀을 뷰포트에 그립니다
 */
#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/EditorViewportVisualizer.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/CameraRegistry.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct CameraFrustumVisualizerInternal
        {
            /** @brief 활성 카메라를 제외한 CameraComponent 의 프러스텀을 그립니다. 카메라 등록부만 봅니다(씬 전체를 훑지 않습니다). */
            static void draw( const EditorViewportVisualizerArgs& args )
            {
                constexpr ImU32 colorCameraWire = IM_COL32( 60, 200, 255, 200 );

                if ( args._pListCamera == nullptr )
                    return;
                for ( CameraComponent* pCam : *args._pListCamera )
                {
                    if ( pCam == args._pActiveCamera || CameraRegistry::isUsableCamera( pCam ) == false )
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
                        args._pDrawList->AddLine( sEye, s0, colorCameraWire, 1.2f );
                        args._pDrawList->AddLine( sEye, s1, colorCameraWire, 1.2f );
                        args._pDrawList->AddLine( sEye, s2, colorCameraWire, 1.2f );
                        args._pDrawList->AddLine( sEye, s3, colorCameraWire, 1.2f );
                        args._pDrawList->AddLine( s0, s1, colorCameraWire, 1.2f );
                        args._pDrawList->AddLine( s1, s2, colorCameraWire, 1.2f );
                        args._pDrawList->AddLine( s2, s3, colorCameraWire, 1.2f );
                        args._pDrawList->AddLine( s3, s0, colorCameraWire, 1.2f );
                    }
                }
            }
        };
    } // namespace

    SW_EDITOR_VISUALIZER( CameraFrustum, "camera_frustum", 200, "Cam", "활성 카메라를 제외한 카메라의 프러스텀을 표시합니다", true,
                          &CameraFrustumVisualizerInternal::draw );
} // namespace sw::editor
