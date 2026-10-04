#include "pch.h"

#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsWorld.h"

#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RtsWorldInternal
        {
            static constexpr float32 kMaxUnitExtent = 4.5f; ///< 버킷 조회에 더하는 몸 크기 상한(건물 반 변)

            static float32 computeFlatDistance( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dz = lhs._z - rhs._z;
                return MathUtil::sqrt( dx * dx + dz * dz );
            }

            static bool isGround( const RtsUnit& unit ) { return unit._pDef->_bAir == SW_FALSE; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( RtsCommandResult result )
    {
        switch ( result )
        {
            case RtsCommandResult::Ok:
                return "Ok";
            case RtsCommandResult::InvalidUnit:
                return "InvalidUnit";
            case RtsCommandResult::NotOwner:
                return "NotOwner";
            case RtsCommandResult::CannotDo:
                return "CannotDo";
            case RtsCommandResult::NotEnoughMinerals:
                return "NotEnoughMinerals";
            case RtsCommandResult::NotEnoughGas:
                return "NotEnoughGas";
            case RtsCommandResult::NotEnoughSupply:
                return "NotEnoughSupply";
            case RtsCommandResult::TechRequired:
                return "TechRequired";
            case RtsCommandResult::QueueFull:
                return "QueueFull";
            case RtsCommandResult::InvalidPlacement:
                return "InvalidPlacement";
        }
        return "Unknown";
    }

    RtsWorld::RtsWorld()
        : _grid{}
        , _airGrid{}
        , _pathfinder{}
        , _listUnit{}
        , _listGeneration{}
        , _listFreeSlot{}
        , _listPlayer{}
        , _listEvent{}
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
        , _visionTimer{ 0.0f }
        , _bucketWidth{ 0 }
        , _bucketHeight{ 0 }
        , _teamCount{ 0 }
        , _winningTeam{ -1 }
    {
    }

    void RtsWorld::initialize( const RtsCatalog* pCatalog, int32 width, int32 height, const RtsSettings& settings )
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
        _listEvent.clear();
        _listTerrainBlocked.assign( static_cast<size_t>( _grid.getWidth() * _grid.getHeight() ), SW_FALSE );
        _listTeamVisibility.clear();
        _listFlowField.clear();
        _bucketWidth  = ( _grid.getWidth() + _settings._bucketSize - 1 ) / _settings._bucketSize;
        _bucketHeight = ( _grid.getHeight() + _settings._bucketSize - 1 ) / _settings._bucketSize;
        _listBucketHead.assign( static_cast<size_t>( _bucketWidth * _bucketHeight ), -1 );
        _listBucketNext.clear();
        _stepTimer   = FixedStepTimer{ _settings._fixedStep, 0.25f };
        _time        = 0.0f;
        _visionTimer = 0.0f;
        _teamCount   = 0;
        _winningTeam = -1;
    }

    void RtsWorld::setTerrainBlocked( int32 x, int32 y, bool bBlocked )
    {
        if ( _grid.isInside( x, y ) == false )
            return;
        _listTerrainBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] = bBlocked ? SW_TRUE : SW_FALSE;
        _grid.setBlocked( x, y, bBlocked );
    }

    int32 RtsWorld::addPlayer( int32 team, int32 minerals, int32 gas, const float3& startPosition )
    {
        RtsPlayer player;
        player._team          = MathUtil::max( 0, team );
        player._minerals      = minerals;
        player._gas           = gas;
        player._startPosition = startPosition;
        _listPlayer.push_back( player );
        _teamCount = MathUtil::max( _teamCount, player._team + 1 );
        while ( static_cast<int32>( _listTeamVisibility.size() ) < _teamCount )
            _listTeamVisibility.emplace_back( static_cast<size_t>( _grid.getWidth() * _grid.getHeight() ), static_cast<uint8>( RtsVisibility::Unexplored ) );
        return static_cast<int32>( _listPlayer.size() ) - 1;
    }

    // ------------------------------------------------------------------------------
    // 유닛 자리
    // ------------------------------------------------------------------------------
    RtsUnitId RtsWorld::allocateUnit()
    {
        if ( _listFreeSlot.empty() == false )
        {
            const uint32 index = _listFreeSlot.back();
            _listFreeSlot.pop_back();
            _listUnit[index] = RtsUnit{};
            return RtsUnitId::make( index, _listGeneration[index] );
        }
        const uint32 index = static_cast<uint32>( _listUnit.size() );
        _listUnit.emplace_back();
        _listGeneration.push_back( 1u );
        return RtsUnitId::make( index, 1u );
    }

    void RtsWorld::freeDeadUnits()
    {
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            RtsUnit& unit = _listUnit[index];
            if ( unit._bAlive || unit._id.isValid() == false )
                continue;
            for ( RtsOrder& order : unit._listOrder )
                releaseOrder( unit, order );
            unit._listOrder.clear();
            unit._id = RtsUnitId{};
            ++_listGeneration[index];
            if ( _listGeneration[index] == 0 )
                _listGeneration[index] = 1;
            _listFreeSlot.push_back( static_cast<uint32>( index ) );
        }
    }

    const RtsUnit* RtsWorld::findUnit( RtsUnitId unitId ) const
    {
        if ( unitId.isValid() == false || unitId.index() >= _listUnit.size() )
            return nullptr;
        const RtsUnit& unit = _listUnit[unitId.index()];
        return unit._bAlive && unit._id == unitId ? &unit : nullptr;
    }

    RtsUnit* RtsWorld::findUnitMutable( RtsUnitId unitId ) { return const_cast<RtsUnit*>( findUnit( unitId ) ); }

    const RtsPlayer* RtsWorld::findPlayer( int32 player ) const
    {
        return player >= 0 && player < static_cast<int32>( _listPlayer.size() ) ? &_listPlayer[static_cast<size_t>( player )] : nullptr;
    }

    float3 RtsWorld::computeFootprintCenter( const int2& cell, int32 footprint ) const
    {
        const float32 half = static_cast<float32>( footprint ) * 0.5f * _grid.getCellSize();
        return float3{ _grid.getOrigin()._x + static_cast<float32>( cell._x ) * _grid.getCellSize() + half, 0.0f,
                       _grid.getOrigin()._z + static_cast<float32>( cell._y ) * _grid.getCellSize() + half };
    }

    void RtsWorld::placeFootprint( const RtsUnit& unit, bool bBlocked )
    {
        const int32 footprint = unit._pDef->_footprint;
        for ( int32 y = unit._cell._y; y < unit._cell._y + footprint; ++y )
        {
            for ( int32 x = unit._cell._x; x < unit._cell._x + footprint; ++x )
            {
                if ( _grid.isInside( x, y ) == false )
                    continue;
                const bool bTerrain = _listTerrainBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] != SW_FALSE;
                _grid.setBlocked( x, y, bBlocked || bTerrain );
            }
        }
    }

    RtsUnitId RtsWorld::spawnUnit( const hashed_string& defId, int32 owner, const float3& position )
    {
        const RtsUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( defId ) : nullptr;
        if ( pDef == nullptr || ( owner != kNoOwner && findPlayer( owner ) == nullptr ) )
            return RtsUnitId{};
        int2      cell = _grid.computeCell( position );
        RtsUnitId linkedResource{};
        if ( pDef->isMobile() == false )
        {
            if ( pDef->_bExtractor )
            {
                if ( canPlaceBuilding( defId, cell ) == false )
                    return RtsUnitId{};
                for ( const RtsUnit& other : _listUnit )
                {
                    if ( other._bAlive && other.isResource() && other._pDef->_resourceType == RtsResourceType::Gas && other._cell == cell )
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
                            return RtsUnitId{};
                    }
                }
            }
        }
        else if ( pDef->_bAir == SW_FALSE && _grid.isWalkable( cell ) == false && _grid.findNearestWalkable( cell, 8, cell ) == false )
        {
            return RtsUnitId{};
        }

        const RtsUnitId unitId = allocateUnit();
        RtsUnit&        unit   = _listUnit[unitId.index()];
        unit._id               = unitId;
        unit._pDef             = pDef;
        unit._owner            = pDef->_kind == RtsUnitKind::Resource ? kNoOwner : owner;
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
            if ( pDef->_kind == RtsUnitKind::Building && owner != kNoOwner )
                _listPlayer[static_cast<size_t>( owner )]._bHadBuilding = SW_TRUE;
        }
        pushEvent( RtsEvent::Kind::UnitCreated, unit._owner, unitId, pDef->_id );
        return unitId;
    }

    // ------------------------------------------------------------------------------
    // 명령
    // ------------------------------------------------------------------------------
    RtsCommandResult RtsWorld::pushOrder( RtsUnit& unit, const RtsOrder& order, bool bQueue )
    {
        if ( bQueue == false )
            clearOrders( unit );
        unit._listOrder.push_back( order );
        if ( unit._listOrder.size() == 1 )
            beginOrder( unit );
        return RtsCommandResult::Ok;
    }

    void RtsWorld::beginOrder( RtsUnit& unit )
    {
        unit._bOrderStarted = SW_FALSE;
        unit._attackTarget  = RtsUnitId{};
        unit._repathTimer   = 0.0f;
        if ( unit._listOrder.empty() )
        {
            unit._agent.stop();
            return;
        }
        if ( unit._listOrder.front()._type == RtsOrderType::Gather )
            unit._gatherPhase = unit._cargoAmount > 0 ? RtsGatherPhase::Returning : RtsGatherPhase::ToResource;
    }

    void RtsWorld::releaseOrder( RtsUnit& unit, RtsOrder& order )
    {
        if ( order._pFlowField != nullptr )
        {
            releaseFlowField( order._pFlowField );
            order._pFlowField = nullptr;
        }
        if ( order._type == RtsOrderType::Gather || order._type == RtsOrderType::Build )
        {
            RtsUnit* pTarget = findUnitMutable( order._targetUnit );
            if ( pTarget != nullptr && pTarget->_harvester == unit._id )
                pTarget->_harvester = RtsUnitId{};
            if ( pTarget != nullptr && pTarget->_builder == unit._id )
                pTarget->_builder = RtsUnitId{};
        }
    }

    void RtsWorld::finishOrder( RtsUnit& unit )
    {
        if ( unit._listOrder.empty() )
            return;
        releaseOrder( unit, unit._listOrder.front() );
        unit._listOrder.pop_front();
        beginOrder( unit );
    }

    void RtsWorld::clearOrders( RtsUnit& unit )
    {
        for ( RtsOrder& order : unit._listOrder )
            releaseOrder( unit, order );
        unit._listOrder.clear();
        beginOrder( unit );
    }

    RtsCommandResult RtsWorld::issueMove( RtsUnitId unitId, const float3& target, bool bQueue )
    {
        RtsUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RtsCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RtsCommandResult::CannotDo;
        RtsOrder order;
        order._type   = RtsOrderType::Move;
        order._target = target;
        return pushOrder( *pUnit, order, bQueue );
    }

    RtsCommandResult RtsWorld::issueAttackMove( RtsUnitId unitId, const float3& target, bool bQueue )
    {
        RtsUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RtsCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RtsCommandResult::CannotDo;
        RtsOrder order;
        order._type   = pUnit->_pDef->canAttack() ? RtsOrderType::AttackMove : RtsOrderType::Move;
        order._target = target;
        return pushOrder( *pUnit, order, bQueue );
    }

    RtsCommandResult RtsWorld::issueAttack( RtsUnitId unitId, RtsUnitId targetId, bool bQueue )
    {
        RtsUnit*       pUnit   = findUnitMutable( unitId );
        const RtsUnit* pTarget = findUnit( targetId );
        if ( pUnit == nullptr || pTarget == nullptr )
            return RtsCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false || canAttack( *pUnit, *pTarget ) == false )
            return RtsCommandResult::CannotDo;
        RtsOrder order;
        order._type       = RtsOrderType::Attack;
        order._targetUnit = targetId;
        order._target     = pTarget->_position;
        return pushOrder( *pUnit, order, bQueue );
    }

    RtsCommandResult RtsWorld::issueGather( RtsUnitId unitId, RtsUnitId resourceId, bool bQueue )
    {
        RtsUnit*       pUnit   = findUnitMutable( unitId );
        const RtsUnit* pTarget = findUnit( resourceId );
        if ( pUnit == nullptr || pTarget == nullptr )
            return RtsCommandResult::InvalidUnit;
        if ( pUnit->_pDef->_bWorker == SW_FALSE )
            return RtsCommandResult::CannotDo;
        const bool bMinerals  = pTarget->isResource() && pTarget->_pDef->_resourceType == RtsResourceType::Minerals;
        const bool bExtractor = pTarget->_pDef->_bExtractor && pTarget->_owner == pUnit->_owner && pTarget->isConstructed();
        if ( bMinerals == false && bExtractor == false )
            return RtsCommandResult::CannotDo;
        RtsOrder order;
        order._type       = RtsOrderType::Gather;
        order._targetUnit = resourceId;
        order._target     = pTarget->_position;
        return pushOrder( *pUnit, order, bQueue );
    }

    RtsCommandResult RtsWorld::evaluateCost( int32 player, const RtsUnitDef& def ) const
    {
        const RtsPlayer* pPlayer = findPlayer( player );
        if ( pPlayer == nullptr )
            return RtsCommandResult::NotOwner;
        if ( def._requires.empty() == false && hasConstructed( player, def._requires ) == false )
            return RtsCommandResult::TechRequired;
        if ( pPlayer->_minerals < def._minerals )
            return RtsCommandResult::NotEnoughMinerals;
        if ( pPlayer->_gas < def._gas )
            return RtsCommandResult::NotEnoughGas;
        return RtsCommandResult::Ok;
    }

    RtsCommandResult RtsWorld::issueBuild( RtsUnitId workerId, const hashed_string& buildingId, const int2& cell, bool bQueue )
    {
        RtsUnit* pWorker = findUnitMutable( workerId );
        if ( pWorker == nullptr )
            return RtsCommandResult::InvalidUnit;
        const RtsUnitDef* pDef = _pCatalog->findUnit( buildingId );
        if ( pDef == nullptr || pDef->_kind != RtsUnitKind::Building || pWorker->_pDef->_bWorker == SW_FALSE || pDef->_producedBy != pWorker->_pDef->_id )
            return RtsCommandResult::CannotDo;
        const RtsCommandResult costResult = evaluateCost( pWorker->_owner, *pDef );
        if ( costResult != RtsCommandResult::Ok )
            return costResult;
        if ( canPlaceBuilding( buildingId, cell, workerId ) == false )
            return RtsCommandResult::InvalidPlacement;
        RtsOrder order;
        order._type      = RtsOrderType::Build;
        order._buildId   = buildingId;
        order._buildCell = cell;
        order._target    = computeFootprintCenter( cell, pDef->_footprint );
        return pushOrder( *pWorker, order, bQueue );
    }

    RtsCommandResult RtsWorld::issueHold( RtsUnitId unitId )
    {
        RtsUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RtsCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
            return RtsCommandResult::CannotDo;
        RtsOrder order;
        order._type   = RtsOrderType::Hold;
        order._target = pUnit->_position;
        return pushOrder( *pUnit, order, false );
    }

    RtsCommandResult RtsWorld::issueStop( RtsUnitId unitId )
    {
        RtsUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RtsCommandResult::InvalidUnit;
        clearOrders( *pUnit );
        return RtsCommandResult::Ok;
    }

    RtsCommandResult RtsWorld::issueSmart( RtsUnitId unitId, const float3& position, RtsUnitId targetId, bool bQueue )
    {
        RtsUnit* pUnit = findUnitMutable( unitId );
        if ( pUnit == nullptr )
            return RtsCommandResult::InvalidUnit;
        if ( pUnit->isMobile() == false )
        {
            setRallyPoint( unitId, position );
            return RtsCommandResult::Ok;
        }
        const RtsUnit* pTarget = findUnit( targetId );
        if ( pTarget != nullptr && pTarget->_id != unitId )
        {
            if ( areEnemies( pUnit->_owner, pTarget->_owner ) && canAttack( *pUnit, *pTarget ) )
                return issueAttack( unitId, targetId, bQueue );
            if ( pUnit->_pDef->_bWorker )
            {
                if ( issueGather( unitId, targetId, bQueue ) == RtsCommandResult::Ok )
                    return RtsCommandResult::Ok;
                if ( pTarget->isBuilding() && pTarget->_owner == pUnit->_owner && pTarget->isConstructed() == false )
                {
                    RtsOrder order;
                    order._type       = RtsOrderType::Build;
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

    int32 RtsWorld::issueGroupMove( const vector<RtsUnitId>& listUnit, const float3& target, bool bAttackMove, bool bQueue )
    {
        int32 groundCount = 0;
        for ( const RtsUnitId unitId : listUnit )
        {
            const RtsUnit* pUnit = findUnit( unitId );
            if ( pUnit != nullptr && pUnit->isMobile() && RtsWorldInternal::isGround( *pUnit ) )
                ++groundCount;
        }
        const float32 arriveRadius = 0.5f + 0.5f * MathUtil::sqrt( static_cast<float32>( listUnit.size() ) );
        const bool    bUseField    = groundCount >= _settings._flowFieldGroupSize;
        const int2    goalCell     = _grid.computeCell( target );
        int32         issuedCount  = 0;
        for ( const RtsUnitId unitId : listUnit )
        {
            RtsUnit* pUnit = findUnitMutable( unitId );
            if ( pUnit == nullptr || pUnit->isMobile() == false )
                continue;
            RtsOrder order;
            order._type         = bAttackMove && pUnit->_pDef->canAttack() ? RtsOrderType::AttackMove : RtsOrderType::Move;
            order._target       = target;
            order._arriveRadius = listUnit.size() > 1 ? arriveRadius : 0.0f;
            if ( bUseField && RtsWorldInternal::isGround( *pUnit ) )
                order._pFlowField = acquireFlowField( goalCell );
            (void)pushOrder( *pUnit, order, bQueue );
            ++issuedCount;
        }
        return issuedCount;
    }

    RtsCommandResult RtsWorld::train( RtsUnitId buildingId, const hashed_string& unitId )
    {
        RtsUnit* pBuilding = findUnitMutable( buildingId );
        if ( pBuilding == nullptr )
            return RtsCommandResult::InvalidUnit;
        const RtsUnitDef* pDef = _pCatalog->findUnit( unitId );
        if ( pDef == nullptr || pDef->isMobile() == false || pBuilding->isBuilding() == false || pBuilding->isConstructed() == false ||
             pDef->_producedBy != pBuilding->_pDef->_id )
            return RtsCommandResult::CannotDo;
        if ( static_cast<int32>( pBuilding->_listProduction.size() ) >= _settings._productionQueueMax )
            return RtsCommandResult::QueueFull;
        const RtsCommandResult costResult = evaluateCost( pBuilding->_owner, *pDef );
        if ( costResult != RtsCommandResult::Ok )
            return costResult;
        RtsPlayer& player = _listPlayer[static_cast<size_t>( pBuilding->_owner )];
        player._minerals -= pDef->_minerals;
        player._gas -= pDef->_gas;
        pBuilding->_listProduction.push_back( unitId );
        return RtsCommandResult::Ok;
    }

    bool RtsWorld::cancelTrain( RtsUnitId buildingId )
    {
        RtsUnit* pBuilding = findUnitMutable( buildingId );
        if ( pBuilding == nullptr || pBuilding->_listProduction.empty() )
            return false;
        const RtsUnitDef* pDef = _pCatalog->findUnit( pBuilding->_listProduction.back() );
        if ( pBuilding->_listProduction.size() == 1 )
        {
            pBuilding->_productionTimer = 0.0f;
            pBuilding->_supplyReserved  = 0;
            pBuilding->_bSupplyBlocked  = SW_FALSE;
        }
        pBuilding->_listProduction.pop_back();
        if ( pDef != nullptr )
        {
            RtsPlayer& player = _listPlayer[static_cast<size_t>( pBuilding->_owner )];
            player._minerals += pDef->_minerals;
            player._gas += pDef->_gas;
        }
        return true;
    }

    void RtsWorld::setRallyPoint( RtsUnitId buildingId, const float3& position )
    {
        RtsUnit* pBuilding = findUnitMutable( buildingId );
        if ( pBuilding == nullptr || pBuilding->isBuilding() == false )
            return;
        pBuilding->_rallyPoint = position;
        pBuilding->_bHasRally  = SW_TRUE;
    }

    // ------------------------------------------------------------------------------
    // 시간
    // ------------------------------------------------------------------------------
    void RtsWorld::update( float32 deltaTime )
    {
        if ( _pCatalog == nullptr )
            return;
        const int32 stepCount = _stepTimer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            stepFixed( _stepTimer.getStep() );
    }

    void RtsWorld::drainEvents( vector<RtsEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    void RtsWorld::stepFixed( float32 deltaTime )
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
            RtsUnit& unit = _listUnit[index];
            if ( unit._bAlive )
                updateUnit( unit, deltaTime );
        }
        _visionTimer -= deltaTime;
        if ( _visionTimer <= 0.0f )
        {
            _visionTimer = _settings._visionInterval;
            updateVision();
        }
        updateDefeat();
        freeDeadUnits();
    }

    void RtsWorld::recomputeSupply()
    {
        for ( RtsPlayer& player : _listPlayer )
        {
            player._supplyUsed = 0;
            player._supplyCap  = 0;
        }
        for ( const RtsUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner == kNoOwner )
                continue;
            RtsPlayer& player = _listPlayer[static_cast<size_t>( unit._owner )];
            if ( unit.isConstructed() )
                player._supplyCap += unit._pDef->_supplyProvided;
            player._supplyUsed += unit._pDef->_supplyCost + unit._supplyReserved;
        }
        for ( RtsPlayer& player : _listPlayer )
            player._supplyCap = MathUtil::min( player._supplyCap, _pCatalog->getSupplyMax() );
    }

    int32 RtsWorld::computeBucketIndex( const float3& position ) const
    {
        const int2  cell    = _grid.computeCell( position );
        const int32 bucketX = MathUtil::clamp( cell._x / _settings._bucketSize, 0, _bucketWidth - 1 );
        const int32 bucketY = MathUtil::clamp( cell._y / _settings._bucketSize, 0, _bucketHeight - 1 );
        return bucketY * _bucketWidth + bucketX;
    }

    void RtsWorld::rebuildBuckets()
    {
        std::fill( _listBucketHead.begin(), _listBucketHead.end(), -1 );
        _listBucketNext.assign( _listUnit.size(), -1 );
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            const RtsUnit& unit = _listUnit[index];
            if ( unit._bAlive == SW_FALSE )
                continue;
            const int32 bucket                             = computeBucketIndex( unit._position );
            _listBucketNext[index]                         = _listBucketHead[static_cast<size_t>( bucket )];
            _listBucketHead[static_cast<size_t>( bucket )] = static_cast<int32>( index );
        }
    }

    void RtsWorld::queryUnits( const float3& center, float32 radius, vector<RtsUnitId>& outListUnit ) const
    {
        outListUnit.clear();
        if ( _listBucketHead.empty() )
            return;
        const float32 reach      = radius + RtsWorldInternal::kMaxUnitExtent;
        const float32 cellSize   = _grid.getCellSize() * static_cast<float32>( _settings._bucketSize );
        const int32   minBucketX = MathUtil::clamp( static_cast<int32>( ( center._x - reach - _grid.getOrigin()._x ) / cellSize ), 0, _bucketWidth - 1 );
        const int32   maxBucketX = MathUtil::clamp( static_cast<int32>( ( center._x + reach - _grid.getOrigin()._x ) / cellSize ), 0, _bucketWidth - 1 );
        const int32   minBucketY = MathUtil::clamp( static_cast<int32>( ( center._z - reach - _grid.getOrigin()._z ) / cellSize ), 0, _bucketHeight - 1 );
        const int32   maxBucketY = MathUtil::clamp( static_cast<int32>( ( center._z + reach - _grid.getOrigin()._z ) / cellSize ), 0, _bucketHeight - 1 );
        for ( int32 bucketY = minBucketY; bucketY <= maxBucketY; ++bucketY )
        {
            for ( int32 bucketX = minBucketX; bucketX <= maxBucketX; ++bucketX )
            {
                for ( int32 index = _listBucketHead[static_cast<size_t>( bucketY * _bucketWidth + bucketX )]; index >= 0;
                      index       = _listBucketNext[static_cast<size_t>( index )] )
                {
                    const RtsUnit& unit = _listUnit[static_cast<size_t>( index )];
                    if ( unit._bAlive == SW_FALSE )
                        continue;
                    const float32 distance = unit.isMobile() ? RtsWorldInternal::computeFlatDistance( center, unit._position ) - unit._pDef->_radius
                                                             : computeRectDistance( center, unit._cell, unit._pDef->_footprint );
                    if ( distance <= radius )
                        outListUnit.push_back( unit._id );
                }
            }
        }
    }

    RtsUnitId RtsWorld::pickUnit( const float3& position ) const
    {
        RtsUnitId bestId{};
        float32   bestDistance = MathUtil::MaxFloat;
        for ( const RtsUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE )
                continue;
            float32 distance = unit.isMobile() ? RtsWorldInternal::computeFlatDistance( position, unit._position ) - unit._pDef->_radius
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
    void RtsWorld::updateUnit( RtsUnit& unit, float32 deltaTime )
    {
        unit._cooldown    = MathUtil::max( 0.0f, unit._cooldown - deltaTime );
        unit._repathTimer = MathUtil::max( 0.0f, unit._repathTimer - deltaTime );
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

    void RtsWorld::approach( RtsUnit& unit, const float3& target, bool bForce )
    {
        if ( RtsWorldInternal::isGround( unit ) == false )
        {
            if ( bForce || unit._agent.isMoving() == false || RtsWorldInternal::computeFlatDistance( unit._moveGoal, target ) > 0.25f )
            {
                unit._moveGoal = target;
                unit._agent.followPath( vector<float3>{
                    float3{ target._x, 0.0f, target._z }
                } );
            }
            return;
        }
        const bool bGoalMoved = RtsWorldInternal::computeFlatDistance( unit._moveGoal, target ) > 1.0f;
        if ( bForce == false && unit._repathTimer > 0.0f )
            return;
        if ( bForce == false && unit._agent.isMoving() && bGoalMoved == false )
            return;
        unit._moveGoal    = target;
        unit._repathTimer = _settings._repathInterval;
        if ( unit._agent.moveTo( _grid, _pathfinder, target ) == false )
            nudgeToWalkable( unit );
    }

    void RtsWorld::nudgeToWalkable( RtsUnit& unit )
    {
        const int2 cell = _grid.computeCell( unit._position );
        int2       freeCell{};
        if ( _grid.isWalkable( cell ) || _grid.findNearestWalkable( cell, 6, freeCell ) == false )
            return;
        unit._position = _grid.computeCellCenter( freeCell );
        unit._agent.setPosition( unit._position );
    }

    void RtsWorld::updateMovement( RtsUnit& unit, float32 deltaTime )
    {
        const bool bGround = RtsWorldInternal::isGround( unit );
        _listNeighborScratch.clear();
        // 채취하는 일꾼은 서로 밀지 않는다(광물 앞에 모여도 된다 — 스타크래프트도 같다).
        const RtsOrder* pOrder = unit.findOrder();
        if ( pOrder == nullptr || pOrder->_type != RtsOrderType::Gather )
        {
            queryUnits( unit._position, unit._pDef->_radius * 2.0f, _listQueryScratch );
            for ( const RtsUnitId otherId : _listQueryScratch )
            {
                const RtsUnit* pOther = findUnit( otherId );
                if ( pOther == nullptr || pOther == &unit || pOther->isMobile() == false || RtsWorldInternal::isGround( *pOther ) != bGround )
                    continue;
                const RtsOrder* pOtherOrder = pOther->findOrder();
                if ( pOtherOrder != nullptr && pOtherOrder->_type == RtsOrderType::Gather )
                    continue;
                _listNeighborScratch.push_back( pOther->_position );
            }
        }
        unit._agent.update( bGround ? _grid : _airGrid, _listNeighborScratch, deltaTime );
        unit._position = unit._agent.getPosition();
    }

    void RtsWorld::updateOrder( RtsUnit& unit, float32 deltaTime )
    {
        const bool bAutoAcquire = unit._pDef->canAttack() && unit._pDef->_bWorker == SW_FALSE;
        if ( unit._listOrder.empty() )
        {
            if ( bAutoAcquire && findUnit( unit._attackTarget ) == nullptr )
                unit._attackTarget = findAutoTarget( unit, unit._pDef->_sight );
            return;
        }
        RtsOrder& order = unit._listOrder.front();
        switch ( order._type )
        {
            case RtsOrderType::Move:
            case RtsOrderType::AttackMove:
            {
                if ( order._type == RtsOrderType::AttackMove )
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
                const float32 distance = RtsWorldInternal::computeFlatDistance( unit._position, order._target );
                const bool    bClose   = order._arriveRadius > 0.0f && distance <= order._arriveRadius;
                if ( bClose || unit._agent.isMoving() == false )
                    finishOrder( unit );
                return;
            }
            case RtsOrderType::Attack:
            {
                if ( findUnit( order._targetUnit ) == nullptr )
                {
                    finishOrder( unit );
                    return;
                }
                unit._attackTarget = order._targetUnit;
                return;
            }
            case RtsOrderType::Hold:
            {
                if ( unit._agent.isMoving() )
                    unit._agent.stop();
                const RtsUnit* pTarget = findUnit( unit._attackTarget );
                if ( unit._pDef->canAttack() && ( pTarget == nullptr || computeEdgeDistance( unit, *pTarget ) > unit._pDef->_range ) )
                    unit._attackTarget = findAutoTarget( unit, unit._pDef->_range );
                return;
            }
            case RtsOrderType::Gather:
            {
                updateGather( unit, order, deltaTime );
                return;
            }
            case RtsOrderType::Build:
            {
                updateBuild( unit, order, deltaTime );
                return;
            }
        }
    }

    // ------------------------------------------------------------------------------
    // 채취
    // ------------------------------------------------------------------------------
    void RtsWorld::updateGather( RtsUnit& unit, RtsOrder& order, float32 deltaTime )
    {
        if ( unit._gatherPhase == RtsGatherPhase::Returning )
        {
            const RtsUnit* pDepot = findUnit( findNearestDepot( unit._owner, unit._position ) );
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
            RtsPlayer& player = _listPlayer[static_cast<size_t>( unit._owner )];
            if ( unit._cargoType == RtsResourceType::Gas )
                player._gas += unit._cargoAmount;
            else
                player._minerals += unit._cargoAmount;
            pushEvent( RtsEvent::Kind::ResourcesDeposited, unit._owner, unit._id, unit._pDef->_id, unit._cargoAmount );
            unit._cargoAmount = 0;
            unit._cargoType   = RtsResourceType::None;
            unit._gatherPhase = RtsGatherPhase::ToResource;
            unit._repathTimer = 0.0f;
            return;
        }

        RtsUnit* pTarget = findUnitMutable( order._targetUnit );
        if ( pTarget == nullptr )
        {
            // 다 캔 광물 — 둘레의 다른 광물로(정제소가 부서졌으면 끝).
            const RtsUnitId nextId = findNearestResource( order._target, RtsResourceType::Minerals, _settings._resourceSearchRadius );
            if ( nextId.isValid() == false )
            {
                finishOrder( unit );
                return;
            }
            order._targetUnit = nextId;
            order._target     = findUnit( nextId )->_position;
            unit._gatherPhase = RtsGatherPhase::ToResource;
            return;
        }
        const bool bExtractor = pTarget->_pDef->_bExtractor != SW_FALSE;
        RtsUnit*   pSource    = bExtractor ? findUnitMutable( pTarget->_linkedResource ) : pTarget;
        if ( pSource == nullptr || pSource->_resourceLeft <= 0 )
        {
            finishOrder( unit );
            return;
        }

        if ( unit._gatherPhase == RtsGatherPhase::Harvesting )
        {
            unit._gatherTimer += deltaTime;
            if ( unit._gatherTimer < unit._pDef->_gatherTime )
                return;
            const int32 amount = MathUtil::min( unit._pDef->_cargo, pSource->_resourceLeft );
            pSource->_resourceLeft -= amount;
            unit._cargoAmount   = amount;
            unit._cargoType     = pSource->_pDef->_resourceType;
            unit._gatherPhase   = RtsGatherPhase::Returning;
            unit._repathTimer   = 0.0f;
            pTarget->_harvester = RtsUnitId{};
            if ( pSource->_resourceLeft <= 0 )
            {
                pushEvent( RtsEvent::Kind::ResourceDepleted, unit._owner, pSource->_id, pSource->_pDef->_id );
                if ( bExtractor == false )
                    killUnit( *pSource, RtsUnitId{} );
            }
            return;
        }

        if ( isWithinReach( unit, *pTarget, _settings._interactSlack ) == false )
        {
            unit._gatherPhase = RtsGatherPhase::ToResource;
            approach( unit, pTarget->_position, false );
            return;
        }
        unit._agent.stop();
        const RtsUnit* pHarvester = findUnit( pTarget->_harvester );
        if ( pHarvester != nullptr && pHarvester != &unit )
        {
            // 남이 캔다 — 광물이면 바로 옆의 빈 광물로 옮긴다.
            if ( bExtractor == false && unit._gatherPhase == RtsGatherPhase::ToResource )
            {
                queryUnits( unit._position, 2.0f, _listQueryScratch );
                for ( const RtsUnitId otherId : _listQueryScratch )
                {
                    const RtsUnit* pOther = findUnit( otherId );
                    if ( pOther != nullptr && pOther != pTarget && pOther->isResource() &&
                         pOther->_pDef->_resourceType == RtsResourceType::Minerals && findUnit( pOther->_harvester ) == nullptr )
                    {
                        order._targetUnit = otherId;
                        order._target     = pOther->_position;
                        return;
                    }
                }
            }
            unit._gatherPhase = RtsGatherPhase::Waiting;
            return;
        }
        pTarget->_harvester = unit._id;
        unit._gatherPhase   = RtsGatherPhase::Harvesting;
        unit._gatherTimer   = 0.0f;
    }

    // ------------------------------------------------------------------------------
    // 건설
    // ------------------------------------------------------------------------------
    void RtsWorld::startConstruction( RtsUnit& worker, RtsOrder& order )
    {
        const RtsUnitDef* pDef = _pCatalog->findUnit( order._buildId );
        if ( pDef == nullptr || evaluateCost( worker._owner, *pDef ) != RtsCommandResult::Ok || canPlaceBuilding( order._buildId, order._buildCell, worker._id ) == false )
        {
            finishOrder( worker );
            return;
        }
        const RtsUnitId buildingId = spawnUnit( order._buildId, worker._owner, _grid.computeCellCenter( order._buildCell ) );
        RtsUnit*        pBuilding  = findUnitMutable( buildingId );
        if ( pBuilding == nullptr )
        {
            finishOrder( worker );
            return;
        }
        RtsPlayer& player = _listPlayer[static_cast<size_t>( worker._owner )];
        player._minerals -= pDef->_minerals;
        player._gas -= pDef->_gas;
        pBuilding->_buildProgress = 0.0f;
        pBuilding->_hp            = pDef->_hp * _settings._constructionStartRatio;
        pBuilding->_builder       = worker._id;
        order._targetUnit         = buildingId;
        nudgeToWalkable( worker );
    }

    void RtsWorld::updateBuild( RtsUnit& unit, RtsOrder& order, float32 deltaTime )
    {
        if ( order._targetUnit.isValid() == false )
        {
            const RtsUnitDef* pDef = _pCatalog->findUnit( order._buildId );
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
        RtsUnit* pBuilding = findUnitMutable( order._targetUnit );
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
        const RtsUnitDef& def     = *pBuilding->_pDef;
        const float32     step    = deltaTime / def._buildTime;
        pBuilding->_buildProgress = MathUtil::min( 1.0f, pBuilding->_buildProgress + step );
        pBuilding->_hp            = MathUtil::min( def._hp, pBuilding->_hp + def._hp * ( 1.0f - _settings._constructionStartRatio ) * step );
        if ( pBuilding->isConstructed() )
        {
            pBuilding->_builder = RtsUnitId{};
            pushEvent( RtsEvent::Kind::ConstructionComplete, pBuilding->_owner, pBuilding->_id, def._id );
            finishOrder( unit );
        }
    }

    // ------------------------------------------------------------------------------
    // 생산
    // ------------------------------------------------------------------------------
    void RtsWorld::updateProduction( RtsUnit& building, float32 deltaTime )
    {
        if ( building._listProduction.empty() )
            return;
        const RtsUnitDef* pDef = _pCatalog->findUnit( building._listProduction.front() );
        if ( pDef == nullptr )
        {
            building._listProduction.pop_front();
            return;
        }
        RtsPlayer& player = _listPlayer[static_cast<size_t>( building._owner )];
        if ( building._productionTimer <= 0.0f && building._supplyReserved == 0 && pDef->_supplyCost > 0 )
        {
            if ( player._supplyUsed + pDef->_supplyCost > player._supplyCap )
            {
                if ( building._bSupplyBlocked == SW_FALSE )
                    pushEvent( RtsEvent::Kind::SupplyBlocked, building._owner, building._id, pDef->_id );
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

    bool RtsWorld::findSpawnPosition( const RtsUnit& building, float3& outPosition ) const
    {
        const float3 toward    = building._bHasRally ? building._rallyPoint : float3{ building._position._x, 0.0f, building._position._z - 10.0f };
        const int32  footprint = building._pDef->_footprint;
        for ( int32 ring = 1; ring <= 4; ++ring )
        {
            bool    bFound       = false;
            float32 bestDistance = MathUtil::MaxFloat;
            for ( int32 y = building._cell._y - ring; y < building._cell._y + footprint + ring; ++y )
            {
                for ( int32 x = building._cell._x - ring; x < building._cell._x + footprint + ring; ++x )
                {
                    const bool bEdge = x == building._cell._x - ring || y == building._cell._y - ring || x == building._cell._x + footprint + ring - 1 ||
                                       y == building._cell._y + footprint + ring - 1;
                    if ( bEdge == false || _grid.isWalkable( x, y ) == false )
                        continue;
                    const float3  center   = _grid.computeCellCenter( int2{ x, y } );
                    const float32 distance = RtsWorldInternal::computeFlatDistance( center, toward );
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

    void RtsWorld::completeProduction( RtsUnit& building )
    {
        const hashed_string defId = building._listProduction.front();
        building._listProduction.pop_front();
        building._productionTimer = 0.0f;
        building._supplyReserved  = 0;
        float3 position{};
        if ( findSpawnPosition( building, position ) == false )
            position = building._position;
        const RtsUnitDef* pDef = _pCatalog->findUnit( defId );
        if ( pDef != nullptr && pDef->_bAir )
            position = float3{ building._position._x, 0.0f, building._position._z };
        const RtsUnitId unitId = spawnUnit( defId, building._owner, position );
        pushEvent( RtsEvent::Kind::ProductionComplete, building._owner, unitId, defId );
        if ( building._bHasRally == SW_FALSE || unitId.isValid() == false )
            return;
        const RtsUnitId rallyTarget = pickUnit( building._rallyPoint );
        (void)issueSmart( unitId, building._rallyPoint, rallyTarget, false );
    }

    // ------------------------------------------------------------------------------
    // 전투
    // ------------------------------------------------------------------------------
    bool RtsWorld::areEnemies( int32 playerA, int32 playerB ) const
    {
        const RtsPlayer* pA = findPlayer( playerA );
        const RtsPlayer* pB = findPlayer( playerB );
        return pA != nullptr && pB != nullptr && pA->_team != pB->_team;
    }

    bool RtsWorld::canAttack( const RtsUnit& attacker, const RtsUnit& target ) const
    {
        if ( attacker._pDef->canAttack() == false || target._bAlive == SW_FALSE || target.isResource() || areEnemies( attacker._owner, target._owner ) == false )
            return false;
        return target._pDef->_bAir ? attacker._pDef->_bTargetsAir != SW_FALSE : attacker._pDef->_bTargetsGround != SW_FALSE;
    }

    float32 RtsWorld::computeRectDistance( const float3& position, const int2& cell, int32 footprint ) const
    {
        const float32 cellSize = _grid.getCellSize();
        const float32 minX     = _grid.getOrigin()._x + static_cast<float32>( cell._x ) * cellSize;
        const float32 minZ     = _grid.getOrigin()._z + static_cast<float32>( cell._y ) * cellSize;
        const float32 size     = static_cast<float32>( footprint ) * cellSize;
        const float32 dx       = MathUtil::max( 0.0f, MathUtil::max( minX - position._x, position._x - ( minX + size ) ) );
        const float32 dz       = MathUtil::max( 0.0f, MathUtil::max( minZ - position._z, position._z - ( minZ + size ) ) );
        return MathUtil::sqrt( dx * dx + dz * dz );
    }

    float32 RtsWorld::computeEdgeDistance( const RtsUnit& unit, const RtsUnit& target ) const
    {
        if ( target.isMobile() == false )
            return computeRectDistance( unit._position, target._cell, target._pDef->_footprint ) - ( unit.isMobile() ? unit._pDef->_radius : 0.0f );
        if ( unit.isMobile() == false )
            return computeRectDistance( target._position, unit._cell, unit._pDef->_footprint ) - target._pDef->_radius;
        return RtsWorldInternal::computeFlatDistance( unit._position, target._position ) - unit._pDef->_radius - target._pDef->_radius;
    }

    bool RtsWorld::isWithinReach( const RtsUnit& unit, const RtsUnit& target, float32 reach ) const { return computeEdgeDistance( unit, target ) <= reach; }

    RtsUnitId RtsWorld::findAutoTarget( const RtsUnit& unit, float32 radius ) const
    {
        vector<RtsUnitId> listCandidate;
        queryUnits( unit._position, radius + ( unit.isMobile() ? unit._pDef->_radius : 0.0f ), listCandidate );
        RtsUnitId bestId{};
        float32   bestScore = MathUtil::MaxFloat;
        for ( const RtsUnitId candidateId : listCandidate )
        {
            const RtsUnit* pCandidate = findUnit( candidateId );
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

    void RtsWorld::updateCombat( RtsUnit& unit, float32 deltaTime )
    {
        (void)deltaTime;
        if ( unit._attackTarget.isValid() == false )
            return;
        RtsUnit* pTarget = findUnitMutable( unit._attackTarget );
        if ( pTarget == nullptr || canAttack( unit, *pTarget ) == false )
        {
            unit._attackTarget = RtsUnitId{};
            if ( unit.isMobile() && unit._listOrder.empty() )
                unit._agent.stop();
            return;
        }
        const float32   distance = computeEdgeDistance( unit, *pTarget );
        const RtsOrder* pOrder   = unit.findOrder();
        const bool      bHold    = pOrder != nullptr && pOrder->_type == RtsOrderType::Hold;
        if ( distance <= unit._pDef->_range )
        {
            if ( unit.isMobile() && unit._agent.isMoving() )
                unit._agent.stop();
            if ( unit._cooldown <= 0.0f )
            {
                unit._cooldown = unit._pDef->_cooldown;
                dealDamage( unit, *pTarget );
            }
            return;
        }
        const bool bOrdered = pOrder != nullptr && pOrder->_type == RtsOrderType::Attack;
        if ( unit.isMobile() == false || bHold || ( bOrdered == false && distance > unit._pDef->_sight * 1.5f ) )
        {
            unit._attackTarget = RtsUnitId{}; // 놓쳤다(자동 목표만 — 명령한 목표는 끝까지 쫓는다)
            return;
        }
        approach( unit, pTarget->_position, false );
    }

    void RtsWorld::dealDamage( RtsUnit& attacker, RtsUnit& target )
    {
        const float32 damage = MathUtil::max( _settings._minimumDamage, attacker._pDef->_damage - target._pDef->_armor );
        target._hp -= damage;
        if ( target._owner != kNoOwner )
        {
            RtsPlayer& owner = _listPlayer[static_cast<size_t>( target._owner )];
            if ( _time >= owner._nextUnderAttackTime )
            {
                owner._nextUnderAttackTime = _time + _settings._underAttackCooldown;
                pushEvent( RtsEvent::Kind::UnderAttack, target._owner, target._id, target._pDef->_id, 0, attacker._id );
            }
        }
        // 할 일 없이 맞으면 되받아친다.
        if ( target.isMobile() && target._listOrder.empty() && target._attackTarget.isValid() == false && canAttack( target, attacker ) )
            target._attackTarget = attacker._id;
        if ( target._hp <= 0.0f )
            killUnit( target, attacker._id );
    }

    void RtsWorld::killUnit( RtsUnit& unit, RtsUnitId killerId )
    {
        if ( unit._bAlive == SW_FALSE )
            return;
        unit._hp = 0.0f;
        for ( RtsOrder& order : unit._listOrder )
            releaseOrder( unit, order );
        unit._listOrder.clear();
        unit._agent.stop();
        if ( unit.isMobile() == false && unit._pDef->_bExtractor == SW_FALSE )
            placeFootprint( unit, false ); // 정제소는 아니다 — 그 칸은 간헐천이 계속 막는다
        // 생산 대기열 값은 돌려주지 않는다(부서진 건물 — 스타크래프트와 같다).
        unit._bAlive = SW_FALSE;
        pushEvent( RtsEvent::Kind::UnitDied, unit._owner, unit._id, unit._pDef->_id, 0, killerId );
    }

    // ------------------------------------------------------------------------------
    // 시야 · 승패
    // ------------------------------------------------------------------------------
    void RtsWorld::updateVision()
    {
        for ( vector<uint8>& listVisibility : _listTeamVisibility )
        {
            for ( uint8& visibility : listVisibility )
            {
                if ( visibility == static_cast<uint8>( RtsVisibility::Visible ) )
                    visibility = static_cast<uint8>( RtsVisibility::Explored );
            }
        }
        for ( const RtsUnit& unit : _listUnit )
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
                        listVisibility[static_cast<size_t>( y * _grid.getWidth() + x )] = static_cast<uint8>( RtsVisibility::Visible );
                }
            }
        }
    }

    RtsVisibility RtsWorld::getVisibility( int32 player, const int2& cell ) const
    {
        const RtsPlayer* pPlayer = findPlayer( player );
        if ( pPlayer == nullptr || _grid.isInside( cell ) == false )
            return RtsVisibility::Unexplored;
        return static_cast<RtsVisibility>( _listTeamVisibility[static_cast<size_t>( pPlayer->_team )][static_cast<size_t>( _grid.computeIndex( cell ) )] );
    }

    bool RtsWorld::isVisibleTo( int32 player, RtsUnitId unitId ) const
    {
        const RtsUnit*   pUnit   = findUnit( unitId );
        const RtsPlayer* pPlayer = findPlayer( player );
        if ( pUnit == nullptr || pPlayer == nullptr )
            return false;
        if ( pUnit->_owner != kNoOwner && _listPlayer[static_cast<size_t>( pUnit->_owner )]._team == pPlayer->_team )
            return true;
        if ( pUnit->isMobile() )
            return getVisibility( player, _grid.computeCell( pUnit->_position ) ) == RtsVisibility::Visible;
        for ( int32 y = pUnit->_cell._y; y < pUnit->_cell._y + pUnit->_pDef->_footprint; ++y )
        {
            for ( int32 x = pUnit->_cell._x; x < pUnit->_cell._x + pUnit->_pDef->_footprint; ++x )
            {
                if ( getVisibility( player, int2{ x, y } ) == RtsVisibility::Visible )
                    return true;
            }
        }
        return false;
    }

    void RtsWorld::updateDefeat()
    {
        if ( _winningTeam >= 0 )
            return;
        vector<int32> listBuildingCount( _listPlayer.size(), 0 );
        for ( const RtsUnit& unit : _listUnit )
        {
            if ( unit._bAlive && unit._owner != kNoOwner && unit.isBuilding() )
                ++listBuildingCount[static_cast<size_t>( unit._owner )];
        }
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            RtsPlayer& player = _listPlayer[index];
            if ( player._bDefeated || player._bHadBuilding == SW_FALSE || listBuildingCount[index] > 0 )
                continue;
            player._bDefeated = SW_TRUE;
            pushEvent( RtsEvent::Kind::PlayerDefeated, static_cast<int32>( index ), RtsUnitId{}, hashed_string{} );
        }
        int32 aliveTeam  = -1;
        int32 aliveCount = 0;
        for ( int32 team = 0; team < _teamCount; ++team )
        {
            for ( const RtsPlayer& player : _listPlayer )
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
            pushEvent( RtsEvent::Kind::GameOver, aliveTeam, RtsUnitId{}, hashed_string{} );
        }
    }

    // ------------------------------------------------------------------------------
    // 조회 · 배치
    // ------------------------------------------------------------------------------
    int32 RtsWorld::countUnits( int32 player, const hashed_string& defId, bool bIncludeUnfinished ) const
    {
        int32 count = 0;
        for ( const RtsUnit& unit : _listUnit )
        {
            if ( unit._bAlive && unit._owner == player && unit._pDef->_id == defId && ( bIncludeUnfinished || unit.isConstructed() ) )
                ++count;
        }
        return count;
    }

    bool RtsWorld::hasConstructed( int32 player, const hashed_string& defId ) const { return countUnits( player, defId, false ) > 0; }

    int32 RtsWorld::countPlanned( int32 player, const hashed_string& defId ) const
    {
        int32 count = 0;
        for ( const RtsUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner != player )
                continue;
            if ( unit._pDef->_id == defId )
                ++count;
            for ( const hashed_string& queued : unit._listProduction )
                count += queued == defId ? 1 : 0;
            for ( const RtsOrder& order : unit._listOrder )
                count += order._type == RtsOrderType::Build && order._buildId == defId && order._targetUnit.isValid() == false ? 1 : 0;
        }
        return count;
    }

    bool RtsWorld::canPlaceBuilding( const hashed_string& buildingId, const int2& cell, RtsUnitId ignoreUnit ) const
    {
        const RtsUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( buildingId ) : nullptr;
        if ( pDef == nullptr || pDef->_kind != RtsUnitKind::Building )
            return false;
        const int32 footprint = pDef->_footprint;
        if ( _grid.isInside( cell ) == false || _grid.isInside( cell._x + footprint - 1, cell._y + footprint - 1 ) == false )
            return false;
        if ( pDef->_bExtractor )
        {
            // 같은 칸 · 같은 크기의 빈 가스 간헐천 위만.
            for ( const RtsUnit& unit : _listUnit )
            {
                if ( unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != RtsResourceType::Gas || unit._cell != cell ||
                     unit._pDef->_footprint != footprint )
                    continue;
                for ( const RtsUnit& other : _listUnit )
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
        for ( const RtsUnit& unit : _listUnit )
        {
            if ( unit._bAlive && unit.isMobile() && unit._id != ignoreUnit && RtsWorldInternal::isGround( unit ) &&
                 computeRectDistance( unit._position, cell, footprint ) < unit._pDef->_radius * 0.9f )
                return false;
        }
        return true;
    }

    bool RtsWorld::findBuildSite( const hashed_string& buildingId, const float3& nearPosition, int32 minRadius, int32 maxRadius, int2& outCell ) const
    {
        const RtsUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( buildingId ) : nullptr;
        if ( pDef == nullptr || pDef->_kind != RtsUnitKind::Building )
            return false;
        if ( pDef->_bExtractor )
        {
            float32 bestDistance = static_cast<float32>( maxRadius ) + 1.0f;
            bool    bFound       = false;
            for ( const RtsUnit& unit : _listUnit )
            {
                if ( unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != RtsResourceType::Gas )
                    continue;
                const float32 distance = RtsWorldInternal::computeFlatDistance( nearPosition, unit._position );
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
                            bClear = _grid.isWalkable( x, y );
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

    RtsUnitId RtsWorld::findNearestResource( const float3& position, RtsResourceType type, float32 maxDistance ) const
    {
        RtsUnitId bestId{};
        float32   bestDistance = maxDistance;
        for ( const RtsUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit.isResource() == false || unit._pDef->_resourceType != type || unit._resourceLeft <= 0 )
                continue;
            const float32 distance = RtsWorldInternal::computeFlatDistance( position, unit._position );
            if ( distance <= bestDistance )
            {
                bestDistance = distance;
                bestId       = unit._id;
            }
        }
        return bestId;
    }

    RtsUnitId RtsWorld::findNearestDepot( int32 player, const float3& position ) const
    {
        RtsUnitId bestId{};
        float32   bestDistance = MathUtil::MaxFloat;
        for ( const RtsUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner != player || unit._pDef->_bDepot == SW_FALSE || unit.isConstructed() == false )
                continue;
            const float32 distance = RtsWorldInternal::computeFlatDistance( position, unit._position );
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
    FlowField* RtsWorld::acquireFlowField( const int2& goal )
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

    void RtsWorld::releaseFlowField( const FlowField* pField )
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

    void RtsWorld::pushEvent( RtsEvent::Kind kind, int32 player, RtsUnitId unitId, const hashed_string& defId, int32 value, RtsUnitId otherId )
    {
        RtsEvent event;
        event._kind   = kind;
        event._player = player;
        event._unit   = unitId;
        event._defId  = defId;
        event._value  = value;
        event._other  = otherId;
        if ( const RtsUnit* pUnit = findUnit( unitId ) )
            event._position = pUnit->_position;
        else if ( unitId.isValid() && unitId.index() < _listUnit.size() )
            event._position = _listUnit[unitId.index()]._position;
        _listEvent.push_back( event );
    }
} // namespace sw
