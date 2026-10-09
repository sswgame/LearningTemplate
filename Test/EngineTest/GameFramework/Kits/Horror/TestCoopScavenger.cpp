#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/AI/SpawnDirector.h"
#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/Base/World/GameFlags.h"
#include "GameFramework/Base/World/WeatherSystem.h"
#include "GameFramework/Base/World/WorldClock.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerCarry.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerCatalog.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerExpedition.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerFacility.h"
#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerQuota.h"

#include "TestFramework/TestFramework.h"

// 협동 수집 공포 키트 — 할당량 공식 · 매입률 · 마감 판정, 절차 시설 그래프(잠긴 문 · 화재 출구 · 결정성), 운반 칸 · 양손 · 무게 속도,
// 하루 흐름(해 질 녘 · 자정 자동 이륙 · 남겨진 사람), 죽음 · 시신 회수 · 벌금 · 전멸 손실, 위성 위험도 · 날씨에 따른 위협, 회사 매입 · 터미널 · 게임 오버.

using namespace sw;

namespace
{
    constexpr const utf8* kScavengerCatalogXml = R"(
<ScavengerCatalog terminalShop="terminal" currency="Credits" crewHealth="100">
  <Quota start="130" days="2" increase="100" steepness="16" randomness="0.5" overtime="5" credits="100"/>
  <BuyRate daysLeft="2" rate="0.4"/>
  <BuyRate daysLeft="0" rate="1"/>
  <Day secondsPerDay="240" arrival="8" dusk="18" depart="24"/>
  <Carry slots="4" speedPerWeight="0.01" minSpeed="0.4" bodyWeight="60"/>
  <Penalty deathFine="0.2" recoveredFine="0.05" allDeadLoss="0.5" allDeadKeep="0"/>
  <Facility rooms="10" roomsMax="14" lockedChance="0.3" loopChance="0.2" fireExits="2"/>
  <Scrap id="bolt" min="20" max="40" weight="5" spawnWeight="5"/>
  <Scrap id="bell" min="60" max="90" weight="30" spawnWeight="2" twoHanded="true"/>
  <Scrap id="ring" min="80" max="120" weight="1" spawnWeight="1"/>
  <Moon id="experimentation" risk="1" cost="0" scrap="8" scrapMax="12"/>
  <Moon id="titan" risk="3" cost="50" scrap="20" scrapMax="25" valueMin="1.2" valueMax="1.5" scraps="bell,ring"/>
  <Moon id="rend" risk="1" cost="0" scrap="8" scrapMax="12"/>
  <Moon id="company" company="true"/>
</ScavengerCatalog>
)";

    constexpr const utf8* kScavengerWeatherXml = R"(
<WeatherCatalog transition="0">
  <Weather id="clear" seasons="experimentation:1,titan:1,company:1" minDuration="100000" maxDuration="100000"><Values threat="1" scrapValue="1"/></Weather>
  <Weather id="eclipsed" seasons="rend:1" minDuration="100000" maxDuration="100000"><Values threat="2" scrapValue="1"/></Weather>
</WeatherCatalog>
)";

    constexpr const utf8* kScavengerThreatXml = R"(
<SpawnTable budgetPerMinute="6" maxBudget="40" startBudget="0">
  <Entry id="bracken" cost="3" weight="1" max="20" tags="Indoor"/>
  <Entry id="lootbug" cost="1" weight="2" max="20" tags="Indoor"/>
  <Entry id="dog" cost="2" weight="1" max="20" tags="Outdoor"/>
</SpawnTable>
)";

    constexpr const utf8* kScavengerItemXml = R"(
<ItemCatalog>
  <Item id="shovel" category="Tool" weight="8" value="30"/>
  <Item id="flashlight" category="Tool" weight="2" value="15"/>
</ItemCatalog>
)";

    constexpr const utf8* kScavengerShopXml = R"(
<ShopCatalog>
  <Shop id="terminal" currency="Credits"><Stock item="shovel" price="30"/><Stock item="flashlight" price="15"/></Shop>
</ShopCatalog>
)";

    struct ScavengerTestData
    {
        ScavengerCatalog _catalog;
        WeatherCatalog   _weatherCatalog;
        SpawnTable       _threatTable;
        ItemCatalog      _itemCatalog;
        ShopCatalog      _shopCatalog;
        Inventory        _shipStorage; ///< 우주선 창고(원정이 빌린다)
        Wallet           _wallet;      ///< 회사 돈(원정이 빌린다)
        WorldClock       _clock;       ///< 공유 시계(원정이 빌린다 — 흘리는 것은 시험)
        WeatherSystem    _weather;     ///< 공유 날씨(원정이 빌린다)
        GameFlags        _flags;       ///< 공유 플래그(시설 문 잠금)

        bool initialize()
        {
            return _catalog.loadFromXmlText( kScavengerCatalogXml, "CoopScavengerTest" ) && _weatherCatalog.loadFromXmlText( kScavengerWeatherXml, "CoopScavengerTest" ) &&
                   _threatTable.loadFromXmlText( kScavengerThreatXml, "CoopScavengerTest" ) && _itemCatalog.loadFromXmlText( kScavengerItemXml, "CoopScavengerTest" ) &&
                   _shopCatalog.loadFromXmlText( kScavengerShopXml, "CoopScavengerTest" );
        }

        /** @brief 원정이 빌릴 우주선 창고를 새로 엽니다. */
        Inventory& openShipStorage()
        {
            _shipStorage.initialize( &_itemCatalog, 64 );
            return _shipStorage;
        }

        /** @brief 원정이 빌릴 공유 상태입니다 — 시계는 도착 시각에서, 날씨는 새로 엽니다. */
        GameStateRefs makeRefs()
        {
            WorldClockSettings clockSettings;
            clockSettings._secondsPerDay = _catalog.getDaySettings()._secondsPerDay;
            clockSettings._startHour     = _catalog.getDaySettings()._arrivalHour;
            _clock.initialize( clockSettings );
            _weather.initialize( &_weatherCatalog, 1u, hashed_string{} );
            GameStateRefs refs;
            refs._pWallet  = &_wallet;
            refs._pClock   = &_clock;
            refs._pWeather = &_weather;
            refs._pFlags   = &_flags;
            return refs;
        }

        /** @brief 이미 연 공유 상태를 다시 열지 않고 빌려 줍니다(되살린 원정이 같은 시계 · 날씨를 본다). */
        GameStateRefs borrowRefs()
        {
            GameStateRefs refs;
            refs._pWallet  = &_wallet;
            refs._pClock   = &_clock;
            refs._pWeather = &_weather;
            refs._pFlags   = &_flags;
            return refs;
        }

        ScavengerExpeditionData makeData() const
        {
            ScavengerExpeditionData data;
            data._pCatalog        = &_catalog;
            data._pWeatherCatalog = &_weatherCatalog;
            data._pThreatTable    = &_threatTable;
            data._pShopCatalog    = &_shopCatalog;
            data._pItemCatalog    = &_itemCatalog;
            return data;
        }
    };

    /** @brief 지금 열린 문만으로 @p target 까지 한 방씩 걷습니다. */
    bool walkTo( ScavengerExpedition& expedition, int32 player, const hashed_string& target )
    {
        const ScavengerFacility& facility = expedition.getFacility();
        vector<hashed_string>    listArea;
        if ( facility.getGraph().findPath( expedition.findCrewMember( player )->_areaId, target, facility.getFlags(), listArea ) == false )
            return false;
        for ( size_t index = 1; index < listArea.size(); ++index )
        {
            if ( expedition.movePlayer( player, listArea[index] ) != ScavengerActionResult::Ok )
                return false;
        }
        return true;
    }

    /** @brief 우주선에서 닿는 첫 고철(시신 · 양손 제외)을 주워 우주선에 싣습니다. 실은 고철의 가치입니다(없으면 −1). */
    int32 fetchOneScrap( ScavengerExpedition& expedition, int32 player )
    {
        const hashed_string ship( ScavengerExpedition::kShipAreaId );
        for ( const ScavengerScrap& scrap : expedition.getFacility().getGroundScrap() )
        {
            if ( scrap.isBody() || scrap._bTwoHanded == SW_TRUE )
                continue;
            vector<hashed_string> listArea;
            if ( expedition.getFacility().getGraph().findPath( ship, scrap._areaId, expedition.getFacility().getFlags(), listArea ) == false )
                continue;
            const int32 uid   = scrap._uid;
            const int32 value = scrap._value;
            if ( walkTo( expedition, player, scrap._areaId ) == false || expedition.pickUp( player, uid ) != ScavengerPickupResult::Ok )
                return -1;
            if ( walkTo( expedition, player, ship ) == false || expedition.depositToShip( player ) != 1 )
                return -1;
            return value;
        }
        return -1;
    }

    int32 countScavengerEvents( const vector<ScavengerEvent>& listEvent, ScavengerEvent::Kind kind )
    {
        int32 count = 0;
        for ( const ScavengerEvent& event : listEvent )
        {
            count += event._kind == kind ? 1 : 0;
        }
        return count;
    }

    void runScavenger( ScavengerExpedition& expedition, float32 seconds, vector<ScavengerEvent>& outListEvent )
    {
        for ( float32 time = 0.0f; time < seconds - 1.0e-4f; time += 0.5f )
        {
            expedition.update( 0.5f );
        }
        expedition.drainEvents( outListEvent );
    }
} // namespace

SW_TEST_CASE( CoopScavengerTest, QuotaFormulaBuyRateAndDeadline )
{
    ScavengerCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kScavengerCatalogXml, "CoopScavengerTest" ) );
    // 매입률 — 마감에 가까울수록 오르고, 사이는 선형.
    SW_EXPECT_NEAR_EQUAL( 0.4f, catalog.computeBuyRate( 2 ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.7f, catalog.computeBuyRate( 1 ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, catalog.computeBuyRate( 0 ), 0.0001f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, catalog.computeBuyRate( 9 ), 0.0001f );

    ScavengerQuota quota;
    quota.initialize( catalog.getQuotaSettings() );
    // 늘어남 = 100 × (1 + 주기² / 16) × (1 + 0.5 × (r − 0.5)) — 주기가 갈수록 가파르다.
    SW_EXPECT_EQUAL( 106, quota.computeIncrease( 0, 0.5f ) );
    SW_EXPECT_EQUAL( 125, quota.computeIncrease( 1, 0.5f ) );
    SW_EXPECT_EQUAL( 325, quota.computeIncrease( 5, 0.5f ) );
    SW_EXPECT_EQUAL( 133, quota.computeIncrease( 0, 1.0f ) );

    // 남은 날 2 → 1 → 0(마감 날) — 마감 날이 끝날 때 판정.
    GameRandom random{ 9u };
    int32      bonus = -1;
    SW_EXPECT_TRUE( quota.endDay( random, bonus ) == ScavengerDeadlineResult::NotDue );
    SW_EXPECT_TRUE( quota.endDay( random, bonus ) == ScavengerDeadlineResult::NotDue );
    SW_EXPECT_EQUAL( 0, quota.getDaysLeft() );
    quota.addFulfilled( 180 );
    SW_EXPECT_TRUE( quota.endDay( random, bonus ) == ScavengerDeadlineResult::QuotaMet );
    SW_EXPECT_EQUAL( 10, bonus ); // (180 − 130) / 5
    SW_EXPECT_EQUAL( 1, quota.getCycle() );
    SW_EXPECT_EQUAL( 2, quota.getDaysLeft() );
    SW_EXPECT_EQUAL( 0, quota.getFulfilled() );
    SW_EXPECT_TRUE( quota.getQuota() >= 130 + 79 && quota.getQuota() <= 130 + 133 );

    // 직렬화 — 받은 쪽이 같은 할당량을 본다.
    Archive writer;
    quota.writeState( writer );
    ScavengerQuota copy;
    Archive        reader( writer.getData(), writer.getSize() );
    SW_ASSERT_TRUE( copy.readState( reader ) );
    SW_EXPECT_EQUAL( quota.getQuota(), copy.getQuota() );
    SW_EXPECT_EQUAL( 1, copy.getCycle() );

    // 두 번째 주기는 모자라 게임 오버 — 그 뒤로는 더 판정하지 않는다.
    quota.addFulfilled( 50 );
    (void)quota.endDay( random, bonus );
    (void)quota.endDay( random, bonus );
    SW_EXPECT_TRUE( quota.endDay( random, bonus ) == ScavengerDeadlineResult::GameOver );
    SW_EXPECT_TRUE( quota.isGameOver() );
    quota.addFulfilled( 1000 );
    SW_EXPECT_EQUAL( 50, quota.getFulfilled() );
}

SW_TEST_CASE( CoopScavengerTest, FacilityGraphIsSeededWithLocksAndFireExits )
{
    ScavengerCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kScavengerCatalogXml, "CoopScavengerTest" ) );
    const ScavengerMoonDef* pMoon = catalog.findMoon( "experimentation" );
    SW_ASSERT_NOT_NULL( pMoon );

    // 잠긴 문이 있는 씨앗을 찾는다(문 9 개 이상 × 30 %).
    uint32            seed = 0;
    GameFlags         facilityFlags; // 시설은 문 잠금 플래그를 빌린다
    ScavengerFacility facility;
    facility.setFlags( &facilityFlags );
    for ( uint32 candidate = 1; candidate < 50 && seed == 0; ++candidate )
    {
        SW_ASSERT_TRUE( facility.createLayout( catalog, *pMoon, candidate, 1.0f ) );
        seed = facility.getLockedDoorCount() > 0 ? candidate : 0;
    }
    SW_ASSERT_TRUE( seed != 0 );
    SW_EXPECT_TRUE( facility.getRoomCount() >= 10 && facility.getRoomCount() <= 14 );
    SW_EXPECT_EQUAL( facility.getRoomCount() + 2, static_cast<int32>( facility.getGraph().getAreas().size() ) );
    const int32 scrapCount = static_cast<int32>( facility.getGroundScrap().size() );
    SW_EXPECT_TRUE( scrapCount >= 8 && scrapCount <= 12 );
    for ( const ScavengerScrap& scrap : facility.getGroundScrap() )
    {
        SW_EXPECT_TRUE( scrap._value >= 20 && scrap._value <= 120 );
        SW_EXPECT_TRUE( scrap._areaId != hashed_string( "outside" ) && scrap._areaId != hashed_string( "ship" ) );
        SW_EXPECT_TRUE( ( scrap._scrapId == hashed_string( "bell" ) ) == ( scrap._bTwoHanded == SW_TRUE ) );
    }
    int32           fireExitCount = 0;
    const AreaLink* pLocked       = nullptr;
    for ( const AreaLink& link : facility.getGraph().getLinks() )
    {
        fireExitCount += link._kind == hashed_string( "FireExit" ) ? 1 : 0;
        if ( pLocked == nullptr && link._requires.empty() == false )
            pLocked = &link;
    }
    SW_EXPECT_EQUAL( 2, fireExitCount );
    SW_ASSERT_NOT_NULL( pLocked );

    // 잠긴 문은 열기 전에는 지나지 못한다 — 열면 지난다.
    const hashed_string lockedFrom = pLocked->_from;
    const hashed_string lockedTo   = pLocked->_to;
    SW_EXPECT_FALSE( facility.canTraverse( lockedFrom, lockedTo ) );
    SW_EXPECT_TRUE( facility.unlockDoor( lockedTo ) );
    SW_EXPECT_FALSE( facility.unlockDoor( lockedTo ) );
    SW_EXPECT_TRUE( facility.canTraverse( lockedFrom, lockedTo ) );
    // 문을 다 열면 모든 방이 바깥(정문 · 화재 출구)에서 닿는다.
    for ( const AreaLink& link : facility.getGraph().getLinks() )
    {
        if ( link._requires.empty() == false )
            (void)facility.unlockDoor( link._to );
    }
    vector<hashed_string> listPath;
    for ( const AreaDef& area : facility.getGraph().getAreas() )
    {
        SW_EXPECT_TRUE( facility.getGraph().findPath( "ship", area._id, facility.getFlags(), listPath ) );
    }

    // 같은 씨앗 · 같은 위성이면 같은 지도와 고철.
    ScavengerFacility again;
    SW_ASSERT_TRUE( again.createLayout( catalog, *pMoon, seed, 1.0f ) );
    SW_ASSERT_TRUE( again.getGroundScrap().size() == facility.getGroundScrap().size() );
    SW_EXPECT_EQUAL( facility.getRoomCount(), again.getRoomCount() );
    SW_EXPECT_TRUE( again.getGraph().getLinks().size() == facility.getGraph().getLinks().size() );
    for ( size_t index = 0; index < again.getGroundScrap().size(); ++index )
    {
        SW_EXPECT_TRUE( again.getGroundScrap()[index]._areaId == facility.getGroundScrap()[index]._areaId );
        SW_EXPECT_EQUAL( facility.getGroundScrap()[index]._value, again.getGroundScrap()[index]._value );
    }

    // 위성마다 고철 목록 · 가치 배율이 다르다. 회사에는 시설이 없다.
    ScavengerFacility titan;
    SW_ASSERT_TRUE( titan.createLayout( catalog, *catalog.findMoon( "titan" ), seed, 1.0f ) );
    SW_EXPECT_TRUE( titan.getGroundScrap().size() >= 20 );
    for ( const ScavengerScrap& scrap : titan.getGroundScrap() )
    {
        SW_EXPECT_TRUE( scrap._scrapId != hashed_string( "bolt" ) );
        SW_EXPECT_TRUE( scrap._value >= 72 && scrap._value <= 180 );
    }
    ScavengerFacility company;
    SW_ASSERT_TRUE( company.createLayout( catalog, *catalog.findMoon( "company" ), seed, 1.0f ) );
    SW_EXPECT_EQUAL( 0, company.getRoomCount() );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( company.getGraph().getAreas().size() ) );
    SW_EXPECT_TRUE( company.getGroundScrap().empty() );
}

SW_TEST_CASE( CoopScavengerTest, CarrySlotsTwoHandedAndWeightSpeed )
{
    ScavengerCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kScavengerCatalogXml, "CoopScavengerTest" ) );
    ScavengerCarry carry;
    carry.initialize( catalog.getCarrySettings() );
    ScavengerScrap bolt;
    bolt._weight = 5.0f;
    bolt._value  = 30;
    for ( int32 index = 0; index < 4; ++index )
    {
        bolt._uid = index + 1;
        SW_EXPECT_TRUE( carry.add( bolt ) == ScavengerPickupResult::Ok );
    }
    bolt._uid = 5;
    SW_EXPECT_TRUE( carry.add( bolt ) == ScavengerPickupResult::SlotsFull );
    SW_EXPECT_EQUAL( 4, carry.getCount() );
    SW_EXPECT_EQUAL( 120, carry.computeValue() );
    SW_EXPECT_NEAR_EQUAL( 0.8f, carry.computeSpeedScale(), 0.0001f ); // 1 − 20 × 0.01

    // 양손 아이템을 들면 빈 칸이 있어도 줍지 못한다.
    ScavengerScrap removed;
    SW_EXPECT_TRUE( carry.tryRemove( 2, removed ) );
    SW_EXPECT_FALSE( carry.tryRemove( 2, removed ) );
    SW_EXPECT_TRUE( carry.tryRemove( 3, removed ) );
    ScavengerScrap bell;
    bell._uid        = 10;
    bell._weight     = 30.0f;
    bell._bTwoHanded = SW_TRUE;
    SW_EXPECT_TRUE( carry.add( bell ) == ScavengerPickupResult::Ok );
    bolt._uid = 11;
    SW_EXPECT_TRUE( carry.add( bolt ) == ScavengerPickupResult::HandsFull );
    SW_EXPECT_NEAR_EQUAL( 0.6f, carry.computeSpeedScale(), 0.0001f );
    // 아주 무거우면 최소 속도에서 멈춘다.
    ScavengerScrap anvil;
    anvil._uid    = 12;
    anvil._weight = 500.0f;
    SW_EXPECT_TRUE( carry.tryRemove( 10, removed ) );
    SW_EXPECT_TRUE( carry.add( anvil ) == ScavengerPickupResult::Ok );
    SW_EXPECT_NEAR_EQUAL( 0.4f, carry.computeSpeedScale(), 0.0001f );
    vector<ScavengerScrap> listDropped;
    carry.takeAll( listDropped );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listDropped.size() ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, carry.computeSpeedScale(), 0.0001f );
}

SW_TEST_CASE( CoopScavengerTest, DayFlowDuskMidnightDepartureAndLeftBehind )
{
    ScavengerTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    ScavengerExpedition expedition;
    expedition.initialize( data.makeData(), data.makeRefs(), data.openShipStorage(), 3u, 2 );
    SW_EXPECT_EQUAL( 100, static_cast<int32>( expedition.getCredits() ) );
    SW_EXPECT_TRUE( expedition.movePlayer( 0, "outside" ) == ScavengerActionResult::WrongPhase );
    SW_EXPECT_TRUE( expedition.routeTo( "nowhere" ) == ScavengerActionResult::UnknownMoon );
    SW_EXPECT_TRUE( expedition.routeTo( "experimentation" ) == ScavengerActionResult::Ok );
    SW_ASSERT_TRUE( expedition.land() == ScavengerActionResult::Ok );
    SW_EXPECT_TRUE( expedition.getPhase() == ScavengerPhase::Landed );
    SW_EXPECT_NEAR_EQUAL( 8.0f, data._clock.getHour(), 0.01f );
    SW_EXPECT_TRUE( data._weather.getCurrent() == hashed_string( "clear" ) );
    SW_EXPECT_TRUE( expedition.routeTo( "titan" ) == ScavengerActionResult::WrongPhase );

    // 0 은 고철 하나를 우주선에 싣고, 1 은 바깥에 남는다.
    const int32 value = fetchOneScrap( expedition, 0 );
    SW_ASSERT_TRUE( value > 0 );
    SW_EXPECT_TRUE( expedition.movePlayer( 1, "entrance" ) == ScavengerActionResult::Blocked ); // 우주선 → 정문은 바로 이어지지 않는다
    SW_EXPECT_TRUE( expedition.movePlayer( 1, "outside" ) == ScavengerActionResult::Ok );

    // 해 질 녘(18 시)은 10 시간 = 100 초 뒤, 자정은 160 초 뒤.
    vector<ScavengerEvent> listEvent;
    runScavenger( expedition, 99.0f, listEvent );
    SW_EXPECT_EQUAL( 0, countScavengerEvents( listEvent, ScavengerEvent::Kind::Dusk ) );
    runScavenger( expedition, 2.0f, listEvent );
    SW_EXPECT_EQUAL( 1, countScavengerEvents( listEvent, ScavengerEvent::Kind::Dusk ) );
    SW_EXPECT_TRUE( expedition.getPhase() == ScavengerPhase::Landed );
    runScavenger( expedition, 60.0f, listEvent );
    SW_EXPECT_TRUE( expedition.getPhase() == ScavengerPhase::InOrbit );

    // 자동 이륙 — 바깥의 1 은 남겨져 죽고 시신도 없다 → 벌금 20 %.
    bool bLeftBehind = false;
    bool bAutoDepart = false;
    for ( const ScavengerEvent& event : listEvent )
    {
        bLeftBehind = bLeftBehind || ( event._kind == ScavengerEvent::Kind::PlayerDied && event._player == 1 && event._value == 1 );
        bAutoDepart = bAutoDepart || ( event._kind == ScavengerEvent::Kind::ShipDeparted && event._value == 1 );
    }
    SW_EXPECT_TRUE( bLeftBehind );
    SW_EXPECT_TRUE( bAutoDepart );
    SW_EXPECT_EQUAL( 80, static_cast<int32>( expedition.getCredits() ) );
    SW_EXPECT_EQUAL( 1, countScavengerEvents( listEvent, ScavengerEvent::Kind::FinePaid ) );
    SW_ASSERT_TRUE( expedition.getShipScrap().size() == 1 );
    SW_EXPECT_EQUAL( value, expedition.computeShipValue() );
    // 하루가 지났고 모두 다시 우주선에 산 채로.
    SW_EXPECT_EQUAL( 1, expedition.getQuota().getDaysLeft() );
    SW_EXPECT_EQUAL( 1, expedition.getDayIndex() );
    SW_EXPECT_EQUAL( 2, expedition.countAlive() );
    SW_EXPECT_TRUE( expedition.findCrewMember( 1 )->_areaId == hashed_string( "ship" ) );

    // 이동 비용 — 모자라면 가지 못한다.
    SW_EXPECT_TRUE( expedition.routeTo( "titan" ) == ScavengerActionResult::Ok );
    SW_EXPECT_EQUAL( 30, static_cast<int32>( expedition.getCredits() ) );
    SW_EXPECT_TRUE( expedition.routeTo( "experimentation" ) == ScavengerActionResult::Ok );
    SW_EXPECT_TRUE( expedition.routeTo( "titan" ) == ScavengerActionResult::NotEnoughCredits );
}

SW_TEST_CASE( CoopScavengerTest, BodyRecoveryFinesAndWipeLoss )
{
    ScavengerTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    const auto playTwoDays = [&]( ScavengerExpedition& expedition, vector<ScavengerEvent>& outListEvent ) -> bool
    {
        expedition.initialize( data.makeData(), data.makeRefs(), data.openShipStorage(), 21u, 2 );
        if ( expedition.land() != ScavengerActionResult::Ok )
            return false;
        // 1 이 고철 셋을 싣는다.
        for ( int32 index = 0; index < 3; ++index )
        {
            if ( fetchOneScrap( expedition, 1 ) <= 0 )
                return false;
        }
        // 0 이 정문에서 죽는다 — 시신이 남는다(양손).
        if ( expedition.movePlayer( 0, "outside" ) != ScavengerActionResult::Ok || expedition.movePlayer( 0, "entrance" ) != ScavengerActionResult::Ok )
            return false;
        SW_EXPECT_NEAR_EQUAL( 40.0f, expedition.applyDamage( 0, 40.0f ), 0.001f );
        SW_EXPECT_NEAR_EQUAL( 60.0f, expedition.applyDamage( 0, 90.0f ), 0.001f );
        SW_EXPECT_TRUE( expedition.findCrewMember( 0 )->isDead() );
        int32 bodyUid = -1;
        for ( const ScavengerScrap& scrap : expedition.getFacility().getGroundScrap() )
        {
            bodyUid = scrap._bodyOf == 0 ? scrap._uid : bodyUid;
        }
        SW_EXPECT_TRUE( bodyUid > 0 );
        SW_EXPECT_TRUE( expedition.getPhase() == ScavengerPhase::Landed ); // 1 이 살아 있다
        // 1 이 시신을 메고 돌아온다 — 메는 동안 다른 것은 줍지 못한다.
        if ( walkTo( expedition, 1, "entrance" ) == false || expedition.pickUp( 1, bodyUid ) != ScavengerPickupResult::Ok )
            return false;
        SW_EXPECT_NEAR_EQUAL( 0.4f, expedition.findCrewMember( 1 )->_carry.computeSpeedScale(), 0.0001f );
        for ( const ScavengerScrap& scrap : expedition.getFacility().getGroundScrap() )
        {
            if ( scrap._areaId == hashed_string( "entrance" ) && scrap.isBody() == false )
                SW_EXPECT_TRUE( expedition.pickUp( 1, scrap._uid ) == ScavengerPickupResult::HandsFull );
        }
        if ( walkTo( expedition, 1, "ship" ) == false || expedition.depositToShip( 1 ) != 1 )
            return false;
        SW_EXPECT_TRUE( expedition.findCrewMember( 0 )->_bBodyRecovered == SW_TRUE );
        if ( expedition.takeOff() != ScavengerActionResult::Ok )
            return false;
        // 시신을 가져왔으니 5 % 만.
        SW_EXPECT_EQUAL( 95, static_cast<int32>( expedition.getCredits() ) );
        SW_EXPECT_EQUAL( 3, static_cast<int32>( expedition.getShipScrap().size() ) );

        // 이튿날 — 둘 다 죽는다: 자동 이륙, 고철 절반(올림)을 잃고 벌금 20 % × 2.
        if ( expedition.land() != ScavengerActionResult::Ok || expedition.movePlayer( 0, "outside" ) != ScavengerActionResult::Ok ||
             expedition.movePlayer( 1, "outside" ) != ScavengerActionResult::Ok )
            return false;
        expedition.killPlayer( 0 );
        SW_EXPECT_TRUE( expedition.getPhase() == ScavengerPhase::Landed );
        expedition.killPlayer( 1 );
        expedition.drainEvents( outListEvent );
        return true;
    };

    ScavengerExpedition    first;
    vector<ScavengerEvent> listEvent;
    SW_ASSERT_TRUE( playTwoDays( first, listEvent ) );
    SW_EXPECT_TRUE( first.getPhase() == ScavengerPhase::InOrbit );
    SW_EXPECT_EQUAL( 1, countScavengerEvents( listEvent, ScavengerEvent::Kind::CrewWiped ) );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( first.getShipScrap().size() ) );
    SW_EXPECT_EQUAL( 57, static_cast<int32>( first.getCredits() ) ); // 95 − floor( 95 × 0.4 )
    SW_EXPECT_EQUAL( 2, first.countAlive() );                        // 다음 날을 위해 되살아났다

    // 같은 씨앗이면 같은 고철이 남는다.
    ScavengerExpedition    second;
    vector<ScavengerEvent> listSecond;
    SW_ASSERT_TRUE( playTwoDays( second, listSecond ) );
    SW_ASSERT_TRUE( second.getShipScrap().size() == 1 );
    SW_EXPECT_EQUAL( first.getShipScrap()[0]._uid, second.getShipScrap()[0]._uid );
}

SW_TEST_CASE( CoopScavengerTest, ThreatsScaleWithMoonRiskAndWeather )
{
    ScavengerTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    struct ThreatCount
    {
        int32 _indoor{ 0 };
        int32 _outdoor{ 0 };
        bool  _bTagsRespected{ true };
    };
    const auto countThreats = [&]( const utf8* pMoonId ) -> ThreatCount
    {
        ScavengerExpedition expedition;
        expedition.initialize( data.makeData(), data.makeRefs(), data.openShipStorage(), 5u, 1 );
        ThreatCount count;
        if ( expedition.routeTo( hashed_string( pMoonId ) ) != ScavengerActionResult::Ok || expedition.land() != ScavengerActionResult::Ok )
            return count;
        vector<ScavengerEvent> listEvent;
        runScavenger( expedition, 60.0f, listEvent );
        for ( const ScavengerEvent& event : listEvent )
        {
            if ( event._kind != ScavengerEvent::Kind::ThreatSpawned )
                continue;
            const bool bDog = event._id == hashed_string( "dog" );
            count._indoor += event._bIndoor == SW_TRUE ? 1 : 0;
            count._outdoor += event._bIndoor == SW_TRUE ? 0 : 1;
            count._bTagsRespected = count._bTagsRespected && bDog == ( event._bIndoor == SW_FALSE );
        }
        return count;
    };
    const ThreatCount calm    = countThreats( "experimentation" );
    const ThreatCount risky   = countThreats( "titan" );
    const ThreatCount eclipse = countThreats( "rend" );
    const ThreatCount company = countThreats( "company" );
    SW_EXPECT_TRUE( calm._indoor > 0 && calm._outdoor > 0 );
    SW_EXPECT_TRUE( calm._bTagsRespected && risky._bTagsRespected && eclipse._bTagsRespected );
    // 위험도 3 · 일식(위협 2) 위성은 같은 시간에 더 많이 나온다.
    SW_EXPECT_TRUE( risky._indoor + risky._outdoor >= 2 * ( calm._indoor + calm._outdoor ) );
    SW_EXPECT_TRUE( eclipse._indoor + eclipse._outdoor > calm._indoor + calm._outdoor );
    SW_EXPECT_EQUAL( 0, company._indoor + company._outdoor );

    ScavengerExpedition expedition;
    expedition.initialize( data.makeData(), data.makeRefs(), data.openShipStorage(), 5u, 1 );
    SW_EXPECT_TRUE( expedition.routeTo( "rend" ) == ScavengerActionResult::Ok );
    SW_ASSERT_TRUE( expedition.land() == ScavengerActionResult::Ok );
    SW_EXPECT_TRUE( data._weather.getCurrent() == hashed_string( "eclipsed" ) );
    SW_EXPECT_NEAR_EQUAL( 2.0f, expedition.computeThreatScale(), 0.0001f );
}

SW_TEST_CASE( CoopScavengerTest, CompanySellingTerminalAndGameOver )
{
    ScavengerTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    ScavengerExpedition expedition;
    expedition.initialize( data.makeData(), data.makeRefs(), data.openShipStorage(), 8u, 1 );

    // 터미널 — 궤도에서 산다(우주선 창고로).
    SW_EXPECT_TRUE( expedition.buyFromTerminal( "flashlight", 2 ) == ShopResult::Ok );
    SW_EXPECT_EQUAL( 70, static_cast<int32>( expedition.getCredits() ) );
    SW_EXPECT_EQUAL( 2, expedition.getShipInventory().getItemCount( "flashlight" ) );
    SW_EXPECT_TRUE( expedition.buyFromTerminal( "shovel", 3 ) == ShopResult::NotEnoughMoney );

    SW_ASSERT_TRUE( expedition.land() == ScavengerActionResult::Ok );
    const int32 value = fetchOneScrap( expedition, 0 );
    SW_ASSERT_TRUE( value > 0 && value <= 120 );
    ScavengerActionResult result = ScavengerActionResult::Ok;
    SW_EXPECT_EQUAL( 0, expedition.sellAllShipScrap( result ) );
    SW_EXPECT_TRUE( result == ScavengerActionResult::NotCompany );
    SW_EXPECT_TRUE( expedition.takeOff() == ScavengerActionResult::Ok );
    SW_EXPECT_EQUAL( 1, expedition.getQuota().getDaysLeft() );

    // 회사 — 남은 날 1 이면 매입률 0.7.
    SW_EXPECT_TRUE( expedition.routeTo( "company" ) == ScavengerActionResult::Ok );
    SW_ASSERT_TRUE( expedition.land() == ScavengerActionResult::Ok );
    SW_EXPECT_NEAR_EQUAL( static_cast<float32>( value ) * 0.7f, static_cast<float32>( expedition.computeSellValue() ), 1.0f );
    const int32 expectedSale = expedition.computeSellValue();
    const int32 sold         = expedition.sellAllShipScrap( result );
    SW_EXPECT_TRUE( result == ScavengerActionResult::Ok );
    SW_EXPECT_EQUAL( expectedSale, sold );
    SW_EXPECT_EQUAL( 70 + sold, static_cast<int32>( expedition.getCredits() ) );
    SW_EXPECT_EQUAL( sold, expedition.getQuota().getFulfilled() );
    SW_EXPECT_TRUE( expedition.getShipScrap().empty() );
    SW_EXPECT_TRUE( expedition.takeOff() == ScavengerActionResult::Ok );
    SW_EXPECT_EQUAL( 0, expedition.getQuota().getDaysLeft() );

    // 마감 날 — 고철 하나로는 130 을 못 채운다: 게임 오버, 더 내릴 수 없다.
    SW_ASSERT_TRUE( expedition.land() == ScavengerActionResult::Ok );
    vector<ScavengerEvent> listEvent;
    SW_EXPECT_TRUE( expedition.takeOff() == ScavengerActionResult::Ok );
    expedition.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countScavengerEvents( listEvent, ScavengerEvent::Kind::GameOver ) );
    SW_EXPECT_TRUE( expedition.getPhase() == ScavengerPhase::GameOver );
    SW_EXPECT_TRUE( expedition.land() == ScavengerActionResult::WrongPhase );
    SW_EXPECT_TRUE( expedition.buyFromTerminal( "flashlight", 1 ) == ShopResult::UnknownShop );
}

/**
 * @brief [CoopScavengerTest] 상태 바이트로 되살린 원정이 같은 원정을 잇는다 — 사람 · 우주선 고철 · 시설(바닥 · 잠금 해제 · 다시 지은 그래프) · 위협 감독 · 할당량이
 *        같은 바이트이고, 같은 걸음을 둘 다 더 돌려도 같은 바이트다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( CoopScavengerTest, StateRoundTripContinuesTheSameExpedition )
{
    ScavengerTestData data;
    SW_ASSERT_TRUE( data.initialize() );
    ScavengerExpedition expedition;
    expedition.initialize( data.makeData(), data.makeRefs(), data.openShipStorage(), 3u, 2 );
    SW_ASSERT_TRUE( expedition.land() == ScavengerActionResult::Ok );
    SW_ASSERT_TRUE( fetchOneScrap( expedition, 0 ) > 0 );
    SW_EXPECT_TRUE( expedition.movePlayer( 1, "outside" ) == ScavengerActionResult::Ok );
    SW_EXPECT_TRUE( expedition.movePlayer( 1, "entrance" ) == ScavengerActionResult::Ok );
    // 잠긴 문이 있으면 하나를 연다 — 빌린 플래그에 둔 것을 시설이 기억한다.
    for ( const AreaLink& link : expedition.getFacility().getGraph().getLinks() )
    {
        if ( link._requires.empty() == false )
        {
            SW_EXPECT_TRUE( expedition.unlockDoor( link._to ) );
            break;
        }
    }
    vector<ScavengerEvent> listEvent;
    runScavenger( expedition, 20.0f, listEvent );

    Archive written;
    expedition.writeState( written );
    ScavengerExpedition restored;
    restored.initialize( data.makeData(), data.borrowRefs(), data._shipStorage, 77u, 2 );
    Archive reader( written.getData(), written.getSize() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, reader.getRemainingBytes() );
    Archive rewritten;
    restored.writeState( rewritten );
    vector<uint8> originalBytes;
    vector<uint8> restoredBytes;
    written.writeData( originalBytes );
    rewritten.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );
    SW_EXPECT_TRUE( restored.getPhase() == ScavengerPhase::Landed );
    SW_EXPECT_EQUAL( expedition.computeShipValue(), restored.computeShipValue() );
    SW_EXPECT_EQUAL( expedition.getFacility().computeGroundValue(), restored.getFacility().computeGroundValue() );
    SW_EXPECT_EQUAL( static_cast<int32>( expedition.getFacility().getGraph().getLinks().size() ),
                     static_cast<int32>( restored.getFacility().getGraph().getLinks().size() ) );
    SW_EXPECT_NEAR_EQUAL( expedition.getHoursOnMoon(), restored.getHoursOnMoon(), 0.0001f );

    // 같은 걸음을 둘 다 — 1 이 바깥으로 나가고(다시 지은 그래프로 걷는다) 시간이 흐른다. 위협이 같은 때에 같은 것으로 나온다.
    SW_EXPECT_TRUE( expedition.movePlayer( 1, "outside" ) == ScavengerActionResult::Ok );
    SW_EXPECT_TRUE( restored.movePlayer( 1, "outside" ) == ScavengerActionResult::Ok );
    vector<ScavengerEvent> listOriginal;
    vector<ScavengerEvent> listRestored;
    runScavenger( expedition, 30.0f, listOriginal );
    runScavenger( restored, 30.0f, listRestored );
    SW_EXPECT_EQUAL( static_cast<int32>( listOriginal.size() ), static_cast<int32>( listRestored.size() ) );
    Archive afterOriginal;
    Archive afterRestored;
    expedition.writeState( afterOriginal );
    restored.writeState( afterRestored );
    afterOriginal.writeData( originalBytes );
    afterRestored.writeData( restoredBytes );
    SW_EXPECT_TRUE( originalBytes == restoredBytes );

    ScavengerExpedition truncated;
    truncated.initialize( data.makeData(), data.borrowRefs(), data._shipStorage, 77u, 2 );
    Archive cut( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( truncated.readState( cut ) );
    SW_EXPECT_TRUE( truncated.getPhase() == ScavengerPhase::InOrbit );
    SW_EXPECT_TRUE( truncated.getShipScrap().empty() );
}
