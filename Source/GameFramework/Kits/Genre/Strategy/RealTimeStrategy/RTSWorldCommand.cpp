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
        bool isGroundUnitCommand( const RTSUnit& unit )
        {
            return unit._pDef->_bAir == SW_FALSE;
        }
    } // namespace

    // ------------------------------------------------------------------------------
    // 명령
    // ------------------------------------------------------------------------------
    RTSCommandResult RTSWorld::pushOrder( RTSUnit& unit, const RTSOrder& order, bool bQueue )
    {
        if ( bQueue == false )
            clearOrders( unit );
        unit._listOrder.push_back( order );
        if ( unit._listOrder.size() == 1 )
            beginOrder( unit );
        return RTSCommandResult::Ok;
    }

    void RTSWorld::beginOrder( RTSUnit& unit )
    {
        unit._bOrderStarted = SW_FALSE;
        unit._attackTarget  = RTSUnitID{};
        unit._repathTimer.clear();
        if ( unit._listOrder.empty() )
        {
            unit._agent.stop();
            return;
        }
        if ( unit._listOrder.front()._type == RTSOrderType::Gather )
            unit._gatherPhase = unit._cargoAmount > 0 ? RTSGatherPhase::Returning : RTSGatherPhase::ToResource;
    }

    void RTSWorld::releaseOrder( RTSUnit& unit, RTSOrder& order )
    {
        if ( order._pFlowField != nullptr )
        {
            releaseFlowField( order._pFlowField );
            order._pFlowField = nullptr;
        }
        if ( order._type == RTSOrderType::Gather || order._type == RTSOrderType::Build )
        {
            RTSUnit* pTarget = findUnitMutable( order._targetUnit );
            if ( pTarget != nullptr && pTarget->_harvester == unit._id )
                pTarget->_harvester = RTSUnitID{};
            if ( pTarget != nullptr && pTarget->_builder == unit._id )
                pTarget->_builder = RTSUnitID{};
        }
    }

    void RTSWorld::finishOrder( RTSUnit& unit )
    {
        if ( unit._listOrder.empty() )
            return;
        releaseOrder( unit, unit._listOrder.front() );
        unit._listOrder.pop_front();
        beginOrder( unit );
    }

    void RTSWorld::clearOrders( RTSUnit& unit )
    {
        for ( RTSOrder& order : unit._listOrder )
        {
            releaseOrder( unit, order );
        }
        unit._listOrder.clear();
        beginOrder( unit );
    }

    RTSCommandResult RTSWorld::issueMove( RTSUnitID unitID, const float3& target, bool bQueue )
    {
        RTSUnit* pUnit = findUnitMutable( unitID );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type   = RTSOrderType::Move;
        order._target = target;
        return pushOrder( *pUnit, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueAttackMove( RTSUnitID unitID, const float3& target, bool bQueue )
    {
        RTSUnit* pUnit = findUnitMutable( unitID );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type   = pUnit->_pDef->canAttack() ? RTSOrderType::AttackMove : RTSOrderType::Move;
        order._target = target;
        return pushOrder( *pUnit, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueAttack( RTSUnitID unitID, RTSUnitID targetID, bool bQueue )
    {
        RTSUnit*       pUnit   = findUnitMutable( unitID );
        const RTSUnit* pTarget = findUnit( targetID );
        if ( pUnit == nullptr || pTarget == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false || canAttack( *pUnit, *pTarget ) == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type       = RTSOrderType::Attack;
        order._targetUnit = targetID;
        order._target     = pTarget->_position;
        return pushOrder( *pUnit, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueGather( RTSUnitID unitID, RTSUnitID assetID, bool bQueue )
    {
        RTSUnit*       pUnit   = findUnitMutable( unitID );
        const RTSUnit* pTarget = findUnit( assetID );
        if ( pUnit == nullptr || pTarget == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->_pDef->_bWorker == SW_FALSE )
            return RTSCommandResult::CannotDo;
        const bool bMinerals  = pTarget->isResource() && pTarget->_pDef->_resourceType == RTSResourceType::Minerals;
        const bool bExtractor = pTarget->_pDef->_bExtractor && pTarget->_owner == pUnit->_owner && pTarget->isConstructed();
        if ( bMinerals == false && bExtractor == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type       = RTSOrderType::Gather;
        order._targetUnit = assetID;
        order._target     = pTarget->_position;
        return pushOrder( *pUnit, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueBuild( RTSUnitID workerID, const hashed_string& buildingID, const int2& cell, bool bQueue )
    {
        RTSUnit* pWorker = findUnitMutable( workerID );
        if ( pWorker == nullptr )
            return RTSCommandResult::InvalidUnit;
        const RTSUnitDef* pDef         = _pCatalog->find( buildingID );
        const bool        bCannotBuild = pDef == nullptr || pDef->_kind != RTSUnitKind::Building || pWorker->_pDef->_bWorker == SW_FALSE || pDef->_producedBy != pWorker->_pDef->_id;
        if ( bCannotBuild )
            return RTSCommandResult::CannotDo;
        const RTSCommandResult costResult = evaluateCost( pWorker->_owner, *pDef );
        if ( costResult != RTSCommandResult::Ok )
            return costResult;
        if ( canPlaceBuilding( buildingID, cell, workerID ) == false )
            return RTSCommandResult::InvalidPlacement;
        RTSOrder order;
        order._type      = RTSOrderType::Build;
        order._buildID   = buildingID;
        order._buildCell = cell;
        order._target    = computeFootprintCenter( cell, pDef->_footprint );
        return pushOrder( *pWorker, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueHold( RTSUnitID unitID )
    {
        RTSUnit* pUnit = findUnitMutable( unitID );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type   = RTSOrderType::Hold;
        order._target = pUnit->_position;
        return pushOrder( *pUnit, order, false );
    }

    RTSCommandResult RTSWorld::issueStop( RTSUnitID unitID )
    {
        RTSUnit* pUnit = findUnitMutable( unitID );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        clearOrders( *pUnit );
        return RTSCommandResult::Ok;
    }

    RTSCommandResult RTSWorld::issueSmart( RTSUnitID unitID, const float3& position, RTSUnitID targetID, bool bQueue )
    {
        RTSUnit* pUnit = findUnitMutable( unitID );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
        {
            setRallyPoint( unitID, position );
            return RTSCommandResult::Ok;
        }
        const RTSUnit* pTarget = findUnit( targetID );
        if ( pTarget != nullptr && pTarget->_id != unitID )
        {
            if ( areEnemies( pUnit->_owner, pTarget->_owner ) && canAttack( *pUnit, *pTarget ) )
                return issueAttack( unitID, targetID, bQueue );
            if ( pUnit->_pDef->_bWorker )
            {
                if ( issueGather( unitID, targetID, bQueue ) == RTSCommandResult::Ok )
                    return RTSCommandResult::Ok;
                const bool bShouldAssistBuild = pTarget->isBuilding() && pTarget->_owner == pUnit->_owner && pTarget->isConstructed() == false;
                if ( bShouldAssistBuild )
                {
                    RTSOrder order;
                    order._type       = RTSOrderType::Build;
                    order._buildID    = pTarget->_pDef->_id;
                    order._buildCell  = pTarget->_cell;
                    order._targetUnit = targetID;
                    order._target     = pTarget->_position;
                    return pushOrder( *pUnit, order, bQueue );
                }
            }
        }
        return issueMove( unitID, position, bQueue );
    }

    int32 RTSWorld::issueGroupMove( const vector<RTSUnitID>& listUnit, const float3& target, bool bAttackMove, bool bQueue )
    {
        int32 groundCount = 0;
        for ( const RTSUnitID unitID : listUnit )
        {
            const RTSUnit* pUnit         = findUnit( unitID );
            const bool     bGroundMobile = pUnit != nullptr && pUnit->isMobile() && isGroundUnitCommand( *pUnit );
            if ( bGroundMobile )
                ++groundCount;
        }
        const float32 arriveRadius = 0.5f + 0.5f * MathUtil::sqrt( static_cast<float32>( listUnit.size() ) );
        const bool    bUseField    = groundCount >= _settings._flowFieldGroupSize;
        const int2    goalCell     = _grid.computeCell( target );
        int32         issuedCount  = 0;
        for ( const RTSUnitID unitID : listUnit )
        {
            RTSUnit* pUnit = findUnitMutable( unitID );
            if ( pUnit == nullptr || pUnit->isMobile() == false )
                continue;
            RTSOrder order;
            order._type         = bAttackMove && pUnit->_pDef->canAttack() ? RTSOrderType::AttackMove : RTSOrderType::Move;
            order._target       = target;
            order._arriveRadius = listUnit.size() > 1 ? arriveRadius : 0.0f;
            if ( bUseField && isGroundUnitCommand( *pUnit ) )
                order._pFlowField = acquireFlowField( goalCell );
            (void)pushOrder( *pUnit, order, bQueue );
            ++issuedCount;
        }
        return issuedCount;
    }

    RTSCommandResult RTSWorld::train( RTSUnitID buildingID, const hashed_string& unitID )
    {
        RTSUnit* pBuilding = findUnitMutable( buildingID );
        if ( pBuilding == nullptr )
            return RTSCommandResult::InvalidUnit;
        const RTSUnitDef* pDef         = _pCatalog->find( unitID );
        const bool        bCannotTrain = pDef == nullptr || pDef->isMobile() == false || pBuilding->isBuilding() == false || pBuilding->isConstructed() == false ||
                                  pDef->_producedBy != pBuilding->_pDef->_id;
        if ( bCannotTrain )
            return RTSCommandResult::CannotDo;
        if ( static_cast<int32>( pBuilding->_listProduction.size() ) >= _settings._productionQueueMax )
            return RTSCommandResult::QueueFull;
        const RTSCommandResult costResult = evaluateCost( pBuilding->_owner, *pDef );
        if ( costResult != RTSCommandResult::Ok )
            return costResult;
        payCost( _listPlayer[static_cast<size_t>( pBuilding->_owner )], *pDef );
        pBuilding->_listProduction.push_back( unitID );
        return RTSCommandResult::Ok;
    }

    bool RTSWorld::cancelTrain( RTSUnitID buildingID )
    {
        RTSUnit* pBuilding = findUnitMutable( buildingID );
        if ( pBuilding == nullptr || pBuilding->_listProduction.empty() )
            return false;
        const RTSUnitDef* pDef = _pCatalog->find( pBuilding->_listProduction.back() );
        if ( pBuilding->_listProduction.size() == 1 )
        {
            pBuilding->_productionTimer = 0.0f;
            pBuilding->_supplyReserved  = 0;
            pBuilding->_bSupplyBlocked  = SW_FALSE;
        }
        pBuilding->_listProduction.pop_back();
        if ( pDef != nullptr )
            refundCost( _listPlayer[static_cast<size_t>( pBuilding->_owner )], *pDef );
        return true;
    }

    void RTSWorld::setRallyPoint( RTSUnitID buildingID, const float3& position )
    {
        RTSUnit* pBuilding = findUnitMutable( buildingID );
        if ( pBuilding == nullptr || pBuilding->isBuilding() == false )
            return;
        pBuilding->_rallyPoint = position;
        pBuilding->_bHasRally  = SW_TRUE;
    }
} // namespace sw
