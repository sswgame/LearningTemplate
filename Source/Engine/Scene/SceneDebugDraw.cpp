#include "pch.h"

#include "Engine/Scene/SceneDebugDraw.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Navigation/SceneNavigation.h"
#include "Engine/Object/Component/Physics/ScenePhysics.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/WorldCellGrid.h"
#include "Engine/Scene/WorldStreamingSubsystem.h"

namespace sw
{
    /** @brief 활성 씬의 강체 물리(바디 셰이프 · 캐릭터 캡슐)를 디버그 선으로 그립니다(`ScenePhysics::drawDebug` → `DebugDrawQueue`). */
    SW_GLOBAL_VARIABLE( bool, gv_physicsDebugDraw, false, "강체 물리 바디 · 캐릭터를 디버그 선으로 그린다" );
    SW_GLOBAL_VARIABLE( int32, gv_navDebugDraw, 0, "내비메시 디버그 — 선(편집기 뷰포트): 1 폴리곤 테두리 · 2 에이전트 경로 · 4 에이전트 속도 · 8 장애물, 16 걷는 면 · 경로를 게임 화면의 메시로 (31 = 모두, 0 = 끔)" );
    /** @brief 씬의 월드 스트리밍 격자를 활성 게임 카메라 둘레에 선으로 그립니다(올라 있는 셀 초록, 로딩 반경 안 노랑, 그 밖 회색). */
    SW_GLOBAL_VARIABLE( bool, gv_worldCellDebugDraw, false, "월드 스트리밍 격자를 카메라 둘레에 그린다 — 올라 있는 셀 초록, 로딩 반경 안 노랑, 그 밖 회색" );
} // namespace sw

namespace sw
{
    namespace
    {
        /**
         * @class SceneDebugDrawLineAdapter
         * @brief 물리 · 내비 디버그 선(`IPhysicsDebugRenderer`)을 디버그 드로우 큐로 옮깁니다.
         */
        class SceneDebugDrawLineAdapter final : public IPhysicsDebugRenderer
        {
        public:
            explicit SceneDebugDrawLineAdapter( DebugDrawQueue& queue )
                : _queue{ queue }
            {
            }

            void drawLine( const float3& from, const float3& to, const float4& color ) override { _queue.drawLine( from, to, color ); }

        private:
            DebugDrawQueue& _queue;
        };

        struct SceneDebugDrawInternal
        {
            /** @brief 카메라 둘레에서 그리는 셀 수(로딩 반경 밖으로 더 보이는 칸)입니다. */
            static constexpr int32 kExtraCellCount = 2;

            /**
             * @brief 씬의 격자(`WorldSettings::_cellGrid`)를 카메라 둘레에 그립니다. 격자가 없는 씬(셀 크기 0)이면 그리지 않습니다.
             * @details 에디터에서 연 씬(XML)은 셀을 나누지 않았으므로 노랑 · 회색만 나온다 — 쿠킹이 나눌 자리의 미리보기다.
             */
            static void drawWorldCells( Scene& scene, DebugDrawQueue& queue )
            {
                const WorldSettings& settings = scene.getWorldSettings();
                WorldCellGrid        grid;
                if ( settings._cellGrid._cellSize <= 0.0f || WorldCellGrid::tryCreate( settings._cellGrid, grid ) == false )
                    return;
                const CameraComponent* pCamera = scene.getActiveGameCamera();
                if ( pCamera == nullptr )
                    return;
                const float3                   origin    = pCamera->getWorldPosition();
                const WorldStreamingSubsystem& streaming = scene.getObjectManager()->getSubsystem<WorldStreamingSubsystem>();
                const float32                  cellSize  = grid.getCellSize();
                const int32                    reach     = static_cast<int32>( grid.getLoadingRadius() / cellSize ) + kExtraCellCount;
                const WorldCellCoord           center    = grid.computeCell( origin );
                const float32                  radiusSq  = grid.getLoadingRadius() * grid.getLoadingRadius();
                const float4                   kLoaded( 0.2f, 0.9f, 0.3f, 1.0f );
                const float4                   kInRange( 0.95f, 0.85f, 0.2f, 1.0f );
                const float4                   kOutside( 0.5f, 0.5f, 0.5f, 0.6f );
                for ( int32 z = center._z - reach; z <= center._z + reach; ++z )
                {
                    for ( int32 x = center._x - reach; x <= center._x + reach; ++x )
                    {
                        const WorldCellCoord cell{ x, z };
                        const bool           bInRange = grid.computeCellDistanceSquared( cell, origin ) <= radiusSq;
                        const float4&        color    = streaming.isCellLoaded( cell ) ? kLoaded : ( bInRange ? kInRange : kOutside );
                        const float32        minX     = static_cast<float32>( x ) * cellSize;
                        const float32        minZ     = static_cast<float32>( z ) * cellSize;
                        const float32        maxX     = minX + cellSize;
                        const float32        maxZ     = minZ + cellSize;
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

        if ( gv_physicsDebugDraw )
        {
            SceneDebugDrawLineAdapter adapter{ queue };
            objectManager.getSubsystem<sw::ScenePhysics>().drawDebug( adapter );
        }

        // 내비메시 · 에이전트 경로 · 속도 · 장애물 — 선은 물리 디버그와 같은 출구로(편집기 뷰포트가 그린다), 비트 16 은 게임 화면에 보이는 메시로.
        SceneNavigation& navigation = objectManager.getSubsystem<SceneNavigation>();
        navigation.updateDebugView( static_cast<uint32>( gv_navDebugDraw ) );
        if ( gv_navDebugDraw != 0 )
        {
            SceneDebugDrawLineAdapter adapter{ queue };
            navigation.drawDebug( adapter, static_cast<uint32>( gv_navDebugDraw ) );
        }

        if ( gv_worldCellDebugDraw )
            SceneDebugDrawInternal::drawWorldCells( *pScene, queue );
    }
} // namespace sw
