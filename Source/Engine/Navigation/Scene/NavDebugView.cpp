#include "pch.h"

#include "Engine/Navigation/Scene/NavDebugView.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/Component/NavMeshModifierComponent.h"
#include "Engine/Navigation/Component/NavMeshObstacleComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/PhysicsDebugDraw.h"
#include "Engine/Visual/Component/MeshComponent.h"

namespace sw
{
    namespace
    {
        struct NavDebugViewInternal
        {
            /** @brief 디버그 메시를 걷는 면 위로 띄우는 높이입니다(바닥과 겹쳐 깜빡이지 않게). */
            static constexpr float32 kViewLift = 0.04f;
            // 대략 그래프(`NavDebugDrawFlag::kCluster`)의 색 · 크기 — 타일 스트리밍 단계마다 색 하나, 먼 경로는 중간 목표 주황 · 대략 점 보라.
            static constexpr float4  kClusterLoadedColor{ 0.2f, 0.85f, 0.3f, 1.0f };
            static constexpr float4  kClusterLoadingColor{ 0.95f, 0.8f, 0.1f, 1.0f };
            static constexpr float4  kClusterUnloadedColor{ 0.5f, 0.5f, 0.5f, 1.0f };
            static constexpr float4  kClusterFailedColor{ 1.0f, 0.2f, 0.15f, 1.0f };
            static constexpr float4  kRouteTargetColor{ 1.0f, 0.55f, 0.05f, 1.0f };
            static constexpr float4  kRouteCoarseColor{ 0.75f, 0.35f, 0.95f, 1.0f };
            static constexpr float32 kClusterLift      = 0.3f; ///< 걷는 면 테두리와 겹치지 않게 띄운다
            static constexpr float32 kClusterMarkSize  = 0.4f; ///< 클러스터 십자의 반 길이
            static constexpr float32 kClusterEdgeAlpha = 0.5f;

            static float4 makeAreaColor( uint8 area, size_t triangleIndex )
            {
                const float4 arrColor[4] = {
                    float4{0.10f, 0.75f, 0.85f, 1.0f},
                    float4{0.35f, 0.80f, 0.25f, 1.0f},
                    float4{0.55f, 0.40f, 0.20f, 1.0f},
                    float4{0.15f, 0.30f, 0.90f, 1.0f},
                };
                const float4& base  = arrColor[area % 4];
                const float32 shade = ( triangleIndex / 2 ) % 2 == 0 ? 1.0f : 0.8f;
                return float4{ base._x * shade, base._y * shade, base._z * shade, 1.0f };
            }

            static RHIVertex makeVertex( const float3& position, const float4& color )
            {
                RHIVertex vertex{};
                vertex._arrPosition[0] = position._x;
                vertex._arrPosition[1] = position._y;
                vertex._arrPosition[2] = position._z;
                vertex._arrNormal[1]   = 1.0f;
                vertex._arrColor[0]    = color._x;
                vertex._arrColor[1]    = color._y;
                vertex._arrColor[2]    = color._z;
                vertex._arrColor[3]    = color._w;
                return vertex;
            }

            /** @brief 두 점을 잇는 폭 0.12 m 띠(위를 보는 사각형 둘 — 양면)를 더합니다. */
            static void appendRibbon( const float3& from, const float3& to, const float4& color, vector<RHIVertex>& outListVertex )
            {
                const float3  delta  = to - from;
                const float32 length = float3::getHorizontalDistance( from, to );
                if ( length < 1.0e-3f )
                    return;
                const float3 side{ -delta._z / length * 0.06f, 0.0f, delta._x / length * 0.06f };
                const float3 lift{ 0.0f, 0.1f, 0.0f };
                const float3 arrCorner[4] = { from - side + lift, from + side + lift, to + side + lift, to - side + lift };
                const uint32 arrIndex[12] = { 0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2 };
                for ( const uint32 index : arrIndex )
                {
                    outListVertex.push_back( makeVertex( arrCorner[index], color ) );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void NavDebugView::drawLines( IPhysicsDebugRenderer& renderer, uint32 flags, const vector<unique_ptr<NavMeshRuntime>>& listRuntime,
                                  const vector<NavMeshObstacleComponent*>& listObstacle, const vector<NavMeshAgentComponent*>& listAgent )
    {
        if ( ( flags & NavDebugDrawFlag::kCluster ) != 0 )
            drawClusterGraph( renderer, listRuntime, listAgent );
        const float4 meshColor{ 0.1f, 0.85f, 0.9f, 1.0f };
        const float4 obstacleColor{ 1.0f, 0.25f, 0.2f, 1.0f };
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            if ( ( flags & NavDebugDrawFlag::kMesh ) != 0 )
                pRuntime->_pNavMesh->drawDebug( renderer, meshColor );
            if ( pRuntime->_pCrowd != nullptr && ( flags & ( NavDebugDrawFlag::kPath | NavDebugDrawFlag::kVelocity ) ) != 0 )
                pRuntime->_pCrowd->drawDebug( renderer, ( flags & NavDebugDrawFlag::kPath ) != 0, ( flags & NavDebugDrawFlag::kVelocity ) != 0 );
        }
        if ( ( flags & NavDebugDrawFlag::kObstacle ) == 0 )
            return;
        NavConvexVolume volume;
        for ( const NavMeshObstacleComponent* pObstacle : listObstacle )
        {
            if ( pObstacle->_bCarved == SW_FALSE || pObstacle->makeVolume( volume ) == false )
                continue;
            for ( size_t pointIndex = 0; pointIndex < volume._listPoint.size(); ++pointIndex )
            {
                const float3& from = volume._listPoint[pointIndex];
                const float3& to   = volume._listPoint[( pointIndex + 1 ) % volume._listPoint.size()];
                renderer.drawLine( float3{ from._x, volume._minY, from._z }, float3{ to._x, volume._minY, to._z }, obstacleColor );
                renderer.drawLine( float3{ from._x, volume._maxY, from._z }, float3{ to._x, volume._maxY, to._z }, obstacleColor );
            }
        }
    }

    void NavDebugView::drawClusterGraph( IPhysicsDebugRenderer& renderer, const vector<unique_ptr<NavMeshRuntime>>& listRuntime, const vector<NavMeshAgentComponent*>& listAgent )
    {
        // 클러스터는 작은 십자, 간선은 중심 사이 선이다. 색은 그 클러스터 타일의 스트리밍 단계(오름 초록 · 읽는 중 노랑 · 내려감 회색 · 실패 빨강).
        using Internal = NavDebugViewInternal;
        const float3 lift{ 0.0f, Internal::kClusterLift, 0.0f };
        const float3 markX{ Internal::kClusterMarkSize, 0.0f, 0.0f };
        const float3 markZ{ 0.0f, 0.0f, Internal::kClusterMarkSize };
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            const vector<NavCluster>&     listCluster = pRuntime->_clusterGraph.getClusterList();
            const vector<NavClusterEdge>& listEdge    = pRuntime->_clusterGraph.getEdges();
            for ( const NavCluster& cluster : listCluster )
            {
                const NavStreamTile* pTile = pRuntime->findStreamTile( cluster._tileX, cluster._tileZ );
                float4               color = Internal::kClusterUnloadedColor;
                if ( pTile != nullptr && pTile->_state == NavStreamTile::State::Loaded )
                    color = Internal::kClusterLoadedColor;
                else if ( pTile != nullptr && pTile->_state == NavStreamTile::State::Loading )
                    color = Internal::kClusterLoadingColor;
                else if ( pTile != nullptr && pTile->_state == NavStreamTile::State::Failed )
                    color = Internal::kClusterFailedColor;
                const float3 center = cluster._center + lift;
                const float4 edgeColor{ color._x, color._y, color._z, Internal::kClusterEdgeAlpha };
                renderer.drawLine( center - markX, center + markX, color );
                renderer.drawLine( center - markZ, center + markZ, color );
                for ( uint32 edgeIndex = cluster._firstEdge; edgeIndex < cluster._firstEdge + cluster._edgeCount; ++edgeIndex )
                {
                    renderer.drawLine( center, listCluster[listEdge[edgeIndex]._target]._center + lift, edgeColor );
                }
            }
        }
        // 먼 경로 — 자리 → 중간 목표(주황) → 대략 점들(보라). 가상 이동은 자리에서 바로 대략 점이다.
        for ( const NavMeshAgentComponent* pAgent : listAgent )
        {
            if ( pAgent->isFollowingCoarseRoute() == false && pAgent->isInVirtualMove() == false )
                continue;
            float3 from = pAgent->getAgentPosition() + lift;
            if ( pAgent->isFollowingCoarseRoute() )
            {
                renderer.drawLine( from, pAgent->getRouteTarget() + lift, Internal::kRouteTargetColor );
                from = pAgent->getRouteTarget() + lift;
            }
            for ( const float3& point : pAgent->getCoarseRoutePoints() )
            {
                renderer.drawLine( from, point + lift, Internal::kRouteCoarseColor );
                from = point + lift;
            }
        }
    }

    void NavDebugView::updateSolidView( GameObjectManager& manager, bool bShown, const vector<unique_ptr<NavMeshRuntime>>& listRuntime,
                                        const vector<NavMeshAgentComponent*>& listAgent )
    {
        using Internal      = NavDebugViewInternal;
        GameObject* pView   = manager.resolveGameObject( _viewObject );
        const bool  bWanted = bShown && listRuntime.empty() == false;
        if ( bWanted == false )
        {
            if ( pView != nullptr )
                manager.destroyGameObject( pView );
            _viewObject = GameObjectHandle{};
            return;
        }
        if ( pView == nullptr )
        {
            pView = manager.createGameObject( hashed_string( "NavMeshDebugView" ) );
            pView->setTransient( true ); // 세운 쪽이 다시 세우는 파생 — 세이브 · 상태 해시에 넣지 않는다(`GameObject::setTransient`)
            pView->addComponent<NavMeshModifierComponent>()->setIgnoreFromBuild( true );
            (void)pView->addComponent<MeshComponent>();
            _viewObject = pView->getHandle();
        }
        MeshComponent* pMeshComponent = pView->getComponent<MeshComponent>();
        if ( pMeshComponent == nullptr )
            return;
        vector<RHIVertex> listVertex;
        vector<float3>    listCorner;
        vector<uint8>     listArea;
        for ( const unique_ptr<NavMeshRuntime>& pRuntime : listRuntime )
        {
            pRuntime->_pNavMesh->collectDebugTriangles( listCorner, listArea );
            for ( size_t triangleIndex = 0; triangleIndex < listArea.size(); ++triangleIndex )
            {
                const float4 color = Internal::makeAreaColor( listArea[triangleIndex], triangleIndex );
                for ( uint32 corner = 0; corner < 3; ++corner )
                {
                    listVertex.push_back( Internal::makeVertex( listCorner[triangleIndex * 3 + corner] + float3{ 0.0f, Internal::kViewLift, 0.0f }, color ) );
                }
            }
        }
        // 에이전트 경로 띠 — 자리 → 다음 모퉁이 → 목적지.
        const float4 pathColor{ 1.0f, 0.55f, 0.05f, 1.0f };
        for ( const NavMeshAgentComponent* pAgent : listAgent )
        {
            const bool bNotFollowingPath = pAgent->_crowdAgentID == NavigationConstant::kInvalidAgentID || pAgent->_bHasDestination == SW_FALSE ||
                                           pAgent->_moveStatus != NavMoveStatus::Moving;
            if ( bNotFollowingPath )
                continue;
            Internal::appendRibbon( pAgent->_agentPosition, pAgent->_nextCorner, pathColor, listVertex );
            Internal::appendRibbon( pAgent->_nextCorner, pAgent->_destination, pathColor, listVertex );
        }
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( listVertex ) );
        pMeshComponent->setMesh( std::move( mesh ) );
    }
} // namespace sw
