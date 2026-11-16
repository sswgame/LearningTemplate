#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

// CityReadStateTest — 도시 상태 바이트의 신뢰 경계: 타일 · 건물 · 일꾼이 서로 맞지 않는 값과 범위 밖 값을 거절하고 도시를 그대로 둔다.
// 꾸민 바이트는 `writeState` 의 형식을 그대로 손으로 적는다. 칸마다 맞는 값을 기본으로 두고 한 칸씩 망가뜨린다.

namespace
{
    constexpr int32       kCitySize   = 4;
    constexpr const utf8* kCatalogXML = R"(<CityCatalog roadCost="1">
  <Good id="grain" name="Grain"/>
  <Building id="hut" name="Hut" kind="House" size="1" cost="1"/>
  <Building id="garden" name="Garden" kind="Decoration" size="2" cost="1"/>
  <HouseLevel name="Hut" population="5"/>
</CityCatalog>)";

    /** @brief 도로 (0,0) (1,0), 집 (1,1), 집에서 나온 일꾼 하나인 도시를 꾸밉니다. */
    struct CityFixture
    {
        sw::vector<sw::CityTile>     _listTile{};
        sw::vector<sw::CityBuilding> _listBuilding{};
        sw::vector<sw::CityWalker>   _listWalker{};
        float32                      _time{ 5.0f };
        float32                      _floodFertility{ 0.8f };
        float32                      _wageDebt{ 0.5f };
        int32                        _workforce{ 2 };
        int32                        _employed{ 1 };
        uint8                        _bRoadsDirty{ SW_FALSE };

        static size_t toIndex( int32 x, int32 y ) { return static_cast<size_t>( y * kCitySize + x ); }

        explicit CityFixture( const sw::CityCatalog& catalog )
            : _listTile( static_cast<size_t>( kCitySize * kCitySize ), sw::CityTile{} )
        {
            _listTile[toIndex( 0, 0 )]._bRoad         = SW_TRUE;
            _listTile[toIndex( 0, 0 )]._roadComponent = 0;
            _listTile[toIndex( 1, 0 )]._bRoad         = SW_TRUE;
            _listTile[toIndex( 1, 0 )]._roadComponent = 0;
            _listTile[toIndex( 1, 1 )]._buildingIndex = 0;

            sw::CityBuilding hut;
            hut._pDef       = catalog.findBuilding( "hut" );
            hut._origin     = sw::int2{ 1, 1 };
            hut._accessTile = sw::int2{ 1, 0 };
            hut._population = 3;
            _listBuilding.push_back( hut );

            sw::CityWalker walker;
            walker._tile         = sw::int2{ 1, 0 };
            walker._nextTile     = sw::int2{ 0, 0 };
            walker._homeBuilding = 0;
            walker._stepsLeft    = 4;
            _listWalker.push_back( walker );
        }

        static void writeTiles( sw::Archive& outArchive, const sw::vector<sw::int2>& listTile )
        {
            outArchive << static_cast<uint32>( listTile.size() );
            for ( const sw::int2& tile : listTile )
            {
                sw::StateIO::writeInt2( outArchive, tile );
            }
        }

        bool readInto( sw::CitySimulation& inoutCity ) const
        {
            sw::Archive written;
            written << kCitySize;
            written << kCitySize;
            for ( const sw::CityTile& tile : _listTile )
            {
                written << tile._buildingIndex;
                written << tile._roadComponent;
                written << tile._desirability;
                written << static_cast<uint8>( tile._terrain );
                written << tile._bRoad;
            }
            written << static_cast<uint32>( _listBuilding.size() );
            for ( const sw::CityBuilding& building : _listBuilding )
            {
                sw::StateIO::writeName( written, building._pDef != nullptr ? building._pDef->_id : sw::hashed_string{} );
                building._stock.writeState( written );
                sw::StateIO::writeInt2( written, building._origin );
                sw::StateIO::writeInt2( written, building._accessTile );
                written << building._efficiency;
                written << building._walkerTimer;
                written << building._productionProgress;
                for ( const float32 serviceTime : building._arrServiceTime )
                {
                    written << serviceTime;
                }
                written << building._evolveTimer;
                written << building._devolveTimer;
                written << building._immigrationTimer;
                written << building._assignedWorkers;
                written << building._activeWalkerCount;
                written << building._level;
                written << building._population;
                written << building._bAlive;
            }
            written << static_cast<uint32>( _listWalker.size() );
            for ( const sw::CityWalker& walker : _listWalker )
            {
                writeTiles( written, walker._listPath );
                writeTiles( written, walker._listVisited );
                sw::StateIO::writeName( written, walker._cargoGood );
                sw::StateIO::writeInt2( written, walker._tile );
                sw::StateIO::writeInt2( written, walker._previousTile );
                sw::StateIO::writeInt2( written, walker._nextTile );
                written << walker._progress;
                written << walker._homeBuilding;
                written << walker._targetBuilding;
                written << walker._stepsLeft;
                written << walker._cargoAmount;
                written << static_cast<uint8>( walker._kind );
                written << static_cast<uint8>( walker._service );
                written << walker._bReturning;
                written << walker._bAlive;
            }
            sw::StateIO::writeStepTimer( written, sw::FixedStepTimer{} );
            sw::StateIO::writeRandom( written, sw::GameRandom( 9u ) );
            written << _time;
            written << _floodFertility;
            written << _wageDebt;
            written << static_cast<int32>( 0 );
            written << _workforce;
            written << _employed;
            written << _bRoadsDirty;
            written << static_cast<uint8>( SW_FALSE );
            sw::Archive reading( written.getData(), written.getSize() );
            return inoutCity.readState( reading );
        }
    };

    struct CityReadStateTest
    {
        sw::CityCatalog    _catalog{};
        sw::Wallet         _wallet{};
        sw::CitySimulation _city{};

        CityReadStateTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kCatalogXML, "CityReadStateTest" ) );
            _wallet.add( sw::CitySettings{}._currency, 1000 );
            SW_REQUIRE_TRUE( open( _city ) );
        }

        bool open( sw::CitySimulation& outCity )
        {
            sw::SessionStateRefs refs;
            refs._pWallet = &_wallet;
            return outCity.initialize( &_catalog, kCitySize, kCitySize, sw::CitySettings{}, refs );
        }
    };
} // namespace

/**
 * @brief [CityReadStateTest] 돌아간 실제 도시는 왕복하고, 형식에 맞게 꾸민 도시도 읽힌다
 */
SW_TEST_CASE_F( CityReadStateTest, RealAndCraftedStatesRead )
{
    SW_REQUIRE_TRUE( _city.placeRoad( 0, 0 ) == sw::CityPlaceResult::Ok );
    SW_REQUIRE_TRUE( _city.placeRoad( 1, 0 ) == sw::CityPlaceResult::Ok );
    SW_REQUIRE_TRUE( _city.placeBuilding( "hut", 1, 1 ) == sw::CityPlaceResult::Ok );
    for ( int32 frame = 0; frame < 40; ++frame )
    {
        _city.update( 0.25f );
    }

    sw::Archive written;
    _city.writeState( written );
    sw::CitySimulation restored;
    SW_REQUIRE_TRUE( open( restored ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( _city.getPopulation(), restored.getPopulation() );
    SW_EXPECT_EQUAL( size_t( 1 ), restored.getBuildings().size() );

    SW_REQUIRE_TRUE( CityFixture( _catalog ).readInto( restored ) );
    SW_EXPECT_EQUAL( 3, restored.getPopulation() );
}

/**
 * @brief [CityReadStateTest] 결함: 타일의 건물 번호를 타일 수로만 막아 건물 목록의 범위 밖을 읽었고, 살아 있는데 정의가 없는 건물은 `_pDef` 를 바로 찔렀고, 격자 밖 건물은 `demolish` 가 범위 밖에 썼다. 거절하고 도시를 둔다
 */
SW_TEST_CASE_F( CityReadStateTest, RefusesTilesAndBuildingsThatDoNotMatch )
{
    SW_REQUIRE_TRUE( CityFixture( _catalog ).readInto( _city ) );

    CityFixture indexOutside( _catalog );
    indexOutside._listTile[0]                                           = sw::CityTile{};
    indexOutside._listTile[CityFixture::toIndex( 2, 2 )]._buildingIndex = 1;
    SW_EXPECT_FALSE( indexOutside.readInto( _city ) );

    CityFixture indexBroken( _catalog );
    indexBroken._listTile[CityFixture::toIndex( 2, 2 )]._buildingIndex = -2;
    SW_EXPECT_FALSE( indexBroken.readInto( _city ) );

    CityFixture tileToDead( _catalog );
    tileToDead._listWalker.clear();
    tileToDead._listBuilding[0]._bAlive = SW_FALSE;
    SW_EXPECT_FALSE( tileToDead.readInto( _city ) );

    CityFixture footprintMissing( _catalog );
    footprintMissing._listTile[CityFixture::toIndex( 1, 1 )]._buildingIndex = -1;
    SW_EXPECT_FALSE( footprintMissing.readInto( _city ) );

    CityFixture strayTile( _catalog );
    strayTile._listTile[CityFixture::toIndex( 3, 3 )]._buildingIndex = 0;
    SW_EXPECT_FALSE( strayTile.readInto( _city ) );

    CityFixture roadUnderBuilding( _catalog );
    roadUnderBuilding._listTile[CityFixture::toIndex( 1, 1 )]._bRoad = SW_TRUE;
    SW_EXPECT_FALSE( roadUnderBuilding.readInto( _city ) );

    CityFixture roadBroken( _catalog );
    roadBroken._listTile[0]._bRoad = 2;
    SW_EXPECT_FALSE( roadBroken.readInto( _city ) );

    CityFixture componentBroken( _catalog );
    componentBroken._listTile[0]._roadComponent = -2;
    SW_EXPECT_FALSE( componentBroken.readInto( _city ) );

    CityFixture aliveNoDef( _catalog );
    aliveNoDef._listBuilding[0]._pDef = nullptr;
    SW_EXPECT_FALSE( aliveNoDef.readInto( _city ) );

    CityFixture originOutside( _catalog );
    originOutside._listBuilding[0]._origin = sw::int2{ kCitySize, 0 };
    SW_EXPECT_FALSE( originOutside.readInto( _city ) );

    CityFixture footprintOver( _catalog );
    footprintOver._listBuilding[0]._pDef       = _catalog.findBuilding( "garden" );
    footprintOver._listBuilding[0]._origin     = sw::int2{ 3, 3 }; // 두 칸짜리가 격자 끝을 넘는다
    footprintOver._listBuilding[0]._population = 0;
    SW_EXPECT_FALSE( footprintOver.readInto( _city ) );

    CityFixture accessOutside( _catalog );
    accessOutside._listBuilding[0]._accessTile = sw::int2{ kCitySize, 0 };
    SW_EXPECT_FALSE( accessOutside.readInto( _city ) );

    CityFixture accessBroken( _catalog );
    accessBroken._listBuilding[0]._accessTile = sw::int2{ -2, 0 };
    SW_EXPECT_FALSE( accessBroken.readInto( _city ) );

    SW_EXPECT_EQUAL( 3, _city.getPopulation() );
    SW_REQUIRE_TRUE( _city.findBuildingAt( 1, 1 ) != nullptr );
}

/**
 * @brief [CityReadStateTest] 결함: 건물 값의 범위를 보지 않았다. 범위 밖 효율, 음수 타이머, 정의보다 많은 일꾼, 음수 일꾼 수, 없는 집 단계, 음수 인구, 집 아닌 건물의 인구, 깨진 표시를 거절하고 도시를 둔다
 */
SW_TEST_CASE_F( CityReadStateTest, RefusesBuildingValuesOutOfRange )
{
    SW_REQUIRE_TRUE( CityFixture( _catalog ).readInto( _city ) );

    CityFixture efficiencyOver( _catalog );
    efficiencyOver._listBuilding[0]._efficiency = 1.5f;
    SW_EXPECT_FALSE( efficiencyOver.readInto( _city ) );

    CityFixture efficiencyNegative( _catalog );
    efficiencyNegative._listBuilding[0]._efficiency = -0.5f;
    SW_EXPECT_FALSE( efficiencyNegative.readInto( _city ) );

    CityFixture timerNegative( _catalog );
    timerNegative._listBuilding[0]._walkerTimer = -1.0f;
    SW_EXPECT_FALSE( timerNegative.readInto( _city ) );

    CityFixture productionNegative( _catalog );
    productionNegative._listBuilding[0]._productionProgress = -1.0f;
    SW_EXPECT_FALSE( productionNegative.readInto( _city ) );

    CityFixture assignedOver( _catalog );
    assignedOver._listBuilding[0]._assignedWorkers = 1; // 집은 일꾼이 필요 없다
    SW_EXPECT_FALSE( assignedOver.readInto( _city ) );

    CityFixture activeNegative( _catalog );
    activeNegative._listBuilding[0]._activeWalkerCount = -1;
    SW_EXPECT_FALSE( activeNegative.readInto( _city ) );

    CityFixture levelOver( _catalog );
    levelOver._listBuilding[0]._level = 1;
    SW_EXPECT_FALSE( levelOver.readInto( _city ) );

    CityFixture levelNegative( _catalog );
    levelNegative._listBuilding[0]._level = -1;
    SW_EXPECT_FALSE( levelNegative.readInto( _city ) );

    CityFixture populationNegative( _catalog );
    populationNegative._listBuilding[0]._population = -1;
    SW_EXPECT_FALSE( populationNegative.readInto( _city ) );

    CityFixture aliveBroken( _catalog );
    aliveBroken._listBuilding[0]._bAlive = 2;
    SW_EXPECT_FALSE( aliveBroken.readInto( _city ) );

    SW_EXPECT_EQUAL( 3, _city.getPopulation() );
}

/**
 * @brief [CityReadStateTest] 결함: 일꾼의 목적지를 보지 않아 건물 목록의 범위 밖을 읽었고, 집이 없는 일꾼은 `startReturn` 이 -1 을 찔렀으며, 터무니없는 진행은 `updateWalkers` 를 끝나지 않게 했다. 거절하고 도시를 둔다
 */
SW_TEST_CASE_F( CityReadStateTest, RefusesBrokenWalkers )
{
    SW_REQUIRE_TRUE( CityFixture( _catalog ).readInto( _city ) );

    CityFixture targetOutside( _catalog );
    targetOutside._listWalker[0]._targetBuilding = 1;
    SW_EXPECT_FALSE( targetOutside.readInto( _city ) );

    CityFixture homeless( _catalog );
    homeless._listWalker[0]._homeBuilding = -1;
    SW_EXPECT_FALSE( homeless.readInto( _city ) );

    CityFixture homeOutside( _catalog );
    homeOutside._listWalker[0]._homeBuilding = 1;
    SW_EXPECT_FALSE( homeOutside.readInto( _city ) );

    CityFixture      homeDead( _catalog );
    sw::CityBuilding dead;
    dead._bAlive = SW_FALSE;
    homeDead._listBuilding.push_back( dead );
    homeDead._listWalker[0]._homeBuilding = 1;
    SW_EXPECT_FALSE( homeDead.readInto( _city ) );

    CityFixture targetDead( _catalog );
    targetDead._listBuilding.push_back( dead );
    targetDead._listWalker[0]._targetBuilding = 1;
    SW_EXPECT_FALSE( targetDead.readInto( _city ) );

    CityFixture progressHuge( _catalog );
    progressHuge._listWalker[0]._progress = 1.0e30f;
    SW_EXPECT_FALSE( progressHuge.readInto( _city ) );

    CityFixture progressNegative( _catalog );
    progressNegative._listWalker[0]._progress = -1.0f;
    SW_EXPECT_FALSE( progressNegative.readInto( _city ) );

    CityFixture tileOutside( _catalog );
    tileOutside._listWalker[0]._tile = sw::int2{ kCitySize, 0 };
    SW_EXPECT_FALSE( tileOutside.readInto( _city ) );

    CityFixture nextOutside( _catalog );
    nextOutside._listWalker[0]._nextTile = sw::int2{ 0, -1 };
    SW_EXPECT_FALSE( nextOutside.readInto( _city ) );

    CityFixture previousOutside( _catalog );
    previousOutside._listWalker[0]._previousTile = sw::int2{ kCitySize, 0 };
    SW_EXPECT_FALSE( previousOutside.readInto( _city ) );

    CityFixture pathOutside( _catalog );
    pathOutside._listWalker[0]._listPath = {
        sw::int2{ kCitySize, 0 }
    };
    SW_EXPECT_FALSE( pathOutside.readInto( _city ) );

    CityFixture visitedOutside( _catalog );
    visitedOutside._listWalker[0]._listVisited = {
        sw::int2{ -1, 0 }
    };
    SW_EXPECT_FALSE( visitedOutside.readInto( _city ) );

    CityFixture cargoNegative( _catalog );
    cargoNegative._listWalker[0]._cargoAmount = -1;
    SW_EXPECT_FALSE( cargoNegative.readInto( _city ) );

    CityFixture returningBroken( _catalog );
    returningBroken._listWalker[0]._bReturning = 2;
    SW_EXPECT_FALSE( returningBroken.readInto( _city ) );

    SW_REQUIRE_EQUAL( size_t( 1 ), _city.getWalkers().size() );
    SW_EXPECT_EQUAL( 0, _city.getWalkers()[0]._homeBuilding );
}

/**
 * @brief [CityReadStateTest] 결함: 도시 전체 값의 범위를 보지 않았다. 음수 시각, 범위 밖 비옥함 · 임금 빚, 음수 노동력, 노동력을 넘는 고용, 깨진 표시를 거절하고 도시를 둔다
 */
SW_TEST_CASE_F( CityReadStateTest, RefusesCityValuesOutOfRange )
{
    SW_REQUIRE_TRUE( CityFixture( _catalog ).readInto( _city ) );

    CityFixture timeNegative( _catalog );
    timeNegative._time = -1.0f;
    SW_EXPECT_FALSE( timeNegative.readInto( _city ) );

    CityFixture fertilityOver( _catalog );
    fertilityOver._floodFertility = 1.5f;
    SW_EXPECT_FALSE( fertilityOver.readInto( _city ) );

    CityFixture debtNegative( _catalog );
    debtNegative._wageDebt = -1.0f;
    SW_EXPECT_FALSE( debtNegative.readInto( _city ) );

    CityFixture debtHuge( _catalog );
    debtHuge._wageDebt = 1.0e20f;
    SW_EXPECT_FALSE( debtHuge.readInto( _city ) );

    CityFixture workforceNegative( _catalog );
    workforceNegative._workforce = -1;
    workforceNegative._employed  = -1;
    SW_EXPECT_FALSE( workforceNegative.readInto( _city ) );

    CityFixture employedOver( _catalog );
    employedOver._employed = 3;
    SW_EXPECT_FALSE( employedOver.readInto( _city ) );

    CityFixture dirtyBroken( _catalog );
    dirtyBroken._bRoadsDirty = 2;
    SW_EXPECT_FALSE( dirtyBroken.readInto( _city ) );

    SW_EXPECT_NEAR( 5.0f, _city.getTime(), 1.0e-5f );
    SW_EXPECT_EQUAL( 2, _city.getWorkforce() );
}
