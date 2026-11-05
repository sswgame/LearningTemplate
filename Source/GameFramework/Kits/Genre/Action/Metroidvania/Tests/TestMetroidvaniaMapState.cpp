#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/Metroidvania/Catalog/MetroidvaniaCatalog.h"
#include "GameFramework/Kits/Genre/Action/Metroidvania/Rule/MetroidvaniaMapState.h"
#include "GameFramework/Runtime/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

// MetroidvaniaMapStateTest — 지도: 지도 사기와 통화, 지도 없으면 안 그려짐, 아이템 표시 세 조건, 줍기, 지점 열기와 빠른 이동, 탐색 · 수집 · 완료율, 세이브 순서와 왕복.
// 코드를 읽고 경계를 맞춰 봤으나 결함은 찾지 못했다. 키트 GF_Metroidvania(nogpu).

namespace
{
    constexpr const utf8* kMapXML = R"(<Metroidvania>
  <Parry><Window grade="Perfect"/></Parry>
  <Map region="crossroads" price="30"/>
  <Map region="deep" price="100"/>
  <Map region="free"/>
  <Site id="bench" area="hall" fastTravel="true"/>
  <Site id="stag" area="cave" fastTravel="true"/>
  <Site id="sign" area="hall"/>
  <Pickup id="mask" area="cave"/>
  <Pickup id="vessel" area="hall"/>
</Metroidvania>)";

    const sw::hashed_string kCurrency{ "geo" };
    const sw::hashed_string kCrossroads{ "crossroads" };
    const sw::hashed_string kDeep{ "deep" };
    const sw::hashed_string kFree{ "free" };
    const sw::hashed_string kHallArea{ "hall" };
    const sw::hashed_string kCaveArea{ "cave" };
    const sw::hashed_string kPitArea{ "pit" };
    const sw::hashed_string kBenchSite{ "bench" };
    const sw::hashed_string kStagSite{ "stag" };
    const sw::hashed_string kSignSite{ "sign" };
    const sw::hashed_string kMask{ "mask" };
    const sw::hashed_string kVessel{ "vessel" };

    constexpr float32 kMapTolerance = 1.0e-3f;

    /** @brief 방 넷: 교차로 지역에 hall · cave, 깊은 지역에 pit, 무료 지역에 glade 입니다. 연결은 없어서 들어가도 이웃이 드러나지 않습니다. */
    struct MapBench
    {
        sw::MetroidvaniaCatalog  _catalog;
        sw::AreaGraph            _graph;
        sw::MetroidvaniaMapState _map;
        sw::Wallet               _wallet;

        [[nodiscard]] bool addArea( const sw::hashed_string& id, const sw::hashed_string& region )
        {
            sw::AreaDef area;
            area._id     = id;
            area._region = region;
            return _graph.addArea( area );
        }

        [[nodiscard]] bool setUp()
        {
            if ( _catalog.loadFromXMLText( kMapXML, "MetroidvaniaMapStateTest" ) == false )
                return false;
            const bool bAreas = addArea( kHallArea, kCrossroads ) && addArea( kCaveArea, kCrossroads ) && addArea( kPitArea, kDeep ) && addArea( sw::hashed_string( "glade" ), kFree );
            _map.initialize( &_catalog, &_graph );
            return bAreas;
        }
    };

    struct MetroidvaniaMapTestUtil
    {
        static void writeListBytes( sw::Archive& outArchive, const sw::vector<sw::hashed_string>& listRegion, const sw::vector<sw::hashed_string>& listSite, const sw::vector<sw::hashed_string>& listPickup )
        {
            for ( const sw::vector<sw::hashed_string>* pList : { &listRegion, &listSite, &listPickup } )
            {
                outArchive << static_cast<uint32>( pList->size() );
                for ( const sw::hashed_string& name : *pList )
                {
                    sw::StateArchiveUtil::writeName( outArchive, name );
                }
            }
        }
    };
} // namespace

/**
 * @brief [MetroidvaniaMapStateTest] 지도를 사면 값만큼 줄고 지역이 드러난다. 모르는 지역 · 이미 산 지도 · 돈 모자람은 통화를 건드리지 않는다. 값 0 은 빈손으로도 산다
 */
SW_TEST_CASE( MetroidvaniaMapStateTest, BuyingAMapSpendsAndReveals )
{
    MapBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    bench._wallet.add( kCurrency, 40 );

    SW_EXPECT_TRUE( bench._map.buyRegionMap( sw::hashed_string( "ghost" ), bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::UnknownRegion );
    SW_EXPECT_TRUE( bench._map.buyRegionMap( kDeep, bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::NotEnoughCurrency );
    SW_EXPECT_EQUAL( int64( 40 ), bench._wallet.getBalance( kCurrency ) );
    SW_EXPECT_FALSE( bench._map.hasRegionMap( kDeep ) );
    SW_EXPECT_FALSE( bench._graph.isDiscovered( kPitArea ) );

    SW_EXPECT_TRUE( bench._map.buyRegionMap( kCrossroads, bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::Bought );
    SW_EXPECT_EQUAL( int64( 10 ), bench._wallet.getBalance( kCurrency ) );
    SW_EXPECT_TRUE( bench._graph.isDiscovered( kHallArea ) );
    SW_EXPECT_TRUE( bench._graph.isDiscovered( kCaveArea ) );

    SW_EXPECT_TRUE( bench._map.buyRegionMap( kCrossroads, bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::AlreadyOwned );
    SW_EXPECT_EQUAL( int64( 10 ), bench._wallet.getBalance( kCurrency ) );

    SW_EXPECT_TRUE( bench._map.buyRegionMap( kFree, bench._wallet, sw::hashed_string( "nothing" ) ) == sw::MetroidvaniaMapPurchase::Bought );

    sw::MetroidvaniaMapState noGraph;
    noGraph.initialize( &bench._catalog, nullptr );
    SW_EXPECT_TRUE( noGraph.buyRegionMap( kDeep, bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::UnknownRegion );
    SW_EXPECT_FALSE( noGraph.enterArea( kHallArea ) );
}

/**
 * @brief [MetroidvaniaMapStateTest] 지도가 없으면 들어가 본 방도 그려지지 않고, 지도가 있으면 그 지역 방만 그려진다
 */
SW_TEST_CASE( MetroidvaniaMapStateTest, RoomsNeedTheRegionMapToBeDrawn )
{
    MapBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    bench._wallet.add( kCurrency, 40 );

    SW_EXPECT_TRUE( bench._map.enterArea( kHallArea ) );
    SW_EXPECT_FALSE( bench._map.enterArea( kHallArea ) );
    SW_EXPECT_FALSE( bench._map.isShownOnMap( kHallArea ) );

    SW_REQUIRE_TRUE( bench._map.buyRegionMap( kCrossroads, bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::Bought );
    SW_EXPECT_TRUE( bench._map.isShownOnMap( kHallArea ) );
    SW_EXPECT_TRUE( bench._map.isShownOnMap( kCaveArea ) );

    SW_EXPECT_TRUE( bench._map.enterArea( kPitArea ) );
    SW_EXPECT_FALSE( bench._map.isShownOnMap( kPitArea ) );
    SW_EXPECT_FALSE( bench._map.isShownOnMap( sw::hashed_string( "ghost" ) ) );
}

/**
 * @brief [MetroidvaniaMapStateTest] 아이템 표시는 그려진 방 · 방문 전 · 줍기 전을 모두 만족할 때만 카탈로그 순서로 나온다. 줍기는 처음만 정의를 돌려준다
 */
SW_TEST_CASE( MetroidvaniaMapStateTest, ItemMarkersNeedAllThreeConditions )
{
    MapBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    bench._wallet.add( kCurrency, 40 );

    sw::vector<const sw::MetroidvaniaPickupDef*> listMarker;
    listMarker.push_back( nullptr );
    bench._map.collectItemMarkers( listMarker );
    SW_EXPECT_TRUE( listMarker.empty() ); // 지도 없음

    SW_REQUIRE_TRUE( bench._map.buyRegionMap( kCrossroads, bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::Bought );
    bench._map.collectItemMarkers( listMarker );
    SW_REQUIRE_EQUAL( size_t( 2 ), listMarker.size() );
    SW_EXPECT_EQUAL( kMask, listMarker[0]->_id );
    SW_EXPECT_EQUAL( kVessel, listMarker[1]->_id );

    SW_REQUIRE_TRUE( bench._map.enterArea( kCaveArea ) ); // 가 본 방은 표시하지 않는다
    bench._map.collectItemMarkers( listMarker );
    SW_REQUIRE_EQUAL( size_t( 1 ), listMarker.size() );
    SW_EXPECT_EQUAL( kVessel, listMarker[0]->_id );

    const sw::MetroidvaniaPickupDef* pPickup = bench._map.collectPickup( kVessel );
    SW_REQUIRE_NOT_NULL( pPickup );
    SW_EXPECT_EQUAL( kHallArea, pPickup->_area );
    SW_EXPECT_TRUE( bench._map.collectPickup( kVessel ) == nullptr );
    SW_EXPECT_TRUE( bench._map.collectPickup( sw::hashed_string( "ghost" ) ) == nullptr );
    SW_EXPECT_TRUE( bench._map.isCollected( kVessel ) );
    bench._map.collectItemMarkers( listMarker );
    SW_EXPECT_TRUE( listMarker.empty() );
}

/**
 * @brief [MetroidvaniaMapStateTest] 지점은 그 방을 방문해야 한 번 열린다. 빠른 이동은 서로 다른 두 곳이 모두 정거장이고 열려 있어야 한다
 */
SW_TEST_CASE( MetroidvaniaMapStateTest, SitesAndFastTravel )
{
    MapBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    SW_EXPECT_FALSE( bench._map.activateSite( kBenchSite ) ); // 아직 방문 전
    SW_EXPECT_FALSE( bench._map.activateSite( sw::hashed_string( "ghost" ) ) );

    SW_REQUIRE_TRUE( bench._map.enterArea( kHallArea ) );
    SW_EXPECT_TRUE( bench._map.activateSite( kBenchSite ) );
    SW_EXPECT_FALSE( bench._map.activateSite( kBenchSite ) );
    SW_EXPECT_TRUE( bench._map.isSiteActive( kBenchSite ) );
    SW_EXPECT_FALSE( bench._map.canFastTravel( kBenchSite, kStagSite ) ); // 정거장 하나는 닫혔다

    SW_REQUIRE_TRUE( bench._map.enterArea( kCaveArea ) );
    SW_REQUIRE_TRUE( bench._map.activateSite( kStagSite ) );
    SW_EXPECT_TRUE( bench._map.canFastTravel( kBenchSite, kStagSite ) );
    SW_EXPECT_TRUE( bench._map.canFastTravel( kStagSite, kBenchSite ) );
    SW_EXPECT_FALSE( bench._map.canFastTravel( kBenchSite, kBenchSite ) );

    SW_REQUIRE_TRUE( bench._map.activateSite( kSignSite ) );
    SW_EXPECT_FALSE( bench._map.canFastTravel( kSignSite, kStagSite ) ); // 정거장이 아니다
    SW_EXPECT_FALSE( bench._map.canFastTravel( sw::hashed_string( "ghost" ), kStagSite ) );
}

/**
 * @brief [MetroidvaniaMapStateTest] 탐색률 · 수집률 · 완료율은 방과 줍는 것을 같은 무게로 센다. 줍는 것이 없으면 수집 만점이고, 그래프가 없으면 0 이다
 */
SW_TEST_CASE( MetroidvaniaMapStateTest, RatiosCountRoomsAndPickups )
{
    MapBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    SW_EXPECT_NEAR( 0.0f, bench._map.computeExplorationRatio(), kMapTolerance );
    SW_EXPECT_NEAR( 0.0f, bench._map.computeCollectionRatio(), kMapTolerance );
    SW_EXPECT_NEAR( 0.0f, bench._map.computeCompletionPercent(), kMapTolerance );

    SW_REQUIRE_TRUE( bench._map.enterArea( kHallArea ) );
    SW_REQUIRE_NOT_NULL( bench._map.collectPickup( kMask ) );
    SW_EXPECT_NEAR( 0.25f, bench._map.computeExplorationRatio(), kMapTolerance );
    SW_EXPECT_NEAR( 0.5f, bench._map.computeCollectionRatio(), kMapTolerance );
    SW_EXPECT_NEAR( 100.0f / 3.0f, bench._map.computeCompletionPercent(), kMapTolerance ); // (1 + 1) / (4 + 2)

    sw::MetroidvaniaMapState noGraph;
    noGraph.initialize( &bench._catalog, nullptr );
    SW_EXPECT_NEAR( 0.0f, noGraph.computeExplorationRatio(), kMapTolerance );
    SW_EXPECT_NEAR( 0.0f, noGraph.computeCompletionPercent(), kMapTolerance );

    sw::MetroidvaniaMapState noCatalog;
    noCatalog.initialize( nullptr, nullptr );
    SW_EXPECT_NEAR( 1.0f, noCatalog.computeCollectionRatio(), kMapTolerance );
    SW_EXPECT_TRUE( noCatalog.collectPickup( kMask ) == nullptr );

    sw::MetroidvaniaCatalog empty;
    SW_EXPECT_FALSE( empty.loadFromXMLText( R"(<Metroidvania><Parry><Window grade="Perfect"/></Parry></Metroidvania>)", "empty" ) ); // 원소가 없어 읽기 실패지만 규칙은 들어온다
    sw::AreaGraph            noAreas;
    sw::MetroidvaniaMapState blank;
    blank.initialize( &empty, &noAreas );
    SW_EXPECT_NEAR( 1.0f, blank.computeCollectionRatio(), kMapTolerance );
    SW_EXPECT_NEAR( 100.0f, blank.computeCompletionPercent(), kMapTolerance );
}

/**
 * @brief [MetroidvaniaMapStateTest] 세이브 목록은 얻은 순서와 상관없이 카탈로그 순서다. 복원은 앞 상태를 지우고 모르는 id · 겹침을 건너뛰며, 산 지역을 다시 드러낸다
 */
SW_TEST_CASE( MetroidvaniaMapStateTest, SaveListsAreInCatalogOrder )
{
    MapBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    bench._wallet.add( kCurrency, 200 );
    SW_REQUIRE_TRUE( bench._map.buyRegionMap( kDeep, bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::Bought );
    SW_REQUIRE_TRUE( bench._map.buyRegionMap( kCrossroads, bench._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::Bought );
    SW_REQUIRE_NOT_NULL( bench._map.collectPickup( kVessel ) );
    SW_REQUIRE_NOT_NULL( bench._map.collectPickup( kMask ) );
    SW_REQUIRE_TRUE( bench._map.enterArea( kHallArea ) );
    SW_REQUIRE_TRUE( bench._map.activateSite( kSignSite ) );
    SW_REQUIRE_TRUE( bench._map.activateSite( kBenchSite ) );

    sw::vector<sw::hashed_string> listRegion;
    sw::vector<sw::hashed_string> listSite;
    sw::vector<sw::hashed_string> listPickup;
    bench._map.fillSaveState( listRegion, listSite, listPickup );
    SW_REQUIRE_EQUAL( size_t( 2 ), listRegion.size() );
    SW_EXPECT_EQUAL( kCrossroads, listRegion[0] );
    SW_EXPECT_EQUAL( kDeep, listRegion[1] );
    SW_REQUIRE_EQUAL( size_t( 2 ), listSite.size() );
    SW_EXPECT_EQUAL( kBenchSite, listSite[0] );
    SW_REQUIRE_EQUAL( size_t( 2 ), listPickup.size() );
    SW_EXPECT_EQUAL( kMask, listPickup[0] );

    MapBench restored;
    SW_REQUIRE_TRUE( restored.setUp() );
    restored._wallet.add( kCurrency, 100 );
    SW_REQUIRE_TRUE( restored._map.buyRegionMap( kFree, restored._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::Bought );
    const sw::hashed_string ghost{ "ghost" };
    restored._map.restoreSaveState( { kDeep, kDeep, ghost }, { kBenchSite, ghost, kBenchSite }, { ghost, kVessel, kVessel } );
    SW_EXPECT_FALSE( restored._map.hasRegionMap( kFree ) );
    SW_EXPECT_TRUE( restored._map.hasRegionMap( kDeep ) );
    SW_EXPECT_TRUE( restored._graph.isDiscovered( kPitArea ) );
    SW_EXPECT_TRUE( restored._map.isSiteActive( kBenchSite ) );
    SW_EXPECT_TRUE( restored._map.isCollected( kVessel ) );
    SW_EXPECT_NEAR( 0.5f, restored._map.computeCollectionRatio(), kMapTolerance ); // 겹친 것은 한 번만 센다
}

/**
 * @brief [MetroidvaniaMapStateTest] 쓰고 읽으면 이어지고 산 지역을 다시 드러낸다. 모르는 id · 겹침 · 잘린 바이트 · 카탈로그 없음은 거절하며 상태도 그래프도 그대로다
 */
SW_TEST_CASE( MetroidvaniaMapStateTest, StateRoundTripAndRejection )
{
    MapBench original;
    SW_REQUIRE_TRUE( original.setUp() );
    original._wallet.add( kCurrency, 100 );
    SW_REQUIRE_TRUE( original._map.buyRegionMap( kDeep, original._wallet, kCurrency ) == sw::MetroidvaniaMapPurchase::Bought );
    SW_REQUIRE_TRUE( original._map.enterArea( kHallArea ) );
    SW_REQUIRE_TRUE( original._map.activateSite( kBenchSite ) );
    SW_REQUIRE_NOT_NULL( original._map.collectPickup( kMask ) );
    sw::Archive written;
    original._map.writeState( written );

    sw::MetroidvaniaMapState noCatalog;
    sw::Archive              forNoCatalog( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noCatalog.readState( forNoCatalog ) );

    MapBench restored;
    SW_REQUIRE_TRUE( restored.setUp() );
    SW_REQUIRE_NOT_NULL( restored._map.collectPickup( kVessel ) );
    const auto expectRejected = [&restored]( const sw::vector<sw::hashed_string>& listRegion, const sw::vector<sw::hashed_string>& listSite, const sw::vector<sw::hashed_string>& listPickup )
    {
        sw::Archive bytes;
        MetroidvaniaMapTestUtil::writeListBytes( bytes, listRegion, listSite, listPickup );
        sw::Archive reading( bytes.getData(), bytes.getSize() );
        SW_EXPECT_FALSE( restored._map.readState( reading ) );
        SW_EXPECT_TRUE( restored._map.isCollected( kVessel ) );
        SW_EXPECT_FALSE( restored._map.hasRegionMap( kDeep ) );
        SW_EXPECT_FALSE( restored._graph.isDiscovered( kPitArea ) ); // 앞 목록이 맞아도 드러내지 않는다
    };
    const sw::hashed_string ghost{ "ghost" };
    expectRejected( { ghost }, {}, {} );
    expectRejected( { kDeep, kDeep }, {}, {} );
    expectRejected( { kDeep }, { ghost }, {} );
    expectRejected( { kDeep }, { kBenchSite, kBenchSite }, {} );
    expectRejected( { kDeep }, {}, { ghost } );
    expectRejected( { kDeep }, {}, { kMask, kMask } );
    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( restored._map.readState( truncated ) );
    SW_EXPECT_FALSE( restored._graph.isDiscovered( kPitArea ) );

    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored._map.readState( reading ) );
    SW_EXPECT_TRUE( restored._map.hasRegionMap( kDeep ) );
    SW_EXPECT_TRUE( restored._graph.isDiscovered( kPitArea ) );
    SW_EXPECT_TRUE( restored._map.isSiteActive( kBenchSite ) );
    SW_EXPECT_TRUE( restored._map.isCollected( kMask ) );
    SW_EXPECT_FALSE( restored._map.isCollected( kVessel ) );
}
