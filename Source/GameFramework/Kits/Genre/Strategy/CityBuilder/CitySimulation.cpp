#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

namespace sw
{
    namespace
    {
        struct CitySimulationInternal
        {
            static constexpr uint32 kMinBuildingStateBytes = 64; ///< 건물 하나의 상태가 최소한 차지하는 바이트 수(개수 상한 계산용)
            static constexpr uint32 kMinWalkerStateBytes   = 56; ///< 일꾼 하나의 상태가 최소한 차지하는 바이트 수(개수 상한 계산용)

            /** @brief 노동을 나누는 순서입니다. 물, 식량 사슬, 나머지 순이며 값이 작을수록 먼저입니다. */
            static int32 computeLaborPriority( const CityBuildingDef& def )
            {
                if ( def._kind == CityBuildingKind::Service && def._service == CityService::Water )
                    return 0;
                const bool bStorageLikeBuilding = def._kind == CityBuildingKind::Producer || def._kind == CityBuildingKind::Storage || def._kind == CityBuildingKind::Market;
                if ( bStorageLikeBuilding )
                    return 1;
                return 2;
            }

            static int32 computeChebyshev( const int2& tile, const int2& origin, int32 size )
            {
                const int32 dx = tile._x < origin._x ? origin._x - tile._x : ( tile._x >= origin._x + size ? tile._x - ( origin._x + size - 1 ) : 0 );
                const int32 dy = tile._y < origin._y ? origin._y - tile._y : ( tile._y >= origin._y + size ? tile._y - ( origin._y + size - 1 ) : 0 );
                return MathUtil::max( dx, dy );
            }

            /** @brief 한 사람 더 받으려는 집이 바라는 물자 재고를 반환합니다(두 달치). */
            static int32 computeStockTarget( int32 population, int32 goodsPerFourPeople )
            {
                return MathUtil::max( 2, ( population + 3 ) / 4 * goodsPerFourPeople * 2 );
            }

            /**
             * @brief 읽은 건물 하나를 봅니다. 살아 있는 건물은 정의를 바로 찌르고(`storage._pDef->_kind`), 발밑을 격자 번호로 바로 바꾼다(`demolish`) — 정의가 없거나 격자 밖이면 범위 밖이다.
             */
            [[nodiscard]] static bool isBuildingValid( const CityBuilding& building, const CityCatalog& catalog, const GridTopology& topology )
            {
                if ( building._bAlive > SW_TRUE )
                    return false;
                if ( building._bAlive == SW_FALSE )
                    return true;
                if ( building._pDef == nullptr )
                    return false;
                const CityBuildingDef& def         = *building._pDef;
                const bool             bPlaceValid = topology.isRectInside( building._origin, int2{ def._size, def._size } ) && ( building.hasRoadAccess() ? topology.isInside( building._accessTile ) : building._accessTile == int2{ -1, -1 } );
                const bool             bTimeValid  = 0.0f <= building._walkerTimer && 0.0f <= building._productionProgress && 0.0f <= building._evolveTimer && 0.0f <= building._devolveTimer && 0.0f <= building._immigrationTimer;
                const bool             bWorkValid  = 0.0f <= building._efficiency && building._efficiency <= 1.0f && 0 <= building._assignedWorkers && building._assignedWorkers <= def._workers && 0 <= building._activeWalkerCount;
                const int32            levelCount  = static_cast<int32>( catalog.getHouseLevels().size() );
                const bool             bHouseValid = building.isHouse() ? 0 <= building._level && ( building._level == 0 || building._level < levelCount ) && 0 <= building._population : building._level == 0 && building._population == 0;
                return bPlaceValid && bTimeValid && bWorkValid && bHouseValid;
            }

            /** @brief 읽은 타일과 건물이 서로 맞는지 봅니다. 타일의 번호는 건물 목록을 바로 찌르고, 살아 있는 건물의 발밑은 모두 그 건물을 가리킵니다. */
            [[nodiscard]] static bool areTilesValid( const vector<CityTile>& listTile, const vector<CityBuilding>& listBuilding, const GridTopology& topology )
            {
                const int32 buildingCount = static_cast<int32>( listBuilding.size() );
                for ( const CityTile& tile : listTile )
                {
                    const bool bIndexValid = constant::invalid_index::kInt32 <= tile._buildingIndex && tile._buildingIndex < buildingCount && ( tile._buildingIndex < 0 || listBuilding[static_cast<size_t>( tile._buildingIndex )]._bAlive != SW_FALSE );
                    // 도로 위에는 건물을 짓지 못한다(`placeBuilding`).
                    const bool bRoadValid = tile._bRoad <= SW_TRUE && -1 <= tile._roadComponent && ( tile._bRoad == SW_FALSE || tile._buildingIndex < 0 );
                    if ( bIndexValid == false || bRoadValid == false )
                        return false;
                }
                int32 footprintSum = 0;
                for ( int32 buildingIndex = 0; buildingIndex < buildingCount; ++buildingIndex )
                {
                    const CityBuilding& building = listBuilding[static_cast<size_t>( buildingIndex )];
                    if ( building._bAlive == SW_FALSE )
                        continue;
                    const int32 size = building._pDef->_size;
                    for ( int32 dy = 0; dy < size; ++dy )
                    {
                        for ( int32 dx = 0; dx < size; ++dx )
                        {
                            if ( listTile[static_cast<size_t>( topology.toIndex( building._origin._x + dx, building._origin._y + dy ) )]._buildingIndex != buildingIndex )
                                return false;
                        }
                    }
                    footprintSum += size * size;
                }
                // 발밑이 모두 맞고 건물을 가리키는 타일 수가 발밑의 합이면, 남의 발밑 밖에서 건물을 가리키는 타일은 없다.
                int32 occupiedCount = 0;
                for ( const CityTile& tile : listTile )
                {
                    occupiedCount += tile._buildingIndex >= 0 ? 1 : 0;
                }
                return occupiedCount == footprintSum;
            }

            /**
             * @brief 읽은 일꾼 하나를 봅니다. 살아 있는 일꾼의 집과 목적지는 살아 있는 건물이다(`demolish` 가 함께 지운다). 집은 `startReturn` 이 바로 찌른다.
             * @details 진행은 걸음마다 1 을 빼 1 아래이다. 터무니없는 값은 `updateWalkers` 의 고리가 끝나지 않는다(1e30 - 1 == 1e30).
             */
            [[nodiscard]] static bool isWalkerValid( const CityWalker& walker, const vector<CityBuilding>& listBuilding, const GridTopology& topology )
            {
                if ( walker._bAlive == SW_FALSE )
                    return true;
                const bool bHomeAlive   = walker._homeBuilding >= 0 && listBuilding[static_cast<size_t>( walker._homeBuilding )]._bAlive != SW_FALSE;
                const bool bTargetAlive = walker._targetBuilding < 0 || listBuilding[static_cast<size_t>( walker._targetBuilding )]._bAlive != SW_FALSE;
                const bool bTilesValid  = topology.isInside( walker._tile ) && topology.isInside( walker._nextTile ) && ( walker._previousTile == int2{ -1, -1 } || topology.isInside( walker._previousTile ) );
                if ( bHomeAlive == false || bTargetAlive == false || bTilesValid == false || walker._progress < 0.0f || 1.0f <= walker._progress )
                    return false;
                for ( const int2& tile : walker._listPath )
                {
                    if ( topology.isInside( tile ) == false )
                        return false;
                }
                for ( const int2& tile : walker._listVisited )
                {
                    if ( topology.isInside( tile ) == false )
                        return false;
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CitySimulation::CitySimulation()
        : _listTile{}
        , _listBuilding{}
        , _listWalker{}
        , _eventBuffer{}
        , _roadSearch{}
        , _pCatalog{ nullptr }
        , _pWallet{ nullptr }
        , _settings{}
        , _stepTimer{}
        , _random{}
        , _time{ 0.0f }
        , _floodFertility{ 0.8f }
        , _wageDebt{ 0.0f }
        , _topology{}
        , _land{}
        , _monthIncome{ 0 }
        , _workforce{ 0 }
        , _employed{ 0 }
        , _bRoadsDirty{ SW_FALSE }
        , _bDesirabilityDirty{ SW_FALSE }
    {
    }

    bool CitySimulation::initialize( const CityCatalog* pCatalog, int32 width, int32 height, const CitySettings& settings, const SessionStateRefs& refs )
    {
        if ( width < 1 || height < 1 )
        {
            SW_LOG_ERROR_ID( "CitySimulation.CitySimulationRefusedMapSize", "CitySimulation: refused map size %#x%# (needs >= 1 in both)", width, height );
            return false;
        }
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "CitySimulation.CitySimulationRefusedSettings", "CitySimulation: refused settings - %#", rangeError.c_str() );
            return false;
        }
        _pCatalog = pCatalog;
        _pWallet  = refs._pWallet;
        if ( _pWallet == nullptr )
            SW_LOG_WARNING_ID( "CitySimulation.CitySimulationNoWalletLent", "CitySimulation: no wallet was lent - every road and building is refused" );
        _settings = settings;
        _topology = GridTopology{ width, height };
        _listTile.assign( static_cast<size_t>( _topology.getCellCount() ), CityTile{} );
        _listBuilding.clear();
        _listWalker.clear();
        _eventBuffer.clear();
        _stepTimer = FixedStepTimer( settings._fixedStep, 5.0f );
        _random.setSeed( settings._randomSeed );
        _time               = 0.0f;
        _floodFertility     = 0.8f;
        _wageDebt           = 0.0f;
        _monthIncome        = 0;
        _workforce          = 0;
        _employed           = 0;
        _bRoadsDirty        = SW_FALSE;
        _bDesirabilityDirty = SW_TRUE;
        return true;
    }

    void CitySimulation::setTerrain( int32 x, int32 y, CityTerrain terrain )
    {
        if ( _topology.isInside( x, y ) == false )
            return;
        _listTile[static_cast<size_t>( _topology.toIndex( x, y ) )]._terrain = terrain;
    }

    void CitySimulation::fillTerrain( int32 minX, int32 minY, int32 maxX, int32 maxY, CityTerrain terrain )
    {
        for ( int32 y = minY; y <= maxY; ++y )
        {
            for ( int32 x = minX; x <= maxX; ++x )
            {
                setTerrain( x, y, terrain );
            }
        }
    }

    const CityTile* CitySimulation::findTile( int32 x, int32 y ) const
    {
        if ( _topology.isInside( x, y ) == false )
            return nullptr;
        return &_listTile[static_cast<size_t>( _topology.toIndex( x, y ) )];
    }

    const CityBuilding* CitySimulation::findBuildingAt( int32 x, int32 y ) const
    {
        const CityTile* pTile = findTile( x, y );
        return pTile != nullptr && pTile->_buildingIndex >= 0 ? &_listBuilding[static_cast<size_t>( pTile->_buildingIndex )] : nullptr;
    }

    bool CitySimulation::isRoad( int32 x, int32 y ) const
    {
        const CityTile* pTile = findTile( x, y );
        return pTile != nullptr && pTile->_bRoad != SW_FALSE;
    }

    int32 CitySimulation::getRoadComponent( const int2& tile ) const
    {
        const CityTile* pTile = findTile( tile._x, tile._y );
        return pTile != nullptr ? pTile->_roadComponent : constant::invalid_index::kInt32;
    }

    // ------------------------------------------------------------------------------
    // 건설
    // ------------------------------------------------------------------------------
    void CitySimulation::bindLand( LandRegistry* pLand, const int2& origin )
    {
        _land.bind( pLand, origin, hashed_string( "CityBuilder" ) );
    }

    CityPlaceResult CitySimulation::placeRoad( int32 x, int32 y )
    {
        const CityTile* pTile = findTile( x, y );
        if ( pTile == nullptr )
            return CityPlaceResult::OutOfBounds;
        const bool bTileOccupied = pTile->_bRoad != SW_FALSE || pTile->_buildingIndex >= 0 || _land.isUsable( x, y ) == false;
        if ( bTileOccupied )
            return CityPlaceResult::Occupied;
        if ( pTile->_terrain == CityTerrain::Water || pTile->_terrain == CityTerrain::Rock )
            return CityPlaceResult::BadTerrain;
        const int32 cost = _pCatalog != nullptr ? _pCatalog->getRoadCost() : 2;
        if ( _pWallet == nullptr || _pWallet->trySpend( _settings._currency, cost ) == false )
            return CityPlaceResult::NotEnoughMoney;
        (void)_land.claimRect( x, y, x, y, false ); // 위에서 이미 확인했다
        _listTile[static_cast<size_t>( _topology.toIndex( x, y ) )]._bRoad = SW_TRUE;
        _bRoadsDirty                                                       = SW_TRUE;
        return CityPlaceResult::Ok;
    }

    int32 CitySimulation::placeRoadLine( const int2& from, const int2& to )
    {
        int32 placedCount = 0;
        int2  cursor      = from;
        placedCount += placeRoad( cursor._x, cursor._y ) == CityPlaceResult::Ok ? 1 : 0;
        while ( cursor._x != to._x )
        {
            cursor._x += to._x > cursor._x ? 1 : -1;
            placedCount += placeRoad( cursor._x, cursor._y ) == CityPlaceResult::Ok ? 1 : 0;
        }
        while ( cursor._y != to._y )
        {
            cursor._y += to._y > cursor._y ? 1 : -1;
            placedCount += placeRoad( cursor._x, cursor._y ) == CityPlaceResult::Ok ? 1 : 0;
        }
        return placedCount;
    }

    CityPlaceResult CitySimulation::placeBuilding( const hashed_string& buildingID, int32 x, int32 y )
    {
        const CityBuildingDef* pDef = _pCatalog != nullptr ? _pCatalog->findBuilding( buildingID ) : nullptr;
        if ( pDef == nullptr )
            return CityPlaceResult::UnknownBuilding;
        // 범위를 먼저 확인한다. 발밑 일부만 맵 밖이어도 "땅이 나쁘다" 가 아니라 "범위 밖" 으로 처리한다.
        if ( _topology.isRectInside( int2{ x, y }, int2{ pDef->_size, pDef->_size } ) == false )
            return CityPlaceResult::OutOfBounds;
        for ( int32 dy = 0; dy < pDef->_size; ++dy )
        {
            for ( int32 dx = 0; dx < pDef->_size; ++dx )
            {
                const CityTile* pTile              = findTile( x + dx, y + dy );
                const bool      bFootprintOccupied = pTile->_bRoad != SW_FALSE || pTile->_buildingIndex >= 0 || _land.isUsable( x + dx, y + dy ) == false;
                if ( bFootprintOccupied )
                    return CityPlaceResult::Occupied;
                if ( pTile->_terrain == CityTerrain::Water || pTile->_terrain == CityTerrain::Rock )
                    return CityPlaceResult::BadTerrain;
                if ( pDef->_bRequiresTerrain != SW_FALSE && pTile->_terrain != pDef->_requiredTerrain )
                    return CityPlaceResult::BadTerrain;
            }
        }
        if ( _pWallet == nullptr || _pWallet->trySpend( _settings._currency, pDef->_cost ) == false )
            return CityPlaceResult::NotEnoughMoney;
        (void)_land.claimRect( x, y, x + pDef->_size - 1, y + pDef->_size - 1, true ); // 위에서 타일마다 확인했다

        // 허문 슬롯을 다시 쓴다. 타일이 가리키는 번호가 바뀌지 않도록 목록에서 지우지 않는다.
        int32 index = constant::invalid_index::kInt32;
        for ( size_t buildingIndex = 0; buildingIndex < _listBuilding.size(); ++buildingIndex )
        {
            if ( _listBuilding[buildingIndex]._bAlive == SW_FALSE )
            {
                index = static_cast<int32>( buildingIndex );
                break;
            }
        }
        if ( index < 0 )
        {
            index = static_cast<int32>( _listBuilding.size() );
            _listBuilding.push_back( CityBuilding{} );
        }
        CityBuilding& building = _listBuilding[static_cast<size_t>( index )];
        building               = CityBuilding{};
        building._pDef         = pDef;
        building._origin       = int2{ x, y };
        for ( float32& serviceTime : building._arrServiceTime )
        {
            serviceTime = -1.0e9f;
        }
        for ( int32 dy = 0; dy < pDef->_size; ++dy )
        {
            for ( int32 dx = 0; dx < pDef->_size; ++dx )
            {
                _listTile[static_cast<size_t>( _topology.toIndex( x + dx, y + dy ) )]._buildingIndex = index;
            }
        }
        refreshAccess( building );
        _bDesirabilityDirty = SW_TRUE;
        return CityPlaceResult::Ok;
    }

    bool CitySimulation::demolish( int32 x, int32 y )
    {
        const CityTile* pTile = findTile( x, y );
        if ( pTile == nullptr )
            return false;
        CityTile& tile = _listTile[static_cast<size_t>( _topology.toIndex( x, y ) )];
        if ( tile._bRoad != SW_FALSE )
        {
            tile._bRoad  = SW_FALSE;
            _bRoadsDirty = SW_TRUE;
            _land.releaseRect( x, y, x, y );
            return true;
        }
        if ( tile._buildingIndex < 0 )
            return false;
        const int32   index    = tile._buildingIndex;
        CityBuilding& building = _listBuilding[static_cast<size_t>( index )];
        for ( int32 dy = 0; dy < building._pDef->_size; ++dy )
        {
            for ( int32 dx = 0; dx < building._pDef->_size; ++dx )
            {
                _listTile[static_cast<size_t>( _topology.toIndex( building._origin._x + dx, building._origin._y + dy ) )]._buildingIndex = constant::invalid_index::kInt32;
            }
        }
        _land.releaseRect( building._origin._x, building._origin._y, building._origin._x + building._pDef->_size - 1, building._origin._y + building._pDef->_size - 1 );
        building._bAlive     = SW_FALSE;
        building._population = 0;
        for ( CityWalker& walker : _listWalker )
        {
            if ( walker._homeBuilding == index || walker._targetBuilding == index )
                walker._bAlive = SW_FALSE;
        }
        _bDesirabilityDirty = SW_TRUE;
        return true;
    }

    void CitySimulation::refreshAccess( CityBuilding& building ) const
    {
        // 둘레 한 셀 안의 도로를 찾는다(대각선은 제외). 집은 파라오처럼 두 셀 안이면 된다.
        building._accessTile  = int2{ -1, -1 };
        const int32 size      = building._pDef->_size;
        const int32 reach     = building.isHouse() ? 2 : 1;
        int32       bestScore = 1 << 30;
        for ( int32 y = building._origin._y - reach; y < building._origin._y + size + reach; ++y )
        {
            for ( int32 x = building._origin._x - reach; x < building._origin._x + size + reach; ++x )
            {
                if ( isRoad( x, y ) == false )
                    continue;
                const bool bInsideX     = x >= building._origin._x && x < building._origin._x + size;
                const bool bInsideY     = y >= building._origin._y && y < building._origin._y + size;
                const bool bNotEntrance = building.isHouse() == false && bInsideX == false && bInsideY == false;
                if ( bNotEntrance )
                    continue; // 대각선 모서리는 입구가 아니다
                const int32 score = CitySimulationInternal::computeChebyshev( int2{ x, y }, building._origin, size ) * 100 + y * 10 + x;
                if ( score < bestScore )
                {
                    bestScore            = score;
                    building._accessTile = int2{ x, y };
                }
            }
        }
    }

    void CitySimulation::recomputeRoadComponents()
    {
        for ( CityTile& tile : _listTile )
        {
            tile._roadComponent = constant::invalid_index::kInt32;
        }
        // 도로 조각마다 너비 우선 탐색을 한다. 큐는 길 찾기와 같은 재사용 스크래치를 쓰고, 조각 번호가 타일의 "봤다" 표시를 겸한다.
        int32 component = 0;
        for ( int32 y = 0; y < _topology._height; ++y )
        {
            for ( int32 x = 0; x < _topology._width; ++x )
            {
                CityTile& seed = _listTile[static_cast<size_t>( _topology.toIndex( x, y ) )];
                if ( seed._bRoad == SW_FALSE || seed._roadComponent >= 0 )
                    continue;
                seed._roadComponent = component;
                _roadSearch.begin( _topology.getCellCount() );
                _roadSearch.visit( _topology.toIndex( x, y ), constant::invalid_index::kInt32 );
                while ( _roadSearch.hasNext() )
                {
                    const int32 index = _roadSearch.popNext();
                    const int2  tile  = _topology.toCell( index );
                    for ( int32 direction = 0; direction < GridTopology::kOrthogonalCount; ++direction )
                    {
                        const int2 next = GridTopology::getNeighbor( tile, direction );
                        if ( isRoad( next._x, next._y ) == false )
                            continue;
                        CityTile& nextTile = _listTile[static_cast<size_t>( _topology.toIndex( next ) )];
                        if ( nextTile._roadComponent >= 0 )
                            continue;
                        nextTile._roadComponent = component;
                        _roadSearch.visit( _topology.toIndex( next ), index );
                    }
                }
                ++component;
            }
        }
        for ( CityBuilding& building : _listBuilding )
        {
            if ( building._bAlive != SW_FALSE )
                refreshAccess( building );
        }
        // 도로가 끊겨 지금 타일이 도로가 아닌 일꾼은 사라진다.
        for ( CityWalker& walker : _listWalker )
        {
            if ( isRoad( walker._tile._x, walker._tile._y ) == false )
                removeWalker( walker );
        }
        _bRoadsDirty = SW_FALSE;
    }

    void CitySimulation::recomputeDesirability()
    {
        for ( CityTile& tile : _listTile )
        {
            tile._desirability = 0;
        }
        for ( const CityBuilding& building : _listBuilding )
        {
            if ( building._bAlive == SW_FALSE || building._pDef->_desirability == 0 )
                continue;
            const int32 radius = building._pDef->_desirabilityRadius;
            const int32 size   = building._pDef->_size;
            for ( int32 y = building._origin._y - radius; y < building._origin._y + size + radius; ++y )
            {
                for ( int32 x = building._origin._x - radius; x < building._origin._x + size + radius; ++x )
                {
                    if ( _topology.isInside( x, y ) == false )
                        continue;
                    // 발밑과 바로 둘레는 다 받고, 반경 끝으로 갈수록 줄어든다.
                    const int32 distance = CitySimulationInternal::computeChebyshev( int2{ x, y }, building._origin, size );
                    const int32 value    = building._pDef->_desirability * ( radius + 1 - MathUtil::max( 0, distance - 1 ) ) / ( radius + 1 );
                    CityTile&   tile     = _listTile[static_cast<size_t>( _topology.toIndex( x, y ) )];
                    tile._desirability   = static_cast<int16>( tile._desirability + value );
                }
            }
        }
        _bDesirabilityDirty = SW_FALSE;
    }

    // ------------------------------------------------------------------------------
    // 시간
    // ------------------------------------------------------------------------------
    void CitySimulation::update( float32 deltaTime )
    {
        const int32 stepCount = _stepTimer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepFixed( _stepTimer.getStep() );
        }
    }

    void CitySimulation::pollEvents( const ConsumerLease& lease, vector<CityEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( lease, outListEvent );
    }

    void CitySimulation::stepFixed( float32 deltaTime )
    {
        _time += deltaTime;
        if ( _bRoadsDirty != SW_FALSE )
            recomputeRoadComponents();
        if ( _bDesirabilityDirty != SW_FALSE )
            recomputeDesirability();
        assignLabor();
        updateBuildings( deltaTime );
        updateWalkers( deltaTime );
        updateHouses( deltaTime );
        VectorUtil::removeIf( _listWalker, []( const CityWalker& walker )
        { return walker._bAlive == SW_FALSE; } );
    }

    void CitySimulation::assignLabor()
    {
        _workforce      = static_cast<int32>( static_cast<float32>( getPopulation() ) * _settings._workerRatio );
        _employed       = 0;
        int32 remaining = _workforce;
        for ( int32 priority = 0; priority < 3; ++priority )
        {
            for ( CityBuilding& building : _listBuilding )
            {
                const bool bSkipLaborBuilding = building._bAlive == SW_FALSE || building.isHouse() || CitySimulationInternal::computeLaborPriority( *building._pDef ) != priority;
                if ( bSkipLaborBuilding )
                    continue;
                const int32 needed = building._pDef->_workers;
                // 길이 없으면 일꾼이 오지 못한다. 반경 서비스(우물)는 길이 없어도 된다.
                const bool bReachable     = building.hasRoadAccess() || building._pDef->_delivery == CityDelivery::Radius;
                building._assignedWorkers = bReachable ? MathUtil::min( needed, remaining ) : 0;
                remaining -= building._assignedWorkers;
                _employed += building._assignedWorkers;
                building._efficiency = bReachable ? ( needed > 0 ? static_cast<float32>( building._assignedWorkers ) / static_cast<float32>( needed ) : 1.0f ) : 0.0f;
            }
        }
    }

    void CitySimulation::updateBuildings( float32 deltaTime )
    {
        for ( int32 buildingIndex = 0; buildingIndex < static_cast<int32>( _listBuilding.size() ); ++buildingIndex )
        {
            CityBuilding& building          = _listBuilding[static_cast<size_t>( buildingIndex )];
            const bool    bInactiveBuilding = building._bAlive == SW_FALSE || building.isHouse() || building._efficiency <= 0.0f;
            if ( bInactiveBuilding )
                continue;
            const CityBuildingDef& def = *building._pDef;
            switch ( def._kind )
            {
                case CityBuildingKind::Service:
                {
                    if ( def._delivery == CityDelivery::Radius )
                    {
                        // 반경 안의 집에 항상 준다(파라오의 우물).
                        for ( CityBuilding& house : _listBuilding )
                        {
                            const bool bHouseInServiceRange = house._bAlive != SW_FALSE && house.isHouse() &&
                                                              CitySimulationInternal::computeChebyshev( house._origin, building._origin, def._size ) <= def._range;
                            if ( bHouseInServiceRange )
                                house._arrServiceTime[static_cast<int32>( def._service )] = _time;
                        }
                        break;
                    }
                    building._walkerTimer += deltaTime * building._efficiency;
                    const bool bShouldSpawnServiceWalker = building._walkerTimer >= def._walkerInterval && building._activeWalkerCount == 0 && building.hasRoadAccess();
                    if ( bShouldSpawnServiceWalker )
                    {
                        building._walkerTimer = 0.0f;
                        spawnRoamer( buildingIndex, CityWalkerKind::Service );
                    }
                    break;
                }
                case CityBuildingKind::Producer:
                {
                    // 범람원 농장은 해마다의 범람이, 다른 땅은 반쯤의 비옥함이 생산량을 정한다.
                    const float32 fertility = ( def._bRequiresTerrain != SW_FALSE && def._requiredTerrain == CityTerrain::Floodplain ) ? _floodFertility : 1.0f;
                    building._productionProgress += deltaTime * building._efficiency * fertility / def._productionTime;
                    if ( building._productionProgress >= 1.0f && def._listGood.empty() == false )
                    {
                        building._productionProgress -= 1.0f;
                        building._stock.addItem( def._listGood.front(), def._productionAmount );
                    }
                    const bool bShouldSpawnCart = building._activeWalkerCount == 0 && building._stock.isEmpty() == false && building.hasRoadAccess();
                    if ( bShouldSpawnCart )
                        spawnCart( buildingIndex );
                    break;
                }
                case CityBuildingKind::Market:
                {
                    // 사 오기: 같은 도로망의 창고에서 파는 물자마다 상한의 일정 비율까지 사 온다.
                    const int32 component = getRoadComponent( building._accessTile );
                    const int32 perGood   = def._listGood.empty() ? 0 : def._capacity / static_cast<int32>( def._listGood.size() );
                    for ( const hashed_string& goodID : def._listGood )
                    {
                        int32 want = perGood - building._stock.getItemCount( goodID );
                        for ( CityBuilding& storage : _listBuilding )
                        {
                            if ( want <= 0 )
                                break;
                            const bool bUnusableStorage = storage._bAlive == SW_FALSE || storage._pDef->_kind != CityBuildingKind::Storage || storage._efficiency <= 0.0f ||
                                                          getRoadComponent( storage._accessTile ) != component || component < 0;
                            if ( bUnusableStorage )
                                continue;
                            const int32 moved = MathUtil::min( want, storage._stock.getItemCount( goodID ) );
                            if ( moved > 0 && storage._stock.moveItemTo( building._stock, goodID, moved ) )
                                want -= moved;
                        }
                    }
                    building._walkerTimer += deltaTime * building._efficiency;
                    const bool bShouldSpawnTrader = building._walkerTimer >= def._walkerInterval && building._activeWalkerCount == 0 && building._stock.isEmpty() == false &&
                                                    building.hasRoadAccess();
                    if ( bShouldSpawnTrader )
                    {
                        building._walkerTimer = 0.0f;
                        spawnRoamer( buildingIndex, CityWalkerKind::Trader );
                    }
                    break;
                }
                case CityBuildingKind::House:
                case CityBuildingKind::Storage:
                case CityBuildingKind::Decoration:
                {
                    break;
                }
            }
        }
    }

    // ------------------------------------------------------------------------------
    // 일꾼
    // ------------------------------------------------------------------------------
    void CitySimulation::spawnRoamer( int32 buildingIndex, CityWalkerKind kind )
    {
        CityBuilding& building = _listBuilding[static_cast<size_t>( buildingIndex )];
        CityWalker    walker;
        walker._kind         = kind;
        walker._service      = building._pDef->_service;
        walker._homeBuilding = buildingIndex;
        walker._tile         = building._accessTile;
        walker._nextTile     = building._accessTile;
        walker._stepsLeft    = building._pDef->_range;
        walker._listVisited.push_back( walker._tile );
        ++building._activeWalkerCount;
        _listWalker.push_back( walker );
        serveAround( _listWalker.back() );
    }

    void CitySimulation::spawnCart( int32 buildingIndex )
    {
        CityBuilding& building = _listBuilding[static_cast<size_t>( buildingIndex )];
        const auto&   items    = building._stock.getItems();
        if ( items.empty() )
            return;
        const hashed_string   goodID       = items.front()._itemID;
        const int32           amount       = items.front()._count;
        const int32           component    = getRoadComponent( building._accessTile );
        const optional<int32> foundStorage = findStorageFor( goodID, component, building._accessTile, 1 );
        if ( foundStorage.has_value() == false )
            return;
        const int32 storage = *foundStorage;
        CityWalker  walker;
        if ( findRoadPath( building._accessTile, _listBuilding[static_cast<size_t>( storage )]._accessTile, walker._listPath ) == false )
            return;
        const int32 load = MathUtil::min( amount, computeStorageFree( _listBuilding[static_cast<size_t>( storage )] ) );
        if ( load <= 0 || building._stock.removeItem( goodID, load ) == false )
            return;
        walker._kind           = CityWalkerKind::Cart;
        walker._homeBuilding   = buildingIndex;
        walker._targetBuilding = storage;
        walker._cargoGood      = goodID;
        walker._cargoAmount    = load;
        walker._tile           = building._accessTile;
        walker._nextTile       = building._accessTile;
        ++building._activeWalkerCount;
        _listWalker.push_back( walker );
    }

    int32 CitySimulation::computeStorageFree( const CityBuilding& storage ) const
    {
        int32 incoming = 0;
        for ( const CityWalker& walker : _listWalker )
        {
            const bool bIncomingCart = walker._bAlive != SW_FALSE && walker._kind == CityWalkerKind::Cart && walker._targetBuilding >= 0 &&
                                     &_listBuilding[static_cast<size_t>( walker._targetBuilding )] == &storage;
            if ( bIncomingCart )
                incoming += walker._cargoAmount;
        }
        return storage._pDef->_capacity - storage._stock.getTotalCount() - incoming;
    }

    optional<int32> CitySimulation::findStorageFor( const hashed_string& goodID, int32 roadComponent, const int2& from, int32 amount ) const
    {
        optional<int32> best;
        int32           bestDistance = 1 << 30;
        for ( int32 buildingIndex = 0; buildingIndex < static_cast<int32>( _listBuilding.size() ); ++buildingIndex )
        {
            const CityBuilding& storage          = _listBuilding[static_cast<size_t>( buildingIndex )];
            const bool          bStorageRejected = storage._bAlive == SW_FALSE || storage._pDef->_kind != CityBuildingKind::Storage || storage._efficiency <= 0.0f || roadComponent < 0 ||
                                          getRoadComponent( storage._accessTile ) != roadComponent;
            if ( bStorageRejected )
                continue;
            if ( VectorUtil::contains( storage._pDef->_listGood, goodID ) == false )
                continue;
            if ( computeStorageFree( storage ) < amount )
                continue;
            const int32 distance = MathUtil::abs( storage._accessTile._x - from._x ) + MathUtil::abs( storage._accessTile._y - from._y );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                best         = buildingIndex;
            }
        }
        return best;
    }

    bool CitySimulation::findRoadPath( const int2& from, const int2& to, vector<int2>& outListPath ) const
    {
        outListPath.clear();
        const bool bNoRoadConnection = isRoad( from._x, from._y ) == false || isRoad( to._x, to._y ) == false || getRoadComponent( from ) != getRoadComponent( to );
        if ( bNoRoadConnection )
            return false;
        // 타일 표시, 부모, 큐는 재사용 스크래치에 둔다. 일꾼을 내보낼 때마다 W x H 를 새로 할당하지 않기 위해서다.
        const int32 goal = _topology.toIndex( to );
        _roadSearch.begin( _topology.getCellCount() );
        _roadSearch.visit( _topology.toIndex( from ), constant::invalid_index::kInt32 );
        while ( _roadSearch.hasNext() )
        {
            const int32 index = _roadSearch.popNext();
            if ( index == goal )
                break;
            const int2 tile = _topology.toCell( index );
            for ( int32 direction = 0; direction < GridTopology::kOrthogonalCount; ++direction )
            {
                const int2 next = GridTopology::getNeighbor( tile, direction );
                if ( isRoad( next._x, next._y ) )
                    _roadSearch.visit( _topology.toIndex( next ), index );
            }
        }
        if ( _roadSearch.isVisited( goal ) == false )
            return false;
        for ( int32 index = goal; index >= 0; index = _roadSearch.getParent( index ) )
        {
            outListPath.push_back( _topology.toCell( index ) );
        }
        VectorUtil::reverse( outListPath );
        outListPath.erase( outListPath.begin() ); // 지금 타일은 빼고 다음 타일부터 간다
        return true;
    }

    void CitySimulation::updateWalkers( float32 deltaTime )
    {
        for ( CityWalker& walker : _listWalker )
        {
            if ( walker._bAlive == SW_FALSE )
                continue;
            walker._progress += deltaTime * _settings._walkerSpeed;
            while ( walker._progress >= 1.0f && walker._bAlive != SW_FALSE )
            {
                walker._progress -= 1.0f;
                stepWalker( walker );
            }
        }
    }

    void CitySimulation::stepWalker( CityWalker& walker )
    {
        // 다음 타일에 닿았으면 그 타일에서 할 일을 하고, 그다음 타일을 고른다.
        walker._previousTile = walker._tile;
        walker._tile         = walker._nextTile;
        if ( isRoad( walker._tile._x, walker._tile._y ) == false )
        {
            removeWalker( walker );
            return;
        }
        if ( walker._bReturning == SW_FALSE && walker._kind != CityWalkerKind::Cart )
            serveAround( walker );

        if ( walker._kind == CityWalkerKind::Cart || walker._bReturning != SW_FALSE )
        {
            if ( walker._listPath.empty() )
            {
                // 다 왔다. 수레는 창고에 내리고, 돌아가는 일꾼은 집에 들어간다.
                if ( walker._kind == CityWalkerKind::Cart && walker._targetBuilding >= 0 )
                {
                    CityBuilding& storage = _listBuilding[static_cast<size_t>( walker._targetBuilding )];
                    if ( storage._bAlive != SW_FALSE )
                    {
                        storage._stock.addItem( walker._cargoGood, walker._cargoAmount );
                        _eventBuffer.push( CityEvent{ walker._cargoAmount, walker._targetBuilding, CityEvent::Kind::GoodsDelivered } );
                    }
                }
                removeWalker( walker );
                return;
            }
            walker._nextTile = walker._listPath.front();
            walker._listPath.erase( walker._listPath.begin() );
            return;
        }

        // 돌아다니기: 걸음을 다 쓰면 돌아간다.
        if ( --walker._stepsLeft <= 0 )
        {
            startReturn( walker );
            return;
        }
        // 갈림길에서는 안 가 본 도로 중에서 시드 난수로 고른다(파라오의 순회 일꾼처럼). 나갈 때마다 다른 쪽으로 가므로 시간이 지나면 고루 닿는다.
        int2  arrCandidate[4];
        int32 candidateCount = 0;
        for ( int32 pass = 0; pass < 2 && candidateCount == 0; ++pass )
        {
            for ( int32 direction = 0; direction < GridTopology::kOrthogonalCount; ++direction )
            {
                const int2 next = GridTopology::getNeighbor( walker._tile, direction );
                if ( isRoad( next._x, next._y ) == false || next == walker._previousTile )
                    continue;
                const bool bVisited = VectorUtil::contains( walker._listVisited, next );
                if ( pass == 0 && bVisited )
                    continue; // 첫 바퀴는 안 가 본 타일만 고른다
                arrCandidate[candidateCount++] = next;
            }
        }
        int2 best = candidateCount > 0 ? arrCandidate[_random.nextInt( 0, candidateCount - 1 )] : walker._previousTile;
        if ( isRoad( best._x, best._y ) == false )
            best = walker._tile; // 혼자 놓인 도로 타일이라 제자리에 있는다
        walker._nextTile = best;
        walker._listVisited.push_back( best );
    }

    void CitySimulation::startReturn( CityWalker& walker )
    {
        const CityBuilding& home = _listBuilding[static_cast<size_t>( walker._homeBuilding )];
        walker._bReturning       = SW_TRUE;
        if ( home._bAlive == SW_FALSE || findRoadPath( walker._tile, home._accessTile, walker._listPath ) == false )
        {
            if ( walker._tile == home._accessTile && home._bAlive != SW_FALSE )
                walker._listPath.clear(); // 이미 집 앞이다
            else
            {
                removeWalker( walker );
                return;
            }
        }
        walker._nextTile = walker._listPath.empty() ? walker._tile : walker._listPath.front();
        if ( walker._listPath.empty() == false )
            walker._listPath.erase( walker._listPath.begin() );
    }

    void CitySimulation::removeWalker( CityWalker& walker )
    {
        if ( walker._bAlive == SW_FALSE )
            return;
        walker._bAlive = SW_FALSE;
        if ( walker._homeBuilding >= 0 && walker._homeBuilding < static_cast<int32>( _listBuilding.size() ) )
        {
            CityBuilding& home      = _listBuilding[static_cast<size_t>( walker._homeBuilding )];
            home._activeWalkerCount = MathUtil::max( 0, home._activeWalkerCount - 1 );
        }
    }

    void CitySimulation::serveAround( CityWalker& walker )
    {
        const int32 reach = _settings._serviceReach;
        for ( int32 y = walker._tile._y - reach; y <= walker._tile._y + reach; ++y )
        {
            for ( int32 x = walker._tile._x - reach; x <= walker._tile._x + reach; ++x )
            {
                const CityTile* pTile = findTile( x, y );
                if ( pTile == nullptr || pTile->_buildingIndex < 0 )
                    continue;
                CityBuilding& house = _listBuilding[static_cast<size_t>( pTile->_buildingIndex )];
                if ( house.isHouse() == false || house._population <= 0 )
                    continue;
                if ( walker._kind == CityWalkerKind::Service && walker._service != CityService::Count )
                {
                    house._arrServiceTime[static_cast<int32>( walker._service )] = _time;
                    continue;
                }
                if ( walker._kind != CityWalkerKind::Trader )
                    continue;
                // 상인은 시장 재고에서 집이 바라는 만큼(두 달치까지) 판다.
                CityBuilding& market = _listBuilding[static_cast<size_t>( walker._homeBuilding )];
                const int32   target = CitySimulationInternal::computeStockTarget( house._population, _settings._goodsPerFourPeople );
                for ( const hashed_string& goodID : market._pDef->_listGood )
                {
                    const int32 want = MathUtil::min( target - house._stock.getItemCount( goodID ), market._stock.getItemCount( goodID ) );
                    if ( want > 0 )
                        (void)market._stock.moveItemTo( house._stock, goodID, want ); // want 는 시장 재고 이하이므로 항상 성공한다
                }
            }
        }
    }

    float2 CitySimulation::computeWalkerPosition( const CityWalker& walker )
    {
        const float32 alpha = MathUtil::clamp( walker._progress, 0.0f, 1.0f );
        return float2{ static_cast<float32>( walker._tile._x ) + 0.5f + static_cast<float32>( walker._nextTile._x - walker._tile._x ) * alpha,
                       static_cast<float32>( walker._tile._y ) + 0.5f + static_cast<float32>( walker._nextTile._y - walker._tile._y ) * alpha };
    }

    // ------------------------------------------------------------------------------
    // 집
    // ------------------------------------------------------------------------------
    bool CitySimulation::isHouseServed( const CityBuilding& house, CityService service ) const
    {
        return service != CityService::Count && _time - house._arrServiceTime[static_cast<int32>( service )] <= _settings._serviceDuration;
    }

    bool CitySimulation::hasReachedHouseLevel( const CityBuilding& house, int32 level ) const
    {
        const CityHouseLevelDef* pLevel = _pCatalog->findHouseLevel( level );
        if ( pLevel == nullptr )
            return false;
        for ( int32 serviceIndex = 0; serviceIndex < static_cast<int32>( CityService::Count ); ++serviceIndex )
        {
            const CityService service = static_cast<CityService>( serviceIndex );
            if ( ( pLevel->_serviceMask & makeCityServiceBit( service ) ) != 0 && isHouseServed( house, service ) == false )
                return false;
        }
        for ( const hashed_string& goodID : pLevel->_listRequiredGood )
        {
            if ( house._stock.getItemCount( goodID ) <= 0 )
                return false;
        }
        const CityTile* pTile = findTile( house._origin._x, house._origin._y );
        return pTile != nullptr && pTile->_desirability >= pLevel->_minDesirability;
    }

    void CitySimulation::updateHouses( float32 deltaTime )
    {
        // 사람이 오는 조건은 일자리가 남거나 실업이 적고, 집에 빈 자리가 있는 것이다.
        const int32 jobs = [this]()
        {
            int32 total = 0;
            for ( const CityBuilding& building : _listBuilding )
            {
                total += building._bAlive != SW_FALSE && building.isHouse() == false ? building._pDef->_workers : 0;
            }
            return total;
        }();
        const bool bAttractive = _workforce < jobs + 4 || ( _workforce > 0 && static_cast<float32>( _workforce - _employed ) / static_cast<float32>( _workforce ) < 0.25f );
        for ( int32 buildingIndex = 0; buildingIndex < static_cast<int32>( _listBuilding.size() ); ++buildingIndex )
        {
            CityBuilding& house = _listBuilding[static_cast<size_t>( buildingIndex )];
            if ( house._bAlive == SW_FALSE || house.isHouse() == false )
                continue;
            const CityHouseLevelDef* pLevel   = _pCatalog->findHouseLevel( house._level );
            const int32              capacity = pLevel != nullptr ? pLevel->_population : 0;
            if ( house._population > capacity )
                house._population = capacity; // 내려간 집에서 사람이 떠난다
            const bool bCanImmigrate = house.hasRoadAccess() && bAttractive && house._population < capacity;
            if ( bCanImmigrate )
            {
                house._immigrationTimer += deltaTime;
                if ( house._immigrationTimer >= _settings._immigrationInterval )
                {
                    house._immigrationTimer = 0.0f;
                    ++house._population;
                }
            }
            if ( house._population <= 0 )
                continue;

            const bool bCanEvolve = hasReachedHouseLevel( house, house._level + 1 );
            house._evolveTimer    = bCanEvolve ? house._evolveTimer + deltaTime : 0.0f;
            if ( house._evolveTimer >= _settings._evolveDelay )
            {
                ++house._level;
                house._evolveTimer = 0.0f;
                _eventBuffer.push( CityEvent{ house._level, buildingIndex, CityEvent::Kind::HouseEvolved } );
                continue;
            }
            const bool bLosing  = house._level > 0 && hasReachedHouseLevel( house, house._level ) == false;
            house._devolveTimer = bLosing ? house._devolveTimer + deltaTime : 0.0f;
            if ( house._devolveTimer >= _settings._evolveDelay )
            {
                --house._level;
                house._devolveTimer = 0.0f;
                _eventBuffer.push( CityEvent{ house._level, buildingIndex, CityEvent::Kind::HouseDevolved } );
            }
        }
    }

    void CitySimulation::settleMonth( bool bNewYear )
    {
        int32 income = 0;
        for ( CityBuilding& house : _listBuilding )
        {
            const bool bSkipHouse = house._bAlive == SW_FALSE || house.isHouse() == false || house._population <= 0;
            if ( bSkipHouse )
                continue;
            // 먹기: 가진 물자마다 네 사람에 하나를 먹는다.
            const int32           need = ( house._population + 3 ) / 4 * _settings._goodsPerFourPeople;
            vector<hashed_string> listGood;
            house._stock.getItemIDs( listGood );
            for ( const hashed_string& goodID : listGood )
            {
                // 재고가 0 이면 false 를 반환한다. 먹을 것이 없을 뿐이다
                (void)house._stock.removeItem( goodID, MathUtil::min( need, house._stock.getItemCount( goodID ) ) );
            }
            // 세금: 세리가 다녀간 집만 낸다.
            const CityHouseLevelDef* pLevel = _pCatalog->findHouseLevel( house._level );
            if ( pLevel != nullptr && isHouseServed( house, CityService::Tax ) )
                income += house._population * pLevel->_taxPerPerson;
        }
        _wageDebt += static_cast<float32>( _employed ) * _settings._wagePerWorkerPerMonth;
        const int32 wages = static_cast<int32>( _wageDebt );
        _wageDebt -= static_cast<float32>( wages );
        // 결산에서 돈이 모자라면 빚이 된다(임금은 미룰 수 없다).
        if ( _pWallet != nullptr && income > wages )
            _pWallet->add( _settings._currency, income - wages );
        else if ( _pWallet != nullptr && income < wages )
            _pWallet->charge( _settings._currency, wages - income );
        _monthIncome = income - wages;
        _eventBuffer.push( CityEvent{ _monthIncome, nullopt, CityEvent::Kind::MonthEnded } );

        if ( bNewYear )
        {
            // 범람: 해마다 다르다(40 % ... 100 %). 범람원 농장의 다음 한 해 생산량을 정한다.
            _floodFertility = _random.nextRange( 0.4f, 1.0f );
            _eventBuffer.push( CityEvent{ static_cast<int32>( _floodFertility * 100.0f ), nullopt, CityEvent::Kind::Flood } );
        }
    }

    // ------------------------------------------------------------------------------
    // 평가
    // ------------------------------------------------------------------------------
    int32 CitySimulation::getPopulation() const
    {
        int32 population = 0;
        for ( const CityBuilding& building : _listBuilding )
        {
            population += building._bAlive != SW_FALSE && building.isHouse() ? building._population : 0;
        }
        return population;
    }

    float32 CitySimulation::computeCultureCoverage() const
    {
        int32 covered = 0;
        int32 total   = 0;
        for ( const CityBuilding& house : _listBuilding )
        {
            if ( house._bAlive == SW_FALSE || house.isHouse() == false )
                continue;
            total += house._population;
            if ( isHouseServed( house, CityService::Religion ) && isHouseServed( house, CityService::Entertainment ) )
                covered += house._population;
        }
        return total > 0 ? static_cast<float32>( covered ) / static_cast<float32>( total ) : 0.0f;
    }

    float32 CitySimulation::computeAverageHouseLevel() const
    {
        int32 weighted = 0;
        int32 total    = 0;
        for ( const CityBuilding& house : _listBuilding )
        {
            if ( house._bAlive == SW_FALSE || house.isHouse() == false )
                continue;
            weighted += house._level * house._population;
            total += house._population;
        }
        return total > 0 ? static_cast<float32>( weighted ) / static_cast<float32>( total ) : 0.0f;
    }
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 상태 쓰기와 읽기(핫 리로드, 세이브)
    // ------------------------------------------------------------------------------
    void CitySimulation::serializeState( StateIO& io )
    {
        io.expect( _topology._width );
        io.expect( _topology._height );
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        for ( CityTile& tile : _listTile )
        {
            io.value( tile._buildingIndex );
            io.value( tile._roadComponent );
            io.value( tile._desirability );
            io.enumValue( tile._terrain, CityTerrain::Rock );
            io.value( tile._bRoad );
        }
        io.list( _listBuilding, CitySimulationInternal::kMinBuildingStateBytes, [&]( CityBuilding& building )
        {
            io.reference( building._pDef, [this]( const hashed_string& defID )
            { return _pCatalog->findBuilding( defID ); } );
            io.value( building._stock );
            io.value( building._origin );
            io.value( building._accessTile );
            io.value( building._efficiency );
            io.value( building._walkerTimer );
            io.value( building._productionProgress );
            for ( float32& serviceTime : building._arrServiceTime )
            {
                io.value( serviceTime );
            }
            io.value( building._evolveTimer );
            io.value( building._devolveTimer );
            io.value( building._immigrationTimer );
            io.value( building._assignedWorkers );
            io.value( building._activeWalkerCount );
            io.value( building._level );
            io.value( building._population );
            io.value( building._bAlive );
            if ( io.isReading() && io.isOk() )
            {
                io.check( CitySimulationInternal::isBuildingValid( building, *_pCatalog, _topology ) );
            }
        } );
        if ( io.isReading() && io.isOk() )
        {
            io.check( CitySimulationInternal::areTilesValid( _listTile, _listBuilding, _topology ) );
        }
        const int32 buildingCount = static_cast<int32>( _listBuilding.size() );
        const auto  serializeTile = [&]( int2& tile )
        { io.value( tile ); };
        io.list( _listWalker, CitySimulationInternal::kMinWalkerStateBytes, [&]( CityWalker& walker )
        {
            io.list( walker._listPath, sizeof( int32 ) * 2, serializeTile );
            io.list( walker._listVisited, sizeof( int32 ) * 2, serializeTile );
            io.value( walker._cargoGood );
            io.value( walker._tile );
            io.value( walker._previousTile );
            io.value( walker._nextTile );
            io.value( walker._progress );
            io.value( walker._homeBuilding );
            io.value( walker._targetBuilding );
            io.value( walker._stepsLeft );
            io.value( walker._cargoAmount );
            io.enumValue( walker._kind, CityWalkerKind::Cart );
            io.enumValue( walker._service, CityService::Count );
            io.flag( walker._bReturning );
            io.flag( walker._bAlive );
            if ( io.check( constant::invalid_index::kInt32 <= walker._homeBuilding && walker._homeBuilding < buildingCount && constant::invalid_index::kInt32 <= walker._targetBuilding && walker._targetBuilding < buildingCount && 0 <= walker._cargoAmount ) && io.isReading() )
            {
                io.check( CitySimulationInternal::isWalkerValid( walker, _listBuilding, _topology ) );
            }
        } );
        io.value( _stepTimer );
        io.value( _random );
        io.value( _time );
        io.value( _floodFertility );
        io.value( _wageDebt );
        io.value( _monthIncome );
        io.value( _workforce );
        io.value( _employed );
        io.flag( _bRoadsDirty );
        io.flag( _bDesirabilityDirty );
        // 임금 빚은 정수 부분을 달마다 내어 1 아래이다. 일하는 사람은 일할 수 있는 사람에서 나눠 준다(`assignLabor`).
        io.check( 0.0f <= _wageDebt && _wageDebt < 1.0f && 0 <= _workforce && 0 <= _employed && _employed <= _workforce && 0.0f <= _time && 0.0f <= _floodFertility && _floodFertility <= 1.0f );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
        }
    }

} // namespace sw
