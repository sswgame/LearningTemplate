#include "pch.h"

#include "Engine/Scene/SceneDebugDraw.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Scene/StreamingCellGrid.h"

namespace sw
{
    /** @brief 씬의 월드 스트리밍 격자를 활성 게임 카메라 둘레에 선으로 그립니다(올라 있는 셀 초록, 로딩 반경 안 노랑, 그 밖 회색). */
    SW_GLOBAL_VARIABLE( bool, gv_worldCellDebugDraw, false, "월드 스트리밍 격자를 카메라 둘레에 그린다 — 올라 있는 셀 초록, 로딩 반경 안 노랑, 그 밖 회색" );
} // namespace sw

namespace sw
{
    namespace
    {
        struct SceneDebugDrawInternal
        {
            /** @brief 카메라 둘레에서 그리는 셀 수(로딩 반경 밖으로 더 보이는 칸)입니다. */
            static constexpr int32 kExtraCellCount = 2;

            /**
             * @brief 씬의 격자(`SceneSettings::_cellGrid`)를 카메라 둘레에 그립니다. 격자가 없는 씬(셀 크기 0)이면 그리지 않습니다.
             * @details 에디터에서 연 씬(XML)은 셀을 나누지 않았으므로 노랑 · 회색만 나온다 — 쿠킹이 나눌 자리의 미리보기다.
             */
            static void drawStreamingCells( Scene& scene, DebugDrawQueue& queue )
            {
                const SceneSettings& settings = scene.getSceneSettings();
                StreamingCellGrid    grid;
                if ( settings._cellGrid._cellSize <= 0.0f || StreamingCellGrid::tryCreate( settings._cellGrid, grid ) == false )
                    return;
                const CameraComponent* pCamera = scene.getActiveGameCamera();
                if ( pCamera == nullptr )
                    return;
                const float3                   origin    = pCamera->getWorldPosition();
                const SceneStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
                const float32                  cellSize  = grid.getCellSize();
                const int32                    reach     = static_cast<int32>( grid.getLoadingRadius() / cellSize ) + kExtraCellCount;
                const StreamingCellCoord       center    = grid.computeCell( origin );
                const float32                  radiusSq  = grid.getLoadingRadius() * grid.getLoadingRadius();
                const float4                   kLoaded( 0.2f, 0.9f, 0.3f, 1.0f );
                const float4                   kInRange( 0.95f, 0.85f, 0.2f, 1.0f );
                const float4                   kOutside( 0.5f, 0.5f, 0.5f, 0.6f );
                for ( int32 z = center._z - reach; z <= center._z + reach; ++z )
                {
                    for ( int32 x = center._x - reach; x <= center._x + reach; ++x )
                    {
                        const StreamingCellCoord cell{ x, z };
                        const bool               bInRange = grid.computeCellDistanceSquared( cell, origin ) <= radiusSq;
                        const float4&            color    = streaming.isCellLoaded( cell ) ? kLoaded : ( bInRange ? kInRange : kOutside );
                        const float32            minX     = static_cast<float32>( x ) * cellSize;
                        const float32            minZ     = static_cast<float32>( z ) * cellSize;
                        const float32            maxX     = minX + cellSize;
                        const float32            maxZ     = minZ + cellSize;
                        // 바닥(높이 0)에 셀 테두리를 그린다. 이웃 셀과 겹치는 변은 두 번 그려지지만 색이 같은 경우가 많고 디버그 선이라 둔다.
                        queue.drawLine( float3( minX, 0.0f, minZ ), float3( maxX, 0.0f, minZ ), color );
                        queue.drawLine( float3( maxX, 0.0f, minZ ), float3( maxX, 0.0f, maxZ ), color );
                        queue.drawLine( float3( maxX, 0.0f, maxZ ), float3( minX, 0.0f, maxZ ), color );
                        queue.drawLine( float3( minX, 0.0f, maxZ ), float3( minX, 0.0f, minZ ), color );
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SceneDebugDraw::update( Scene* pScene, DebugDrawQueue& queue )
    {
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        GameObjectManager& objectManager = *pScene->getObjectManager();

        // 물리 · 내비 같은 기능은 자기 서비스가 그린다(`ISceneService::drawDebug`) — 씬은 기능을 모른다.
        objectManager.drawServiceDebug( queue );

        if ( gv_worldCellDebugDraw )
            SceneDebugDrawInternal::drawStreamingCells( *pScene, queue );
    }
} // namespace sw
