#include "pch.h"

#include "Editor/Viewport/EditorViewportAssetDrop.h"

#include "Core/Container/string.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Viewport/EditorViewportProjection.h"
#include "Editor/Viewport/Model/EditorViewportRayPick.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    void EditorViewportAssetDrop::processDrop( const float2& canvasPos, const float2& canvasSize, const float32* pView, const float32* pProj, bool b2DMode )
    {
        string droppedAssetPath;
        // 캔버스 사각형 전체가 타깃이다 — 캔버스 위에 그린 오버레이 · 기즈모 · HUD 아이템이 "마지막 아이템" 이 되어 놓은 것을 가로채지 않는다.
        const float2 canvasMax{ canvasPos._x + canvasSize._x, canvasPos._y + canvasSize._y };
        if ( EditorWidgets::acceptAssetDropInRect( canvasPos, canvasMax, "##ViewportAssetDrop", droppedAssetPath ) == false )
            return;
        if ( pView == nullptr || pProj == nullptr )
            return;
        if ( EditorPlaySession::refuseSceneEdit( "Viewport asset drop" ) )
            return; // 드롭을 받은 순간에만 닿는다 - 거절 이유가 경고 로그로 남는다

        Scene* pScene = editor::getActiveScene();
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;

        GameObjectManager* pManager = pScene->getObjectManager();

        // 마우스 아래의 바닥에 놓는다(2D 는 Z = 0 평면, 3D 는 Y = 0). 캔버스 밖이거나 평면과 평행하면 원점에 놓는다.
        float4x4 viewProj{};
        EditorViewportProjectionUtil::loadViewProjection( pView, pProj, viewProj );

        float3        spawnPos{ 0.0f, 0.0f, 0.0f };
        EditorPickRay mouseRay{};
        if ( EditorViewportProjectionUtil::makeMouseRay( viewProj.invert(), canvasPos, canvasSize, mouseRay ) )
        {
            const uint32 planeAxis = b2DMode ? 2u : 1u;
            float3       hitPt{};
            if ( EditorViewportRayPick::rayHitsAxisPlane( mouseRay, planeAxis, hitPt ) )
                spawnPos = hitPt;
        }

        EditorAssetCommands::dropAt( pManager, droppedAssetPath.c_str(), spawnPos );
    }
} // namespace sw::editor
