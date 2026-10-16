#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Gameplay/Match/TeamAttitude.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        bool isGroundUnitSimulation( const RTSUnit& unit )
        {
            return unit._pDef->_bAir == SW_FALSE;
        }
    } // namespace

    void RTSWorld::recomputeSupply()
    {
        for ( RTSPlayer& player : _listPlayer )
        {
            player._supplyUsed = 0;
            player._supplyCap  = 0;
        }
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner == kNoOwner )
                continue;
            RTSPlayer& player = _listPlayer[static_cast<size_t>( unit._owner )];
            if ( unit.isConstructed() )
                player._supplyCap += unit._pDef->_supplyProvided;
            player._supplyUsed += unit._pDef->_supplyCost + unit._supplyReserved;
        }
        for ( RTSPlayer& player : _listPlayer )
        {
            player._supplyCap = MathUtil::min( player._supplyCap, _pCatalog->getSupplyMax() );
        }
    }

    void RTSWorld::rebuildBuckets()
    {
        std::fill( _listBucketHead.begin(), _listBucketHead.end(), -1 );
        _listBucketNext.assign( _listUnit.size(), -1 );
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            const RTSUnit& unit = _listUnit[index];
            if ( unit._bAlive == SW_FALSE )
                continue;
            const int32 bucket                             = computeBucketIndex( unit._position );
            _listBucketNext[index]                         = _listBucketHead[static_cast<size_t>( bucket )];
            _listBucketHead[static_cast<size_t>( bucket )] = static_cast<int32>( index );
        }
    }

    // ------------------------------------------------------------------------------
    // 유닛 한 걸음
    // ------------------------------------------------------------------------------
    void RTSWorld::updateUnit( RTSUnit& unit, float32 deltaTime )
    {
        unit._cooldown.update( deltaTime );
        unit._repathTimer.update( deltaTime );
        if ( unit.isResource() )
            return;
        if ( unit.isBuilding() )
        {
            if ( unit.isConstructed() == false )
                return;
            updateProduction( unit, deltaTime );
            if ( unit._pDef->canAttack() )
            {
                if ( findUnit( unit._attackTarget ) == nullptr )
                    unit._attackTarget = findAutoTarget( unit, unit._pDef->_range );
                updateCombat( unit, deltaTime );
            }
            return;
        }
        updateOrder( unit, deltaTime );
        if ( unit._bAlive == SW_FALSE )
            return;
        updateCombat( unit, deltaTime );
        updateMovement( unit, deltaTime );
    }

    void RTSWorld::approach( RTSUnit& unit, const float3& target, bool bForce )
    {
        if ( isGroundUnitSimulation( unit ) == false )
        {
            const bool bShouldRepathAir = bForce || unit._agent.isMoving() == false || float3::getHorizontalDistance( unit._moveGoal, target ) > 0.25f;
            if ( bShouldRepathAir )
            {
                unit._moveGoal = target;
                unit._agent.followPath( vector<float3>{
                    float3{ target._x, 0.0f, target._z }
                } );
            }
            return;
        }
        const bool bGoalMoved = float3::getHorizontalDistance( unit._moveGoal, target ) > 1.0f;
        if ( bForce == false && unit._repathTimer.isActive() )
            return;
        const bool bPathStillValid = bForce == false && unit._agent.isMoving() && bGoalMoved == false;
        if ( bPathStillValid )
            return;
        unit._moveGoal = target;
        unit._repathTimer.start( _settings._repathInterval );
        if ( unit._agent.moveTo( _grid, _pathfinder, target ) == false )
            nudgeToWalkable( unit );
    }

    void RTSWorld::nudgeToWalkable( RTSUnit& unit )
    {
        const int2 cell = _grid.computeCell( unit._position );
        int2       freeCell{};
        if ( _grid.isWalkable( cell ) || _grid.findNearestWalkable( cell, 6, freeCell ) == false )
            return;
        unit._position = _grid.computeCellCenter( freeCell );
        unit._agent.setPosition( unit._position );
    }

    void RTSWorld::updateMovement( RTSUnit& unit, float32 deltaTime )
    {
        const bool bGround = isGroundUnitSimulation( unit );
        _listNeighborScratch.clear();
        // 채취하는 일꾼은 서로 밀지 않는다. 스타크래프트처럼 광물 앞에 모여도 된다.
        const RTSOrder* pOrder = unit.findOrder();
        if ( pOrder == nullptr || pOrder->_type != RTSOrderType::Gather )
        {
            queryUnits( unit._position, unit._pDef->_radius * 2.0f, _listQueryScratch );
            for ( const RTSUnitID otherID : _listQueryScratch )
            {
                const RTSUnit* pOther         = findUnit( otherID );
                const bool     bSkipOtherUnit = pOther == nullptr || pOther == &unit || pOther->isMobile() == false || isGroundUnitSimulation( *pOther ) != bGround;
                if ( bSkipOtherUnit )
                    continue;
                const RTSOrder* pOtherOrder = pOther->findOrder();
                if ( pOtherOrder != nullptr && pOtherOrder->_type == RTSOrderType::Gather )
                    continue;
                _listNeighborScratch.push_back( pOther->_position );
            }
        }
        unit._agent.update( bGround ? _grid : _airGrid, _listNeighborScratch, deltaTime );
        unit._position = unit._agent.getPosition();
    }

    void RTSWorld::updateOrder( RTSUnit& unit, float32 deltaTime )
    {
        const bool bAutoAcquire = unit._pDef->canAttack() && unit._pDef->_bWorker == SW_FALSE;
        if ( unit._listOrder.empty() )
        {
            if ( bAutoAcquire && findUnit( unit._attackTarget ) == nullptr )
                unit._attackTarget = findAutoTarget( unit, unit._pDef->_sight );
            return;
        }
        RTSOrder& order = unit._listOrder.front();
        switch ( order._type )
        {
            case RTSOrderType::Move:
            case RTSOrderType::AttackMove:
            {
                if ( order._type == RTSOrderType::AttackMove )
                {
                    if ( findUnit( unit._attackTarget ) == nullptr )
                        unit._attackTarget = findAutoTarget( unit, unit._pDef->_sight );
                    if ( unit._attackTarget.isValid() )
                    {
                        unit._bOrderStarted = SW_FALSE; // 싸운 뒤에는 경로를 다시 구한다
                        return;
                    }
                }
                if ( unit._bOrderStarted == SW_FALSE )
                {
                    unit._bOrderStarted = SW_TRUE;
                    if ( order._pFlowField != nullptr )
                    {
                        unit._moveGoal = order._target;
                        unit._agent.followFlowField( order._pFlowField, order._target );
                    }
                    else
                    {
                        approach( unit, order._target, true );
                    }
                    return;
                }
                const float32 distance = float3::getHorizontalDistance( unit._position, order._target );
                const bool    bClose   = order._arriveRadius > 0.0f && distance <= order._arriveRadius;
                if ( bClose || unit._agent.isMoving() == false )
                    finishOrder( unit );
                return;
            }
            case RTSOrderType::Attack:
            {
                if ( findUnit( order._targetUnit ) == nullptr )
                {
                    finishOrder( unit );
                    return;
                }
                unit._attackTarget = order._targetUnit;
                return;
            }
            case RTSOrderType::Hold:
            {
                if ( unit._agent.isMoving() )
                    unit._agent.stop();
                const RTSUnit* pTarget = findUnit( unit._attackTarget );
                if ( unit._pDef->canAttack() && ( pTarget == nullptr || computeEdgeDistance( unit, *pTarget ) > unit._pDef->_range ) )
                    unit._attackTarget = findAutoTarget( unit, unit._pDef->_range );
                return;
            }
            case RTSOrderType::Gather:
            {
                updateGather( unit, order, deltaTime );
                return;
            }
            case RTSOrderType::Build:
            {
                updateBuild( unit, order, deltaTime );
                return;
            }
        }
    }

    void RTSWorld::updateCombat( RTSUnit& unit, float32 deltaTime )
    {
        (void)deltaTime;
        if ( unit._attackTarget.isValid() == false )
            return;
        RTSUnit* pTarget = findUnitMutable( unit._attackTarget );
        if ( pTarget == nullptr || canAttack( unit, *pTarget ) == false )
        {
            unit._attackTarget = RTSUnitID{};
            if ( unit.isMobile() && unit._listOrder.empty() )
                unit._agent.stop();
            return;
        }
        const float32   distance = computeEdgeDistance( unit, *pTarget );
        const RTSOrder* pOrder   = unit.findOrder();
        const bool      bHold    = pOrder != nullptr && pOrder->_type == RTSOrderType::Hold;
        if ( distance <= unit._pDef->_range )
        {
            if ( unit.isMobile() && unit._agent.isMoving() )
                unit._agent.stop();
            if ( unit._cooldown.isActive() == false )
            {
                // 늦은 만큼을 이어받아 공격 빈도가 고정 걸음 격자에 맞춰 내려가지 않게 한다. 1.2초 쿨다운이 25걸음(1.25초)이 되면 안 된다.
                unit._cooldown.restart( unit._pDef->_cooldown );
                dealDamage( unit, *pTarget );
            }
            return;
        }
        const bool bOrdered    = pOrder != nullptr && pOrder->_type == RTSOrderType::Attack;
        const bool bLostTarget = unit.isMobile() == false || bHold || ( bOrdered == false && distance > unit._pDef->_sight * 1.5f );
        if ( bLostTarget )
        {
            unit._attackTarget = RTSUnitID{}; // 목표를 놓쳤다. 자동으로 고른 목표에만 해당하며, 명령한 목표는 끝까지 쫓는다
            return;
        }
        approach( unit, pTarget->_position, false );
    }

    void RTSWorld::dealDamage( RTSUnit& attacker, RTSUnit& target )
    {
        const float32 damage = MathUtil::max( _settings._minimumDamage, attacker._pDef->_damage - target._pDef->_armor );
        target._hp -= damage;
        if ( target._owner != kNoOwner )
        {
            RTSPlayer& owner = _listPlayer[static_cast<size_t>( target._owner )];
            if ( _time >= owner._nextUnderAttackTime )
            {
                owner._nextUnderAttackTime = _time + _settings._underAttackCooldown;
                pushEvent( RTSEvent::Kind::UnderAttack, target._owner, target._id, target._pDef->_id, 0, attacker._id );
            }
        }
        // 할 일이 없을 때 맞으면 되받아친다.
        const bool bShouldRetaliate = target.isMobile() && target._listOrder.empty() && target._attackTarget.isValid() == false && canAttack( target, attacker );
        if ( bShouldRetaliate )
            target._attackTarget = attacker._id;
        if ( target._hp <= 0.0f )
            killUnit( target, attacker._id );
    }

    void RTSWorld::killUnit( RTSUnit& unit, RTSUnitID killerID )
    {
        if ( unit._bAlive == SW_FALSE )
            return;
        unit._hp = 0.0f;
        for ( RTSOrder& order : unit._listOrder )
        {
            releaseOrder( unit, order );
        }
        unit._listOrder.clear();
        unit._agent.stop();
        if ( unit.isMobile() == false && unit._pDef->_bExtractor == SW_FALSE )
            placeFootprint( unit, false ); // 정제소는 해당하지 않는다. 간헐천이 그 셀을 계속 막고 있다
        if ( unit._pDef->_kind == RTSUnitKind::Building )
            _land.releaseRect( unit._cell._x, unit._cell._y, unit._cell._x + unit._pDef->_footprint - 1, unit._cell._y + unit._pDef->_footprint - 1 );
        // 부서진 건물의 생산 대기열 비용은 돌려주지 않는다(스타크래프트와 같다).
        unit._bAlive = SW_FALSE;
        pushEvent( RTSEvent::Kind::UnitDied, unit._owner, unit._id, unit._pDef->_id, 0, killerID );
    }

    // ------------------------------------------------------------------------------
    // 시야와 승패
    // ------------------------------------------------------------------------------
    void RTSWorld::updateVision()
    {
        for ( vector<uint8>& listVisibility : _listTeamVisibility )
        {
            for ( uint8& visibility : listVisibility )
            {
                if ( visibility == static_cast<uint8>( RTSVisibility::Visible ) )
                    visibility = static_cast<uint8>( RTSVisibility::Explored );
            }
        }
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner == kNoOwner )
                continue;
            vector<uint8>& listVisibility = _listTeamVisibility[static_cast<size_t>( _listPlayer[static_cast<size_t>( unit._owner )]._team )];
            const float32  sight          = unit._pDef->_sight + ( unit.isMobile() ? 0.0f : static_cast<float32>( unit._pDef->_footprint ) * 0.5f );
            const int2     center         = _grid.computeCell( unit._position );
            const int32    radius         = static_cast<int32>( sight );
            const float32  sightSquared   = sight * sight;
            for ( int32 y = MathUtil::max( 0, center._y - radius ); y <= MathUtil::min( _grid.getHeight() - 1, center._y + radius ); ++y )
            {
                for ( int32 x = MathUtil::max( 0, center._x - radius ); x <= MathUtil::min( _grid.getWidth() - 1, center._x + radius ); ++x )
                {
                    const float32 dx = static_cast<float32>( x - center._x );
                    const float32 dy = static_cast<float32>( y - center._y );
                    if ( dx * dx + dy * dy <= sightSquared )
                        listVisibility[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] = static_cast<uint8>( RTSVisibility::Visible );
                }
            }
        }
    }

    void RTSWorld::updateDefeat()
    {
        if ( _winningTeam >= 0 )
            return;
        vector<int32> listBuildingCount( _listPlayer.size(), 0 );
        for ( const RTSUnit& unit : _listUnit )
        {
            const bool bCountsAsBuilding = unit._bAlive && unit._owner != kNoOwner && unit.isBuilding();
            if ( bCountsAsBuilding )
                ++listBuildingCount[static_cast<size_t>( unit._owner )];
        }
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            RTSPlayer& player            = _listPlayer[index];
            const bool bNotNewlyDefeated = player._bDefeated || player._bHadBuilding == SW_FALSE || listBuildingCount[index] > 0;
            if ( bNotNewlyDefeated )
                continue;
            player._bDefeated = SW_TRUE;
            pushEvent( RTSEvent::Kind::PlayerDefeated, static_cast<int32>( index ), RTSUnitID{}, hashed_string{} );
        }
        int32 aliveTeam  = -1;
        int32 aliveCount = 0;
        for ( int32 team = 0; team < _teamCount; ++team )
        {
            for ( const RTSPlayer& player : _listPlayer )
            {
                if ( player._team == team && player._bDefeated == SW_FALSE )
                {
                    aliveTeam = team;
                    ++aliveCount;
                    break;
                }
            }
        }
        if ( _teamCount > 1 && aliveCount == 1 )
        {
            _winningTeam = aliveTeam;
            pushEvent( RTSEvent::Kind::GameOver, aliveTeam, RTSUnitID{}, hashed_string{} );
        }
    }
} // namespace sw
