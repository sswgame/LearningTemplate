#include "pch.h"

#include "Engine/Navigation/Scene/NavStreamRoute.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/Component/NavMeshAgentComponent.h"
#include "Engine/Navigation/INavMesh.h"
#include "Engine/Navigation/NavClusterGraph.h"
#include "Engine/Navigation/Scene/NavMeshRuntime.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

namespace sw
{
    namespace
    {
        struct NavStreamRouteInternal
        {
            /** @brief 앞선 가장자리 점을 타일 경계에서 안쪽으로 당기는 거리(미터)입니다 — 경계 바로 위 점은 이웃 타일이 내려가면 붙을 폴리곤이 없다. */
            static constexpr float32 kFrontierInset = 0.5f;
            /** @brief 중간 목표에 닿았다고 보는 최소 거리(미터)입니다. 에이전트의 멈춤 거리가 더 크면 그것이다. */
            static constexpr float32 kMinIntermediateReach = 1.0f;
            /** @brief 새 중간 목표가 이전 것과 이보다 가까우면 군중 요청을 다시 넣지 않는다(미터). */
            static constexpr float32 kSameTargetDistance = 0.05f;
            /** @brief 타일 크기 · 방향 성분을 0 으로 보는 문턱(미터)입니다. */
            static constexpr float32 kLengthEpsilon = 1.0e-4f;
            /** @brief 가장자리 점을 내비메시에 붙일 때 종류의 찾기 범위에 곱하는 배율입니다(타일 가장자리는 걷는 면이 깎여 있다). */
            static constexpr float32 kSnapExtentScale = 2.0f;

            static int2 computeTile( const NavTileGrid& grid, const float3& position )
            {
                const float32 size = MathUtil::max( grid._tileWorldSize, kLengthEpsilon );
                return int2{ static_cast<int32>( MathUtil::floor( ( position._x - grid._origin._x ) / size ) ), static_cast<int32>( MathUtil::floor( ( position._z - grid._origin._z ) / size ) ) };
            }

            /** @brief 자리의 클러스터 — 제 타일에 없으면(걸을 곳 밖 · 덩어리 경계) 여덟 이웃 타일에서 중심이 가장 가까운 것입니다. */
            static uint32 findNearbyCluster( const NavClusterGraph& graph, const NavTileGrid& grid, const float3& position )
            {
                const int2   tile  = computeTile( grid, position );
                const uint32 found = graph.findCluster( tile._x, tile._y, position );
                if ( found != NavClusterGraph::kNoCluster )
                    return found;
                uint32  best         = NavClusterGraph::kNoCluster;
                float32 bestDistance = MathUtil::kMaxFloat;
                for ( int32 offsetZ = -1; offsetZ <= 1; ++offsetZ )
                {
                    for ( int32 offsetX = -1; offsetX <= 1; ++offsetX )
                    {
                        const uint32 neighbour = graph.findCluster( tile._x + offsetX, tile._y + offsetZ, position );
                        if ( neighbour == NavClusterGraph::kNoCluster )
                            continue;
                        const float32 distanceSquared = float3::getDistanceSquared( graph.getClusterList()[neighbour]._center, position );
                        if ( distanceSquared < bestDistance )
                        {
                            bestDistance = distanceSquared;
                            best         = neighbour;
                        }
                    }
                }
                return best;
            }

            static bool isTileLoaded( const NavMeshRuntime& runtime, int32 tileX, int32 tileZ )
            {
                const NavStreamTile* pTile = runtime.findStreamTile( tileX, tileZ );
                return pTile != nullptr && pTile->_state == NavStreamTile::State::Loaded;
            }

            static float32 computeArrivalRadius( const NavMeshAgentComponent& agent ) { return MathUtil::max( agent.getStoppingDistance(), kMinIntermediateReach ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool NavStreamRoute::usesRoutes( const NavMeshRuntime& runtime )
    {
        return runtime._listStreamTile.empty() == false && runtime._clusterGraph.isEmpty() == false;
    }

    bool NavStreamRoute::isPositionLoaded( const NavMeshRuntime& runtime, const float3& position )
    {
        if ( runtime._listStreamTile.empty() )
            return true;
        const int2 tile = NavStreamRouteInternal::computeTile( runtime._pNavMesh->getTileGrid(), position );
        return NavStreamRouteInternal::isTileLoaded( runtime, tile._x, tile._y );
    }

    bool NavStreamRoute::plan( const NavMeshRuntime& runtime, const float3& start, const float3& end, NavRoutePlan& outPlan )
    {
        using Internal = NavStreamRouteInternal;
        outPlan        = NavRoutePlan{};
        if ( usesRoutes( runtime ) == false )
            return false;
        const NavClusterGraph& graph        = runtime._clusterGraph;
        const NavTileGrid&     grid         = runtime._pNavMesh->getTileGrid();
        const uint32           startCluster = Internal::findNearbyCluster( graph, grid, start );
        const uint32           goalCluster  = Internal::findNearbyCluster( graph, grid, end );
        if ( startCluster == NavClusterGraph::kNoCluster || goalCluster == NavClusterGraph::kNoCluster || graph.findRoute( startCluster, goalCluster, outPlan._listCluster ) == false )
            return false;
        const vector<NavCluster>& listCluster = graph.getClusterList();
        const int2                startTile   = Internal::computeTile( grid, start );
        outPlan._bStartLoaded                 = Internal::isTileLoaded( runtime, startTile._x, startTile._y );
        if ( outPlan._bStartLoaded == false )
            return true;
        // 시작 클러스터부터 타일이 이어 오른 데까지가 세밀 경로의 몫이다.
        for ( uint32 routeIndex = 0; routeIndex < outPlan._listCluster.size(); ++routeIndex )
        {
            const NavCluster& cluster = listCluster[outPlan._listCluster[routeIndex]];
            if ( Internal::isTileLoaded( runtime, cluster._tileX, cluster._tileZ ) == false )
                break;
            outPlan._frontier = routeIndex;
        }
        return true;
    }

    float3 NavStreamRoute::computeFrontierTarget( const NavMeshRuntime& runtime, const NavRoutePlan& route, const float3& end )
    {
        using Internal = NavStreamRouteInternal;
        if ( route.isLoadedToGoal() )
            return end;
        const vector<NavCluster>& listCluster = runtime._clusterGraph.getClusterList();
        const NavCluster&         frontier    = listCluster[route._listCluster[route._frontier]];
        const NavCluster&         next        = listCluster[route._listCluster[route._frontier + 1]];
        // 앞선 중심에서 다음 중심으로 가는 선이 앞선 타일을 나가는 자리(조금 안쪽)다.
        const NavTileGrid& grid   = runtime._pNavMesh->getTileGrid();
        const AABB         bounds = grid.computeTileBounds( frontier._tileX, frontier._tileZ, 0.0f, 0.0f );
        const float3       delta  = next._center - frontier._center;
        float32            exit   = 1.0f;
        if ( delta._x > Internal::kLengthEpsilon )
            exit = MathUtil::min( exit, ( bounds._max._x - Internal::kFrontierInset - frontier._center._x ) / delta._x );
        else if ( delta._x < -Internal::kLengthEpsilon )
            exit = MathUtil::min( exit, ( bounds._min._x + Internal::kFrontierInset - frontier._center._x ) / delta._x );
        if ( delta._z > Internal::kLengthEpsilon )
            exit = MathUtil::min( exit, ( bounds._max._z - Internal::kFrontierInset - frontier._center._z ) / delta._z );
        else if ( delta._z < -Internal::kLengthEpsilon )
            exit = MathUtil::min( exit, ( bounds._min._z + Internal::kFrontierInset - frontier._center._z ) / delta._z );
        const float3 wanted = frontier._center + delta * MathUtil::clamp( exit, 0.0f, 1.0f );
        NavLocation  location{};
        if ( runtime._pNavMesh->findNearestPoint( wanted, runtime._searchExtent * Internal::kSnapExtentScale, runtime._filter, location ) )
            return location._position;
        return frontier._center;
    }

    void NavStreamRoute::collectCoarsePoints( const NavMeshRuntime& runtime, const NavRoutePlan& route, uint32 firstIndex, const float3& end, vector<float3>& outListPoint )
    {
        outListPoint.clear();
        const vector<NavCluster>& listCluster = runtime._clusterGraph.getClusterList();
        // 마지막 클러스터의 중심은 넣지 않는다 — 목적지가 그 덩어리 안이다.
        for ( size_t routeIndex = firstIndex; routeIndex + 1 < route._listCluster.size(); ++routeIndex )
        {
            outListPoint.push_back( listCluster[route._listCluster[routeIndex]]._center );
        }
        outListPoint.push_back( end );
    }

    void NavStreamRoute::refreshAgent( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const float3& currentPosition )
    {
        if ( usesRoutes( runtime ) == false || agent._bHasDestination == SW_FALSE || agent._bDestinationDirty == SW_TRUE )
            return;
        const uint32 revision         = runtime._pNavMesh->getRevision();
        const bool   bRevisionChanged = revision != agent._routeRevision;
        if ( agent._bVirtualMove == SW_TRUE )
        {
            // 지나는 자리의 타일이 새로 올랐거나(판이 바뀌었거나) 오른 타일에 막 들었으면, 세밀 이동으로 돌아갈 수 있는지 다시 본다.
            const bool bLoaded        = isPositionLoaded( runtime, currentPosition );
            const bool bJustEntered   = bLoaded && agent._bVirtualOverLoaded == SW_FALSE;
            agent._bVirtualOverLoaded = bLoaded ? SW_TRUE : SW_FALSE;
            if ( bLoaded && ( bRevisionChanged || bJustEntered ) )
                agent._bDestinationDirty = SW_TRUE;
            agent._routeRevision = revision;
            return;
        }
        if ( agent._bRouteIntermediate == SW_TRUE && bRevisionChanged )
            agent._bDestinationDirty = SW_TRUE;
    }

    void NavStreamRoute::requestAgentMove( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const float3& currentPosition )
    {
        using Internal       = NavStreamRouteInternal;
        INavCrowd&   crowd   = *runtime._pCrowd;
        const float3 end     = agent._destination;
        agent._routeRevision = runtime._pNavMesh->getRevision();
        NavRoutePlan route;
        if ( plan( runtime, currentPosition, end, route ) == false || route.isLoadedToGoal() )
        {
            // 대략 그래프 밖이거나 다 올라 있다 — 세밀 경로 하나다(내려간 목적지면 군중이 `Failed` 로 알린다).
            const bool bWasVirtual = agent._bVirtualMove == SW_TRUE;
            clearAgentRoute( agent );
            NavCrowdAgentState state;
            if ( bWasVirtual || ( crowd.findAgentState( agent._crowdAgentID, state ) && state._bOnNavMesh == false ) )
                crowd.teleportAgent( agent._crowdAgentID, currentPosition ); // 내려갔다 다시 오른 타일 위의 에이전트는 Detour 가 스스로 되살리지 않는다
            agent._moveStatus = crowd.requestMoveTarget( agent._crowdAgentID, end ) ? NavMoveStatus::Moving : NavMoveStatus::Failed;
            return;
        }
        const bool bCanVirtualMove = agent._driveMode != NavAgentDriveMode::SteerOnly;
        if ( route._bStartLoaded == false )
        {
            if ( bCanVirtualMove )
            {
                enterVirtualMove( agent, runtime, route, 1, currentPosition );
                return;
            }
            agent._moveStatus = NavMoveStatus::Moving; // 폰 이동이 자리를 정한다 — 발밑 타일이 오를 때까지 기다린다
            return;
        }
        if ( agent._bVirtualMove == SW_TRUE && route._frontier == 0 )
            return; // 오른 타일 하나를 지나는 중이다 — 그 너머가 이어질 때까지 가상 이동을 잇는다
        NavCrowdAgentState state;
        if ( agent._bVirtualMove == SW_TRUE || ( crowd.findAgentState( agent._crowdAgentID, state ) && state._bOnNavMesh == false ) )
            crowd.teleportAgent( agent._crowdAgentID, currentPosition );
        agent._bVirtualMove = SW_FALSE;
        const float3 target = computeFrontierTarget( runtime, route, end );
        if ( route._frontier == 0 && float3::getHorizontalDistance( currentPosition, target ) <= Internal::computeArrivalRadius( agent ) )
        {
            // 앞선 가장자리에 닿았는데 그 너머가 아직 내려가 있다.
            if ( bCanVirtualMove )
                enterVirtualMove( agent, runtime, route, 1, currentPosition );
            else
                agent._moveStatus = NavMoveStatus::Moving;
            return;
        }
        const bool bSameTarget    = agent._bRouteIntermediate == SW_TRUE && float3::getDistance( target, agent._routeTarget ) < Internal::kSameTargetDistance;
        agent._routeTarget        = target;
        agent._bRouteIntermediate = SW_TRUE;
        collectCoarsePoints( runtime, route, route._frontier + 1, end, agent._listRoutePoint );
        if ( bSameTarget )
            return;
        ++agent._routeReplanCount;
        ++runtime._routeReplanCount;
        if ( crowd.requestMoveTarget( agent._crowdAgentID, target ) )
        {
            agent._moveStatus = NavMoveStatus::Moving;
            return;
        }
        // 가장자리 점이 붙을 곳이 없다(좁은 덩어리) — 가상 이동으로 넘긴다.
        if ( bCanVirtualMove )
            enterVirtualMove( agent, runtime, route, route._frontier + 1, currentPosition );
        else
            agent._moveStatus = NavMoveStatus::Failed;
    }

    void NavStreamRoute::clearAgentRoute( NavMeshAgentComponent& agent )
    {
        agent._listRoutePoint.clear();
        agent._bRouteIntermediate = SW_FALSE;
        agent._bVirtualMove       = SW_FALSE;
        agent._bVirtualOverLoaded = SW_FALSE;
    }

    void NavStreamRoute::handleIntermediateReached( NavMeshAgentComponent& agent, const float3& agentPosition, bool bCrowdArrived )
    {
        if ( agent._bRouteIntermediate == SW_FALSE )
            return;
        if ( bCrowdArrived || float3::getHorizontalDistance( agentPosition, agent._routeTarget ) <= NavStreamRouteInternal::computeArrivalRadius( agent ) )
            agent._bDestinationDirty = SW_TRUE; // 다음 갱신이 다시 계획한다(그 사이 오른 타일로 이어 가거나, 가장자리에서 가상 이동으로)
    }

    void NavStreamRoute::writeVirtualResult( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, float32 deltaTime, vector<SceneTransformWrite>& inoutListWrite )
    {
        const float3 previous = agent._agentPosition;
        float3       position = previous;
        float32      step     = agent._maxSpeed * deltaTime;
        size_t       reached  = 0;
        while ( step > 0.0f && reached < agent._listRoutePoint.size() )
        {
            const float3& next     = agent._listRoutePoint[reached];
            const float32 distance = float3::getDistance( position, next );
            if ( distance <= step )
            {
                position = next;
                step -= distance;
                ++reached;
                continue;
            }
            position = position + ( next - position ) * ( step / distance );
            step     = 0.0f;
        }
        agent._listRoutePoint.erase( agent._listRoutePoint.begin(), agent._listRoutePoint.begin() + static_cast<ptrdiff_t>( reached ) );
        const float3 velocity  = deltaTime > 0.0f ? ( position - previous ) * ( 1.0f / deltaTime ) : float3{};
        agent._agentPosition   = position;
        agent._velocity        = velocity;
        agent._desiredVelocity = velocity;
        agent._nextCorner      = agent._listRoutePoint.empty() ? position : agent._listRoutePoint.front();
        agent._bOnNavMesh      = SW_FALSE;
        agent._bOnOffMeshLink  = SW_FALSE;
        if ( agent._listRoutePoint.empty() )
        {
            // 내려간 구역 안의 목적지에 대략 그래프로 닿았다. 군중 쪽 몸도 여기로 옮긴다 — 다음 갱신부터 쓰는 군중 자리가 첫 대략 점으로 되돌아가지 않게.
            runtime._pCrowd->teleportAgent( agent._crowdAgentID, position );
            clearAgentRoute( agent );
            agent._moveStatus = NavMoveStatus::Arrived;
        }
        GameObject*                   pOwner      = agent.getOwner();
        SceneComponent*               pScene      = pOwner->getPrimarySceneComponent();
        CharacterControllerComponent* pController = agent.getDriveMode() == NavAgentDriveMode::CharacterController ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        if ( pController != nullptr )
        {
            pController->setMoveVelocity( float3{} );
            pController->setWorldPosition( position );
            return;
        }
        if ( agent.getDriveMode() != NavAgentDriveMode::Transform || pScene == nullptr )
            return;
        agent._writtenPosition = position;
        agent._bHasWritten     = SW_TRUE;
        if ( pScene->getParent() != nullptr )
        {
            pScene->setWorldPosition( position );
            return;
        }
        SceneTransformWrite write{};
        write._handle        = pScene->getHandle();
        write._localPosition = position;
        write._bSetPosition  = SW_TRUE;
        inoutListWrite.push_back( write );
    }
} // namespace sw

namespace sw
{
    void NavStreamRoute::enterVirtualMove( NavMeshAgentComponent& agent, NavMeshRuntime& runtime, const NavRoutePlan& route, uint32 firstIndex, const float3& currentPosition )
    {
        if ( agent._bVirtualMove == SW_FALSE )
        {
            ++agent._routeReplanCount;
            ++runtime._routeReplanCount;
        }
        NavStreamRoute::collectCoarsePoints( runtime, route, firstIndex, agent._destination, agent._listRoutePoint );
        // 군중 쪽 몸은 내려간 구역(첫 대략 점)으로 보낸다 — 붙을 폴리곤이 없어 걷지 않고(`INVALID`), 앞선 가장자리에 남아 다른 에이전트를 막지 않는다.
        runtime._pCrowd->teleportAgent( agent._crowdAgentID, agent._listRoutePoint.front() );
        agent._agentPosition      = currentPosition;
        agent._bVirtualMove       = SW_TRUE;
        agent._bRouteIntermediate = SW_FALSE;
        agent._bVirtualOverLoaded = NavStreamRoute::isPositionLoaded( runtime, currentPosition ) ? SW_TRUE : SW_FALSE;
        agent._moveStatus         = NavMoveStatus::Moving;
    }
} // namespace sw
