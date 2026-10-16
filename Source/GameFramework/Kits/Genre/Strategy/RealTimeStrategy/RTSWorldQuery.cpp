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
        bool isGroundUnitQuery( const RTSUnit& unit )
        {
            return unit._pDef->_bAir == SW_FALSE;
        }
    } // namespace

    // ------------------------------------------------------------------------------
    // 조회와 배치
    // ------------------------------------------------------------------------------
    int32 RTSWorld::countUnits( int32 player, const hashed_string& defID, bool bIncludeUnfinished ) const
    {
        int32 count = 0;
        for ( const RTSUnit& unit : _listUnit )
        {
            const bool bCountsTowardTotal = unit._bAlive && unit._owner == player && unit._pDef->_id == defID && ( bIncludeUnfinished || unit.isConstructed() );
            if ( bCountsTowardTotal )
                ++count;
        }
        return count;
    }

    bool RTSWorld::hasConstructed( int32 player, const hashed_string& defID ) const
    {
        return countUnits( player, defID, false ) > 0;
    }

    int32 RTSWorld::countPlanned( int32 player, const hashed_string& defID ) const
    {
        int32 count = 0;
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner != player )
                continue;
            if ( unit._pDef->_id == defID )
                ++count;
            for ( const hashed_string& queued : unit._listProduction )
            {
                count += queued == defID ? 1 : 0;
            }
            for ( const RTSOrder& order : unit._listOrder )
            {
                count += order._type == RTSOrderType::Build && order._buildID == defID && order._targetUnit.isValid() == false ? 1 : 0;
            }
        }
        return count;
    }

    bool RTSWorld::canPlaceBuilding( const hashed_string& buildingID, const int2& cell, RTSUnitID ignoreUnit ) const
    {
        const RTSUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->find( buildingID ) : nullptr;
        if ( pDef == nullptr || pDef->_kind != RTSUnitKind::Building )
            return false;
        const int32 footprint = pDef->_footprint;
        if ( _grid.isInside( cell ) == false || _grid.isInside( cell._x + footprint - 1, cell._y + footprint - 1 ) == false )
            return false;
        for ( int32 y = cell._y; y < cell._y + footprint; ++y )
        {
            for ( int32 x = cell._x; x < cell._x + footprint; ++x )
            {
                if ( _land.isUsable( x, y ) == false )
                    return false; // 남의 땅이다. 막히지 않은 땅(도로)에도 건물을 짓지 못한다
            }
        }
        if ( pDef->_bExtractor )
        {
            // 같은 셀 위치에 같은 크기의 빈 가스 간헐천이 있을 때만 놓을 수 있다.
            for ( const RTSUnit& unit : _listUnit )
            {
                const bool bNotMatchingGas = unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != RTSResourceType::Gas || unit._cell != cell ||
                                             unit._pDef->_footprint != footprint;
                if ( bNotMatchingGas )
                    continue;
                for ( const RTSUnit& other : _listUnit )
                {
                    const bool bExtractorLinked = other._bAlive && other._pDef->_bExtractor && other._linkedResource == unit._id;
                    if ( bExtractorLinked )
                        return false;
                }
                return true;
            }
            return false;
        }
        for ( int32 y = cell._y; y < cell._y + footprint; ++y )
        {
            for ( int32 x = cell._x; x < cell._x + footprint; ++x )
            {
                if ( _grid.isWalkable( x, y ) == false )
                    return false;
            }
        }
        for ( const RTSUnit& unit : _listUnit )
        {
            const bool bBlockingUnit = unit._bAlive && unit.isMobile() && unit._id != ignoreUnit && isGroundUnitQuery( unit ) &&
                                       computeRectDistance( unit._position, cell, footprint ) < unit._pDef->_radius * 0.9f;
            if ( bBlockingUnit )
                return false;
        }
        return true;
    }

    bool RTSWorld::findBuildSite( const hashed_string& buildingID, const float3& nearPosition, int32 minRadius, int32 maxRadius, int2& outCell ) const
    {
        const RTSUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->find( buildingID ) : nullptr;
        if ( pDef == nullptr || pDef->_kind != RTSUnitKind::Building )
            return false;
        if ( pDef->_bExtractor )
        {
            float32 bestDistance = static_cast<float32>( maxRadius ) + 1.0f;
            bool    bFound       = false;
            for ( const RTSUnit& unit : _listUnit )
            {
                const bool bNotGasResource = unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != RTSResourceType::Gas;
                if ( bNotGasResource )
                    continue;
                const float32 distance = float3::getHorizontalDistance( nearPosition, unit._position );
                if ( distance < bestDistance && canPlaceBuilding( buildingID, unit._cell ) )
                {
                    bestDistance = distance;
                    outCell      = unit._cell;
                    bFound       = true;
                }
            }
            return bFound;
        }
        const int32 footprint = pDef->_footprint;
        const int2  center    = _grid.computeCell( nearPosition );
        for ( int32 ring = MathUtil::max( 0, minRadius ); ring <= maxRadius; ++ring )
        {
            for ( int32 offsetY = -ring; offsetY <= ring; ++offsetY )
            {
                for ( int32 offsetX = -ring; offsetX <= ring; ++offsetX )
                {
                    if ( MathUtil::max( MathUtil::abs( offsetX ), MathUtil::abs( offsetY ) ) != ring )
                        continue;
                    const int2 cell{ center._x + offsetX - footprint / 2, center._y + offsetY - footprint / 2 };
                    if ( canPlaceBuilding( buildingID, cell ) == false )
                        continue;
                    // 둘레 한 셀은 비워 둔다. 경로를 막지 않기 위해서다.
                    bool bClear = true;
                    for ( int32 y = cell._y - 1; y <= cell._y + footprint && bClear; ++y )
                    {
                        for ( int32 x = cell._x - 1; x <= cell._x + footprint && bClear; ++x )
                        {
                            bClear = _grid.isWalkable( x, y );
                        }
                    }
                    if ( bClear )
                    {
                        outCell = cell;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    RTSUnitID RTSWorld::findNearestResource( const float3& position, RTSResourceType type, float32 maxDistance ) const
    {
        RTSUnitID bestID{};
        float32   bestDistance = maxDistance;
        for ( const RTSUnit& unit : _listUnit )
        {
            const bool bNotUsableResource = unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != type || unit._resourceLeft <= 0;
            if ( bNotUsableResource )
                continue;
            const float32 distance = float3::getHorizontalDistance( position, unit._position );
            if ( distance <= bestDistance )
            {
                bestDistance = distance;
                bestID       = unit._id;
            }
        }
        return bestID;
    }

    RTSUnitID RTSWorld::findNearestDepot( int32 player, const float3& position ) const
    {
        RTSUnitID bestID{};
        float32   bestDistance = MathUtil::kMaxFloat;
        for ( const RTSUnit& unit : _listUnit )
        {
            const bool bNotUsableDepot = unit._bAlive == SW_FALSE || unit._owner != player || unit._pDef->_bDepot == SW_FALSE || unit.isConstructed() == false;
            if ( bNotUsableDepot )
                continue;
            const float32 distance = float3::getHorizontalDistance( position, unit._position );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                bestID       = unit._id;
            }
        }
        return bestID;
    }

    // ------------------------------------------------------------------------------
    // 흐름장과 알림
    // ------------------------------------------------------------------------------
    FlowField* RTSWorld::acquireFlowField( const int2& goal )
    {
        FlowFieldSlot* pFree = nullptr;
        for ( FlowFieldSlot& slot : _listFlowField )
        {
            if ( slot._userCount > 0 && slot._goal == goal )
            {
                ++slot._userCount;
                return &slot._field;
            }
            if ( slot._userCount == 0 && pFree == nullptr )
                pFree = &slot;
        }
        if ( pFree == nullptr )
        {
            _listFlowField.emplace_back();
            pFree = &_listFlowField.back();
        }
        pFree->_goal      = goal;
        pFree->_userCount = 1;
        (void)pFree->_field.computeToCell( _grid, goal );
        return &pFree->_field;
    }

    void RTSWorld::releaseFlowField( const FlowField* pField )
    {
        for ( FlowFieldSlot& slot : _listFlowField )
        {
            if ( &slot._field == pField && slot._userCount > 0 )
            {
                --slot._userCount;
                return;
            }
        }
    }
} // namespace sw
