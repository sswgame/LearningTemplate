#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSWorld.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Inventory/Shop.h"
#include "GameFramework/Base/Gameplay/Match/TeamAttitude.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct RTSWorldInternal
        {
            static constexpr float32 kMaxUnitExtent = 4.5f; ///< 버킷 조회에 더하는 몸 크기의 상한(건물 한 변의 절반)
        };
    } // namespace

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
            const bool bStaticBlocker = unit._bAlive != SW_FALSE && unit.isMobile() == false && unit._pDef->_bExtractor == SW_FALSE;
            if ( bStaticBlocker )
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
    // 유닛 슬롯
    // ------------------------------------------------------------------------------
    RTSUnitID RTSWorld::allocateUnit()
    {
        // 빈 슬롯은 마지막에 놓인 것부터 다시 나간다(LIFO). 유닛 id 가 그 순서를 따른다.
        const uint32 index = reserveFreeListIndex( _listUnit, _listFreeSlot );
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
            _listFreeSlot.push_back( static_cast<uint32>( index ) );
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
        const RTSUnitDef* pDef = _pCatalog != nullptr ? _pCatalog->findUnit( defID ) : nullptr;
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
        const size_t unitCount = _listUnit.size(); // 이번 걸음에 생긴 유닛은 다음 걸음부터 처리한다
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

    int32 RTSWorld::computeBucketIndex( const float3& position ) const
    {
        const int2  cell    = _grid.computeCell( position );
        const int32 bucketX = MathUtil::clamp( cell._x / _settings._bucketSize, 0, _bucketTopology._width - 1 );
        const int32 bucketY = MathUtil::clamp( cell._y / _settings._bucketSize, 0, _bucketTopology._height - 1 );
        return _bucketTopology.toIndex( bucketX, bucketY );
    }

    void RTSWorld::queryUnits( const float3& center, float32 radius, vector<RTSUnitID>& outListUnit ) const
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
                    const float32 distance = unit.isMobile() ? float3::getHorizontalDistance( center, unit._position ) - unit._pDef->_radius
                                                             : computeRectDistance( center, unit._cell, unit._pDef->_footprint );
                    if ( distance <= radius )
                        outListUnit.push_back( unit._id );
                }
            }
        }
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

    RTSUnitID RTSWorld::findAutoTarget( const RTSUnit& unit, float32 radius ) const
    {
        vector<RTSUnitID> listCandidate;
        queryUnits( unit._position, radius + ( unit.isMobile() ? unit._pDef->_radius : 0.0f ), listCandidate );
        RTSUnitID bestID{};
        float32   bestScore = MathUtil::kMaxFloat;
        for ( const RTSUnitID candidateID : listCandidate )
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
        if ( pPlayer == nullptr || _grid.isInside( cell ) == false )
            return RTSVisibility::Unexplored;
        return static_cast<RTSVisibility>( _listTeamVisibility[static_cast<size_t>( pPlayer->_team )][static_cast<size_t>( _grid.computeIndex( cell ) )] );
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
        event._player = player;
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
} // namespace sw
