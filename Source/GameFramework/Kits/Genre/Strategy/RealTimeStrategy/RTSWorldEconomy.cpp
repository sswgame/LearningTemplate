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
    SW_LOG_CALLER( "RTSWorld" );

    // ------------------------------------------------------------------------------
    // 채취
    // ------------------------------------------------------------------------------
    void RTSWorld::returnCargo( RTSUnit& unit )
    {
        const RTSUnit* pDepot = findUnit( findNearestDepot( unit._owner, unit._position ) );
        if ( pDepot == nullptr )
        {
            unit._agent.stop();
            return;
        }
        if ( isWithinReach( unit, *pDepot, _settings._interactSlack ) == false )
        {
            approach( unit, pDepot->_position, false );
            return;
        }
        unit._agent.stop();
        RTSPlayer& player = _listPlayer[static_cast<size_t>( unit._owner )];
        if ( player._pWallet != nullptr )
            player._pWallet->add( unit._cargoType == RTSResourceType::Gas ? _settings._gasCurrency : _settings._mineralCurrency, unit._cargoAmount );
        pushEvent( RTSEvent::Kind::ResourcesDeposited, unit._owner, unit._id, unit._pDef->_id, unit._cargoAmount );
        unit._cargoAmount = 0;
        unit._cargoType   = RTSResourceType::None;
        unit._gatherPhase = RTSGatherPhase::ToResource;
        unit._repathTimer.clear();
    }

    void RTSWorld::updateHarvest( RTSUnit& unit, RTSUnit& target, RTSUnit& source, float32 deltaTime )
    {
        unit._gatherTimer += deltaTime;
        if ( unit._gatherTimer < unit._pDef->_gatherTime )
            return;
        const int32 amount = MathUtil::min( unit._pDef->_cargo, source._resourceLeft );
        source._resourceLeft -= amount;
        unit._cargoAmount = amount;
        unit._cargoType   = source._pDef->_resourceType;
        unit._gatherPhase = RTSGatherPhase::Returning;
        unit._repathTimer.clear();
        target._harvester = RTSUnitID{};
        if ( source._resourceLeft > 0 )
            return;
        pushEvent( RTSEvent::Kind::ResourceDepleted, unit._owner, source._id, source._pDef->_id );
        const bool bExtractor = target._pDef->_bExtractor != SW_FALSE;
        if ( bExtractor == false )
            killUnit( source, RTSUnitID{} );
    }

    void RTSWorld::updateGather( RTSUnit& unit, RTSOrder& order, float32 deltaTime )
    {
        if ( unit._gatherPhase == RTSGatherPhase::Returning )
        {
            returnCargo( unit );
            return;
        }

        RTSUnit* pTarget = findUnitMutable( order._targetUnit );
        if ( pTarget == nullptr )
        {
            // 다 캔 광물이면 둘레의 다른 광물로 옮긴다. 정제소가 부서졌으면 끝낸다.
            const RTSUnitID nextID = findNearestResource( order._target, RTSResourceType::Minerals, _settings._resourceSearchRadius );
            if ( nextID.isValid() == false )
            {
                finishOrder( unit );
                return;
            }
            order._targetUnit = nextID;
            order._target     = findUnit( nextID )->_position;
            unit._gatherPhase = RTSGatherPhase::ToResource;
            return;
        }
        const bool bExtractor = pTarget->_pDef->_bExtractor != SW_FALSE;
        RTSUnit*   pSource    = bExtractor ? findUnitMutable( pTarget->_linkedResource ) : pTarget;
        if ( pSource == nullptr || pSource->_resourceLeft <= 0 )
        {
            finishOrder( unit );
            return;
        }

        if ( unit._gatherPhase == RTSGatherPhase::Harvesting )
        {
            updateHarvest( unit, *pTarget, *pSource, deltaTime );
            return;
        }

        if ( isWithinReach( unit, *pTarget, _settings._interactSlack ) == false )
        {
            unit._gatherPhase = RTSGatherPhase::ToResource;
            approach( unit, pTarget->_position, false );
            return;
        }
        unit._agent.stop();
        const RTSUnit* pHarvester = findUnit( pTarget->_harvester );
        if ( pHarvester != nullptr && pHarvester != &unit )
        {
            // 다른 일꾼이 캐는 중이면, 광물일 때 바로 옆의 빈 광물로 옮긴다.
            if ( bExtractor == false && unit._gatherPhase == RTSGatherPhase::ToResource )
            {
                queryUnits( unit._position, 2.0f, _listQueryScratch );
                for ( const RTSUnitID otherID : _listQueryScratch )
                {
                    const RTSUnit* pOther       = findUnit( otherID );
                    const bool     bFreeMineral = pOther != nullptr && pOther != pTarget && pOther->isResource() &&
                                              pOther->_pDef->_resourceType == RTSResourceType::Minerals && findUnit( pOther->_harvester ) == nullptr;
                    if ( bFreeMineral )
                    {
                        order._targetUnit = otherID;
                        order._target     = pOther->_position;
                        return;
                    }
                }
            }
            unit._gatherPhase = RTSGatherPhase::Waiting;
            return;
        }
        pTarget->_harvester = unit._id;
        unit._gatherPhase   = RTSGatherPhase::Harvesting;
        unit._gatherTimer   = 0.0f;
    }

    // ------------------------------------------------------------------------------
    // 건설
    // ------------------------------------------------------------------------------
    void RTSWorld::startConstruction( RTSUnit& worker, RTSOrder& order )
    {
        const RTSUnitDef* pDef          = _pCatalog->find( order._buildID );
        const bool        bBuildAborted = pDef == nullptr || evaluateCost( worker._owner, *pDef ) != RTSCommandResult::Ok || canPlaceBuilding( order._buildID, order._buildCell, worker._id ) == false;
        if ( bBuildAborted )
        {
            finishOrder( worker );
            return;
        }
        const RTSUnitID buildingID = spawnUnit( order._buildID, worker._owner, _grid.computeCellCenter( order._buildCell ) );
        RTSUnit*        pBuilding  = findUnitMutable( buildingID );
        if ( pBuilding == nullptr )
        {
            finishOrder( worker );
            return;
        }
        payCost( _listPlayer[static_cast<size_t>( worker._owner )], *pDef );
        pBuilding->_buildProgress = 0.0f;
        pBuilding->_hp            = pDef->_hp * _settings._constructionStartRatio;
        pBuilding->_builder       = worker._id;
        order._targetUnit         = buildingID;
        nudgeToWalkable( worker );
    }

    void RTSWorld::updateBuild( RTSUnit& unit, RTSOrder& order, float32 deltaTime )
    {
        if ( order._targetUnit.isValid() == false )
        {
            const RTSUnitDef* pDef = _pCatalog->find( order._buildID );
            if ( pDef == nullptr )
            {
                finishOrder( unit );
                return;
            }
            // 건물 영역 사각형의 가장자리까지 다가간다.
            if ( computeRectDistance( unit._position, order._buildCell, pDef->_footprint ) - unit._pDef->_radius > _settings._interactSlack )
            {
                approach( unit, order._target, false );
                return;
            }
            unit._agent.stop();
            startConstruction( unit, order );
            return;
        }
        RTSUnit* pBuilding = findUnitMutable( order._targetUnit );
        if ( pBuilding == nullptr || pBuilding->isConstructed() )
        {
            finishOrder( unit );
            return;
        }
        if ( findUnit( pBuilding->_builder ) != nullptr && pBuilding->_builder != unit._id )
        {
            finishOrder( unit ); // 다른 일꾼이 짓는 중이다
            return;
        }
        pBuilding->_builder = unit._id;
        if ( isWithinReach( unit, *pBuilding, _settings._interactSlack ) == false )
        {
            approach( unit, pBuilding->_position, false );
            return;
        }
        unit._agent.stop();
        const RTSUnitDef& def     = *pBuilding->_pDef;
        const float32     step    = deltaTime / def._buildTime;
        pBuilding->_buildProgress = MathUtil::min( 1.0f, pBuilding->_buildProgress + step );
        pBuilding->_hp            = MathUtil::min( def._hp, pBuilding->_hp + def._hp * ( 1.0f - _settings._constructionStartRatio ) * step );
        if ( pBuilding->isConstructed() )
        {
            pBuilding->_builder = RTSUnitID{};
            pushEvent( RTSEvent::Kind::ConstructionComplete, pBuilding->_owner, pBuilding->_id, def._id );
            finishOrder( unit );
        }
    }

    // ------------------------------------------------------------------------------
    // 생산
    // ------------------------------------------------------------------------------
    void RTSWorld::updateProduction( RTSUnit& building, float32 deltaTime )
    {
        if ( building._listProduction.empty() )
            return;
        const RTSUnitDef* pDef = _pCatalog->find( building._listProduction.front() );
        if ( pDef == nullptr )
        {
            building._listProduction.pop_front();
            return;
        }
        RTSPlayer& player                  = _listPlayer[static_cast<size_t>( building._owner )];
        const bool bNeedsSupplyReservation = building._productionTimer <= 0.0f && building._supplyReserved == 0 && pDef->_supplyCost > 0;
        if ( bNeedsSupplyReservation )
        {
            if ( player._supplyUsed + pDef->_supplyCost > player._supplyCap )
            {
                if ( building._bSupplyBlocked == SW_FALSE )
                    pushEvent( RTSEvent::Kind::SupplyBlocked, building._owner, building._id, pDef->_id );
                building._bSupplyBlocked = SW_TRUE;
                return;
            }
            building._supplyReserved = pDef->_supplyCost;
            building._bSupplyBlocked = SW_FALSE;
            player._supplyUsed += pDef->_supplyCost;
        }
        building._productionTimer += deltaTime;
        if ( building._productionTimer >= pDef->_buildTime )
            completeProduction( building );
    }

    void RTSWorld::completeProduction( RTSUnit& building )
    {
        const hashed_string defID = building._listProduction.front();
        building._listProduction.pop_front();
        building._productionTimer = 0.0f;
        building._supplyReserved  = 0;
        float3 position{};
        if ( findSpawnPosition( building, position ) == false )
            position = building._position;
        const RTSUnitDef* pDef = _pCatalog->find( defID );
        if ( pDef != nullptr && pDef->_bAir )
            position = float3{ building._position._x, 0.0f, building._position._z };
        const RTSUnitID unitID = spawnUnit( defID, building._owner, position );
        pushEvent( RTSEvent::Kind::ProductionComplete, building._owner, unitID, defID );
        if ( building._bHasRally == SW_FALSE || unitID.isValid() == false )
            return;
        const RTSUnitID rallyTarget = pickUnit( building._rallyPoint );
        (void)issueSmart( unitID, building._rallyPoint, rallyTarget, false );
    }

    void RTSWorld::payCost( RTSPlayer& player, const RTSUnitDef& def )
    {
        // evaluateCost 가 둘 다 된다고 판단한 뒤에만 호출한다. 한쪽만 빠지는 일이 없게 하려는 것이다.
        if ( player._pWallet == nullptr )
            return;
        if ( player._pWallet->trySpend( _settings._mineralCurrency, def._minerals ) == false || player._pWallet->trySpend( _settings._gasCurrency, def._gas ) == false )
            SW_LOG_WARNING( "RTSWorld: '%#' cost was checked but could not be paid", def._id.c_str() );
    }

    void RTSWorld::refundCost( RTSPlayer& player, const RTSUnitDef& def )
    {
        if ( player._pWallet == nullptr )
            return;
        player._pWallet->add( _settings._mineralCurrency, def._minerals );
        player._pWallet->add( _settings._gasCurrency, def._gas );
    }
} // namespace sw
