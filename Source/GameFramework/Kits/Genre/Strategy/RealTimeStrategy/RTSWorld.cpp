#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/TimeRollover.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommand.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Match/TeamAttitude.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RTSWorldInternal
        {
            static constexpr float32 kMaxUnitExtent       = 4.5f; ///< 버킷 조회에 더하는 몸 크기의 상한(건물 한 변의 절반)
            static constexpr uint32  kMinUnitStateBytes   = 128;  ///< 유닛 하나의 상태가 최소한 차지하는 바이트 수(개수 상한 계산용)
            static constexpr uint32  kMinOrderStateBytes  = 34;   ///< 명령 하나의 상태가 최소한 차지하는 바이트 수
            static constexpr uint32  kMinPlayerStateBytes = 30;   ///< 플레이어 하나의 상태가 최소한 차지하는 바이트 수

            static void serializeUnitID( StateIO& io, RTSUnitID& unitID )
            {
                uint64 packed = unitID.packed();
                io.value( packed );
                if ( io.isReading() )
                {
                    unitID = RTSUnitID::fromPacked( packed );
                }
            }

            static void serializeOrder( StateIO& io, const RTSCatalog* pCatalog, RTSOrder& order, vector<uint8>& outListFlowFieldOrder )
            {
                uint8 bFlowField = order._pFlowField != nullptr ? SW_TRUE : SW_FALSE;
                io.value( order._buildID );
                io.value( order._target );
                io.value( order._buildCell );
                serializeUnitID( io, order._targetUnit );
                io.value( order._arriveRadius );
                io.enumValue( order._type, RTSOrderType::Hold );
                io.flag( bFlowField );
                io.check( 0.0f <= order._arriveRadius );
                if ( io.isReading() )
                {
                    io.check( order._buildID.empty() || pCatalog->find( order._buildID ) != nullptr );
                    outListFlowFieldOrder.push_back( bFlowField );
                }
            }

            static void serializeUnit( StateIO& io, const RTSCatalog* pCatalog, const RTSSettings& settings, RTSUnit& unit, vector<uint8>& outListFlowFieldOrder )
            {
                io.reference( unit._pDef, [pCatalog]( const hashed_string& defID )
                { return pCatalog->find( defID ); } );
                uint32 orderCount = static_cast<uint32>( unit._listOrder.size() );
                io.count( kMinOrderStateBytes, orderCount );
                for ( uint32 orderIndex = 0; orderIndex < orderCount && io.isOk(); ++orderIndex )
                {
                    serializeOrder( io, pCatalog, io.isWriting() ? unit._listOrder[orderIndex] : unit._listOrder.emplace_back(), outListFlowFieldOrder );
                }
                uint32 productionCount = static_cast<uint32>( unit._listProduction.size() );
                io.count( sizeof( uint32 ), productionCount );
                if ( io.check( productionCount <= static_cast<uint32>( settings._productionQueueMax ) ) == false )
                    return;
                for ( uint32 productionIndex = 0; productionIndex < productionCount; ++productionIndex )
                {
                    hashed_string productionID = io.isWriting() ? unit._listProduction[productionIndex] : hashed_string{};
                    io.value( productionID );
                    if ( io.isReading() )
                    {
                        if ( io.isOk() == false || io.check( pCatalog->find( productionID ) != nullptr ) == false )
                            return;
                        unit._listProduction.push_back( productionID );
                    }
                }
                io.value( unit._position );
                io.value( unit._rallyPoint );
                io.value( unit._moveGoal );
                io.value( unit._cell );
                serializeUnitID( io, unit._id );
                serializeUnitID( io, unit._attackTarget );
                serializeUnitID( io, unit._gatherTarget );
                serializeUnitID( io, unit._harvester );
                serializeUnitID( io, unit._builder );
                serializeUnitID( io, unit._linkedResource );
                io.value( unit._hp );
                io.value( unit._cooldown._remaining );
                io.value( unit._buildProgress );
                io.value( unit._productionTimer );
                io.value( unit._gatherTimer );
                io.value( unit._owner );
                io.value( unit._resourceLeft );
                io.value( unit._cargoAmount );
                io.value( unit._supplyReserved );
                io.enumValue( unit._cargoType, RTSResourceType::None );
                io.enumValue( unit._gatherPhase, RTSGatherPhase::Returning );
                io.value( unit._bAlive );
                io.value( unit._bHasRally );
                io.value( unit._bSupplyBlocked );
                io.check( unit._bAlive == SW_FALSE || unit._pDef != nullptr );
                if ( io.isReading() )
                {
                    // 경로와 흐름장은 싣지 않는다. 맨 앞 명령을 처음부터 다시 진행시키면 이 격자에서 경로를 다시 구한다.
                    unit._bOrderStarted = SW_FALSE;
                    unit._repathTimer.clear();
                }
            }

            static void serializePlayer( StateIO& io, RTSPlayer& player )
            {
                io.value( player._startPosition );
                io.value( player._supplyUsed );
                io.value( player._supplyCap );
                io.value( player._team );
                io.value( player._nextUnderAttackTime );
                io.value( player._bDefeated );
                io.value( player._bHadBuilding );
            }

            /**
             * @brief 읽은 유닛 하나가 슬롯 @p slot 에 놓일 수 있는지 봅니다. 주인은 `_listPlayer` 를 바로 찌르는 번호라 죽은 유닛도 범위 안이어야 합니다.
             * @details 살아 있는 유닛의 id 는 자기 슬롯과 세대를 가리켜야 `findUnit` 이 찾고, 건물과 자원의 셀은 격자 안이어야 놓였던 자리이다.
             */
            [[nodiscard]] static bool isUnitValid( const RTSUnit& unit, uint32 slot, uint32 generation, int32 playerCount, const NavGrid& grid )
            {
                const bool bOwnerValid = unit._owner == RTSWorld::kNoOwner || ( 0 <= unit._owner && unit._owner < playerCount );
                const bool bFlagsValid = unit._bAlive <= SW_TRUE && unit._bHasRally <= SW_TRUE && unit._bSupplyBlocked <= SW_TRUE;
                if ( bOwnerValid == false || bFlagsValid == false || generation == 0 )
                    return false;
                if ( unit._bAlive == SW_FALSE )
                    return true;
                const bool bIDValid       = unit._id.isValid() && unit._id.index() == slot && unit._id.generation() == generation;
                const bool bHPValid       = 0.0f <= unit._hp && unit._hp <= unit._pDef->_hp;
                const bool bProgressValid = 0.0f <= unit._buildProgress && unit._buildProgress <= 1.0f && 0.0f <= unit._productionTimer && 0.0f <= unit._gatherTimer;
                const bool bAmountValid   = 0 <= unit._resourceLeft && 0 <= unit._cargoAmount && 0 <= unit._supplyReserved;
                const bool bCellValid     = unit.isMobile() || grid.isInside( unit._cell );
                return bIDValid && bHPValid && bProgressValid && bAmountValid && bCellValid;
            }

            /** @brief 읽은 빈 슬롯 목록을 봅니다. 겹치거나 살아 있는 슬롯을 내주면 `allocateUnit` 이 살아 있는 유닛을 덮어씁니다. */
            [[nodiscard]] static bool areFreeSlotsValid( const vector<uint32>& listFreeSlot, const deque<RTSUnit>& listUnit )
            {
                vector<uint8> listSeen( listUnit.size(), SW_FALSE );
                for ( const uint32 slot : listFreeSlot )
                {
                    const RTSUnit& unit = listUnit[slot];
                    if ( listSeen[slot] != SW_FALSE || unit._bAlive != SW_FALSE || unit._id.isValid() )
                        return false;
                    listSeen[slot] = SW_TRUE;
                }
                return true;
            }

            /** @brief 읽은 플레이어 하나를 봅니다. 팀은 시야 표(`RTSVision::reveal`)를 바로 찌르는 번호입니다. */
            [[nodiscard]] static bool isPlayerValid( const RTSPlayer& player, int32 teamCount )
            {
                const bool bTeamValid = 0 <= player._team && player._team < teamCount;
                return bTeamValid && 0 <= player._supplyUsed && 0 <= player._supplyCap && 0.0f <= player._nextUnderAttackTime && player._bDefeated <= SW_TRUE && player._bHadBuilding <= SW_TRUE;
            }
        };

        bool isGroundUnitCommand( const RTSUnit& unit )
        {
            return unit._pDef->_bAir == SW_FALSE;
        }

        bool isGroundUnitQuery( const RTSUnit& unit )
        {
            return unit._pDef->_bAir == SW_FALSE;
        }

        bool isGroundUnitSimulation( const RTSUnit& unit )
        {
            return unit._pDef->_bAir == SW_FALSE;
        }
    } // namespace

    RTSWorld::RTSWorld()
        : _grid{}
        , _airGrid{}
        , _pathSolver{ _grid }
        , _pathQueue{ _pathSolver }
        , _listUnit{}
        , _listGeneration{}
        , _freeSlot{}
        , _listPlayer{}
        , _eventBuffer{}
        , _listTerrainBlocked{}
        , _vision{}
        , _buckets{}
        , _listNeighborScratch{}
        , _listQueryScratch{}
        , _flowParams{}
        , _pCatalog{ nullptr }
        , _settings{}
        , _stepTimer{}
        , _time{ 0.0f }
        , _land{}
        , _winningTeam{ constant::invalid_index::kInt32 }
        , _landRevision{ 0 }
    {
    }

    bool RTSWorld::initialize( const RTSCatalog* pCatalog, int32 width, int32 height, const RTSSettings& settings )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "RTSWorld.RTSWorldRefusedSettings", "RTSWorld: refused settings - %#", rangeError.c_str() );
            return false;
        }
        if ( width < 1 || height < 1 )
        {
            SW_LOG_ERROR_ID( "RTSWorld.RTSWorldRefusedSizeX", "RTSWorld: refused size %#x%# (both need 1 or more)", width, height );
            return false;
        }
        _pCatalog = pCatalog;
        _settings = settings;
        if ( _grid.initialize( width, height, 1.0f, float3{ 0.0f, 0.0f, 0.0f } ) == false || _airGrid.initialize( width, height, 1.0f, float3{ 0.0f, 0.0f, 0.0f } ) == false )
            return false;
        _listUnit.clear();
        _listGeneration.clear();
        _freeSlot.clear();
        _listPlayer.clear();
        _eventBuffer.clear();
        _listTerrainBlocked.assign( static_cast<size_t>( _grid.getWidth() * _grid.getHeight() ), SW_FALSE );
        _vision.reset( _grid.getWidth(), _grid.getHeight() );
        _flowParams.clear();
        _buckets.initialize( _grid.getWidth(), _grid.getHeight(), _settings._bucketSize );
        _stepTimer   = FixedStepTimer{ _settings._fixedStep, constant::time::kDefaultMaxFrameSeconds };
        _time        = 0.0f;
        _winningTeam = constant::invalid_index::kInt32;
        return true;
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
        // 칠할 막힘을 먼저 다 구한 뒤 바뀐 칸만 쓴다. 지형 · 땅으로 칠하고 건물 발자국을 다시 덮으면 그대로인 건물 칸이 풀렸다 막히며 격자 리비전이 올라,
        // 땅이 이 월드와 상관없이 바뀌어도(다른 키트의 도로) 모든 흐름장이 다시 구해졌다.
        vector<uint8> listBlocked( _listTerrainBlocked.size(), SW_FALSE );
        for ( int32 y = 0; y < _grid.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _grid.getWidth(); ++x )
            {
                const size_t index = static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) );
                listBlocked[index] = ( _listTerrainBlocked[index] != SW_FALSE || _land.isBlocked( x, y ) ) ? SW_TRUE : SW_FALSE;
            }
        }
        for ( const RTSUnit& unit : _listUnit )
        {
            const bool bStaticBlocker = unit._bAlive != SW_FALSE && unit.isMobile() == false && unit._pDef->_bExtractor == SW_FALSE;
            if ( bStaticBlocker == false )
                continue;
            const int32 footprint = unit._pDef->_footprint;
            for ( int32 y = unit._cell._y; y < unit._cell._y + footprint; ++y )
            {
                for ( int32 x = unit._cell._x; x < unit._cell._x + footprint; ++x )
                {
                    if ( _grid.isInside( x, y ) )
                        listBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] = SW_TRUE;
                }
            }
        }
        for ( int32 y = 0; y < _grid.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _grid.getWidth(); ++x )
            {
                _grid.setBlocked( x, y, listBlocked[static_cast<size_t>( _grid.computeIndex( int2{ x, y } ) )] != SW_FALSE ); // 같은 값이면 리비전이 오르지 않는다
            }
        }
    }

    int32 RTSWorld::addPlayer( int32 team, Wallet* pWallet, const float3& startPosition )
    {
        RTSPlayer player;
        player._team          = MathUtil::max( 0, team );
        player._pWallet       = pWallet;
        player._startPosition = startPosition;
        _listPlayer.push_back( player );
        _vision.ensureTeamCount( player._team + 1 );
        return static_cast<int32>( _listPlayer.size() ) - 1;
    }

    // ------------------------------------------------------------------------------
    // 유닛 슬롯
    // ------------------------------------------------------------------------------
    RTSUnitID RTSWorld::allocateUnit()
    {
        // 빈 슬롯은 마지막에 놓인 것부터 다시 나간다(LIFO). 유닛 id 가 그 순서를 따른다.
        const uint32 index = _freeSlot.reserve( _listUnit );
        if ( index < _listUnit.size() )
        {
            _listUnit[index] = RTSUnit{};
            return RTSUnitID::make( index, _listGeneration[index] );
        }
        _listUnit.emplace_back();
        _listGeneration.push_back( 1u );
        return RTSUnitID::make( index, 1u );
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
            unit._id = RTSUnitID{};
            ++_listGeneration[index];
            if ( _listGeneration[index] == 0 )
                _listGeneration[index] = 1;
            _freeSlot.push( static_cast<uint32>( index ) );
        }
    }

    const RTSUnit* RTSWorld::findUnit( RTSUnitID unitID ) const
    {
        if ( unitID.isValid() == false || unitID.index() >= _listUnit.size() )
            return nullptr;
        const RTSUnit& unit = _listUnit[unitID.index()];
        return unit._bAlive && unit._id == unitID ? &unit : nullptr;
    }

    RTSUnit* RTSWorld::findUnitMutable( RTSUnitID unitID )
    {
        return const_cast<RTSUnit*>( findUnit( unitID ) );
    }

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

    RTSUnitID RTSWorld::spawnUnit( const hashed_string& defID, int32 owner, const float3& position )
    {
        const RTSUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->find( defID ) : nullptr;
        if ( pDef == nullptr || ( owner != kNoOwner && findPlayer( owner ) == nullptr ) )
            return RTSUnitID{};
        int2      cell = _grid.computeCell( position );
        RTSUnitID linkedResource{};
        if ( pDef->isMobile() == false )
        {
            if ( pDef->_bExtractor )
            {
                if ( canPlaceBuilding( defID, cell ) == false )
                    return RTSUnitID{};
                for ( const RTSUnit& other : _listUnit )
                {
                    const bool bGasAtCell = other._bAlive && other.isResource() && other._pDef->_resourceType == RTSResourceType::Gas && other._cell == cell;
                    if ( bGasAtCell )
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
                            return RTSUnitID{};
                    }
                }
            }
        }
        else
        {
            const bool bNoWalkableCellNearby = pDef->_bAir == SW_FALSE && _grid.isWalkable( cell ) == false && _grid.findNearestWalkable( cell, 8, cell ) == false;
            if ( bNoWalkableCellNearby )
                return RTSUnitID{};
        }
        const int32 footprint = pDef->_footprint;
        if ( pDef->_kind == RTSUnitKind::Building && _land.claimRect( cell._x, cell._y, cell._x + footprint - 1, cell._y + footprint - 1, true ) == false )
            return RTSUnitID{};

        const RTSUnitID unitID = allocateUnit();
        RTSUnit&        unit   = _listUnit[unitID.index()];
        unit._id               = unitID;
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
        pushEvent( RTSEvent::Kind::UnitCreated, unit._owner, unitID, pDef->_id );
        return unitID;
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

    // ------------------------------------------------------------------------------
    // 시간
    // ------------------------------------------------------------------------------
    void RTSWorld::update( float32 deltaTime )
    {
        if ( _pCatalog == nullptr )
            return;
        // 다른 키트가 땅을 얻거나 반납했으면 땅 격자를 다시 칠한다. 흐름장은 격자 리비전이 바뀌면 스스로 다시 구한다.
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

    void RTSWorld::pollEvents( const ConsumerLease& lease, vector<RTSEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( lease, outListEvent );
    }

    void RTSWorld::stepFixed( float32 deltaTime )
    {
        _time += deltaTime;
        // 시각은 알림 간격(차이)만 본다 — float32 걸음을 지키려 기준을 옮기고, 비교하는 알림 시각도 같이 옮긴다(감사 K-5).
        if ( TimeRolloverUtil::rebase( _time ) )
        {
            for ( RTSPlayer& player : _listPlayer )
            {
                player._nextUnderAttackTime = MathUtil::max( player._nextUnderAttackTime - TimeRolloverUtil::kRebaseSeconds, 0.0f );
            }
        }
        _flowParams.refreshStale( _grid );
        recomputeSupply();
        rebuildBuckets();
        const size_t unitCount = _listUnit.size(); // 이번 걸음에 생긴 유닛은 다음 걸음부터 처리한다
        for ( size_t index = 0; index < unitCount; ++index )
        {
            RTSUnit& unit = _listUnit[index];
            if ( unit._bAlive )
                updateUnit( unit, deltaTime );
        }
        applyPathResults();
        if ( _vision.updateRepaintTimer( deltaTime, _settings._visionInterval ) )
            updateVision();
        updateDefeat();
        freeDeadUnits();
    }

    void RTSWorld::queryUnits( const float3& center, float32 radius, vector<RTSUnitID>& outListUnit ) const
    {
        outListUnit.clear();
        _buckets.forEachSlotNear( _grid, center, radius + RTSWorldInternal::kMaxUnitExtent,
                                  [&]( int32 slot )
        {
            const RTSUnit& unit = _listUnit[static_cast<size_t>( slot )];
            if ( unit._bAlive == SW_FALSE )
                return;
            const float32 distance = unit.isMobile() ? float3::getHorizontalDistance( center, unit._position ) - unit._pDef->_radius
                                                     : computeRectDistance( center, unit._cell, unit._pDef->_footprint );
            if ( distance <= radius )
                outListUnit.push_back( unit._id );
        } );
    }

    RTSUnitID RTSWorld::pickUnit( const float3& position ) const
    {
        RTSUnitID bestID{};
        float32   bestDistance = MathUtil::kMaxFloat;
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE )
                continue;
            float32 distance = unit.isMobile() ? float3::getHorizontalDistance( position, unit._position ) - unit._pDef->_radius
                                               : computeRectDistance( position, unit._cell, unit._pDef->_footprint );
            if ( distance > 0.0f )
                continue;
            // 유닛이 건물 위에 겹치면 유닛을 고른다(정제소는 간헐천보다 먼저 고른다).
            distance -= unit.isMobile() ? 10.0f : ( unit._pDef->_bExtractor ? 5.0f : 0.0f );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                bestID       = unit._id;
            }
        }
        return bestID;
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
                    const float32 distance = float3::getHorizontalDistance( center, toward );
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
        const bool bCannotAttack = attacker._pDef->canAttack() == false || target._bAlive == SW_FALSE || target.isResource() || areEnemies( attacker._owner, target._owner ) == false;
        if ( bCannotAttack )
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
        return float3::getHorizontalDistance( unit._position, target._position ) - unit._pDef->_radius - target._pDef->_radius;
    }

    bool RTSWorld::isWithinReach( const RTSUnit& unit, const RTSUnit& target, float32 reach ) const
    {
        return computeEdgeDistance( unit, target ) <= reach;
    }

    RTSUnitID RTSWorld::findAutoTarget( const RTSUnit& unit, float32 radius )
    {
        queryUnits( unit._position, radius + ( unit.isMobile() ? unit._pDef->_radius : 0.0f ), _listQueryScratch );
        RTSUnitID bestID{};
        float32   bestScore = MathUtil::kMaxFloat;
        for ( const RTSUnitID candidateID : _listQueryScratch )
        {
            const RTSUnit* pCandidate = findUnit( candidateID );
            if ( pCandidate == nullptr || canAttack( unit, *pCandidate ) == false )
                continue;
            const float32 distance = computeEdgeDistance( unit, *pCandidate );
            if ( distance > radius )
                continue;
            // 싸울 수 있는 유닛, 그 밖의 유닛, 건물 순으로 고른다(스타크래프트의 자동 목표 우선순위).
            const float32 priority = pCandidate->_pDef->canAttack() && pCandidate->isMobile() ? 0.0f : ( pCandidate->isMobile() ? 100.0f : 200.0f );
            if ( priority + distance < bestScore )
            {
                bestScore = priority + distance;
                bestID    = candidateID;
            }
        }
        return bestID;
    }

    RTSVisibility RTSWorld::getVisibility( int32 player, const int2& cell ) const
    {
        const RTSPlayer* pPlayer = findPlayer( player );
        return pPlayer == nullptr ? RTSVisibility::Unexplored : _vision.get( pPlayer->_team, cell );
    }

    bool RTSWorld::isVisibleTo( int32 player, RTSUnitID unitID ) const
    {
        const RTSUnit*   pUnit   = findUnit( unitID );
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

    void RTSWorld::pushEvent( RTSEvent::Kind kind, int32 player, RTSUnitID unitID, const hashed_string& defID, int32 value, RTSUnitID otherID )
    {
        RTSEvent event;
        event._kind   = kind;
        event._player = player >= 0 ? optional<int32>( player ) : nullopt; // 유닛의 `_owner` 는 상태 칸이라 주인 없음이 -1 이다
        event._unit   = unitID;
        event._defID  = defID;
        event._value  = value;
        event._other  = otherID;
        if ( const RTSUnit* pUnit = findUnit( unitID ) )
            event._position = pUnit->_position;
        else if ( unitID.isValid() && unitID.index() < _listUnit.size() )
            event._position = _listUnit[unitID.index()]._position;
        _eventBuffer.push( event );
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
            _flowParams.release( order._pFlowField );
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
                order._pFlowField = _flowParams.acquire( _grid, goalCell );
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
            SW_LOG_WARNING_ID( "RTSWorld.RTSWorldCostCheckedBut", "RTSWorld: '%#' cost was checked but could not be paid", def._id.c_str() );
    }

    void RTSWorld::refundCost( RTSPlayer& player, const RTSUnitDef& def )
    {
        if ( player._pWallet == nullptr )
            return;
        player._pWallet->add( _settings._mineralCurrency, def._minerals );
        player._pWallet->add( _settings._gasCurrency, def._gas );
    }

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
    // 보급과 버킷
    // ------------------------------------------------------------------------------
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
        _buckets.beginRebuild( _listUnit.size() );
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            const RTSUnit& unit = _listUnit[index];
            if ( unit._bAlive )
                _buckets.insert( static_cast<int32>( index ), _grid.computeCell( unit._position ) );
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
        // 걸음 끝에 모아 푼다(`applyPathResults`) — 유닛 수만큼의 A* 가 워커로 나뉜다. 같은 걸음에 다시 다가가면 앞 요청은 버린다(마지막 목표가 이긴다).
        _pathQueue.cancel( unit._pathRequest );
        GridPathJob job;
        job._from         = unit._position;
        job._target       = target;
        unit._pathRequest = _pathQueue.request( std::move( job ) );
    }

    void RTSWorld::applyPathResults()
    {
        _pathQueue.launch( "RTSPath", game::findSubsystem<TaskManager>() );
        (void)_pathQueue.complete(); // 격자는 걸음 안에서만 바뀌고 그 뒤에 풀므로 격자 쪽에 증표를 넘길 데가 없다
        // 유닛 순서(슬롯 순서)로 적용한다 — 결과는 요청 순서 · 레인과 상관없이 같고, 적용 순서도 늘 같다(결정적).
        for ( RTSUnit& unit : _listUnit )
        {
            if ( unit._pathRequest.isValid() == false )
                continue;
            GridPathJob            job;
            const PathRequestState state = _pathQueue.takeResult( unit._pathRequest, job );
            unit._pathRequest            = PathRequestHandle{};
            if ( state != PathRequestState::Ready || unit._bAlive == SW_FALSE )
                continue;
            if ( job._result == GridPathResult::Found || job._result == GridPathResult::Partial )
                unit._agent.followPath( job._listPoint );
            else
                nudgeToWalkable( unit );
        }
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
        const float32 damage    = MathUtil::max( _settings._minimumDamage, attacker._pDef->_damage - target._pDef->_armor );
        const bool    bDepleted = DepletingValue::take( target._hp, damage )._bDepleted == SW_TRUE;
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
        if ( bDepleted )
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
        _vision.beginRepaint();
        for ( const RTSUnit& unit : _listUnit )
        {
            if ( unit._bAlive == SW_FALSE || unit._owner == kNoOwner )
                continue;
            const float32 sight = unit._pDef->_sight + ( unit.isMobile() ? 0.0f : static_cast<float32>( unit._pDef->_footprint ) * 0.5f );
            _vision.reveal( _listPlayer[static_cast<size_t>( unit._owner )]._team, _grid.computeCell( unit._position ), sight );
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
        int32       aliveTeam  = constant::invalid_index::kInt32;
        int32       aliveCount = 0;
        const int32 teamCount  = _vision.getTeamCount();
        for ( int32 team = 0; team < teamCount; ++team )
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
        if ( teamCount > 1 && aliveCount == 1 )
        {
            _winningTeam = aliveTeam;
            pushEvent( RTSEvent::Kind::GameOver, aliveTeam, RTSUnitID{}, hashed_string{} );
        }
    }

    // ------------------------------------------------------------------------------
    // 상태 쓰기와 읽기(핫 리로드, 세이브)
    // ------------------------------------------------------------------------------
    void RTSWorld::serializeState( StateIO& io )
    {
        using Internal = RTSWorldInternal;
        // 이 형식은 복사하지 않는다 — 읽기는 새 목록에 받아 끝까지 맞으면 옮기고, 쓰기는 지금 목록을 그대로 놓는다.
        io.expect( _grid.getWidth() );
        io.expect( _grid.getHeight() );
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        vector<uint8>  restoredTerrain;
        vector<uint8>& listTerrainBlocked = io.isWriting() ? _listTerrainBlocked : restoredTerrain;
        io.value( listTerrainBlocked );
        if ( io.check( listTerrainBlocked.size() == _listTerrainBlocked.size() ) == false )
            return;
        for ( const uint8 bBlocked : listTerrainBlocked )
        {
            io.check( bBlocked <= SW_TRUE );
        }

        uint32 unitCount = static_cast<uint32>( _listUnit.size() );
        io.count( Internal::kMinUnitStateBytes, unitCount );
        if ( io.isOk() == false )
            return;
        deque<RTSUnit> restoredUnit;
        vector<uint32> restoredGeneration;
        vector<uint8>  listFlowFieldOrder; // 무리 이동 명령마다 흐름장이 있었는지(유닛, 명령 순서)
        if ( io.isReading() )
        {
            restoredGeneration.resize( unitCount );
        }
        deque<RTSUnit>& listUnit       = io.isWriting() ? _listUnit : restoredUnit;
        vector<uint32>& listGeneration = io.isWriting() ? _listGeneration : restoredGeneration;
        for ( uint32 index = 0; index < unitCount; ++index )
        {
            io.value( listGeneration[index] );
            RTSUnit& unit = io.isWriting() ? listUnit[index] : listUnit.emplace_back();
            Internal::serializeUnit( io, _pCatalog, _settings, unit, listFlowFieldOrder );
            if ( io.isOk() == false )
                return;
            if ( io.isReading() && io.check( Internal::isUnitValid( unit, index, listGeneration[index], static_cast<int32>( _listPlayer.size() ), _grid ) ) == false )
                return;
        }

        // 쓸 때는 빈 칸 스택의 사본을 싣는다(저장 경로라 복사 한 번), 읽을 때는 검사를 마친 뒤에야 `restore` 로 바꾼다.
        vector<uint32> listFreeSlot = io.isWriting() ? _freeSlot.getIndices() : vector<uint32>{};
        io.list( listFreeSlot, sizeof( uint32 ), [&]( uint32& slot )
        {
            io.value( slot );
            io.check( slot < unitCount );
        } );
        uint32 playerCount = static_cast<uint32>( _listPlayer.size() );
        io.count( Internal::kMinPlayerStateBytes, playerCount );
        // 지갑은 플레이어를 추가한 쪽이 빌려 준 것이라 싣지 않는다. 같은 수의 플레이어를 먼저 추가한 월드에만 읽어야 한다.
        if ( io.check( playerCount == _listPlayer.size() ) == false )
            return;
        if ( io.isReading() && io.check( Internal::areFreeSlotsValid( listFreeSlot, listUnit ) ) == false )
            return;
        vector<RTSPlayer> restoredPlayer;
        if ( io.isReading() )
        {
            restoredPlayer.resize( playerCount );
            for ( uint32 playerIndex = 0; playerIndex < playerCount; ++playerIndex )
            {
                restoredPlayer[playerIndex]._pWallet = _listPlayer[playerIndex]._pWallet;
            }
        }
        vector<RTSPlayer>& listPlayer = io.isWriting() ? _listPlayer : restoredPlayer;
        for ( RTSPlayer& player : listPlayer )
        {
            Internal::serializePlayer( io, player );
        }
        // 시야는 사본에 읽는다 — 뒤가 깨졌으면 상태를 그대로 둬야 한다.
        RTSVision restoredVision;
        if ( io.isReading() )
        {
            restoredVision = _vision;
        }
        RTSVision& vision = io.isWriting() ? _vision : restoredVision;
        io.value( vision );
        FixedStepTimer stepTimer   = _stepTimer;
        float32        time        = _time;
        int32          winningTeam = _winningTeam;
        io.value( stepTimer );
        io.value( time );
        io.value( winningTeam );
        if ( io.isWriting() || io.isOk() == false )
            return;
        const int32 teamCount = vision.getTeamCount();
        io.check( 0.0f <= time && -1 <= winningTeam && winningTeam < teamCount );
        for ( const RTSPlayer& player : listPlayer )
        {
            io.check( Internal::isPlayerValid( player, teamCount ) );
        }
        if ( io.isOk() == false )
            return;

        _listTerrainBlocked = std::move( restoredTerrain );
        _listUnit           = std::move( restoredUnit );
        _listGeneration     = std::move( restoredGeneration );
        _freeSlot.restore( std::move( listFreeSlot ) );
        _listPlayer = std::move( restoredPlayer );
        _vision     = std::move( restoredVision );
        _eventBuffer.clear();
        _flowParams.clear();
        _stepTimer   = stepTimer;
        _time        = time;
        _winningTeam = winningTeam;

        // 격자는 땅과 서 있는 건물, 자원의 발자국으로 다시 칠한다(`repaintGrid`). 같은 데이터에서 나오므로 저장하지 않는다.
        const int32   width    = _grid.getWidth();
        const int32   height   = _grid.getHeight();
        const float32 cellSize = _grid.getCellSize();
        const float3  origin   = _grid.getOrigin();
        if ( io.check( _grid.initialize( width, height, cellSize, origin ) ) == false )
            return;
        repaintGrid();
        restoreAgents( listFlowFieldOrder );
        rebuildBuckets();
    }

    void RTSWorld::restoreAgents( const vector<uint8>& listFlowFieldOrder )
    {
        size_t flowFieldOrderIndex = 0;
        for ( RTSUnit& unit : _listUnit )
        {
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
                    order._pFlowField = _flowParams.acquire( _grid, _grid.computeCell( order._target ) );
            }
        }
    }
} // namespace sw

namespace sw
{
    int32 RTSWorld::applyCommand( int32 player, const RTSCommand& command )
    {
        vector<RTSUnitID> listOwned;
        listOwned.reserve( command._listUnit.size() );
        for ( const RTSUnitID unitID : command._listUnit )
        {
            const RTSUnit* pUnit = findUnit( unitID );
            if ( pUnit != nullptr && pUnit->_owner == player )
                listOwned.push_back( unitID );
        }
        if ( listOwned.empty() )
            return 0;
        const bool bQueue = command._bQueue != SW_FALSE;
        int32      count  = 0;
        switch ( command._kind )
        {
            case RTSCommandKind::Smart:
                for ( const RTSUnitID unitID : listOwned )
                {
                    count += issueSmart( unitID, command._target, command._targetID, bQueue ) == RTSCommandResult::Ok ? 1 : 0;
                }
                break;
            case RTSCommandKind::GroupMove:
                count = issueGroupMove( listOwned, command._target, command._bAttackMove != SW_FALSE, bQueue );
                break;
            case RTSCommandKind::Hold:
            case RTSCommandKind::Stop:
                for ( const RTSUnitID unitID : listOwned )
                {
                    const RTSCommandResult result = command._kind == RTSCommandKind::Hold ? issueHold( unitID ) : issueStop( unitID );
                    count += result == RTSCommandResult::Ok ? 1 : 0;
                }
                break;
            case RTSCommandKind::Build:
                count = issueBuild( listOwned.front(), command._defID, command._cell, bQueue ) == RTSCommandResult::Ok ? 1 : 0;
                break;
            case RTSCommandKind::Train:
                count = train( listOwned.front(), command._defID ) == RTSCommandResult::Ok ? 1 : 0;
                break;
            case RTSCommandKind::Rally:
                for ( const RTSUnitID unitID : listOwned )
                {
                    const RTSUnit* pUnit = findUnit( unitID );
                    if ( pUnit->isBuilding() == false )
                        continue;
                    setRallyPoint( unitID, command._target );
                    ++count;
                }
                break;
            case RTSCommandKind::Count:
                break;
        }
        return count;
    }
} // namespace sw
