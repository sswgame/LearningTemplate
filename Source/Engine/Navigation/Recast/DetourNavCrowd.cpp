#include "pch.h"

#include "Engine/Navigation/Recast/DetourNavCrowd.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/Recast/RecastNavMesh.h"
#include "Engine/Physics/PhysicsDebugDraw.h"

#include <recastnavigation/DetourCommon.h>
#include <recastnavigation/DetourCrowd.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshQuery.h>

namespace sw
{
    namespace
    {
        struct DetourNavCrowdInternal
        {
            /** @brief 군중 밖 질의 객체의 노드 수입니다(목적지 붙이기 · 자리 맞추기는 짧다). */
            static constexpr int32 kQueryNodeCount = 512;
            /** @brief 회피 품질 넷의 적응 표본(나눔 · 고리 · 깊이) — Detour 예제의 Low · Medium · Good · High 값입니다. */
            static constexpr uint8 kArrAdaptiveDivision[4] = { 5, 5, 7, 7 };
            static constexpr uint8 kArrAdaptiveRing[4]     = { 2, 2, 2, 3 };
            static constexpr uint8 kArrAdaptiveDepth[4]    = { 1, 2, 3, 3 };

            static void toDetourParams( const NavCrowdAgentParams& params, dtCrowdAgentParams& outParams )
            {
                outParams                       = dtCrowdAgentParams{};
                outParams.radius                = params._radius;
                outParams.height                = params._height;
                outParams.maxAcceleration       = params._maxAcceleration;
                outParams.maxSpeed              = params._maxSpeed;
                outParams.collisionQueryRange   = params._collisionQueryRange;
                outParams.pathOptimizationRange = params._pathOptimizationRange;
                outParams.separationWeight      = params._separationWeight;
                uint8 flags                     = DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
                if ( params._bAnticipateTurns )
                    flags |= DT_CROWD_ANTICIPATE_TURNS;
                if ( params._bAvoidance )
                    flags |= DT_CROWD_OBSTACLE_AVOIDANCE;
                if ( params._bSeparation )
                    flags |= DT_CROWD_SEPARATION;
                outParams.updateFlags           = flags;
                outParams.obstacleAvoidanceType = static_cast<uint8>( params._avoidanceQuality );
                outParams.queryFilterType       = 0;
                outParams.userData              = nullptr;
            }

            static float3 toFloat3( const float32* pValue ) { return float3{ pValue[0], pValue[1], pValue[2] }; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DetourNavCrowd::DetourNavCrowd()
        : _pCrowd{ nullptr }
        , _pQuery{ nullptr }
        , _listFailed{}
        , _listTarget{}
        , _maxAgentCount{ 0 }
    {
    }

    DetourNavCrowd::~DetourNavCrowd()
    {
        shutdown();
    }

    bool DetourNavCrowd::initialize( RecastNavMesh& navMesh, uint32 maxAgentCount, float32 maxAgentRadius )
    {
        using Internal = DetourNavCrowdInternal;
        shutdown();
        dtNavMesh* pNavMesh = navMesh.getDetourNavMesh();
        if ( pNavMesh == nullptr || maxAgentCount == 0 )
            return false;
        _pCrowd = dtAllocCrowd();
        _pQuery = dtAllocNavMeshQuery();
        if ( _pCrowd == nullptr || _pQuery == nullptr || _pCrowd->init( static_cast<int32>( maxAgentCount ), maxAgentRadius, pNavMesh ) == false ||
             dtStatusFailed( _pQuery->init( pNavMesh, Internal::kQueryNodeCount ) ) )
        {
            shutdown();
            return false;
        }
        for ( uint32 quality = 0; quality < 4; ++quality )
        {
            dtObstacleAvoidanceParams avoidance = *_pCrowd->getObstacleAvoidanceParams( 0 );
            avoidance.velBias                   = 0.5f;
            avoidance.adaptiveDivs              = Internal::kArrAdaptiveDivision[quality];
            avoidance.adaptiveRings             = Internal::kArrAdaptiveRing[quality];
            avoidance.adaptiveDepth             = Internal::kArrAdaptiveDepth[quality];
            _pCrowd->setObstacleAvoidanceParams( static_cast<int32>( quality ), &avoidance );
        }
        _maxAgentCount = maxAgentCount;
        _listFailed.assign( maxAgentCount, SW_FALSE );
        _listTarget.assign( maxAgentCount, float3{} );
        return true;
    }

    void DetourNavCrowd::shutdown()
    {
        dtFreeCrowd( _pCrowd );
        dtFreeNavMeshQuery( _pQuery );
        _pCrowd = nullptr;
        _pQuery = nullptr;
        _listFailed.clear();
        _listTarget.clear();
        _maxAgentCount = 0;
    }

    bool DetourNavCrowd::isActiveAgent( NavCrowdAgentId agentId ) const
    {
        if ( _pCrowd == nullptr || agentId < 0 || static_cast<uint32>( agentId ) >= _maxAgentCount )
            return false;
        const dtCrowdAgent* pAgent = _pCrowd->getAgent( agentId );
        return pAgent != nullptr && pAgent->active;
    }

    NavCrowdAgentId DetourNavCrowd::addAgent( const float3& position, const NavCrowdAgentParams& params )
    {
        if ( _pCrowd == nullptr )
            return NavigationConstant::kInvalidAgentId;
        dtCrowdAgentParams detourParams{};
        DetourNavCrowdInternal::toDetourParams( params, detourParams );
        const int32 agentIndex = _pCrowd->addAgent( &position._x, &detourParams );
        if ( agentIndex < 0 )
            return NavigationConstant::kInvalidAgentId;
        _listFailed[static_cast<size_t>( agentIndex )] = SW_FALSE;
        _listTarget[static_cast<size_t>( agentIndex )] = position;
        return agentIndex;
    }

    void DetourNavCrowd::removeAgent( NavCrowdAgentId agentId )
    {
        if ( isActiveAgent( agentId ) )
            _pCrowd->removeAgent( agentId );
    }

    void DetourNavCrowd::updateAgentParams( NavCrowdAgentId agentId, const NavCrowdAgentParams& params )
    {
        if ( isActiveAgent( agentId ) == false )
            return;
        dtCrowdAgentParams detourParams{};
        DetourNavCrowdInternal::toDetourParams( params, detourParams );
        _pCrowd->updateAgentParameters( agentId, &detourParams );
    }

    bool DetourNavCrowd::requestMoveTarget( NavCrowdAgentId agentId, const float3& target )
    {
        if ( isActiveAgent( agentId ) == false )
            return false;
        _listTarget[static_cast<size_t>( agentId )] = target;
        dtPolyRef targetRef                         = 0;
        float32   arrNearest[3]{};
        (void)_pQuery->findNearestPoly( &target._x, _pCrowd->getQueryHalfExtents(), _pCrowd->getFilter( 0 ), &targetRef, arrNearest );
        if ( targetRef == 0 )
        {
            _pCrowd->resetMoveTarget( agentId );
            _listFailed[static_cast<size_t>( agentId )] = SW_TRUE;
            return false;
        }
        _listFailed[static_cast<size_t>( agentId )] = SW_FALSE;
        return _pCrowd->requestMoveTarget( agentId, targetRef, arrNearest );
    }

    void DetourNavCrowd::resetMoveTarget( NavCrowdAgentId agentId )
    {
        if ( isActiveAgent( agentId ) == false )
            return;
        _pCrowd->resetMoveTarget( agentId );
        _listFailed[static_cast<size_t>( agentId )] = SW_FALSE;
    }

    void DetourNavCrowd::teleportAgent( NavCrowdAgentId agentId, const float3& position )
    {
        if ( isActiveAgent( agentId ) == false )
            return;
        // Detour 의 addAgent 가 하는 것과 같은 초기화 — 통로를 새 자리에서 다시 시작한다.
        dtCrowdAgent* pAgent  = _pCrowd->getEditableAgent( agentId );
        dtPolyRef     polyRef = 0;
        float32       arrNearest[3]{ position._x, position._y, position._z };
        (void)_pQuery->findNearestPoly( &position._x, _pCrowd->getQueryHalfExtents(), _pCrowd->getFilter( 0 ), &polyRef, arrNearest );
        pAgent->corridor.reset( polyRef, arrNearest );
        pAgent->boundary.reset();
        pAgent->partial          = false;
        pAgent->topologyOptTime  = 0.0f;
        pAgent->targetReplanTime = 0.0f;
        pAgent->nneis            = 0;
        pAgent->ncorners         = 0;
        pAgent->desiredSpeed     = 0.0f;
        dtVset( pAgent->dvel, 0.0f, 0.0f, 0.0f );
        dtVset( pAgent->nvel, 0.0f, 0.0f, 0.0f );
        dtVset( pAgent->vel, 0.0f, 0.0f, 0.0f );
        dtVcopy( pAgent->npos, arrNearest );
        pAgent->state = polyRef != 0 ? DT_CROWDAGENT_STATE_WALKING : DT_CROWDAGENT_STATE_INVALID;
        _pCrowd->resetMoveTarget( agentId );
        _listFailed[static_cast<size_t>( agentId )] = SW_FALSE;
    }

    void DetourNavCrowd::syncAgentPosition( NavCrowdAgentId agentId, const float3& position )
    {
        if ( isActiveAgent( agentId ) == false )
            return;
        dtCrowdAgent* pAgent = _pCrowd->getEditableAgent( agentId );
        if ( pAgent->state != DT_CROWDAGENT_STATE_WALKING || pAgent->corridor.getFirstPoly() == 0 )
        {
            teleportAgent( agentId, position );
            return;
        }
        // 통로를 따라 새 자리로 끌어 옮긴다(벽을 넘지 않는다) — 경로는 지킨다.
        if ( pAgent->corridor.movePosition( &position._x, _pQuery, _pCrowd->getFilter( 0 ) ) )
            dtVcopy( pAgent->npos, pAgent->corridor.getPos() );
    }

    void DetourNavCrowd::update( float32 deltaTime )
    {
        if ( _pCrowd == nullptr || deltaTime <= 0.0f )
            return;
        _pCrowd->update( deltaTime, nullptr );
    }

    bool DetourNavCrowd::findAgentState( NavCrowdAgentId agentId, NavCrowdAgentState& outState ) const
    {
        outState = NavCrowdAgentState{};
        if ( isActiveAgent( agentId ) == false )
            return false;
        using Internal             = DetourNavCrowdInternal;
        const dtCrowdAgent* pAgent = _pCrowd->getAgent( agentId );
        outState._position         = Internal::toFloat3( pAgent->npos );
        outState._velocity         = Internal::toFloat3( pAgent->vel );
        outState._desiredVelocity  = Internal::toFloat3( pAgent->dvel );
        outState._target           = _listTarget[static_cast<size_t>( agentId )];
        outState._bOnNavMesh       = pAgent->state == DT_CROWDAGENT_STATE_WALKING;
        outState._nextCorner       = pAgent->ncorners > 0 ? Internal::toFloat3( pAgent->cornerVerts ) : outState._position;
        if ( _listFailed[static_cast<size_t>( agentId )] == SW_TRUE )
        {
            outState._moveState = NavCrowdMoveState::Failed;
            return true;
        }
        switch ( pAgent->targetState )
        {
            case DT_CROWDAGENT_TARGET_NONE:
            case DT_CROWDAGENT_TARGET_VELOCITY:
            {
                outState._moveState = NavCrowdMoveState::Idle;
                break;
            }
            case DT_CROWDAGENT_TARGET_FAILED:
            {
                outState._moveState = NavCrowdMoveState::Failed;
                break;
            }
            case DT_CROWDAGENT_TARGET_VALID:
            {
                // 마지막 모퉁이가 끝점이고 몸 반지름의 4 분의 1 안이면 닿았다(Detour 는 끝점 앞에서 감속해 멈춘다).
                const bool bLastIsEnd = pAgent->ncorners > 0 && ( pAgent->cornerFlags[pAgent->ncorners - 1] & DT_STRAIGHTPATH_END ) != 0;
                float32    remaining  = MathUtil::kMaxFloat;
                if ( bLastIsEnd )
                    remaining = dtVdist2D( pAgent->npos, &pAgent->cornerVerts[( pAgent->ncorners - 1 ) * 3] );
                const bool bArrived = pAgent->ncorners == 0 || ( bLastIsEnd && pAgent->ncorners == 1 && remaining <= pAgent->params.radius * 0.25f + 0.01f );
                outState._moveState = bArrived ? NavCrowdMoveState::Arrived : NavCrowdMoveState::Moving;
                break;
            }
            default:
            {
                outState._moveState = NavCrowdMoveState::Pending;
                break;
            }
        }
        return true;
    }

    void DetourNavCrowd::setQueryFilter( const NavQueryFilter& filter )
    {
        if ( _pCrowd == nullptr )
            return;
        dtQueryFilter* pFilter = _pCrowd->getEditableFilter( 0 );
        if ( pFilter != nullptr )
            RecastNavMesh::applyQueryFilter( filter, *pFilter );
    }

    uint32 DetourNavCrowd::getActiveAgentCount() const
    {
        uint32 count = 0;
        for ( uint32 agentIndex = 0; agentIndex < _maxAgentCount; ++agentIndex )
        {
            count += isActiveAgent( static_cast<NavCrowdAgentId>( agentIndex ) ) ? 1u : 0u;
        }
        return count;
    }

    void DetourNavCrowd::drawDebug( IPhysicsDebugRenderer& renderer, bool bPath, bool bVelocity ) const
    {
        using Internal = DetourNavCrowdInternal;
        const float3 lift{ 0.0f, 0.15f, 0.0f };
        const float4 pathColor{ 1.0f, 0.8f, 0.1f, 1.0f };
        const float4 velocityColor{ 0.1f, 1.0f, 0.3f, 1.0f };
        const float4 desiredColor{ 0.2f, 0.5f, 1.0f, 1.0f };
        for ( uint32 agentIndex = 0; agentIndex < _maxAgentCount; ++agentIndex )
        {
            if ( isActiveAgent( static_cast<NavCrowdAgentId>( agentIndex ) ) == false )
                continue;
            const dtCrowdAgent* pAgent   = _pCrowd->getAgent( static_cast<int32>( agentIndex ) );
            const float3        position = Internal::toFloat3( pAgent->npos ) + lift;
            if ( bPath )
            {
                float3 from = position;
                for ( int32 cornerIndex = 0; cornerIndex < pAgent->ncorners; ++cornerIndex )
                {
                    const float3 corner = Internal::toFloat3( &pAgent->cornerVerts[cornerIndex * 3] ) + lift;
                    renderer.drawLine( from, corner, pathColor );
                    from = corner;
                }
            }
            if ( bVelocity )
            {
                renderer.drawLine( position, position + Internal::toFloat3( pAgent->vel ), velocityColor );
                renderer.drawLine( position + lift, position + lift + Internal::toFloat3( pAgent->dvel ), desiredColor );
            }
        }
    }
} // namespace sw
