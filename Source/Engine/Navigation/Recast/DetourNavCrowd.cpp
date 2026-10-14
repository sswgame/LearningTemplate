#include "pch.h"

#include "Engine/Navigation/Recast/DetourNavCrowd.h"

#include "Core/Common/Defines.h"
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
            /** @brief 군중 밖 질의 객체의 노드 수입니다(목적지 붙이기와 위치 맞추기는 짧습니다). */
            static constexpr int32 kQueryNodeCount = 512;
            /** @brief 회피 품질 넷의 적응 샘플(분할 수, 고리 수, 깊이)입니다. Detour 예제의 Low, Medium, Good, High 값입니다. */
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
        , _pNavMesh{ nullptr }
        , _listFailed{}
        , _listTarget{}
        , _listLink{}
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
        _pCrowd                     = dtAllocCrowd();
        _pQuery                     = dtAllocNavMeshQuery();
        const bool bCrowdInitFailed = _pCrowd == nullptr || _pQuery == nullptr || _pCrowd->init( static_cast<int32>( maxAgentCount ), maxAgentRadius, pNavMesh ) == false ||
                                      dtStatusFailed( _pQuery->init( pNavMesh, Internal::kQueryNodeCount ) );
        if ( bCrowdInitFailed )
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
        _pNavMesh      = pNavMesh;
        _listFailed.assign( maxAgentCount, SW_FALSE );
        _listTarget.assign( maxAgentCount, float3{} );
        _listLink.assign( maxAgentCount, LinkTraversal{} );
        return true;
    }

    void DetourNavCrowd::shutdown()
    {
        dtFreeCrowd( _pCrowd );
        dtFreeNavMeshQuery( _pQuery );
        _pCrowd   = nullptr;
        _pQuery   = nullptr;
        _pNavMesh = nullptr;
        _listFailed.clear();
        _listTarget.clear();
        _listLink.clear();
        _maxAgentCount = 0;
    }

    bool DetourNavCrowd::isActiveAgent( NavCrowdAgentID agentID ) const
    {
        const bool bInvalidAgentID = _pCrowd == nullptr || agentID < 0 || static_cast<uint32>( agentID ) >= _maxAgentCount;
        if ( bInvalidAgentID )
            return false;
        const dtCrowdAgent* pAgent = _pCrowd->getAgent( agentID );
        return pAgent != nullptr && pAgent->active;
    }

    NavCrowdAgentID DetourNavCrowd::addAgent( const float3& position, const NavCrowdAgentParams& params )
    {
        if ( _pCrowd == nullptr )
            return NavigationConstant::kInvalidAgentID;
        dtCrowdAgentParams detourParams{};
        DetourNavCrowdInternal::toDetourParams( params, detourParams );
        const int32 agentIndex = _pCrowd->addAgent( &position._x, &detourParams );
        if ( agentIndex < 0 )
            return NavigationConstant::kInvalidAgentID;
        _listFailed[static_cast<size_t>( agentIndex )] = SW_FALSE;
        _listTarget[static_cast<size_t>( agentIndex )] = position;
        _listLink[static_cast<size_t>( agentIndex )]   = LinkTraversal{};
        return agentIndex;
    }

    void DetourNavCrowd::removeAgent( NavCrowdAgentID agentID )
    {
        if ( isActiveAgent( agentID ) )
            _pCrowd->removeAgent( agentID );
    }

    void DetourNavCrowd::updateAgentParams( NavCrowdAgentID agentID, const NavCrowdAgentParams& params )
    {
        if ( isActiveAgent( agentID ) == false )
            return;
        dtCrowdAgentParams detourParams{};
        DetourNavCrowdInternal::toDetourParams( params, detourParams );
        _pCrowd->updateAgentParameters( agentID, &detourParams );
    }

    bool DetourNavCrowd::requestMoveTarget( NavCrowdAgentID agentID, const float3& target )
    {
        if ( isActiveAgent( agentID ) == false )
            return false;
        _listTarget[static_cast<size_t>( agentID )] = target;
        dtPolyRef targetRef                         = 0;
        float32   arrNearest[3]{};
        (void)_pQuery->findNearestPoly( &target._x, _pCrowd->getQueryHalfExtents(), _pCrowd->getFilter( 0 ), &targetRef, arrNearest );
        if ( targetRef == 0 )
        {
            _pCrowd->resetMoveTarget( agentID );
            _listFailed[static_cast<size_t>( agentID )] = SW_TRUE;
            return false;
        }
        _listFailed[static_cast<size_t>( agentID )] = SW_FALSE;
        return _pCrowd->requestMoveTarget( agentID, targetRef, arrNearest );
    }

    void DetourNavCrowd::resetMoveTarget( NavCrowdAgentID agentID )
    {
        if ( isActiveAgent( agentID ) == false )
            return;
        _pCrowd->resetMoveTarget( agentID );
        _listFailed[static_cast<size_t>( agentID )] = SW_FALSE;
    }

    void DetourNavCrowd::teleportAgent( NavCrowdAgentID agentID, const float3& position )
    {
        if ( isActiveAgent( agentID ) == false )
            return;
        // Detour 의 addAgent 가 하는 것과 같은 초기화다. 통로를 새 위치에서 다시 시작한다.
        dtCrowdAgent* pAgent  = _pCrowd->getEditableAgent( agentID );
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
        _pCrowd->resetMoveTarget( agentID );
        _listFailed[static_cast<size_t>( agentID )] = SW_FALSE;
        _listLink[static_cast<size_t>( agentID )]   = LinkTraversal{};
    }

    void DetourNavCrowd::syncAgentPosition( NavCrowdAgentID agentID, const float3& position )
    {
        if ( isActiveAgent( agentID ) == false )
            return;
        dtCrowdAgent* pAgent = _pCrowd->getEditableAgent( agentID );
        // 링크를 건너는 동안 위치는 군중이 정한다. 바깥(컨트롤러 · 폰)은 아직 출발점에 있으므로 받아들이면 순간이동이 되어 건너기가 끊긴다.
        if ( pAgent->state == DT_CROWDAGENT_STATE_OFFMESH )
            return;
        if ( pAgent->state != DT_CROWDAGENT_STATE_WALKING || pAgent->corridor.getFirstPoly() == 0 )
        {
            teleportAgent( agentID, position );
            return;
        }
        // 통로를 따라 새 위치로 끌어 옮긴다(벽을 넘지 않는다). 경로는 유지한다.
        if ( pAgent->corridor.movePosition( &position._x, _pQuery, _pCrowd->getFilter( 0 ) ) )
            dtVcopy( pAgent->npos, pAgent->corridor.getPos() );
    }

    void DetourNavCrowd::update( float32 deltaTime )
    {
        using Internal = DetourNavCrowdInternal;
        if ( _pCrowd == nullptr || deltaTime <= 0.0f )
            return;
        // 갱신이 에이전트를 링크 위로 옮기면 모퉁이를 지운다 — 어느 링크인지는 갱신 전의 마지막 모퉁이로만 알 수 있다.
        for ( uint32 agentIndex = 0; agentIndex < _maxAgentCount; ++agentIndex )
        {
            if ( isActiveAgent( static_cast<NavCrowdAgentID>( agentIndex ) ) == false )
                continue;
            const dtCrowdAgent* pAgent      = _pCrowd->getAgent( static_cast<int32>( agentIndex ) );
            LinkTraversal&      traversal   = _listLink[agentIndex];
            const bool          bLastIsLink = pAgent->ncorners > 0 && ( pAgent->cornerFlags[pAgent->ncorners - 1] & DT_STRAIGHTPATH_OFFMESH_CONNECTION ) != 0;
            traversal._previousPosition     = Internal::toFloat3( pAgent->npos );
            traversal._pendingPoly          = bLastIsLink ? static_cast<uint32>( pAgent->cornerPolys[pAgent->ncorners - 1] ) : 0u;
        }
        _pCrowd->update( deltaTime, nullptr );
        for ( uint32 agentIndex = 0; agentIndex < _maxAgentCount; ++agentIndex )
        {
            if ( isActiveAgent( static_cast<NavCrowdAgentID>( agentIndex ) ) == false )
                continue;
            const dtCrowdAgent* pAgent    = _pCrowd->getAgent( static_cast<int32>( agentIndex ) );
            LinkTraversal&      traversal = _listLink[agentIndex];
            if ( pAgent->state != DT_CROWDAGENT_STATE_OFFMESH )
            {
                traversal._linkPoly = 0;
                traversal._linkID   = 0;
                continue;
            }
            if ( traversal._linkPoly == 0 )
                beginLinkTraversal( traversal );
            traversal._velocity = ( Internal::toFloat3( pAgent->npos ) - traversal._previousPosition ) * ( 1.0f / deltaTime );
        }
    }

    void DetourNavCrowd::beginLinkTraversal( LinkTraversal& inoutTraversal ) const
    {
        const dtOffMeshConnection* pLink = inoutTraversal._pendingPoly != 0 ? _pNavMesh->getOffMeshConnectionByRef( inoutTraversal._pendingPoly ) : nullptr;
        if ( pLink == nullptr )
        {
            // 한 갱신 안에서 모퉁이를 찾고 바로 올랐다 — 링크는 모르지만 건너는 중이라는 것은 알린다.
            inoutTraversal._linkPoly = invalid_index::kUint32;
            inoutTraversal._linkID   = 0;
            inoutTraversal._start    = inoutTraversal._previousPosition;
            inoutTraversal._end      = inoutTraversal._previousPosition;
            return;
        }
        // 양방향 링크는 거꾸로 건널 수 있다 — 오르기 직전 위치에 가까운 끝이 오른 끝이다.
        const float3 first    = DetourNavCrowdInternal::toFloat3( &pLink->pos[0] );
        const float3 second   = DetourNavCrowdInternal::toFloat3( &pLink->pos[3] );
        const bool   bForward = float3::getHorizontalDistance( first, inoutTraversal._previousPosition ) <=
                              float3::getHorizontalDistance( second, inoutTraversal._previousPosition );
        inoutTraversal._linkPoly = inoutTraversal._pendingPoly;
        inoutTraversal._linkID   = pLink->userId;
        inoutTraversal._start    = bForward ? first : second;
        inoutTraversal._end      = bForward ? second : first;
    }

    bool DetourNavCrowd::findAgentState( NavCrowdAgentID agentID, NavCrowdAgentState& outState ) const
    {
        outState = NavCrowdAgentState{};
        if ( isActiveAgent( agentID ) == false )
            return false;
        using Internal             = DetourNavCrowdInternal;
        const dtCrowdAgent* pAgent = _pCrowd->getAgent( agentID );
        outState._position         = Internal::toFloat3( pAgent->npos );
        outState._velocity         = Internal::toFloat3( pAgent->vel );
        outState._desiredVelocity  = Internal::toFloat3( pAgent->dvel );
        outState._target           = _listTarget[static_cast<size_t>( agentID )];
        outState._bOnNavMesh       = pAgent->state == DT_CROWDAGENT_STATE_WALKING;
        outState._nextCorner       = pAgent->ncorners > 0 ? Internal::toFloat3( pAgent->cornerVerts ) : outState._position;
        if ( _listFailed[static_cast<size_t>( agentID )] == SW_TRUE )
        {
            outState._moveState = NavCrowdMoveState::Failed;
            return true;
        }
        // 링크를 건너는 동안 Detour 는 모퉁이를 비운다 — 모퉁이 수로 도착을 판정하면 링크 위에서 도착으로 읽힌다.
        if ( pAgent->state == DT_CROWDAGENT_STATE_OFFMESH )
        {
            const LinkTraversal& traversal = _listLink[static_cast<size_t>( agentID )];
            const float3         span      = traversal._end - traversal._start;
            const float32        spanSq    = span._x * span._x + span._z * span._z;
            const float3         along     = outState._position - traversal._start;
            const float32        progress  = spanSq > 0.0f ? ( along._x * span._x + along._z * span._z ) / spanSq : 1.0f;
            outState._bOnOffMeshLink       = true;
            outState._velocity             = traversal._velocity;
            outState._nextCorner           = traversal._end;
            outState._link._start          = traversal._start;
            outState._link._end            = traversal._end;
            outState._link._linkID         = traversal._linkID;
            outState._link._progress       = MathUtil::clamp( progress, 0.0f, 1.0f );
            outState._moveState            = NavCrowdMoveState::Moving;
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
                // 마지막 모퉁이가 엔드포인트이고 몸 반지름의 4 분의 1 안이면 닿았다(Detour 는 엔드포인트 앞에서 감속해 멈춘다).
                const bool bLastIsEnd = pAgent->ncorners > 0 && ( pAgent->cornerFlags[pAgent->ncorners - 1] & DT_STRAIGHTPATH_END ) != 0;
                float32    remaining  = MathUtil::kMaxFloat;
                if ( bLastIsEnd )
                    remaining = dtVdist2D( pAgent->npos, &pAgent->cornerVerts[( pAgent->ncorners - 1 ) * 3] );
                // 모퉁이가 하나도 없는 프레임은 경로를 다시 찾기 전(링크 착지 직후 등)일 수 있다 — 목표가 몸 반지름 안일 때만 도착이다.
                const float32 targetDistance = dtVdist2D( pAgent->npos, pAgent->targetPos );
                const bool    bNoCorner      = pAgent->ncorners == 0 && targetDistance <= pAgent->params.radius + 0.01f;
                const bool    bArrived       = bNoCorner || ( bLastIsEnd && pAgent->ncorners == 1 && remaining <= pAgent->params.radius * 0.25f + 0.01f );
                outState._moveState          = bArrived ? NavCrowdMoveState::Arrived : NavCrowdMoveState::Moving;
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
            count += isActiveAgent( static_cast<NavCrowdAgentID>( agentIndex ) ) ? 1u : 0u;
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
            if ( isActiveAgent( static_cast<NavCrowdAgentID>( agentIndex ) ) == false )
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
