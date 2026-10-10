#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Gameplay/Match/TeamAttitude.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "RTSWorld" );

    namespace
    {
        struct RTSWorldInternal
        {
            static constexpr float32 kMaxUnitExtent       = 4.5f; ///< 버킷 조회에 더하는 몸 크기 상한(건물 반 변)
            static constexpr uint32  kMinUnitStateBytes   = 128;  ///< 유닛 하나의 상태가 적어도 쓰는 바이트(개수 상한)
            static constexpr uint32  kMinOrderStateBytes  = 34;   ///< 명령 하나의 상태가 적어도 쓰는 바이트
            static constexpr uint32  kMinPlayerStateBytes = 30;   ///< 플레이어 하나의 상태가 적어도 쓰는 바이트

            static float32 computeFlatDistance( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dz = lhs._z - rhs._z;
                return MathUtil::sqrt( dx * dx + dz * dz );
            }

            static bool isGround( const RTSUnit& unit ) { return unit._pDef->_bAir == SW_FALSE; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( RTSCommandResult result )
    {
        switch ( result )
        {
            case RTSCommandResult::Ok:
                return "Ok";
            case RTSCommandResult::InvalidUnit:
                return "InvalidUnit";
            case RTSCommandResult::NotOwner:
                return "NotOwner";
            case RTSCommandResult::CannotDo:
                return "CannotDo";
            case RTSCommandResult::NotEnoughMinerals:
                return "NotEnoughMinerals";
            case RTSCommandResult::NotEnoughGas:
                return "NotEnoughGas";
            case RTSCommandResult::NotEnoughSupply:
                return "NotEnoughSupply";
            case RTSCommandResult::TechRequired:
                return "TechRequired";
            case RTSCommandResult::QueueFull:
                return "QueueFull";
            case RTSCommandResult::InvalidPlacement:
                return "InvalidPlacement";
        }
        return "Unknown";
    }

    RTSWorld::RTSWorld()
        : _grid{}
        , _airGrid{}
        , _pathfinder{}
        , _listUnit{}
        , _listGeneration{}
        , _listFreeSlot{}
        , _listPlayer{}
        , _eventBuffer{}
        , _listTerrainBlocked{}
        , _listTeamVisibility{}
        , _listBucketHead{}
        , _listBucketNext{}
        , _listNeighborScratch{}
        , _listQueryScratch{}
        , _listFlowField{}
        , _pCatalog{ nullptr }
        , _settings{}
        , _stepTimer{}
        , _time{ 0.0f }
        , _visionTimer{}
        , _bucketTopology{}
        , _land{}
        , _teamCount{ 0 }
        , _winningTeam{ -1 }
        , _landRevision{ 0 }
    {
    }

    void RTSWorld::initialize( const RTSCatalog* pCatalog, int32 width, int32 height, const RTSSettings& settings )
    {
        _pCatalog             = pCatalog;
        _settings             = settings;
        _settings._bucketSize = MathUtil::max( 1, _settings._bucketSize );
        _grid.initialize( MathUtil::max( 1, width ), MathUtil::max( 1, height ), 1.0f, float3{ 0.0f, 0.0f, 0.0f } );
        _airGrid.initialize( _grid.getWidth(), _grid.getHeight(), 1.0f, float3{ 0.0f, 0.0f, 0.0f } );
        _listUnit.clear();
        _listGeneration.clear();
        _listFreeSlot.clear();
        _listPlayer.clear();
        _eventBuffer.clear();
        _listTerrainBlocked.assign( static_cast<size_t>( _grid.getWidth() * _grid.getHeight() ), SW_FALSE );
        _listTeamVisibility.clear();
        _listFlowField.clear();
        _bucketTopology = GridTopology{ ( _grid.getWidth() + _settings._bucketSize - 1 ) / _settings._bucketSize,
                                        ( _grid.getHeight() + _settings._bucketSize - 1 ) / _settings._bucketSize };
        _listBucketHead.assign( static_cast<size_t>( _bucketTopology.getCellCount() ), -1 );
        _listBucketNext.clear();
        _stepTimer = FixedStepTimer{ _settings._fixedStep, 0.25f };
        _time      = 0.0f;
        _visionTimer.clear();
        _teamCount   = 0;
        _winningTeam = -1;
    }

    void RTSWorld::setTerrainBlocked( int32 x, int32 y, bool bBlocked )
    {
        if ( _grid.isInside( x, y ) == false )
            return;
        _listTerrainBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] = bBlocked ? SW_TRUE : SW_FALSE;
        _grid.setBlocked( x, y, bBlocked || _land.isBlocked( x, y ) );
    }

    void RTSWorld::bindLand( LandRegistry* pLand, const int2& origin )
    {
        _land.bind( pLand, origin, hashed_string( "RealTimeStrategy" ) );
        repaintGrid();
        _landRevision = _land.getRevision();
    }

    void RTSWorld::repaintGrid()
    {
        for ( int32 y = 0; y < _grid.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _grid.getWidth(); ++x )
            {
                const bool bTerrain = _listTerrainBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] != SW_FALSE;
                _grid.setBlocked( x, y, bTerrain || _land.isBlocked( x, y ) );
            }
        }
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive != SW_FALSE && unit.isMobile() == false && unit._pDef->_bExtractor == SW_FALSE )
                placeFootprint( unit, true );
        }
    }

    int32 RTSWorld::addPlayer( int32 team, Wallet* pWallet, const float3& startPosition )
    {
        RTSPlayer player;
        player._team          = MathUtil::max( 0, team );
        player._pWallet       = pWallet;
        player._startPosition = startPosition;
        _listPlayer.push_back( player );
        _teamCount = MathUtil::max( _teamCount, player._team + 1 );
        while ( static_cast<int32>( _listTeamVisibility.size() ) < _teamCount )
        {
            _listTeamVisibility.emplace_back( static_cast<size_t>( _grid.getWidth() * _grid.getHeight() ), static_cast<uint8>( RTSVisibility::Unexplored ) );
        }
        return static_cast<int32>( _listPlayer.size() ) - 1;
    }

    // ------------------------------------------------------------------------------
    // 유닛 자리
    // ------------------------------------------------------------------------------
    RTSUnitId RTSWorld::allocateUnit()
    {
        if ( _listFreeSlot.empty() == false )
        {
            const uint32 index = _listFreeSlot.back();
            _listFreeSlot.pop_back();
            _listUnit[index] = RTSUnit{};
            return RTSUnitId::make( index, _listGeneration[index] );
        }
        const uint32 index = static_cast<uint32>( _listUnit.size() );
        _listUnit.emplace_back();
        _listGeneration.push_back( 1u );
        return RTSUnitId::make( index, 1u );
    }

    void RTSWorld::freeDeadUnits()
    {
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            RTSUnit& unit = _listUnit[index];
            if ( unit._bAlive || unit._id.isValid() == false )
                continue;
            for ( RTSOrder& order : unit._listOrder )
            {
                releaseOrder( unit, order );
            }
            unit._listOrder.clear();
            unit._id = RTSUnitId{};
            ++_listGeneration[index];
            if ( _listGeneration[index] == 0 )
                _listGeneration[index] = 1;
            _listFreeSlot.push_back( static_cast<uint32>( index ) );
        }
    }

    const RTSUnit* RTSWorld::findUnit( RTSUnitId unitId ) const
    {
        if ( unitId.isValid() == false || unitId.index() >= _listUnit.size() )
            return nullptr;
        const RTSUnit& unit = _listUnit[unitId.index()];
        return unit._bAlive && unit._id == unitId ? &unit : nullptr;
    }

    RTSUnit* RTSWorld::findUnitMutable( RTSUnitId unitId ) { return const_cast<RTSUnit*>( findUnit( unitId ) ); }

    const RTSPlayer* RTSWorld::findPlayer( int32 player ) const
    {
        return player >= 0 && player < static_cast<int32>( _listPlayer.size() ) ? &_listPlayer[static_cast<size_t>( player )] : nullptr;
    }

    float3 RTSWorld::computeFootprintCenter( const int2& cell, int32 footprint ) const
    {
        const float32 half = static_cast<float32>( footprint ) * 0.5f * _grid.getCellSize();
        return float3{ _grid.getOrigin()._x + static_cast<float32>( cell._x ) * _grid.getCellSize() + half, 0.0f,
                       _grid.getOrigin()._z + static_cast<float32>( cell._y ) * _grid.getCellSize() + half };
    }

    void RTSWorld::placeFootprint( const RTSUnit& unit, bool bBlocked )
    {
        const int32 footprint = unit._pDef->_footprint;
        for ( int32 y = unit._cell._y; y < unit._cell._y + footprint; ++y )
        {
            for ( int32 x = unit._cell._x; x < unit._cell._x + footprint; ++x )
            {
                if ( _grid.isInside( x, y ) == false )
                    continue;
                const bool bTerrain = _listTerrainBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] != SW_FALSE;
                _grid.setBlocked( x, y, bBlocked || bTerrain || _land.isBlocked( x, y ) );
            }
        }
    }

    RTSUnitId RTSWorld::spawnUnit( const hashed_string& defId, int32 owner, const float3& position )
    {
        const RTSUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( defId ) : nullptr;
        if ( pDef == nullptr || ( owner != kNoOwner && findPlayer( owner ) == nullptr ) )
            return RTSUnitId{};
        int2      cell = _grid.computeCell( position );
        RTSUnitId linkedResource{};
        if ( pDef->isMobile() == false )
        {
            if ( pDef->_bExtractor )
            {
                if ( canPlaceBuilding( defId, cell ) == false )
                    return RTSUnitId{};
                for ( const RTSUnit& other : _listUnit )
                {
                    if ( other._bAlive && other.isResource() && other._pDef->_resourceType == RTSResourceType::Gas && other._cell == cell )
                        linkedResource = other._id;
                }
            }
            else
            {
                for ( int32 y = cell._y; y < cell._y + pDef->_footprint; ++y )
                {
                    for ( int32 x = cell._x; x < cell._x + pDef->_footprint; ++x )
                    {
                        if ( _grid.isWalkable( x, y ) == false )
                            return RTSUnitId{};
                    }
                }
            }
        }
        else if ( pDef->_bAir == SW_FALSE && _grid.isWalkable( cell ) == false && _grid.findNearestWalkable( cell, 8, cell ) == false )
        {
            return RTSUnitId{};
        }
        const int32 footprint = pDef->_footprint;
        if ( pDef->_kind == RTSUnitKind::Building && _land.claimRect( cell._x, cell._y, cell._x + footprint - 1, cell._y + footprint - 1, true ) == false )
            return RTSUnitId{};

        const RTSUnitId unitId = allocateUnit();
        RTSUnit&        unit   = _listUnit[unitId.index()];
        unit._id               = unitId;
        unit._pDef             = pDef;
        unit._owner            = pDef->_kind == RTSUnitKind::Resource ? kNoOwner : owner;
        unit._hp               = pDef->_hp;
        unit._resourceLeft     = pDef->_resourceAmount;
        unit._linkedResource   = linkedResource;
        if ( pDef->isMobile() )
        {
            const bool bSnapped = pDef->_bAir == SW_FALSE && _grid.isWalkable( _grid.computeCell( position ) ) == false;
            unit._position      = bSnapped ? _grid.computeCellCenter( cell ) : float3{ position._x, 0.0f, position._z };
            NavAgentSettings agentSettings;
            agentSettings._radius   = pDef->_radius;
            agentSettings._maxSpeed = MathUtil::max( 0.01f, pDef->_speed );
            unit._agent.setSettings( agentSettings );
            unit._agent.setPosition( unit._position );
        }
        else
        {
            unit._cell     = cell;
            unit._position = computeFootprintCenter( cell, pDef->_footprint );
            if ( pDef->_bExtractor == SW_FALSE )
                placeFootprint( unit, true );
            if ( pDef->_kind == RTSUnitKind::Building && owner != kNoOwner )
                _listPlayer[static_cast<size_t>( owner )]._bHadBuilding = SW_TRUE;
        }
        pushEvent( RTSEvent::Kind::UnitCreated, unit._owner, unitId, pDef->_id );
        return unitId;
    }

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
        unit._attackTarget  = RTSUnitId{};
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
                pTarget->_harvester = RTSUnitId{};
            if ( pTarget != nullptr && pTarget->_builder == unit._id )
                pTarget->_builder = RTSUnitId{};
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

    RTSCommandResult RTSWorld::issueMove( RTSUnitId unitId, const float3& target, bool bQueue )
    {
        RTSUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type   = RTSOrderType::Move;
        order._target = target;
        return pushOrder( *pUnit, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueAttackMove( RTSUnitId unitId, const float3& target, bool bQueue )
    {
        RTSUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type   = pUnit->_pDef->canAttack() ? RTSOrderType::AttackMove : RTSOrderType::Move;
        order._target = target;
        return pushOrder( *pUnit, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueAttack( RTSUnitId unitId, RTSUnitId targetId, bool bQueue )
    {
        RTSUnit*       pUnit   = findUnitMutable( unitId );
        const RTSUnit* pTarget = findUnit( targetId );
        if ( pUnit == nullptr || pTarget == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false || canAttack( *pUnit, *pTarget ) == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type       = RTSOrderType::Attack;
        order._targetUnit = targetId;
        order._target     = pTarget->_position;
        return pushOrder( *pUnit, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueGather( RTSUnitId unitId, RTSUnitId resourceId, bool bQueue )
    {
        RTSUnit*       pUnit   = findUnitMutable( unitId );
        const RTSUnit* pTarget = findUnit( resourceId );
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
        order._targetUnit = resourceId;
        order._target     = pTarget->_position;
        return pushOrder( *pUnit, order, bQueue );
    }

    RTSCommandResult RTSWorld::evaluateCost( int32 player, const RTSUnitDef& def ) const
    {
        const RTSPlayer* pPlayer = findPlayer( player );
        if ( pPlayer == nullptr )
            return RTSCommandResult::NotOwner;
        if ( def._requires.empty() == false && hasConstructed( player, def._requires ) == false )
            return RTSCommandResult::TechRequired;
        if ( def._minerals > 0 && ( pPlayer->_pWallet == nullptr || pPlayer->_pWallet->canAfford( _settings._mineralCurrency, def._minerals ) == false ) )
            return RTSCommandResult::NotEnoughMinerals;
        if ( def._gas > 0 && ( pPlayer->_pWallet == nullptr || pPlayer->_pWallet->canAfford( _settings._gasCurrency, def._gas ) == false ) )
            return RTSCommandResult::NotEnoughGas;
        return RTSCommandResult::Ok;
    }

    RTSCommandResult RTSWorld::issueBuild( RTSUnitId workerId, const hashed_string& buildingId, const int2& cell, bool bQueue )
    {
        RTSUnit* pWorker = findUnitMutable( workerId );
        if ( pWorker == nullptr )
            return RTSCommandResult::InvalidUnit;
        const RTSUnitDef* pDef = _pCatalog->findUnit( buildingId );
        if ( pDef == nullptr || pDef->_kind != RTSUnitKind::Building || pWorker->_pDef->_bWorker == SW_FALSE || pDef->_producedBy != pWorker->_pDef->_id )
            return RTSCommandResult::CannotDo;
        const RTSCommandResult costResult = evaluateCost( pWorker->_owner, *pDef );
        if ( costResult != RTSCommandResult::Ok )
            return costResult;
        if ( canPlaceBuilding( buildingId, cell, workerId ) == false )
            return RTSCommandResult::InvalidPlacement;
        RTSOrder order;
        order._type      = RTSOrderType::Build;
        order._buildId   = buildingId;
        order._buildCell = cell;
        order._target    = computeFootprintCenter( cell, pDef->_footprint );
        return pushOrder( *pWorker, order, bQueue );
    }

    RTSCommandResult RTSWorld::issueHold( RTSUnitId unitId )
    {
        RTSUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RTSCommandResult::CannotDo;
        RTSOrder order;
        order._type   = RTSOrderType::Hold;
        order._target = pUnit->_position;
        return pushOrder( *pUnit, order, false );
    }

    RTSCommandResult RTSWorld::issueStop( RTSUnitId unitId )
    {
        RTSUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        clearOrders( *pUnit );
        return RTSCommandResult::Ok;
    }

    RTSCommandResult RTSWorld::issueSmart( RTSUnitId unitId, const float3& position, RTSUnitId targetId, bool bQueue )
    {
        RTSUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RTSCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
        {
            setRallyPoint( unitId, position );
            return RTSCommandResult::Ok;
        }
        const RTSUnit* pTarget = findUnit( targetId );
        if ( pTarget != nullptr && pTarget->_id != unitId )
        {
            if ( areEnemies( pUnit->_owner, pTarget->_owner ) && canAttack( *pUnit, *pTarget ) )
                return issueAttack( unitId, targetId, bQueue );
            if ( pUnit->_pDef->_bWorker )
            {
                if ( issueGather( unitId, targetId, bQueue ) == RTSCommandResult::Ok )
                    return RTSCommandResult::Ok;
                if ( pTarget->isBuilding() && pTarget->_owner == pUnit->_owner && pTarget->isConstructed() == false )
                {
                    RTSOrder order;
                    order._type       = RTSOrderType::Build;
                    order._buildId    = pTarget->_pDef->_id;
                    order._buildCell  = pTarget->_cell;
                    order._targetUnit = targetId;
                    order._target     = pTarget->_position;
                    return pushOrder( *pUnit, order, bQueue );
                }
            }
        }
        return issueMove( unitId, position, bQueue );
    }

    int32 RTSWorld::issueGroupMove( const vector<RTSUnitId>& listUnit, const float3& target, bool bAttackMove, bool bQueue )
    {
        int32 groundCount = 0;
        for ( const RTSUnitId unitId : listUnit )
        {
            const RTSUnit* pUnit = findUnit( unitId );
            if ( pUnit != nullptr && pUnit->isMobile() && RTSWorldInternal::isGround( *pUnit ) )
                ++groundCount;
        }
        const float32 arriveRadius = 0.5f + 0.5f * MathUtil::sqrt( static_cast<float32>( listUnit.size() ) );
        const bool    bUseField    = groundCount >= _settings._flowFieldGroupSize;
        const int2    goalCell     = _grid.computeCell( target );
        int32         issuedCount  = 0;
        for ( const RTSUnitId unitId : listUnit )
        {
            RTSUnit* pUnit = findUnitMutable( unitId );
            if ( pUnit == nullptr || pUnit->isMobile() == false )
                continue;
            RTSOrder order;
            order._type         = bAttackMove && pUnit->_pDef->canAttack() ? RTSOrderType::AttackMove : RTSOrderType::Move;
            order._target       = target;
            order._arriveRadius = listUnit.size() > 1 ? arriveRadius : 0.0f;
            if ( bUseField && RTSWorldInternal::isGround( *pUnit ) )
                order._pFlowField = acquireFlowField( goalCell );
            (void)pushOrder( *pUnit, order, bQueue );
            ++issuedCount;
        }
        return issuedCount;
    }

    RTSCommandResult RTSWorld::train( RTSUnitId buildingId, const hashed_string& unitId )
    {
        RTSUnit* pBuilding = findUnitMutable( buildingId );
        if ( pBuilding == nullptr )
            return RTSCommandResult::InvalidUnit;
        const RTSUnitDef* pDef = _pCatalog->findUnit( unitId );
        if ( pDef == nullptr || pDef->isMobile() == false || pBuilding->isBuilding() == false || pBuilding->isConstructed() == false ||
             pDef->_producedBy != pBuilding->_pDef->_id )
            return RTSCommandResult::CannotDo;
        if ( static_cast<int32>( pBuilding->_listProduction.size() ) >= _settings._productionQueueMax )
            return RTSCommandResult::QueueFull;
        const RTSCommandResult costResult = evaluateCost( pBuilding->_owner, *pDef );
        if ( costResult != RTSCommandResult::Ok )
            return costResult;
        payCost( _listPlayer[static_cast<size_t>( pBuilding->_owner )], *pDef );
        pBuilding->_listProduction.push_back( unitId );
        return RTSCommandResult::Ok;
    }

    bool RTSWorld::cancelTrain( RTSUnitId buildingId )
    {
        RTSUnit* pBuilding = findUnitMutable( buildingId );
        if ( pBuilding == nullptr || pBuilding->_listProduction.empty() )
            return false;
        const RTSUnitDef* pDef = _pCatalog->findUnit( pBuilding->_listProduction.back() );
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

    void RTSWorld::setRallyPoint( RTSUnitId buildingId, const float3& position )
    {
        RTSUnit* pBuilding = findUnitMutable( buildingId );
        if ( pBuilding == nullptr || pBuilding->isBuilding() == false )
            return;
        pBuilding->_rallyPoint = position;
        pBuilding->_bHasRally  = SW_TRUE;
    }

    // ------------------------------------------------------------------------------
    // 시간
    // ------------------------------------------------------------------------------
    void RTSWorld::update( float32 deltaTime )
    {
        if ( _pCatalog == nullptr )
            return;
        // 다른 키트가 땅을 얻거나 놓았으면 땅 격자를 다시 칠한다(흐름장은 격자 리비전으로 스스로 다시 구한다).
        if ( _land.isBound() && _land.getRevision() != _landRevision )
        {
            repaintGrid();
            _landRevision = _land.getRevision();
        }
        const int32 stepCount = _stepTimer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepFixed( _stepTimer.getStep() );
        }
    }

    void RTSWorld::drainEvents( vector<RTSEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void RTSWorld::stepFixed( float32 deltaTime )
    {
        _time += deltaTime;
        for ( FlowFieldSlot& slot : _listFlowField )
        {
            if ( slot._userCount > 0 && slot._field.isStale( _grid ) )
                (void)slot._field.computeToCell( _grid, slot._goal );
        }
        recomputeSupply();
        rebuildBuckets();
        const size_t unitCount = _listUnit.size(); // 이번 걸음에 생긴 유닛은 다음 걸음부터
        for ( size_t index = 0; index < unitCount; ++index )
        {
            RTSUnit& unit = _listUnit[index];
            if ( unit._bAlive )
                updateUnit( unit, deltaTime );
        }
        _visionTimer.tick( deltaTime );
        if ( _visionTimer.isActive() == false )
        {
            _visionTimer.start( _settings._visionInterval );
            updateVision();
        }
        updateDefeat();
        freeDeadUnits();
    }

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

    int32 RTSWorld::computeBucketIndex( const float3& position ) const
    {
        const int2  cell    = _grid.computeCell( position );
        const int32 bucketX = MathUtil::clamp( cell._x / _settings._bucketSize, 0, _bucketTopology._width - 1 );
        const int32 bucketY = MathUtil::clamp( cell._y / _settings._bucketSize, 0, _bucketTopology._height - 1 );
        return _bucketTopology.toIndex( bucketX, bucketY );
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

    void RTSWorld::queryUnits( const float3& center, float32 radius, vector<RTSUnitId>& outListUnit ) const
    {
        outListUnit.clear();
        if ( _listBucketHead.empty() )
            return;
        const float32 reach       = radius + RTSWorldInternal::kMaxUnitExtent;
        const float32 cellSize    = _grid.getCellSize() * static_cast<float32>( _settings._bucketSize );
        const int32   lastBucketX = _bucketTopology._width - 1;
        const int32   lastBucketY = _bucketTopology._height - 1;
        const int32   minBucketX  = MathUtil::clamp( static_cast<int32>( ( center._x - reach - _grid.getOrigin()._x ) / cellSize ), 0, lastBucketX );
        const int32   maxBucketX  = MathUtil::clamp( static_cast<int32>( ( center._x + reach - _grid.getOrigin()._x ) / cellSize ), 0, lastBucketX );
        const int32   minBucketY  = MathUtil::clamp( static_cast<int32>( ( center._z - reach - _grid.getOrigin()._z ) / cellSize ), 0, lastBucketY );
        const int32   maxBucketY  = MathUtil::clamp( static_cast<int32>( ( center._z + reach - _grid.getOrigin()._z ) / cellSize ), 0, lastBucketY );
        for ( int32 bucketY = minBucketY; bucketY <= maxBucketY; ++bucketY )
        {
            for ( int32 bucketX = minBucketX; bucketX <= maxBucketX; ++bucketX )
            {
                for ( int32 index = _listBucketHead[static_cast<size_t>( _bucketTopology.toIndex( bucketX, bucketY ) )]; index >= 0;
                      index       = _listBucketNext[static_cast<size_t>( index )] )
                {
                    const RTSUnit& unit = _listUnit[static_cast<size_t>( index )];
                    if ( unit._bAlive == SW_FALSE )
                        continue;
                    const float32 distance = unit.isMobile() ? RTSWorldInternal::computeFlatDistance( center, unit._position ) - unit._pDef->_radius
                                                             : computeRectDistance( center, unit._cell, unit._pDef->_footprint );
                    if ( distance <= radius )
                        outListUnit.push_back( unit._id );
                }
            }
        }
    }

    RTSUnitId RTSWorld::pickUnit( const float3& position ) const
    {
        RTSUnitId bestId{};
        float32   bestDistance = MathUtil::kMaxFloat;
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE )
                continue;
            float32 distance = unit.isMobile() ? RTSWorldInternal::computeFlatDistance( position, unit._position ) - unit._pDef->_radius
                                               : computeRectDistance( position, unit._cell, unit._pDef->_footprint );
            if ( distance > 0.0f )
                continue;
            // 유닛이 건물 위에 겹치면 유닛을 고른다(정제소는 간헐천보다 먼저).
            distance -= unit.isMobile() ? 10.0f : ( unit._pDef->_bExtractor ? 5.0f : 0.0f );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                bestId       = unit._id;
            }
        }
        return bestId;
    }

    // ------------------------------------------------------------------------------
    // 유닛 한 걸음
    // ------------------------------------------------------------------------------
    void RTSWorld::updateUnit( RTSUnit& unit, float32 deltaTime )
    {
        unit._cooldown.tick( deltaTime );
        unit._repathTimer.tick( deltaTime );
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
        if ( RTSWorldInternal::isGround( unit ) == false )
        {
            if ( bForce || unit._agent.isMoving() == false || RTSWorldInternal::computeFlatDistance( unit._moveGoal, target ) > 0.25f )
            {
                unit._moveGoal = target;
                unit._agent.followPath( vector<float3>{
                    float3{ target._x, 0.0f, target._z }
                } );
            }
            return;
        }
        const bool bGoalMoved = RTSWorldInternal::computeFlatDistance( unit._moveGoal, target ) > 1.0f;
        if ( bForce == false && unit._repathTimer.isActive() )
            return;
        if ( bForce == false && unit._agent.isMoving() && bGoalMoved == false )
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
        const bool bGround = RTSWorldInternal::isGround( unit );
        _listNeighborScratch.clear();
        // 채취하는 일꾼은 서로 밀지 않는다(광물 앞에 모여도 된다 — 스타크래프트도 같다).
        const RTSOrder* pOrder = unit.findOrder();
        if ( pOrder == nullptr || pOrder->_type != RTSOrderType::Gather )
        {
            queryUnits( unit._position, unit._pDef->_radius * 2.0f, _listQueryScratch );
            for ( const RTSUnitId otherId : _listQueryScratch )
            {
                const RTSUnit* pOther = findUnit( otherId );
                if ( pOther == nullptr || pOther == &unit || pOther->isMobile() == false || RTSWorldInternal::isGround( *pOther ) != bGround )
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
                        unit._bOrderStarted = SW_FALSE; // 싸운 뒤 길을 다시 구한다
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
                const float32 distance = RTSWorldInternal::computeFlatDistance( unit._position, order._target );
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

    // ------------------------------------------------------------------------------
    // 채취
    // ------------------------------------------------------------------------------
    void RTSWorld::updateGather( RTSUnit& unit, RTSOrder& order, float32 deltaTime )
    {
        if ( unit._gatherPhase == RTSGatherPhase::Returning )
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
            return;
        }

        RTSUnit* pTarget = findUnitMutable( order._targetUnit );
        if ( pTarget == nullptr )
        {
            // 다 캔 광물 — 둘레의 다른 광물로(정제소가 부서졌으면 끝).
            const RTSUnitId nextId = findNearestResource( order._target, RTSResourceType::Minerals, _settings._resourceSearchRadius );
            if ( nextId.isValid() == false )
            {
                finishOrder( unit );
                return;
            }
            order._targetUnit = nextId;
            order._target     = findUnit( nextId )->_position;
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
            unit._gatherTimer += deltaTime;
            if ( unit._gatherTimer < unit._pDef->_gatherTime )
                return;
            const int32 amount = MathUtil::min( unit._pDef->_cargo, pSource->_resourceLeft );
            pSource->_resourceLeft -= amount;
            unit._cargoAmount = amount;
            unit._cargoType   = pSource->_pDef->_resourceType;
            unit._gatherPhase = RTSGatherPhase::Returning;
            unit._repathTimer.clear();
            pTarget->_harvester = RTSUnitId{};
            if ( pSource->_resourceLeft <= 0 )
            {
                pushEvent( RTSEvent::Kind::ResourceDepleted, unit._owner, pSource->_id, pSource->_pDef->_id );
                if ( bExtractor == false )
                    killUnit( *pSource, RTSUnitId{} );
            }
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
            // 남이 캔다 — 광물이면 바로 옆의 빈 광물로 옮긴다.
            if ( bExtractor == false && unit._gatherPhase == RTSGatherPhase::ToResource )
            {
                queryUnits( unit._position, 2.0f, _listQueryScratch );
                for ( const RTSUnitId otherId : _listQueryScratch )
                {
                    const RTSUnit* pOther = findUnit( otherId );
                    if ( pOther != nullptr && pOther != pTarget && pOther->isResource() &&
                         pOther->_pDef->_resourceType == RTSResourceType::Minerals && findUnit( pOther->_harvester ) == nullptr )
                    {
                        order._targetUnit = otherId;
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
        const RTSUnitDef* pDef = _pCatalog->findUnit( order._buildId );
        if ( pDef == nullptr || evaluateCost( worker._owner, *pDef ) != RTSCommandResult::Ok || canPlaceBuilding( order._buildId, order._buildCell, worker._id ) == false )
        {
            finishOrder( worker );
            return;
        }
        const RTSUnitId buildingId = spawnUnit( order._buildId, worker._owner, _grid.computeCellCenter( order._buildCell ) );
        RTSUnit*        pBuilding  = findUnitMutable( buildingId );
        if ( pBuilding == nullptr )
        {
            finishOrder( worker );
            return;
        }
        payCost( _listPlayer[static_cast<size_t>( worker._owner )], *pDef );
        pBuilding->_buildProgress = 0.0f;
        pBuilding->_hp            = pDef->_hp * _settings._constructionStartRatio;
        pBuilding->_builder       = worker._id;
        order._targetUnit         = buildingId;
        nudgeToWalkable( worker );
    }

    void RTSWorld::updateBuild( RTSUnit& unit, RTSOrder& order, float32 deltaTime )
    {
        if ( order._targetUnit.isValid() == false )
        {
            const RTSUnitDef* pDef = _pCatalog->findUnit( order._buildId );
            if ( pDef == nullptr )
            {
                finishOrder( unit );
                return;
            }
            // 자리 사각형의 가장자리까지.
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
            finishOrder( unit ); // 다른 일꾼이 짓는다
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
            pBuilding->_builder = RTSUnitId{};
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
        const RTSUnitDef* pDef = _pCatalog->findUnit( building._listProduction.front() );
        if ( pDef == nullptr )
        {
            building._listProduction.pop_front();
            return;
        }
        RTSPlayer& player = _listPlayer[static_cast<size_t>( building._owner )];
        if ( building._productionTimer <= 0.0f && building._supplyReserved == 0 && pDef->_supplyCost > 0 )
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

    bool RTSWorld::findSpawnPosition( const RTSUnit& building, float3& outPosition ) const
    {
        const float3 toward    = building._bHasRally ? building._rallyPoint : float3{ building._position._x, 0.0f, building._position._z - 10.0f };
        const int32  footprint = building._pDef->_footprint;
        for ( int32 ring = 1; ring <= 4; ++ring )
        {
            bool    bFound       = false;
            float32 bestDistance = MathUtil::kMaxFloat;
            for ( int32 y = building._cell._y - ring; y < building._cell._y + footprint + ring; ++y )
            {
                for ( int32 x = building._cell._x - ring; x < building._cell._x + footprint + ring; ++x )
                {
                    const bool bEdge = x == building._cell._x - ring || y == building._cell._y - ring || x == building._cell._x + footprint + ring - 1 ||
                                       y == building._cell._y + footprint + ring - 1;
                    if ( bEdge == false || _grid.isWalkable( x, y ) == false )
                        continue;
                    const float3  center   = _grid.computeCellCenter( int2{ x, y } );
                    const float32 distance = RTSWorldInternal::computeFlatDistance( center, toward );
                    if ( distance < bestDistance )
                    {
                        bestDistance = distance;
                        outPosition  = center;
                        bFound       = true;
                    }
                }
            }
            if ( bFound )
                return true;
        }
        return false;
    }

    void RTSWorld::completeProduction( RTSUnit& building )
    {
        const hashed_string defId = building._listProduction.front();
        building._listProduction.pop_front();
        building._productionTimer = 0.0f;
        building._supplyReserved  = 0;
        float3 position{};
        if ( findSpawnPosition( building, position ) == false )
            position = building._position;
        const RTSUnitDef* pDef = _pCatalog->findUnit( defId );
        if ( pDef != nullptr && pDef->_bAir )
            position = float3{ building._position._x, 0.0f, building._position._z };
        const RTSUnitId unitId = spawnUnit( defId, building._owner, position );
        pushEvent( RTSEvent::Kind::ProductionComplete, building._owner, unitId, defId );
        if ( building._bHasRally == SW_FALSE || unitId.isValid() == false )
            return;
        const RTSUnitId rallyTarget = pickUnit( building._rallyPoint );
        (void)issueSmart( unitId, building._rallyPoint, rallyTarget, false );
    }

    // ------------------------------------------------------------------------------
    // 전투
    // ------------------------------------------------------------------------------
    bool RTSWorld::areEnemies( int32 playerA, int32 playerB ) const
    {
        const RTSPlayer* pA = findPlayer( playerA );
        const RTSPlayer* pB = findPlayer( playerB );
        return pA != nullptr && pB != nullptr && TeamAttitudeUtil::isHostile( pA->_team, pB->_team );
    }

    bool RTSWorld::canAttack( const RTSUnit& attacker, const RTSUnit& target ) const
    {
        if ( attacker._pDef->canAttack() == false || target._bAlive == SW_FALSE || target.isResource() || areEnemies( attacker._owner, target._owner ) == false )
            return false;
        return target._pDef->_bAir ? attacker._pDef->_bTargetsAir != SW_FALSE : attacker._pDef->_bTargetsGround != SW_FALSE;
    }

    float32 RTSWorld::computeRectDistance( const float3& position, const int2& cell, int32 footprint ) const
    {
        const float32 cellSize = _grid.getCellSize();
        const float32 minX     = _grid.getOrigin()._x + static_cast<float32>( cell._x ) * cellSize;
        const float32 minZ     = _grid.getOrigin()._z + static_cast<float32>( cell._y ) * cellSize;
        const float32 size     = static_cast<float32>( footprint ) * cellSize;
        const float32 dx       = MathUtil::max( 0.0f, MathUtil::max( minX - position._x, position._x - ( minX + size ) ) );
        const float32 dz       = MathUtil::max( 0.0f, MathUtil::max( minZ - position._z, position._z - ( minZ + size ) ) );
        return MathUtil::sqrt( dx * dx + dz * dz );
    }

    float32 RTSWorld::computeEdgeDistance( const RTSUnit& unit, const RTSUnit& target ) const
    {
        if ( target.isMobile() == false )
            return computeRectDistance( unit._position, target._cell, target._pDef->_footprint ) - ( unit.isMobile() ? unit._pDef->_radius : 0.0f );
        if ( unit.isMobile() == false )
            return computeRectDistance( target._position, unit._cell, unit._pDef->_footprint ) - target._pDef->_radius;
        return RTSWorldInternal::computeFlatDistance( unit._position, target._position ) - unit._pDef->_radius - target._pDef->_radius;
    }

    bool RTSWorld::isWithinReach( const RTSUnit& unit, const RTSUnit& target, float32 reach ) const { return computeEdgeDistance( unit, target ) <= reach; }

    RTSUnitId RTSWorld::findAutoTarget( const RTSUnit& unit, float32 radius ) const
    {
        vector<RTSUnitId> listCandidate;
        queryUnits( unit._position, radius + ( unit.isMobile() ? unit._pDef->_radius : 0.0f ), listCandidate );
        RTSUnitId bestId{};
        float32   bestScore = MathUtil::kMaxFloat;
        for ( const RTSUnitId candidateId : listCandidate )
        {
            const RTSUnit* pCandidate = findUnit( candidateId );
            if ( pCandidate == nullptr || canAttack( unit, *pCandidate ) == false )
                continue;
            const float32 distance = computeEdgeDistance( unit, *pCandidate );
            if ( distance > radius )
                continue;
            // 싸울 수 있는 유닛 → 그 밖의 유닛 → 건물 순(스타크래프트의 자동 목표 우선순위).
            const float32 priority = pCandidate->_pDef->canAttack() && pCandidate->isMobile() ? 0.0f : ( pCandidate->isMobile() ? 100.0f : 200.0f );
            if ( priority + distance < bestScore )
            {
                bestScore = priority + distance;
                bestId    = candidateId;
            }
        }
        return bestId;
    }

    void RTSWorld::updateCombat( RTSUnit& unit, float32 deltaTime )
    {
        (void)deltaTime;
        if ( unit._attackTarget.isValid() == false )
            return;
        RTSUnit* pTarget = findUnitMutable( unit._attackTarget );
        if ( pTarget == nullptr || canAttack( unit, *pTarget ) == false )
        {
            unit._attackTarget = RTSUnitId{};
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
                // 늦음을 이어 공격 빈도가 고정 걸음 격자에 맞춰 내려가지 않게 한다(1.2 초가 25 걸음 = 1.25 초가 되지 않게).
                unit._cooldown.restart( unit._pDef->_cooldown );
                dealDamage( unit, *pTarget );
            }
            return;
        }
        const bool bOrdered = pOrder != nullptr && pOrder->_type == RTSOrderType::Attack;
        if ( unit.isMobile() == false || bHold || ( bOrdered == false && distance > unit._pDef->_sight * 1.5f ) )
        {
            unit._attackTarget = RTSUnitId{}; // 놓쳤다(자동 목표만 — 명령한 목표는 끝까지 쫓는다)
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
        // 할 일 없이 맞으면 되받아친다.
        if ( target.isMobile() && target._listOrder.empty() && target._attackTarget.isValid() == false && canAttack( target, attacker ) )
            target._attackTarget = attacker._id;
        if ( target._hp <= 0.0f )
            killUnit( target, attacker._id );
    }

    void RTSWorld::killUnit( RTSUnit& unit, RTSUnitId killerId )
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
            placeFootprint( unit, false ); // 정제소는 아니다 — 그 칸은 간헐천이 계속 막는다
        if ( unit._pDef->_kind == RTSUnitKind::Building )
            _land.releaseRect( unit._cell._x, unit._cell._y, unit._cell._x + unit._pDef->_footprint - 1, unit._cell._y + unit._pDef->_footprint - 1 );
        // 생산 대기열 값은 돌려주지 않는다(부서진 건물 — 스타크래프트와 같다).
        unit._bAlive = SW_FALSE;
        pushEvent( RTSEvent::Kind::UnitDied, unit._owner, unit._id, unit._pDef->_id, 0, killerId );
    }

    // ------------------------------------------------------------------------------
    // 시야 · 승패
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

    RTSVisibility RTSWorld::getVisibility( int32 player, const int2& cell ) const
    {
        const RTSPlayer* pPlayer = findPlayer( player );
        if ( pPlayer == nullptr || _grid.isInside( cell ) == false )
            return RTSVisibility::Unexplored;
        return static_cast<RTSVisibility>( _listTeamVisibility[static_cast<size_t>( pPlayer->_team )][static_cast<size_t>( _grid.computeIndex( cell ) )] );
    }

    bool RTSWorld::isVisibleTo( int32 player, RTSUnitId unitId ) const
    {
        const RTSUnit*   pUnit   = findUnit( unitId );
        const RTSPlayer* pPlayer = findPlayer( player );
        if ( pUnit == nullptr || pPlayer == nullptr )
            return false;
        if ( pUnit->_owner != kNoOwner && TeamAttitudeUtil::isFriendly( _listPlayer[static_cast<size_t>( pUnit->_owner )]._team, pPlayer->_team ) )
            return true;
        if ( pUnit->isMobile() )
            return getVisibility( player, _grid.computeCell( pUnit->_position ) ) == RTSVisibility::Visible;
        for ( int32 y = pUnit->_cell._y; y < pUnit->_cell._y + pUnit->_pDef->_footprint; ++y )
        {
            for ( int32 x = pUnit->_cell._x; x < pUnit->_cell._x + pUnit->_pDef->_footprint; ++x )
            {
                if ( getVisibility( player, int2{ x, y } ) == RTSVisibility::Visible )
                    return true;
            }
        }
        return false;
    }

    void RTSWorld::updateDefeat()
    {
        if ( _winningTeam >= 0 )
            return;
        vector<int32> listBuildingCount( _listPlayer.size(), 0 );
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive && unit._owner != kNoOwner && unit.isBuilding() )
                ++listBuildingCount[static_cast<size_t>( unit._owner )];
        }
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            RTSPlayer& player = _listPlayer[index];
            if ( player._bDefeated || player._bHadBuilding == SW_FALSE || listBuildingCount[index] > 0 )
                continue;
            player._bDefeated = SW_TRUE;
            pushEvent( RTSEvent::Kind::PlayerDefeated, static_cast<int32>( index ), RTSUnitId{}, hashed_string{} );
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
            pushEvent( RTSEvent::Kind::GameOver, aliveTeam, RTSUnitId{}, hashed_string{} );
        }
    }

    // ------------------------------------------------------------------------------
    // 조회 · 배치
    // ------------------------------------------------------------------------------
    int32 RTSWorld::countUnits( int32 player, const hashed_string& defId, bool bIncludeUnfinished ) const
    {
        int32 count = 0;
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive && unit._owner == player && unit._pDef->_id == defId && ( bIncludeUnfinished || unit.isConstructed() ) )
                ++count;
        }
        return count;
    }

    bool RTSWorld::hasConstructed( int32 player, const hashed_string& defId ) const { return countUnits( player, defId, false ) > 0; }

    int32 RTSWorld::countPlanned( int32 player, const hashed_string& defId ) const
    {
        int32 count = 0;
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner != player )
                continue;
            if ( unit._pDef->_id == defId )
                ++count;
            for ( const hashed_string& queued : unit._listProduction )
            {
                count += queued == defId ? 1 : 0;
            }
            for ( const RTSOrder& order : unit._listOrder )
            {
                count += order._type == RTSOrderType::Build && order._buildId == defId && order._targetUnit.isValid() == false ? 1 : 0;
            }
        }
        return count;
    }

    bool RTSWorld::canPlaceBuilding( const hashed_string& buildingId, const int2& cell, RTSUnitId ignoreUnit ) const
    {
        const RTSUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( buildingId ) : nullptr;
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
                    return false; // 남의 땅 — 막히지 않은 땅(도로)도 짓지 못한다
            }
        }
        if ( pDef->_bExtractor )
        {
            // 같은 칸 · 같은 크기의 빈 가스 간헐천 위만.
            for ( const RTSUnit& unit : _listUnit )
            {
                if ( unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != RTSResourceType::Gas || unit._cell != cell ||
                     unit._pDef->_footprint != footprint )
                    continue;
                for ( const RTSUnit& other : _listUnit )
                {
                    if ( other._bAlive && other._pDef->_bExtractor && other._linkedResource == unit._id )
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
            if ( unit._bAlive && unit.isMobile() && unit._id != ignoreUnit && RTSWorldInternal::isGround( unit ) &&
                 computeRectDistance( unit._position, cell, footprint ) < unit._pDef->_radius * 0.9f )
                return false;
        }
        return true;
    }

    bool RTSWorld::findBuildSite( const hashed_string& buildingId, const float3& nearPosition, int32 minRadius, int32 maxRadius, int2& outCell ) const
    {
        const RTSUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( buildingId ) : nullptr;
        if ( pDef == nullptr || pDef->_kind != RTSUnitKind::Building )
            return false;
        if ( pDef->_bExtractor )
        {
            float32 bestDistance = static_cast<float32>( maxRadius ) + 1.0f;
            bool    bFound       = false;
            for ( const RTSUnit& unit : _listUnit )
            {
                if ( unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != RTSResourceType::Gas )
                    continue;
                const float32 distance = RTSWorldInternal::computeFlatDistance( nearPosition, unit._position );
                if ( distance < bestDistance && canPlaceBuilding( buildingId, unit._cell ) )
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
                    if ( canPlaceBuilding( buildingId, cell ) == false )
                        continue;
                    // 둘레 한 칸은 비운다 — 길을 막지 않게.
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

    RTSUnitId RTSWorld::findNearestResource( const float3& position, RTSResourceType type, float32 maxDistance ) const
    {
        RTSUnitId bestId{};
        float32   bestDistance = maxDistance;
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != type || unit._resourceLeft <= 0 )
                continue;
            const float32 distance = RTSWorldInternal::computeFlatDistance( position, unit._position );
            if ( distance <= bestDistance )
            {
                bestDistance = distance;
                bestId       = unit._id;
            }
        }
        return bestId;
    }

    RTSUnitId RTSWorld::findNearestDepot( int32 player, const float3& position ) const
    {
        RTSUnitId bestId{};
        float32   bestDistance = MathUtil::kMaxFloat;
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner != player || unit._pDef->_bDepot == SW_FALSE || unit.isConstructed() == false )
                continue;
            const float32 distance = RTSWorldInternal::computeFlatDistance( position, unit._position );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                bestId       = unit._id;
            }
        }
        return bestId;
    }

    // ------------------------------------------------------------------------------
    // 흐름장 · 알림
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

    void RTSWorld::pushEvent( RTSEvent::Kind kind, int32 player, RTSUnitId unitId, const hashed_string& defId, int32 value, RTSUnitId otherId )
    {
        RTSEvent event;
        event._kind   = kind;
        event._player = player;
        event._unit   = unitId;
        event._defId  = defId;
        event._value  = value;
        event._other  = otherId;
        if ( const RTSUnit* pUnit = findUnit( unitId ) )
            event._position = pUnit->_position;
        else if ( unitId.isValid() && unitId.index() < _listUnit.size() )
            event._position = _listUnit[unitId.index()]._position;
        _eventBuffer.push( event );
    }
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 상태 쓰기 · 읽기(핫 리로드 · 세이브)
    // ------------------------------------------------------------------------------
    void RTSWorld::writeState( Archive& outArchive ) const
    {
        outArchive << _grid.getWidth();
        outArchive << _grid.getHeight();
        outArchive << _listTerrainBlocked;
        outArchive << static_cast<uint32>( _listUnit.size() );
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            const RTSUnit& unit = _listUnit[index];
            outArchive << _listGeneration[index];
            StateArchiveUtil::writeName( outArchive, unit._pDef != nullptr ? unit._pDef->_id : hashed_string{} );
            outArchive << static_cast<uint32>( unit._listOrder.size() );
            for ( const RTSOrder& order : unit._listOrder )
            {
                StateArchiveUtil::writeName( outArchive, order._buildId );
                outArchive << order._target;
                StateArchiveUtil::writeInt2( outArchive, order._buildCell );
                outArchive << order._targetUnit.packed();
                outArchive << order._arriveRadius;
                outArchive << static_cast<uint8>( order._type );
                outArchive << static_cast<uint8>( order._pFlowField != nullptr ? SW_TRUE : SW_FALSE );
            }
            outArchive << static_cast<uint32>( unit._listProduction.size() );
            for ( const hashed_string& productionId : unit._listProduction )
            {
                StateArchiveUtil::writeName( outArchive, productionId );
            }
            outArchive << unit._position;
            outArchive << unit._rallyPoint;
            outArchive << unit._moveGoal;
            StateArchiveUtil::writeInt2( outArchive, unit._cell );
            outArchive << unit._id.packed();
            outArchive << unit._attackTarget.packed();
            outArchive << unit._gatherTarget.packed();
            outArchive << unit._harvester.packed();
            outArchive << unit._builder.packed();
            outArchive << unit._linkedResource.packed();
            outArchive << unit._hp;
            outArchive << unit._cooldown._remaining;
            outArchive << unit._buildProgress;
            outArchive << unit._productionTimer;
            outArchive << unit._gatherTimer;
            outArchive << unit._owner;
            outArchive << unit._resourceLeft;
            outArchive << unit._cargoAmount;
            outArchive << unit._supplyReserved;
            outArchive << static_cast<uint8>( unit._cargoType );
            outArchive << static_cast<uint8>( unit._gatherPhase );
            outArchive << unit._bAlive;
            outArchive << unit._bHasRally;
            outArchive << unit._bSupplyBlocked;
        }
        outArchive << static_cast<uint32>( _listFreeSlot.size() );
        for ( const uint32 slot : _listFreeSlot )
        {
            outArchive << slot;
        }
        outArchive << static_cast<uint32>( _listPlayer.size() );
        for ( const RTSPlayer& player : _listPlayer )
        {
            outArchive << player._startPosition;
            outArchive << player._supplyUsed;
            outArchive << player._supplyCap;
            outArchive << player._team;
            outArchive << player._nextUnderAttackTime;
            outArchive << player._bDefeated;
            outArchive << player._bHadBuilding;
        }
        outArchive << static_cast<uint32>( _listTeamVisibility.size() );
        for ( const vector<uint8>& listVisibility : _listTeamVisibility )
        {
            outArchive << listVisibility;
        }
        StateArchiveUtil::writeStepTimer( outArchive, _stepTimer );
        outArchive << _time;
        outArchive << _visionTimer._remaining;
        outArchive << _teamCount;
        outArchive << _winningTeam;
    }

    bool RTSWorld::readState( Archive& archive )
    {
        using Internal = RTSWorldInternal;
        int32 width    = 0;
        int32 height   = 0;
        archive >> width;
        archive >> height;
        if ( archive.isError() || width != _grid.getWidth() || height != _grid.getHeight() || _pCatalog == nullptr )
            return false;
        vector<uint8> listTerrainBlocked;
        archive >> listTerrainBlocked;
        if ( archive.isError() || listTerrainBlocked.size() != _listTerrainBlocked.size() )
            return false;

        uint32 unitCount = 0;
        if ( StateArchiveUtil::readCount( archive, Internal::kMinUnitStateBytes, unitCount ) == false )
            return false;
        deque<RTSUnit> listUnit;
        vector<uint32> listGeneration( unitCount );
        vector<uint8>  listFlowFieldOrder; // 무리 이동 명령마다 흐름장이 있었는가(유닛 · 명령 순)
        for ( uint32 index = 0; index < unitCount; ++index )
        {
            RTSUnit&      unit = listUnit.emplace_back();
            hashed_string defId;
            archive >> listGeneration[index];
            if ( StateArchiveUtil::readName( archive, defId ) == false )
                return false;
            unit._pDef = defId.empty() ? nullptr : _pCatalog->findUnit( defId );
            if ( defId.empty() == false && unit._pDef == nullptr )
                return false; // 카탈로그에서 빠진 유닛 — 판을 맞출 수 없다
            uint32 orderCount = 0;
            if ( StateArchiveUtil::readCount( archive, Internal::kMinOrderStateBytes, orderCount ) == false )
                return false;
            for ( uint32 orderIndex = 0; orderIndex < orderCount; ++orderIndex )
            {
                RTSOrder& order      = unit._listOrder.emplace_back();
                uint64    targetUnit = 0;
                uint8     type       = 0;
                uint8     bFlowField = SW_FALSE;
                if ( StateArchiveUtil::readName( archive, order._buildId ) == false )
                    return false;
                archive >> order._target;
                StateArchiveUtil::readInt2( archive, order._buildCell );
                archive >> targetUnit;
                archive >> order._arriveRadius;
                archive >> type;
                archive >> bFlowField;
                if ( archive.isError() || type > static_cast<uint8>( RTSOrderType::Hold ) )
                    return false;
                order._targetUnit = RTSUnitId::fromPacked( targetUnit );
                order._type       = static_cast<RTSOrderType>( type );
                listFlowFieldOrder.push_back( bFlowField );
            }
            uint32 productionCount = 0;
            if ( StateArchiveUtil::readCount( archive, sizeof( uint32 ), productionCount ) == false )
                return false;
            for ( uint32 productionIndex = 0; productionIndex < productionCount; ++productionIndex )
            {
                hashed_string productionId;
                if ( StateArchiveUtil::readName( archive, productionId ) == false )
                    return false;
                unit._listProduction.push_back( productionId );
            }
            uint64 id             = 0;
            uint64 attackTarget   = 0;
            uint64 gatherTarget   = 0;
            uint64 harvester      = 0;
            uint64 builder        = 0;
            uint64 linkedResource = 0;
            uint8  cargoType      = 0;
            uint8  gatherPhase    = 0;
            archive >> unit._position;
            archive >> unit._rallyPoint;
            archive >> unit._moveGoal;
            StateArchiveUtil::readInt2( archive, unit._cell );
            archive >> id;
            archive >> attackTarget;
            archive >> gatherTarget;
            archive >> harvester;
            archive >> builder;
            archive >> linkedResource;
            archive >> unit._hp;
            archive >> unit._cooldown._remaining;
            archive >> unit._buildProgress;
            archive >> unit._productionTimer;
            archive >> unit._gatherTimer;
            archive >> unit._owner;
            archive >> unit._resourceLeft;
            archive >> unit._cargoAmount;
            archive >> unit._supplyReserved;
            archive >> cargoType;
            archive >> gatherPhase;
            archive >> unit._bAlive;
            archive >> unit._bHasRally;
            archive >> unit._bSupplyBlocked;
            const bool bEnumValid  = cargoType <= static_cast<uint8>( RTSResourceType::None ) && gatherPhase <= static_cast<uint8>( RTSGatherPhase::Returning );
            const bool bAliveValid = unit._bAlive == SW_FALSE || unit._pDef != nullptr;
            if ( archive.isError() || bEnumValid == false || bAliveValid == false )
                return false;
            unit._id             = RTSUnitId::fromPacked( id );
            unit._attackTarget   = RTSUnitId::fromPacked( attackTarget );
            unit._gatherTarget   = RTSUnitId::fromPacked( gatherTarget );
            unit._harvester      = RTSUnitId::fromPacked( harvester );
            unit._builder        = RTSUnitId::fromPacked( builder );
            unit._linkedResource = RTSUnitId::fromPacked( linkedResource );
            unit._cargoType      = static_cast<RTSResourceType>( cargoType );
            unit._gatherPhase    = static_cast<RTSGatherPhase>( gatherPhase );
            // 길(경로 · 흐름장)은 싣지 않는다 — 앞 명령을 처음부터 다시 걷게 해 이 격자에서 길을 다시 구한다.
            unit._bOrderStarted = SW_FALSE;
            unit._repathTimer.clear();
        }

        uint32 freeSlotCount = 0;
        if ( StateArchiveUtil::readCount( archive, sizeof( uint32 ), freeSlotCount ) == false )
            return false;
        vector<uint32> listFreeSlot( freeSlotCount );
        for ( uint32& slot : listFreeSlot )
        {
            archive >> slot;
            if ( slot >= unitCount )
                archive.setError();
        }
        uint32 playerCount = 0;
        // 지갑은 플레이어를 더한 쪽이 빌려 준 것이라 싣지 않는다 — 같은 수의 플레이어를 먼저 더한 월드에만 읽는다.
        if ( archive.isError() || StateArchiveUtil::readCount( archive, Internal::kMinPlayerStateBytes, playerCount ) == false || playerCount != _listPlayer.size() )
            return false;
        vector<RTSPlayer> listPlayer( playerCount );
        for ( uint32 playerIndex = 0; playerIndex < playerCount; ++playerIndex )
        {
            listPlayer[playerIndex]._pWallet = _listPlayer[playerIndex]._pWallet;
        }
        for ( RTSPlayer& player : listPlayer )
        {
            archive >> player._startPosition;
            archive >> player._supplyUsed;
            archive >> player._supplyCap;
            archive >> player._team;
            archive >> player._nextUnderAttackTime;
            archive >> player._bDefeated;
            archive >> player._bHadBuilding;
        }
        uint32 teamCount = 0;
        if ( archive.isError() || StateArchiveUtil::readCount( archive, sizeof( uint32 ), teamCount ) == false )
            return false;
        vector<vector<uint8>> listTeamVisibility( teamCount );
        for ( vector<uint8>& listVisibility : listTeamVisibility )
        {
            archive >> listVisibility;
            if ( listVisibility.size() != _listTerrainBlocked.size() )
                archive.setError();
        }
        FixedStepTimer stepTimer   = _stepTimer;
        float32        time        = 0.0f;
        float32        visionTimer = 0.0f;
        int32          teams       = 0;
        int32          winningTeam = -1;
        if ( archive.isError() || StateArchiveUtil::readStepTimer( archive, stepTimer ) == false )
            return false;
        archive >> time;
        archive >> visionTimer;
        archive >> teams;
        archive >> winningTeam;
        if ( archive.isError() || teams != static_cast<int32>( teamCount ) )
            return false;

        _listTerrainBlocked = std::move( listTerrainBlocked );
        _listUnit           = std::move( listUnit );
        _listGeneration     = std::move( listGeneration );
        _listFreeSlot       = std::move( listFreeSlot );
        _listPlayer         = std::move( listPlayer );
        _listTeamVisibility = std::move( listTeamVisibility );
        _eventBuffer.clear();
        _listFlowField.clear();
        _stepTimer              = stepTimer;
        _time                   = time;
        _visionTimer._remaining = visionTimer;
        _teamCount              = teams;
        _winningTeam            = winningTeam;

        // 격자는 땅 + 서 있는 건물 · 자원의 발자국으로 다시 칠한다(저장하지 않는다 — 같은 것에서 나온다).
        const float32 cellSize = _grid.getCellSize();
        const float3  origin   = _grid.getOrigin();
        _grid.initialize( width, height, cellSize, origin );
        for ( int32 y = 0; y < height; ++y )
        {
            for ( int32 x = 0; x < width; ++x )
            {
                if ( _listTerrainBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] != SW_FALSE || _land.isBlocked( x, y ) )
                    _grid.setBlocked( x, y, true );
            }
        }
        size_t flowFieldOrderIndex = 0;
        for ( RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive != SW_FALSE && unit.isMobile() == false && unit._pDef->_bExtractor == SW_FALSE )
                placeFootprint( unit, true );
            if ( unit._bAlive != SW_FALSE && unit.isMobile() )
            {
                NavAgentSettings agentSettings;
                agentSettings._radius   = unit._pDef->_radius;
                agentSettings._maxSpeed = MathUtil::max( 0.01f, unit._pDef->_speed );
                unit._agent.setSettings( agentSettings );
                unit._agent.setPosition( unit._position );
            }
            for ( RTSOrder& order : unit._listOrder )
            {
                const bool bFlowField = listFlowFieldOrder[flowFieldOrderIndex++] != SW_FALSE;
                if ( bFlowField && unit._bAlive != SW_FALSE )
                    order._pFlowField = acquireFlowField( _grid.computeCell( order._target ) );
            }
        }
        rebuildBuckets();
        return true;
    }

    int64 RTSWorld::getMinerals( int32 player ) const
    {
        const RTSPlayer* pPlayer = findPlayer( player );
        return pPlayer != nullptr && pPlayer->_pWallet != nullptr ? pPlayer->_pWallet->getBalance( _settings._mineralCurrency ) : 0;
    }

    int64 RTSWorld::getGas( int32 player ) const
    {
        const RTSPlayer* pPlayer = findPlayer( player );
        return pPlayer != nullptr && pPlayer->_pWallet != nullptr ? pPlayer->_pWallet->getBalance( _settings._gasCurrency ) : 0;
    }

    void RTSWorld::payCost( RTSPlayer& player, const RTSUnitDef& def )
    {
        // evaluateCost 가 둘 다 된다고 본 뒤에만 부른다 — 한쪽만 빠지지 않는다.
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
