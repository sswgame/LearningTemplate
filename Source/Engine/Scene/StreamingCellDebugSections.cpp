/**
 * @file StreamingCellDebugSections.cpp
 * @brief 디버그 HUD 범주 `worldcells` — 월드 스트리밍 격자를 활성 게임 카메라 둘레에 선으로 그립니다(올라 있는 셀 초록, 로딩 반경 안 노랑, 그 밖 회색).
 */
#include "pch.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Scene/StreamingCellGrid.h"

namespace sw
{
    namespace
    {
        struct StreamingCellDebugSectionsInternal
        {
            /** @brief 카메라 둘레에서 그리는 셀 수(로딩 반경 밖으로 더 보이는 칸)입니다. */
            static constexpr int32 kExtraCellCount = 2;
            /** @brief HUD 섹션 순서 — 씬 섹션(40) 바로 아래다. */
            static constexpr int32 kSectionOrder = 41;

            /**
             * @brief 씬의 격자(`SceneSettings::_cellGrid`)를 카메라 둘레에 그립니다. 격자가 없는 씬(셀 크기 0)이면 그리지 않습니다.
             * @details 에디터에서 연 씬(XML)은 셀을 나누지 않았으므로 노랑 · 회색만 나온다 — 쿠킹이 나눌 자리의 미리보기다.
             */
            static void drawStreamingCells( const DebugHUDSectionDraw& draw )
            {
                Scene&               scene    = draw._scene;
                const hashed_string  category = hashed_string( "worldcells" );
                const SceneSettings& settings = scene.getSceneSettings();
                StreamingCellGrid    grid;
                if ( settings._cellGrid._cellSize <= 0.0f || StreamingCellGrid::tryCreate( settings._cellGrid, grid ) == false )
                    return;
                const CameraComponent* pCamera = scene.getActiveGameCamera();
                if ( pCamera == nullptr )
                    return;
                // 격자 · 스트리밍 원점은 절대 자리다 — 카메라를 절대로 옮겨 묻고, 선은 큐가 다시 옮긴다.
                const WorldOrigin&             worldOrigin = scene.getObjectManager()->getWorldOrigin();
                const float3                   origin      = worldOrigin.toAbsolute( pCamera->getWorldPosition() );
                const SceneStreamingSubsystem& streaming   = scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
                const float32                  cellSize    = grid.getCellSize();
                const int32                    reach       = static_cast<int32>( grid.getLoadingRadius() / cellSize ) + kExtraCellCount;
                const StreamingCellCoord       center      = grid.computeCell( origin );
                // 스트리밍 중이면 실제 원점(원점마다 반경)으로, 셀을 받지 않은 씬(에디터 XML)은 카메라 원점(격자 로딩 반경)으로 범위를 칠한다 — 카메라 원점의 정의가 그 반경이다.
                const StreamingSource             cameraSource{ origin, grid.getLoadingRadius(), StreamingSourcePriority::Normal };
                const span<const StreamingSource> listSource = streaming.isStreaming() ? streaming.getActiveSources() : span<const StreamingSource>( &cameraSource, 1 );
                const auto                        isInRange  = [&grid, &listSource]( const StreamingCellCoord& cell )
                {
                    for ( const StreamingSource& source : listSource )
                    {
                        if ( grid.computeCellDistanceSquared( cell, source._position ) <= source._loadingRadius * source._loadingRadius )
                            return true;
                    }
                    return false;
                };
                const float4 kLoaded( 0.2f, 0.9f, 0.3f, 1.0f );
                const float4 kInRange( 0.95f, 0.85f, 0.2f, 1.0f );
                const float4 kOutside( 0.5f, 0.5f, 0.5f, 0.6f );
                for ( int32 z = center._z - reach; z <= center._z + reach; ++z )
                {
                    for ( int32 x = center._x - reach; x <= center._x + reach; ++x )
                    {
                        const StreamingCellCoord cell{ x, z };
                        const bool               bInRange = isInRange( cell );
                        const float4&            color    = streaming.isCellLoaded( cell ) ? kLoaded : ( bInRange ? kInRange : kOutside );
                        const float32            minX     = static_cast<float32>( x ) * cellSize;
                        const float32            minZ     = static_cast<float32>( z ) * cellSize;
                        const float32            maxX     = minX + cellSize;
                        const float32            maxZ     = minZ + cellSize;
                        // 바닥(높이 0)에 셀 테두리를 그린다. 이웃 셀과 겹치는 변은 두 번 그려지지만 색이 같은 경우가 많고 디버그 선이라 둔다.
                        draw._queue.drawLine( worldOrigin, AbsolutePosition{ float3( minX, 0.0f, minZ ) }, AbsolutePosition{ float3( maxX, 0.0f, minZ ) }, color, 0.0f, category );
                        draw._queue.drawLine( worldOrigin, AbsolutePosition{ float3( maxX, 0.0f, minZ ) }, AbsolutePosition{ float3( maxX, 0.0f, maxZ ) }, color, 0.0f, category );
                        draw._queue.drawLine( worldOrigin, AbsolutePosition{ float3( maxX, 0.0f, maxZ ) }, AbsolutePosition{ float3( minX, 0.0f, maxZ ) }, color, 0.0f, category );
                        draw._queue.drawLine( worldOrigin, AbsolutePosition{ float3( minX, 0.0f, maxZ ) }, AbsolutePosition{ float3( minX, 0.0f, minZ ) }, color, 0.0f, category );
                    }
                }
            }
        };
    } // namespace

    SW_DEBUG_HUD_CATEGORY( worldcells, "worldcells", "World streaming cells around the camera", StreamingCellDebugSectionsInternal::kSectionOrder, DebugHUDSectionFlag::kNone, nullptr,
                           &StreamingCellDebugSectionsInternal::drawStreamingCells, nullptr );
} // namespace sw
