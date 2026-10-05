#include "pch.h"

#include "GameFramework/Kits/Strategy/CityBuilder/CitySimulation.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    SW_LOG_CALLER( "CitySimulation" );

    namespace
    {
        struct CitySimulationInternal
        {
            static constexpr uint32 kMinBuildingStateBytes = 64; ///< 건물 하나의 상태가 적어도 쓰는 바이트(개수 상한)
            static constexpr uint32 kMinWalkerStateBytes   = 56; ///< 일꾼 하나의 상태가 적어도 쓰는 바이트(개수 상한)

            /** @brief 노동 순서 — 물 → 식량 사슬 → 나머지(작을수록 먼저). */
            static int32 computeLaborPriority( const CityBuildingDef& def )
            {
                if ( def._kind == CityBuildingKind::Service && def._service == CityService::Water )
                    return 0;
                if ( def._kind == CityBuildingKind::Producer || def._kind == CityBuildingKind::Storage || def._kind == CityBuildingKind::Market )
                    return 1;
                return 2;
            }

            static int32 computeChebyshev( const int2& tile, const int2& origin, int32 size )
            {
                const int32 dx = tile._x < origin._x ? origin._x - tile._x : ( tile._x >= origin._x + size ? tile._x - ( origin._x + size - 1 ) : 0 );
                const int32 dy = tile._y < origin._y ? origin._y - tile._y : ( tile._y >= origin._y + size ? tile._y - ( origin._y + size - 1 ) : 0 );
                return MathUtil::max( dx, dy );
            }

            /** @brief 한 사람 더 받으려는 집이 바라는 물자 재고입니다(두 달치). */
            static int32 computeStockTarget( int32 population, int32 goodsPerFourPeople )
            {
                return MathUtil::max( 2, ( population + 3 ) / 4 * goodsPerFourPeople * 2 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( CityPlaceResult result )
    {
        switch ( result )
        {
            case CityPlaceResult::Ok:
                return "Ok";
            case CityPlaceResult::OutOfBounds:
                return "OutOfBounds";
            case CityPlaceResult::Occupied:
                return "Occupied";
            case CityPlaceResult::BadTerrain:
                return "BadTerrain";
            case CityPlaceResult::NotEnoughMoney:
                return "NotEnoughMoney";
            case CityPlaceResult::UnknownBuilding:
                return "UnknownBuilding";
        }
        return "?";
    }

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

    void CitySimulation::initialize( const CityCatalog* pCatalog, int32 width, int32 height, const CitySettings& settings, const GameStateRefs& refs )
    {
        _pCatalog = pCatalog;
        _pWallet  = refs._pWallet;
        if ( _pWallet == nullptr )
            SW_LOG_WARNING( "CitySimulation: no wallet was lent - every road and building is refused" );
        _settings = settings;
        _topology = GridTopology{ MathUtil::max( 1, width ), MathUtil::max( 1, height ) };
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
                setTerrain( x, y, terrain );
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
        return pTile != nullptr ? pTile->_roadComponent : -1;
    }

    // ------------------------------------------------------------------------------
    // 짓기
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
        if ( pTile->_bRoad != SW_FALSE || pTile->_buildingIndex >= 0 || _land.isUsable( x, y ) == false )
            return CityPlaceResult::Occupied;
        if ( pTile->_terrain == CityTerrain::Water || pTile->_terrain == CityTerrain::Rock )
            return CityPlaceResult::BadTerrain;
        const int32 cost = _pCatalog != nullptr ? _pCatalog->getRoadCost() : 2;
        if ( _pWallet == nullptr || _pWallet->trySpend( _settings._currency, cost ) == false )
            return CityPlaceResult::NotEnoughMoney;
        (void)_land.claimRect( x, y, x, y, false ); // 위에서 볼 수 있음을 확인했다
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

    CityPlaceResult CitySimulation::placeBuilding( const hashed_string& buildingId, int32 x, int32 y )
    {
        const CityBuildingDef* pDef = _pCatalog != nullptr ? _pCatalog->findBuilding( buildingId ) : nullptr;
        if ( pDef == nullptr )
            return CityPlaceResult::UnknownBuilding;
        // 범위를 먼저 — 발밑 일부만 맵 밖이어도 "땅이 나쁘다" 가 아니라 "밖" 이다.
        if ( _topology.isRectInside( int2{ x, y }, int2{ pDef->_size, pDef->_size } ) == false )
            return CityPlaceResult::OutOfBounds;
        for ( int32 dy = 0; dy < pDef->_size; ++dy )
        {
            for ( int32 dx = 0; dx < pDef->_size; ++dx )
            {
                const CityTile* pTile = findTile( x + dx, y + dy );
                if ( pTile->_bRoad != SW_FALSE || pTile->_buildingIndex >= 0 || _land.isUsable( x + dx, y + dy ) == false )
                    return CityPlaceResult::Occupied;
                if ( pTile->_terrain == CityTerrain::Water || pTile->_terrain == CityTerrain::Rock )
                    return CityPlaceResult::BadTerrain;
                if ( pDef->_bRequiresTerrain != SW_FALSE && pTile->_terrain != pDef->_requiredTerrain )
                    return CityPlaceResult::BadTerrain;
            }
        }
        if ( _pWallet == nullptr || _pWallet->trySpend( _settings._currency, pDef->_cost ) == false )
            return CityPlaceResult::NotEnoughMoney;
        (void)_land.claimRect( x, y, x + pDef->_size - 1, y + pDef->_size - 1, true ); // 위에서 칸마다 볼 수 있음을 확인했다

        // 허문 자리를 다시 쓴다 — 칸이 가리키는 번호가 안정적이게 목록에서 지우지 않았다.
        int32 index = -1;
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
            serviceTime = -1.0e9f;
        for ( int32 dy = 0; dy < pDef->_size; ++dy )
        {
            for ( int32 dx = 0; dx < pDef->_size; ++dx )
                _listTile[static_cast<size_t>( _topology.toIndex( x + dx, y + dy ) )]._buildingIndex = index;
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
                _listTile[static_cast<size_t>( _topology.toIndex( building._origin._x + dx, building._origin._y + dy ) )]._buildingIndex = -1;
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
        // 둘레 한 칸 안의 도로(대각선 빼고). 집은 파라오처럼 두 칸 안이면 된다.
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
                const bool bInsideX = x >= building._origin._x && x < building._origin._x + size;
                const bool bInsideY = y >= building._origin._y && y < building._origin._y + size;
                if ( building.isHouse() == false && bInsideX == false && bInsideY == false )
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
            tile._roadComponent = -1;
        // 도로 조각마다 너비 우선 — 큐는 길 찾기와 같은 재사용 스크래치(조각 번호가 칸의 "봤다" 표시다).
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
                _roadSearch.visit( _topology.toIndex( x, y ), -1 );
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
        // 도로가 끊겨 지금 칸이 도로가 아닌 일꾼은 사라진다.
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
            tile._desirability = 0;
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
                    // 발밑과 바로 둘레는 다 받고 반경 끝으로 갈수록 줄어든다.
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
            stepFixed( _stepTimer.getStep() );
    }

    void CitySimulation::drainEvents( vector<CityEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
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
        _listWalker.erase( std::remove_if( _listWalker.begin(), _listWalker.end(), []( const CityWalker& walker )
        { return walker._bAlive == SW_FALSE; } ),
                           _listWalker.end() );
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
                if ( building._bAlive == SW_FALSE || building.isHouse() || CitySimulationInternal::computeLaborPriority( *building._pDef ) != priority )
                    continue;
                const int32 needed = building._pDef->_workers;
                // 길이 없으면 일꾼이 오지 못한다(반경 서비스 — 우물 — 는 길이 없어도 된다).
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
            CityBuilding& building = _listBuilding[static_cast<size_t>( buildingIndex )];
            if ( building._bAlive == SW_FALSE || building.isHouse() || building._efficiency <= 0.0f )
                continue;
            const CityBuildingDef& def = *building._pDef;
            switch ( def._kind )
            {
                case CityBuildingKind::Service:
                {
                    if ( def._delivery == CityDelivery::Radius )
                    {
                        // 반경 안의 집에 늘 준다(파라오의 우물).
                        for ( CityBuilding& house : _listBuilding )
                        {
                            if ( house._bAlive != SW_FALSE && house.isHouse() &&
                                 CitySimulationInternal::computeChebyshev( house._origin, building._origin, def._size ) <= def._range )
                                house._arrServiceTime[static_cast<int32>( def._service )] = _time;
                        }
                        break;
                    }
                    building._walkerTimer += deltaTime * building._efficiency;
                    if ( building._walkerTimer >= def._walkerInterval && building._activeWalkerCount == 0 && building.hasRoadAccess() )
                    {
                        building._walkerTimer = 0.0f;
                        spawnRoamer( buildingIndex, CityWalkerKind::Service );
                    }
                    break;
                }
                case CityBuildingKind::Producer:
                {
                    // 범람원 농장은 해마다의 범람이, 다른 땅은 반쯤의 비옥함이 만든 양을 정한다.
                    const float32 fertility = ( def._bRequiresTerrain != SW_FALSE && def._requiredTerrain == CityTerrain::Floodplain ) ? _floodFertility : 1.0f;
                    building._productionProgress += deltaTime * building._efficiency * fertility / def._productionTime;
                    if ( building._productionProgress >= 1.0f && def._listGood.empty() == false )
                    {
                        building._productionProgress -= 1.0f;
                        building._stock.addItem( def._listGood.front(), def._productionAmount );
                    }
                    if ( building._activeWalkerCount == 0 && building._stock.isEmpty() == false && building.hasRoadAccess() )
                        spawnCart( buildingIndex );
                    break;
                }
                case CityBuildingKind::Market:
                {
                    // 사 오기 — 같은 도로망의 창고에서 파는 물자마다 상한의 몫까지(바자 구매인을 줄였다).
                    const int32 component = getRoadComponent( building._accessTile );
                    const int32 perGood   = def._listGood.empty() ? 0 : def._capacity / static_cast<int32>( def._listGood.size() );
                    for ( const hashed_string& goodId : def._listGood )
                    {
                        int32 want = perGood - building._stock.getItemCount( goodId );
                        for ( CityBuilding& storage : _listBuilding )
                        {
                            if ( want <= 0 )
                                break;
                            if ( storage._bAlive == SW_FALSE || storage._pDef->_kind != CityBuildingKind::Storage || storage._efficiency <= 0.0f ||
                                 getRoadComponent( storage._accessTile ) != component || component < 0 )
                                continue;
                            const int32 moved = MathUtil::min( want, storage._stock.getItemCount( goodId ) );
                            if ( moved > 0 && storage._stock.moveItemTo( building._stock, goodId, moved ) )
                                want -= moved;
                        }
                    }
                    building._walkerTimer += deltaTime * building._efficiency;
                    if ( building._walkerTimer >= def._walkerInterval && building._activeWalkerCount == 0 && building._stock.isEmpty() == false &&
                         building.hasRoadAccess() )
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
        const hashed_string goodId    = items.front()._itemId;
        const int32         amount    = items.front()._count;
        const int32         component = getRoadComponent( building._accessTile );
        const int32         storage   = findStorageFor( goodId, component, building._accessTile, 1 );
        if ( storage < 0 )
            return;
        CityWalker walker;
        if ( findRoadPath( building._accessTile, _listBuilding[static_cast<size_t>( storage )]._accessTile, walker._listPath ) == false )
            return;
        const int32 load = MathUtil::min( amount, computeStorageFree( _listBuilding[static_cast<size_t>( storage )] ) );
        if ( load <= 0 || building._stock.removeItem( goodId, load ) == false )
            return;
        walker._kind           = CityWalkerKind::Cart;
        walker._homeBuilding   = buildingIndex;
        walker._targetBuilding = storage;
        walker._cargoGood      = goodId;
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
            if ( walker._bAlive != SW_FALSE && walker._kind == CityWalkerKind::Cart && walker._targetBuilding >= 0 &&
                 &_listBuilding[static_cast<size_t>( walker._targetBuilding )] == &storage )
                incoming += walker._cargoAmount;
        }
        return storage._pDef->_capacity - storage._stock.getTotalCount() - incoming;
    }

    int32 CitySimulation::findStorageFor( const hashed_string& goodId, int32 roadComponent, const int2& from, int32 amount ) const
    {
        int32 best         = -1;
        int32 bestDistance = 1 << 30;
        for ( int32 buildingIndex = 0; buildingIndex < static_cast<int32>( _listBuilding.size() ); ++buildingIndex )
        {
            const CityBuilding& storage = _listBuilding[static_cast<size_t>( buildingIndex )];
            if ( storage._bAlive == SW_FALSE || storage._pDef->_kind != CityBuildingKind::Storage || storage._efficiency <= 0.0f || roadComponent < 0 ||
                 getRoadComponent( storage._accessTile ) != roadComponent )
                continue;
            if ( std::find( storage._pDef->_listGood.begin(), storage._pDef->_listGood.end(), goodId ) == storage._pDef->_listGood.end() )
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
        if ( isRoad( from._x, from._y ) == false || isRoad( to._x, to._y ) == false || getRoadComponent( from ) != getRoadComponent( to ) )
            return false;
        // 칸 표시 · 부모 · 큐는 재사용 스크래치에 — 일꾼을 내보낼 때마다 W × H 를 새로 잡지 않는다.
        const int32 goal = _topology.toIndex( to );
        _roadSearch.begin( _topology.getCellCount() );
        _roadSearch.visit( _topology.toIndex( from ), -1 );
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
            outListPath.push_back( _topology.toCell( index ) );
        std::reverse( outListPath.begin(), outListPath.end() );
        outListPath.erase( outListPath.begin() ); // 지금 칸은 빼고 다음 칸부터
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
        // 다음 칸에 닿았다 → 그 칸에서 할 일 → 그다음 칸을 고른다.
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
                // 다 왔다 — 수레는 창고에 내리고, 돌아가는 일꾼은 집에 든다.
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

        // 돌아다니기 — 걸음을 다 쓰면 돌아간다.
        if ( --walker._stepsLeft <= 0 )
        {
            startReturn( walker );
            return;
        }
        // 갈림길 — 안 가 본 도로 중에서 씨앗 난수로 고른다(파라오의 순회 일꾼처럼). 나갈 때마다 다른 쪽으로 가서 시간이 지나면 고루 닿는다.
        int2  arrCandidate[4];
        int32 candidateCount = 0;
        for ( int32 pass = 0; pass < 2 && candidateCount == 0; ++pass )
        {
            for ( int32 direction = 0; direction < GridTopology::kOrthogonalCount; ++direction )
            {
                const int2 next = GridTopology::getNeighbor( walker._tile, direction );
                if ( isRoad( next._x, next._y ) == false || next == walker._previousTile )
                    continue;
                const bool bVisited = std::find( walker._listVisited.begin(), walker._listVisited.end(), next ) != walker._listVisited.end();
                if ( pass == 0 && bVisited )
                    continue; // 첫 바퀴는 안 가 본 칸만
                arrCandidate[candidateCount++] = next;
            }
        }
        int2 best = candidateCount > 0 ? arrCandidate[_random.nextInt( 0, candidateCount - 1 )] : walker._previousTile;
        if ( isRoad( best._x, best._y ) == false )
            best = walker._tile; // 혼자 놓인 도로 칸 — 제자리
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
                walker._listPath.clear(); // 이미 집 앞
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
                // 상인 — 시장 재고에서 집이 바라는 만큼(두 달치까지) 판다.
                CityBuilding& market = _listBuilding[static_cast<size_t>( walker._homeBuilding )];
                const int32   target = CitySimulationInternal::computeStockTarget( house._population, _settings._goodsPerFourPeople );
                for ( const hashed_string& goodId : market._pDef->_listGood )
                {
                    const int32 want = MathUtil::min( target - house._stock.getItemCount( goodId ), market._stock.getItemCount( goodId ) );
                    if ( want > 0 )
                        (void)market._stock.moveItemTo( house._stock, goodId, want );
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

    bool CitySimulation::meetsHouseLevel( const CityBuilding& house, int32 level ) const
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
        for ( const hashed_string& goodId : pLevel->_listRequiredGood )
        {
            if ( house._stock.getItemCount( goodId ) <= 0 )
                return false;
        }
        const CityTile* pTile = findTile( house._origin._x, house._origin._y );
        return pTile != nullptr && pTile->_desirability >= pLevel->_minDesirability;
    }

    void CitySimulation::updateHouses( float32 deltaTime )
    {
        // 사람이 오는가 — 일자리가 남거나 실업이 적고, 집에 빈 자리가 있으면.
        const int32 jobs = [this]()
        {
            int32 total = 0;
            for ( const CityBuilding& building : _listBuilding )
                total += building._bAlive != SW_FALSE && building.isHouse() == false ? building._pDef->_workers : 0;
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
            if ( house.hasRoadAccess() && bAttractive && house._population < capacity )
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

            const bool bCanEvolve = meetsHouseLevel( house, house._level + 1 );
            house._evolveTimer    = bCanEvolve ? house._evolveTimer + deltaTime : 0.0f;
            if ( house._evolveTimer >= _settings._evolveDelay )
            {
                ++house._level;
                house._evolveTimer = 0.0f;
                _eventBuffer.push( CityEvent{ house._level, buildingIndex, CityEvent::Kind::HouseEvolved } );
                continue;
            }
            const bool bLosing  = house._level > 0 && meetsHouseLevel( house, house._level ) == false;
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
            if ( house._bAlive == SW_FALSE || house.isHouse() == false || house._population <= 0 )
                continue;
            // 먹기 — 가진 물자마다 네 사람에 하나.
            const int32           need = ( house._population + 3 ) / 4 * _settings._goodsPerFourPeople;
            vector<hashed_string> listGood;
            house._stock.getItemIds( listGood );
            for ( const hashed_string& goodId : listGood )
                (void)house._stock.removeItem( goodId, MathUtil::min( need, house._stock.getItemCount( goodId ) ) );
            // 세금 — 세리가 다녀간 집만.
            const CityHouseLevelDef* pLevel = _pCatalog->findHouseLevel( house._level );
            if ( pLevel != nullptr && isHouseServed( house, CityService::Tax ) )
                income += house._population * pLevel->_taxPerPerson;
        }
        _wageDebt += static_cast<float32>( _employed ) * _settings._wagePerWorkerPerMonth;
        const int32 wages = static_cast<int32>( _wageDebt );
        _wageDebt -= static_cast<float32>( wages );
        // 결산이 모자라면 빚이다(임금은 미룰 수 없다).
        if ( _pWallet != nullptr && income > wages )
            _pWallet->add( _settings._currency, income - wages );
        else if ( _pWallet != nullptr && income < wages )
            _pWallet->charge( _settings._currency, wages - income );
        _monthIncome = income - wages;
        _eventBuffer.push( CityEvent{ _monthIncome, -1, CityEvent::Kind::MonthEnded } );

        if ( bNewYear )
        {
            // 범람 — 해마다 다르다(40 % … 100 %). 범람원 농장의 다음 한 해를 정한다.
            _floodFertility = _random.nextRange( 0.4f, 1.0f );
            _eventBuffer.push( CityEvent{ static_cast<int32>( _floodFertility * 100.0f ), -1, CityEvent::Kind::Flood } );
        }
    }

    // ------------------------------------------------------------------------------
    // 평가
    // ------------------------------------------------------------------------------
    int32 CitySimulation::getPopulation() const
    {
        int32 population = 0;
        for ( const CityBuilding& building : _listBuilding )
            population += building._bAlive != SW_FALSE && building.isHouse() ? building._population : 0;
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
    // 상태 쓰기 · 읽기(핫 리로드 · 세이브)
    // ------------------------------------------------------------------------------
    void CitySimulation::writeState( Archive& outArchive ) const
    {
        outArchive << _topology._width;
        outArchive << _topology._height;
        for ( const CityTile& tile : _listTile )
        {
            outArchive << tile._buildingIndex;
            outArchive << tile._roadComponent;
            outArchive << tile._desirability;
            outArchive << static_cast<uint8>( tile._terrain );
            outArchive << tile._bRoad;
        }
        outArchive << static_cast<uint32>( _listBuilding.size() );
        for ( const CityBuilding& building : _listBuilding )
        {
            StateArchiveUtil::writeName( outArchive, building._pDef != nullptr ? building._pDef->_id : hashed_string{} );
            building._stock.writeState( outArchive );
            StateArchiveUtil::writeInt2( outArchive, building._origin );
            StateArchiveUtil::writeInt2( outArchive, building._accessTile );
            outArchive << building._efficiency;
            outArchive << building._walkerTimer;
            outArchive << building._productionProgress;
            for ( const float32 serviceTime : building._arrServiceTime )
                outArchive << serviceTime;
            outArchive << building._evolveTimer;
            outArchive << building._devolveTimer;
            outArchive << building._immigrationTimer;
            outArchive << building._assignedWorkers;
            outArchive << building._activeWalkerCount;
            outArchive << building._level;
            outArchive << building._population;
            outArchive << building._bAlive;
        }
        outArchive << static_cast<uint32>( _listWalker.size() );
        for ( const CityWalker& walker : _listWalker )
        {
            outArchive << static_cast<uint32>( walker._listPath.size() );
            for ( const int2& tile : walker._listPath )
                StateArchiveUtil::writeInt2( outArchive, tile );
            outArchive << static_cast<uint32>( walker._listVisited.size() );
            for ( const int2& tile : walker._listVisited )
                StateArchiveUtil::writeInt2( outArchive, tile );
            StateArchiveUtil::writeName( outArchive, walker._cargoGood );
            StateArchiveUtil::writeInt2( outArchive, walker._tile );
            StateArchiveUtil::writeInt2( outArchive, walker._previousTile );
            StateArchiveUtil::writeInt2( outArchive, walker._nextTile );
            outArchive << walker._progress;
            outArchive << walker._homeBuilding;
            outArchive << walker._targetBuilding;
            outArchive << walker._stepsLeft;
            outArchive << walker._cargoAmount;
            outArchive << static_cast<uint8>( walker._kind );
            outArchive << static_cast<uint8>( walker._service );
            outArchive << walker._bReturning;
            outArchive << walker._bAlive;
        }
        StateArchiveUtil::writeStepTimer( outArchive, _stepTimer );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _time;
        outArchive << _floodFertility;
        outArchive << _wageDebt;
        outArchive << _monthIncome;
        outArchive << _workforce;
        outArchive << _employed;
        outArchive << _bRoadsDirty;
        outArchive << _bDesirabilityDirty;
    }

    bool CitySimulation::readState( Archive& archive )
    {
        int32 width  = 0;
        int32 height = 0;
        archive >> width;
        archive >> height;
        if ( archive.isError() || width != _topology._width || height != _topology._height || _pCatalog == nullptr )
            return false;

        const int32      buildingLimit = static_cast<int32>( _listTile.size() ); // 건물은 적어도 한 칸이다
        vector<CityTile> listTile( _listTile.size() );
        for ( CityTile& tile : listTile )
        {
            uint8 terrain = 0;
            archive >> tile._buildingIndex;
            archive >> tile._roadComponent;
            archive >> tile._desirability;
            archive >> terrain;
            archive >> tile._bRoad;
            if ( archive.isError() || terrain > static_cast<uint8>( CityTerrain::Rock ) || tile._buildingIndex >= buildingLimit )
                return false;
            tile._terrain = static_cast<CityTerrain>( terrain );
        }

        uint32 buildingCount = 0;
        if ( StateArchiveUtil::readCount( archive, CitySimulationInternal::kMinBuildingStateBytes, buildingCount ) == false )
            return false;
        vector<CityBuilding> listBuilding( buildingCount );
        for ( CityBuilding& building : listBuilding )
        {
            hashed_string defId;
            if ( StateArchiveUtil::readName( archive, defId ) == false )
                return false;
            building._pDef = defId.empty() ? nullptr : _pCatalog->findBuilding( defId );
            if ( defId.empty() == false && building._pDef == nullptr )
                return false; // 카탈로그에서 빠진 건물 — 도시를 맞출 수 없다
            if ( building._stock.readState( archive ) == false )
                return false;
            StateArchiveUtil::readInt2( archive, building._origin );
            StateArchiveUtil::readInt2( archive, building._accessTile );
            archive >> building._efficiency;
            archive >> building._walkerTimer;
            archive >> building._productionProgress;
            for ( float32& serviceTime : building._arrServiceTime )
                archive >> serviceTime;
            archive >> building._evolveTimer;
            archive >> building._devolveTimer;
            archive >> building._immigrationTimer;
            archive >> building._assignedWorkers;
            archive >> building._activeWalkerCount;
            archive >> building._level;
            archive >> building._population;
            archive >> building._bAlive;
            if ( archive.isError() )
                return false;
        }

        uint32 walkerCount = 0;
        if ( StateArchiveUtil::readCount( archive, CitySimulationInternal::kMinWalkerStateBytes, walkerCount ) == false )
            return false;
        vector<CityWalker> listWalker( walkerCount );
        for ( CityWalker& walker : listWalker )
        {
            uint32 pathCount = 0;
            if ( StateArchiveUtil::readCount( archive, sizeof( int32 ) * 2, pathCount ) == false )
                return false;
            walker._listPath.resize( pathCount );
            for ( int2& tile : walker._listPath )
                StateArchiveUtil::readInt2( archive, tile );
            uint32 visitedCount = 0;
            if ( StateArchiveUtil::readCount( archive, sizeof( int32 ) * 2, visitedCount ) == false )
                return false;
            walker._listVisited.resize( visitedCount );
            for ( int2& tile : walker._listVisited )
                StateArchiveUtil::readInt2( archive, tile );
            if ( StateArchiveUtil::readName( archive, walker._cargoGood ) == false )
                return false;
            uint8 kind    = 0;
            uint8 service = 0;
            StateArchiveUtil::readInt2( archive, walker._tile );
            StateArchiveUtil::readInt2( archive, walker._previousTile );
            StateArchiveUtil::readInt2( archive, walker._nextTile );
            archive >> walker._progress;
            archive >> walker._homeBuilding;
            archive >> walker._targetBuilding;
            archive >> walker._stepsLeft;
            archive >> walker._cargoAmount;
            archive >> kind;
            archive >> service;
            archive >> walker._bReturning;
            archive >> walker._bAlive;
            const bool bHomeValid = -1 <= walker._homeBuilding && walker._homeBuilding < static_cast<int32>( buildingCount );
            const bool bEnumValid = kind <= static_cast<uint8>( CityWalkerKind::Cart ) && service <= static_cast<uint8>( CityService::Count );
            if ( archive.isError() || bHomeValid == false || bEnumValid == false )
                return false;
            walker._kind    = static_cast<CityWalkerKind>( kind );
            walker._service = static_cast<CityService>( service );
        }

        FixedStepTimer stepTimer = _stepTimer;
        GameRandom     random;
        if ( StateArchiveUtil::readStepTimer( archive, stepTimer ) == false || StateArchiveUtil::readRandom( archive, random ) == false )
            return false;
        float32 time               = 0.0f;
        float32 floodFertility     = 0.0f;
        float32 wageDebt           = 0.0f;
        int32   monthIncome        = 0;
        int32   workforce          = 0;
        int32   employed           = 0;
        uint8   bRoadsDirty        = SW_FALSE;
        uint8   bDesirabilityDirty = SW_FALSE;
        archive >> time;
        archive >> floodFertility;
        archive >> wageDebt;
        archive >> monthIncome;
        archive >> workforce;
        archive >> employed;
        archive >> bRoadsDirty;
        archive >> bDesirabilityDirty;
        if ( archive.isError() )
            return false;

        _listTile           = std::move( listTile );
        _listBuilding       = std::move( listBuilding );
        _listWalker         = std::move( listWalker );
        _stepTimer          = stepTimer;
        _random             = random;
        _time               = time;
        _floodFertility     = floodFertility;
        _wageDebt           = wageDebt;
        _monthIncome        = monthIncome;
        _workforce          = workforce;
        _employed           = employed;
        _bRoadsDirty        = bRoadsDirty;
        _bDesirabilityDirty = bDesirabilityDirty;
        _eventBuffer.clear();
        return true;
    }
} // namespace sw
