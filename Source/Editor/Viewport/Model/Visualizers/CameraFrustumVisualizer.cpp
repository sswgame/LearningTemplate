/**
 * @file CameraFrustumVisualizer.cpp
 * @brief 활성 카메라를 뺀 CameraComponent 의 프러스텀을 뷰포트 도형 값으로 모읍니다(그리기는 `EditorViewportVisualizer::drawAll`).
 */
#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Editor/Viewport/EditorViewportVisualizer.h"
#include "Editor/Viewport/Model/EditorVisualizerDrawList.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 활성 카메라를 뺀 CameraComponent 의 프러스텀(눈에서 근평면 사각형으로 넷, 사각형 테두리 넷)을 모읍니다. 카메라 레지스트리만 봅니다. 색은 테마 토큰 `CameraWire` 입니다. */
        void gatherCameraFrustums( const EditorViewportVisualizerArgs& args, EditorVisualizerDrawList& outDrawList )
        {
            if ( args._pListCamera == nullptr )
                return;
            EditorVisualizerLine prototype{};
            prototype._thickness = 1.2f;
            prototype._colorRole = EditorVisualizerColorRole::CameraWire;
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
                const float3 arrNear[4] = {
                    nearCenter - rgt * 0.6f - up * 0.4f,
                    nearCenter + rgt * 0.6f - up * 0.4f,
                    nearCenter + rgt * 0.6f + up * 0.4f,
                    nearCenter - rgt * 0.6f + up * 0.4f,
                };
                for ( const float3& corner : arrNear )
                {
                    EditorVisualizerLine line = prototype;
                    line._from                = eye;
                    line._to                  = corner;
                    outDrawList.addLine( line );
                }
                outDrawList.addLoop( arrNear, 4, prototype );
            }
        }
    } // namespace

    SW_EDITOR_VISUALIZER_GATHER( CameraFrustum, "camera_frustum", 200, SW_EDITOR_ICON_NAME( "Camera" ), SW_LOCKEYREF( "EdVisualizer", "CameraFrustumLabel" ),
                                 SW_LOCKEYREF( "EdVisualizer", "CameraFrustumTip" ), true, &gatherCameraFrustums );
} // namespace sw::editor
