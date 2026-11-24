#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/ScavengerCarry.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/ScavengerCatalog.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/ScavengerExpedition.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/ScavengerFacility.h"
#include "GameFramework/Kits/Genre/Horror/CoopScavenger/ScavengerQuota.h"
#include "GameFramework/Runtime/Character/AI/SpawnDirector.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

#include <limits>

// 협동 수집 공포 키트의 규칙: 할당량 주기, 운반 슬롯, 시설 생성과 잠긴 문, 한 판(위성 이동 · 하루 · 죽음과 시신 · 벌금 · 전멸 손실 · 매입 · 마감), 카탈로그 오류, 상태 바이트의 범위 검사.
// 상태 바이트는 형식을 맞춰 손으로 쓴 뒤(정상 값이 읽히는 것을 함께 본다) 범위 밖 값만 바꿔 넣는다. 위협 감독과 터미널 상점은 기반 쪽이라 여기서 보지 않는다(줄이 없는 데이터를 준다). 키트 GF_CoopScavenger(nogpu).

namespace
{
    constexpr const utf8* kScavengerXML = R"(<ScavengerCatalog terminalShop="terminal" currency="Credits" crewHealth="100">
  <Quota start="20" days="1" increase="50" steepness="16" randomness="0" overtime="5" credits="60"/>
  <BuyRate daysLeft="2" rate="0.5"/>
  <BuyRate daysLeft="0" rate="1"/>
  <Day secondsPerDay="240" arrival="6" dusk="18" depart="24"/>
  <Carry slots="2" speedPerWeight="0.01" minSpeed="0.4" bodyWeight="60"/>
  <Penalty deathFine="0.2" recoveredFine="0.05" allDeadLoss="0.5" allDeadKeep="0"/>
  <Facility rooms="3" roomsMax="3" lockedChance="0" loopChance="0" fireExits="1"/>
  <Scrap id="bolt" min="10" max="10" weight="5"/>
  <Scrap id="bell" min="40" max="40" weight="20" spawnWeight="0" twoHanded="true"/>
  <Moon id="rust" name="Rust" cost="0" scrap="3" scrapMax="3"/>
  <Moon id="far" name="Far" cost="50" scrap="1" scrapMax="1"/>
  <Moon id="dear" name="Dear" cost="1000"/>
  <Moon id="company" name="Company" company="true"/>
</ScavengerCatalog>)";

    constexpr const utf8* kScavengerLockedXML = R"(<ScavengerCatalog>
  <Facility rooms="3" roomsMax="3" lockedChance="1" loopChance="0" fireExits="0"/>
  <Scrap id="bolt" min="10" max="10"/>
  <Moon id="rust" name="Rust" scrap="0" scrapMax="0"/>
</ScavengerCatalog>)";

    constexpr const utf8* kScavengerKeepXML = R"(<ScavengerCatalog>
  <Quota start="20" days="5" credits="0"/>
  <Penalty allDeadLoss="1" allDeadKeep="2"/>
  <Facility rooms="1" roomsMax="1" lockedChance="0" loopChance="0" fireExits="0"/>
  <Scrap id="bolt" min="10" max="10"/>
  <Moon id="rust" name="Rust" scrap="3" scrapMax="3"/>
</ScavengerCatalog>)";

    constexpr float32 kScavengerTolerance = 1.0e-3f;

    /** @brief 손으로 짠 상태 바이트의 조각입니다. 정상 경로로 만들 수 없는 판을 읽기에 넣을 때 씁니다. */
    struct ScavengerStateBytes
    {
        /** @brief @p TState 의 정상 `writeState` 바이트에서 리플렉션 머리를 가져와 씁니다 — 손으로 짜면 머리가 달라 다른 이유로 거절되어 거짓 통과한다. */
        template <typename TState>
        static void writeHeader( sw::Archive& outArchive )
        {
            writeHeader( outArchive, TState{} );
        }
        /** @brief 초기화된 @p state 의 바이트에서 머리를 가져옵니다(빈 객체를 쓸 수 없는 형식). */
        template <typename TState>
        static void writeHeader( sw::Archive& outArchive, const TState& state )
        {
            sw::Archive stateBytes;
            state.writeState( stateBytes );
            outArchive.writeBytes( stateBytes.getData(), sw::StateIO::kReflectedHeaderBytes );
        }

        /** @brief 고철 목록입니다(리플렉션 칸 `ScavengerScrap` 의 선언 순서: 이름 둘, 무게, 번호, 가치, 시신, 날, 양손). */
        static void writeScraps( sw::Archive& outArchive, const sw::vector<sw::ScavengerScrap>& listScrap )
        {
            outArchive << static_cast<uint32>( listScrap.size() );
            for ( const sw::ScavengerScrap& scrap : listScrap )
            {
                sw::StateIO::writeName( outArchive, scrap._scrapID );
                sw::StateIO::writeName( outArchive, scrap._areaID );
                outArchive << scrap._weight;
                outArchive << scrap._uid;
                outArchive << scrap._value;
                outArchive << scrap._bodyOf;
                outArchive << scrap._day;
                outArchive << scrap._bTwoHanded;
            }
        }
    };

    /** @brief 카탈로그를 읽어 둔 판입니다. */
    struct ScavengerFacilityTest
    {
        sw::ScavengerCatalog  _catalog{};
        sw::ScavengerFacility _facility{};

        ScavengerFacilityTest() { SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kScavengerXML, "ScavengerTest" ) ); }

        /** @brief 바닥 고철과 번호를 손으로 쓴 상태를 읽습니다. */
        bool readFixture( sw::ScavengerFacility& facility, uint32 seed, const sw::ScavengerScrap& scrap, int32 nextUid )
        {
            sw::Archive written;
            ScavengerStateBytes::writeHeader<sw::ScavengerFacility>( written );
            ScavengerStateBytes::writeScraps( written, { scrap } );
            written << static_cast<uint32>( 0 ); // 잠금 해제 목록
            sw::StateIO::writeName( written, "rust" );
            written << seed;
            written << nextUid;
            written << static_cast<int32>( 3 );
            written << static_cast<int32>( 0 );
            sw::Archive reading( written.getData(), written.getSize() );
            return facility.readState( reading );
        }
    };

    /** @brief 날씨 · 위협 · 상점 줄이 없는 데이터입니다(기능을 데이터로 끈다). */
    struct ScavengerBareData
    {
        sw::WeatherCatalog _weather{};
        sw::SpawnTable     _threats{};
        sw::ShopCatalog    _shops{};
        sw::ItemCatalog    _items{};

        sw::ScavengerExpeditionData make( const sw::ScavengerCatalog& catalog ) const { return sw::ScavengerExpeditionData{ catalog, _weather, _threats, _shops, _items }; }
    };

    /** @brief 두 사람의 한 판입니다. 지갑 · 플래그 · 시계 · 날씨는 빌려 줍니다. */
    struct ScavengerExpeditionTest
    {
        ScavengerBareData              _bare{};
        sw::ScavengerCatalog           _catalog{};
        sw::Wallet                     _wallet{};
        sw::WorldClock                 _clock{};
        sw::WeatherSystem              _weather{};
        sw::GameFlags                  _flags{};
        sw::Inventory                  _storage{};
        sw::ScavengerExpedition        _expedition{};
        sw::vector<sw::ScavengerEvent> _listEvent{};

        ScavengerExpeditionTest()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kScavengerXML, "ScavengerTest" ) );
            SW_REQUIRE_TRUE( open( _expedition, _catalog, 11 ) );
        }

        bool open( sw::ScavengerExpedition& expedition, const sw::ScavengerCatalog& catalog, uint32 seed )
        {
            return _clock.initialize( sw::WorldClockSettings{} ) && expedition.initialize( _bare.make( catalog ), _wallet, _clock, _weather, _flags, _storage, seed, 2 );
        }

        /** @brief 쌓인 알림을 `_listEvent` 뒤로 모으고 @p kind 의 수를 셉니다. */
        int32 countEvents( sw::ScavengerEvent::Kind kind )
        {
            _expedition.pollEvents( _listEvent );
            return sw::VectorUtil::countBy<&sw::ScavengerEvent::_kind>( _listEvent, kind );
        }

        /** @brief 이어진 길을 찾아 그곳까지 한 칸씩 걷습니다. 길이 없으면 false 입니다(시설은 방 셋이라 전수로 찾는다). */
        bool walk( sw::ScavengerExpedition& expedition, int32 player, const sw::hashed_string& target )
        {
            const sw::hashed_string       arrArea[] = { "ship", "outside", "entrance", "room1", "room2" };
            sw::vector<sw::hashed_string> listVisited{ expedition.findCrewMember( player )->_areaID };
            sw::vector<int32>             listParent{ -1 };
            for ( size_t index = 0; index < listVisited.size(); ++index )
            {
                if ( listVisited[index] == target )
                {
                    sw::vector<sw::hashed_string> listPath;
                    for ( int32 step = static_cast<int32>( index ); step > 0; step = listParent[static_cast<size_t>( step )] )
                    {
                        listPath.insert( listPath.begin(), listVisited[static_cast<size_t>( step )] );
                    }
                    for ( const sw::hashed_string& area : listPath )
                    {
                        if ( expedition.movePlayer( player, area ) != sw::ScavengerActionResult::Ok )
                            return false;
                    }
                    return true;
                }
                for ( const sw::hashed_string& area : arrArea )
                {
                    bool bSeen = false;
                    for ( const sw::hashed_string& visited : listVisited )
                    {
                        bSeen = bSeen || visited == area;
                    }
                    if ( bSeen == false && expedition.getFacility().canTraverse( listVisited[index], area ) )
                    {
                        listVisited.push_back( area );
                        listParent.push_back( static_cast<int32>( index ) );
                    }
                }
            }
            return false;
        }

        /** @brief @p player 가 바닥 고철을 모두 우주선으로 나릅니다. */
        bool haulEverything( sw::ScavengerExpedition& expedition, int32 player )
        {
            while ( expedition.getFacility().getGroundScrap().empty() == false )
            {
                const sw::ScavengerScrap scrap = expedition.getFacility().getGroundScrap().front();
                if ( walk( expedition, player, scrap._areaID ) == false )
                    return false;
                if ( expedition.pickUp( player, scrap._uid ) != sw::ScavengerPickupResult::Ok )
                    return false;
                if ( walk( expedition, player, "ship" ) == false || expedition.depositToShip( player ) != 1 )
                    return false;
            }
            return true;
        }

        /** @brief 궤도 상태의 상태 바이트를 손으로 씁니다. 사람 0 이 @p carried 를 듭니다. */
        bool readFixture( const sw::ScavengerScrap& carried, float32 hoursOnMoon )
        {
            sw::ScavengerCarry carry;
            carry.initialize( _catalog.getCarrySettings() );
            (void)carry.add( carried );
            sw::GameRandom random;
            random.setSeed( 3 );
            sw::SpawnDirector director;
            director.initialize( nullptr, 1u );
            sw::ShopManager shop;
            shop.initialize( nullptr, nullptr );

            sw::Archive written;
            ScavengerStateBytes::writeHeader( written, _expedition );
            ScavengerStateBytes::writeScraps( written, {} ); // 우주선 고철
            written << static_cast<uint32>( 11 );            // 시드
            written << hoursOnMoon;
            written << static_cast<int64>( sw::ScavengerPhase::InOrbit ); // 리플렉션 열거는 int64 로 놓인다
            written << static_cast<uint8>( SW_FALSE );                    // 해 질 녘 알림
            written << static_cast<uint32>( 2 );
            for ( int32 player = 0; player < 2; ++player )
            {
                _expedition.findCrewMember( player )->_healthMachine.writeState( written );
                ( player == 0 ? carry : _expedition.findCrewMember( player )->_carry ).writeState( written );
                sw::StateIO::writeName( written, "ship" );
                written << static_cast<uint8>( SW_FALSE );
            }
            sw::StateIO::writeName( written, "rust" );
            sw::StateIO::writeRandom( written, random );
            _expedition.getQuota().writeState( written );
            _expedition.getFacility().writeState( written );
            director.writeState( written );
            director.writeState( written );
            shop.writeState( written );
            sw::Archive reading( written.getData(), written.getSize() );
            return _expedition.readState( reading );
        }
    };

    /** @brief 고철 하나를 만듭니다. */
    struct ScavengerTestUtil
    {
        static sw::ScavengerScrap makeScrap( int32 uid, int32 value, float32 weight, bool bTwoHanded )
        {
            sw::ScavengerScrap scrap;
            scrap._scrapID    = "bolt";
            scrap._uid        = uid;
            scrap._value      = value;
            scrap._weight     = weight;
            scrap._bTwoHanded = bTwoHanded ? SW_TRUE : SW_FALSE;
            return scrap;
        }

        static sw::ScavengerQuotaDef makeQuota( int32 startQuota, int32 days, float32 overtimeDivisor )
        {
            sw::ScavengerQuotaDef settings;
            settings._startQuota      = startQuota;
            settings._daysPerCycle    = days;
            settings._overtimeDivisor = overtimeDivisor;
            settings._increase        = 100.0f;
            settings._steepness       = 16.0f;
            settings._randomness      = 0.5f;
            return settings;
        }
    };
} // namespace

/**
 * @brief [ScavengerQuotaTest] 늘어나는 양은 주기의 제곱으로 가파르고 무작위 폭을 곱해 반올림한다. 무작위가 커도 음수가 되지 않는다
 */
SW_TEST_CASE( ScavengerQuotaTest, IncreaseFollowsCycleAndNoise )
{
    sw::ScavengerQuota    quota;
    sw::ScavengerQuotaDef settings = ScavengerTestUtil::makeQuota( 100, 3, 5.0f );
    quota.initialize( settings );
    SW_EXPECT_EQUAL( 106, quota.computeIncrease( 0, 0.5f ) ); // 100 × (1 + 1/16)
    SW_EXPECT_EQUAL( 133, quota.computeIncrease( 0, 1.0f ) ); // × 1.25
    SW_EXPECT_EQUAL( 80, quota.computeIncrease( 0, 0.0f ) );  // × 0.75
    SW_EXPECT_EQUAL( 200, quota.computeIncrease( 3, 0.5f ) ); // 100 × (1 + 16/16)

    settings._randomness = 2.0f;
    quota.initialize( settings );
    SW_EXPECT_EQUAL( 0, quota.computeIncrease( 0, 0.0f ) ); // 1 + 2 × (−0.5) = 0
}

/**
 * @brief [ScavengerQuotaTest] 남은 날이 0 인 날의 끝에 판정한다. 같으면 채운 것이고 넘긴 만큼만 보너스다. 못 채우면 끝이고 그 뒤로는 아무것도 바뀌지 않는다
 */
SW_TEST_CASE( ScavengerQuotaTest, DeadlineCycle )
{
    sw::GameRandom random;
    random.setSeed( 5 );
    sw::ScavengerQuota quota;
    quota.initialize( ScavengerTestUtil::makeQuota( 100, 1, 5.0f ) );

    int32 bonus = -1;
    quota.addFulfilled( 100 );
    quota.addFulfilled( 0 );
    quota.addFulfilled( -50 );
    SW_EXPECT_EQUAL( 100, quota.getFulfilled() );
    SW_EXPECT_TRUE( quota.isMet() );

    SW_EXPECT_EQUAL( sw::ScavengerDeadlineResult::NotDue, quota.endDay( random, bonus ) );
    SW_EXPECT_EQUAL( 0, quota.getDaysLeft() );
    SW_EXPECT_EQUAL( 0, bonus );

    SW_EXPECT_EQUAL( sw::ScavengerDeadlineResult::QuotaMet, quota.endDay( random, bonus ) );
    SW_EXPECT_EQUAL( 0, bonus ); // 딱 채웠다
    SW_EXPECT_EQUAL( 1, quota.getCycle() );
    SW_EXPECT_EQUAL( 0, quota.getFulfilled() );
    SW_EXPECT_EQUAL( 1, quota.getDaysLeft() );
    const int32 raised = quota.getQuota();
    SW_EXPECT_TRUE( raised >= 180 && raised <= 233 ); // 100 + 106 × 0.75..1.25

    quota.addFulfilled( raised + 12 );
    SW_EXPECT_EQUAL( sw::ScavengerDeadlineResult::NotDue, quota.endDay( random, bonus ) );
    SW_EXPECT_EQUAL( sw::ScavengerDeadlineResult::QuotaMet, quota.endDay( random, bonus ) );
    SW_EXPECT_EQUAL( 2, bonus ); // 12 / 5

    SW_EXPECT_EQUAL( sw::ScavengerDeadlineResult::NotDue, quota.endDay( random, bonus ) );
    SW_EXPECT_EQUAL( sw::ScavengerDeadlineResult::GameOver, quota.endDay( random, bonus ) );
    SW_EXPECT_TRUE( quota.isGameOver() );
    quota.addFulfilled( 999999 );
    SW_EXPECT_EQUAL( 0, quota.getFulfilled() );
    SW_EXPECT_EQUAL( sw::ScavengerDeadlineResult::GameOver, quota.endDay( random, bonus ) );

    sw::ScavengerQuota noBonus;
    noBonus.initialize( ScavengerTestUtil::makeQuota( 10, 0, 0.0f ) ); // 나누는 값 0 은 보너스 없음, 날 0 은 첫날이 마감
    noBonus.addFulfilled( 50 );
    SW_EXPECT_EQUAL( sw::ScavengerDeadlineResult::QuotaMet, noBonus.endDay( random, bonus ) );
    SW_EXPECT_EQUAL( 0, bonus );
}

/**
 * @brief [ScavengerQuotaTest] 결함: 음수 할당량을 받아 빈손으로도 마감을 채웠다 — 거절하고 상태를 둔다. 왕복은 값을 지킨다
 */
SW_TEST_CASE( ScavengerQuotaTest, ReadStateChecksRange )
{
    sw::ScavengerQuota quota;
    quota.initialize( ScavengerTestUtil::makeQuota( 100, 2, 5.0f ) );
    quota.addFulfilled( 30 );

    sw::Archive written;
    quota.writeState( written );
    sw::ScavengerQuota restored;
    restored.initialize( ScavengerTestUtil::makeQuota( 1, 0, 0.0f ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 100, restored.getQuota() );
    SW_EXPECT_EQUAL( 30, restored.getFulfilled() );
    SW_EXPECT_EQUAL( 2, restored.getDaysLeft() );

    const auto readFixture = [&restored]( int32 quotaValue, int32 fulfilled, int32 daysLeft, int32 cycle, uint8 bGameOver )
    {
        sw::Archive fixture;
        ScavengerStateBytes::writeHeader<sw::ScavengerQuota>( fixture );
        fixture << quotaValue;
        fixture << fulfilled;
        fixture << daysLeft;
        fixture << cycle;
        fixture << bGameOver;
        sw::Archive readingFixture( fixture.getData(), fixture.getSize() );
        return restored.readState( readingFixture );
    };
    SW_EXPECT_TRUE( readFixture( 10, 0, 0, 0, 0 ) ); // 대조: 같은 도우미로 짠 맞는 바이트는 읽힌다
    SW_EXPECT_FALSE( readFixture( -1, 0, 0, 0, 0 ) );
    SW_EXPECT_FALSE( readFixture( 0, 0, 0, 0, 0 ) );
    SW_EXPECT_FALSE( readFixture( 10, -1, 0, 0, 0 ) );
    SW_EXPECT_FALSE( readFixture( 10, 0, -1, 0, 0 ) );
    SW_EXPECT_FALSE( readFixture( 10, 0, 0, -1, 0 ) );
    SW_EXPECT_FALSE( readFixture( 10, 0, 0, 0, 2 ) );
    SW_EXPECT_EQUAL( 10, restored.getQuota() );
}

/**
 * @brief [ScavengerCarryTest] 슬롯 수 · 양손 판정과 꺼내기. 양손 아이템은 빈 슬롯이 있으면 들 수 있고 그 뒤로는 아무것도 못 든다
 */
SW_TEST_CASE( ScavengerCarryTest, SlotsAndTwoHanded )
{
    sw::ScavengerCarryDef settings;
    settings._slotCount      = 2;
    settings._speedPerWeight = 0.01f;
    settings._minSpeedScale  = 0.4f;
    sw::ScavengerCarry carry;
    carry.initialize( settings );

    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::Ok, carry.add( ScavengerTestUtil::makeScrap( 1, 10, 5.0f, false ) ) );
    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::Ok, carry.add( ScavengerTestUtil::makeScrap( 2, 20, 5.0f, false ) ) );
    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::SlotsFull, carry.add( ScavengerTestUtil::makeScrap( 3, 30, 5.0f, false ) ) );
    SW_EXPECT_EQUAL( 2, carry.getCount() );
    SW_EXPECT_EQUAL( 30, carry.computeValue() );

    sw::ScavengerScrap taken;
    SW_EXPECT_FALSE( carry.tryRemove( 99, taken ) );
    SW_REQUIRE_TRUE( carry.tryRemove( 2, taken ) );
    SW_EXPECT_EQUAL( 20, taken._value );

    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::Ok, carry.add( ScavengerTestUtil::makeScrap( 4, 40, 20.0f, true ) ) );
    SW_EXPECT_TRUE( carry.isHoldingTwoHanded() );
    SW_REQUIRE_TRUE( carry.tryRemove( 1, taken ) );
    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::HandsFull, carry.add( ScavengerTestUtil::makeScrap( 5, 50, 5.0f, false ) ) ); // 슬롯이 비어도

    sw::vector<sw::ScavengerScrap> listAll{ ScavengerTestUtil::makeScrap( 7, 1, 1.0f, false ) };
    carry.takeAll( listAll );
    SW_EXPECT_EQUAL( size_t( 2 ), listAll.size() ); // 뒤에 붙인다
    SW_EXPECT_EQUAL( 0, carry.getCount() );
    SW_EXPECT_FALSE( carry.isHoldingTwoHanded() );
}

/**
 * @brief [ScavengerCarryTest] 이동 속도는 무게만큼 줄고 최소에서 멈춘다. 상태는 슬롯 수를 넘거나 무게 · 가치가 범위 밖이면 거절한다(결함: NaN 무게가 속도를 깨뜨렸다)
 */
SW_TEST_CASE( ScavengerCarryTest, SpeedAndState )
{
    sw::ScavengerCarryDef settings;
    settings._slotCount      = 2;
    settings._speedPerWeight = 0.01f;
    settings._minSpeedScale  = 0.4f;
    sw::ScavengerCarry carry;
    carry.initialize( settings );
    SW_EXPECT_NEAR( 1.0f, carry.computeSpeedScale(), kScavengerTolerance );
    (void)carry.add( ScavengerTestUtil::makeScrap( 1, 10, 25.0f, false ) );
    SW_EXPECT_NEAR( 0.75f, carry.computeSpeedScale(), kScavengerTolerance );
    (void)carry.add( ScavengerTestUtil::makeScrap( 2, 10, 1000.0f, false ) );
    SW_EXPECT_NEAR( 0.4f, carry.computeSpeedScale(), kScavengerTolerance );

    sw::Archive written;
    carry.writeState( written );
    sw::ScavengerCarry restored;
    restored.initialize( settings );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 2, restored.getCount() );
    SW_EXPECT_NEAR( 1025.0f, restored.computeWeight(), kScavengerTolerance );

    const auto readFixture = [&restored]( const sw::vector<sw::ScavengerScrap>& listScrap )
    {
        sw::Archive fixture;
        ScavengerStateBytes::writeHeader<sw::ScavengerCarry>( fixture );
        ScavengerStateBytes::writeScraps( fixture, listScrap );
        sw::Archive readingFixture( fixture.getData(), fixture.getSize() );
        return restored.readState( readingFixture );
    };
    const sw::ScavengerScrap good = ScavengerTestUtil::makeScrap( 1, 10, 5.0f, false );
    SW_EXPECT_TRUE( readFixture( { good } ) );
    SW_EXPECT_FALSE( readFixture( { good, good, good } ) ); // 슬롯 둘
    SW_EXPECT_FALSE( readFixture( { ScavengerTestUtil::makeScrap( 1, 10, std::numeric_limits<float32>::quiet_NaN(), false ) } ) );
    SW_EXPECT_FALSE( readFixture( { ScavengerTestUtil::makeScrap( 1, 10, -1.0f, false ) } ) );
    SW_EXPECT_FALSE( readFixture( { ScavengerTestUtil::makeScrap( 1, -10, 5.0f, false ) } ) );
    SW_EXPECT_EQUAL( 1, restored.getCount() );
}

/**
 * @brief [ScavengerFacilityTest] 지도: 방 수를 지키고 모든 방이 정문에서 닿으며(잠금 없음) 화재 출구가 바깥으로 난다. 시드가 같으면 같고, 고철 가치는 배율을 곱한다
 */
SW_TEST_CASE_F( ScavengerFacilityTest, LayoutIsConnectedAndDeterministic )
{
    const sw::ScavengerMoonDef* pMoon = _catalog.findMoon( "rust" );
    SW_REQUIRE_TRUE( pMoon != nullptr );
    SW_REQUIRE_TRUE( _facility.createLayout( _catalog, *pMoon, 7u, 1.0f ) );

    SW_EXPECT_EQUAL( 3, _facility.getRoomCount() );
    SW_EXPECT_EQUAL( 0, _facility.getLockedDoorCount() );
    SW_EXPECT_TRUE( _facility.canTraverse( "ship", "outside" ) );
    SW_EXPECT_TRUE( _facility.canTraverse( "outside", "entrance" ) );
    SW_EXPECT_TRUE( _facility.canTraverse( "entrance", "room1" ) ); // 첫 방의 부모는 정문뿐이다
    SW_EXPECT_TRUE( _facility.canTraverse( "entrance", "room2" ) || _facility.canTraverse( "room1", "room2" ) );
    SW_EXPECT_TRUE( _facility.canTraverse( "outside", "room1" ) || _facility.canTraverse( "outside", "room2" ) ); // 화재 출구
    SW_EXPECT_FALSE( _facility.canTraverse( "ship", "room1" ) );
    SW_EXPECT_FALSE( _facility.canTraverse( "ship", "nowhere" ) );

    SW_REQUIRE_EQUAL( size_t( 3 ), _facility.getGroundScrap().size() );
    SW_EXPECT_EQUAL( 30, _facility.computeGroundValue() );
    SW_EXPECT_NOT_EQUAL( _facility.getGroundScrap()[0]._uid, _facility.getGroundScrap()[1]._uid );

    sw::ScavengerFacility twin;
    SW_REQUIRE_TRUE( twin.createLayout( _catalog, *pMoon, 7u, 2.0f ) );
    SW_REQUIRE_EQUAL( size_t( 3 ), twin.getGroundScrap().size() );
    for ( size_t index = 0; index < 3; ++index )
    {
        SW_EXPECT_EQUAL( _facility.getGroundScrap()[index]._areaID, twin.getGroundScrap()[index]._areaID );
    }
    SW_EXPECT_EQUAL( 60, twin.computeGroundValue() ); // 날씨 배율 2

    sw::ScavengerFacility worthless;
    SW_REQUIRE_TRUE( worthless.createLayout( _catalog, *pMoon, 7u, -3.0f ) );
    SW_EXPECT_EQUAL( 0, worthless.computeGroundValue() ); // 음수 배율은 0 으로 본다
}

/**
 * @brief [ScavengerFacilityTest] 회사 위성에는 바깥과 우주선만 있고 고철도 없다
 */
SW_TEST_CASE_F( ScavengerFacilityTest, CompanyMoonHasNoFacility )
{
    SW_REQUIRE_TRUE( _facility.createLayout( _catalog, *_catalog.findMoon( "company" ), 7u, 1.0f ) );
    SW_EXPECT_EQUAL( 0, _facility.getRoomCount() );
    SW_EXPECT_TRUE( _facility.getGroundScrap().empty() );
    SW_EXPECT_TRUE( _facility.canTraverse( "ship", "outside" ) );
    SW_EXPECT_FALSE( _facility.canTraverse( "outside", "entrance" ) );
}

/**
 * @brief [ScavengerFacilityTest] 바닥: 놓으면 번호를 주고(이미 있으면 지킨다) 다른 방에서는 집지 못한다. 시신은 가치에 들지 않는다
 */
SW_TEST_CASE_F( ScavengerFacilityTest, GroundScrapRules )
{
    const int32 placed = _facility.placeScrap( ScavengerTestUtil::makeScrap( 0, 25, 1.0f, false ), "entrance" );
    SW_EXPECT_TRUE( placed > 0 );
    SW_EXPECT_EQUAL( 77, _facility.placeScrap( ScavengerTestUtil::makeScrap( 77, 5, 1.0f, false ), "entrance" ) );

    sw::ScavengerScrap body = ScavengerTestUtil::makeScrap( 0, 500, 60.0f, true );
    body._bodyOf            = 1;
    const int32 bodyUid     = _facility.placeScrap( body, "room1" );
    SW_EXPECT_EQUAL( 30, _facility.computeGroundValue() );
    SW_REQUIRE_TRUE( _facility.findGroundScrap( bodyUid ) != nullptr );
    SW_EXPECT_TRUE( _facility.findGroundScrap( bodyUid )->isBody() );

    sw::ScavengerScrap taken;
    SW_EXPECT_FALSE( _facility.tryTakeScrap( placed, "room1", taken ) );
    SW_EXPECT_FALSE( _facility.tryTakeScrap( 999, "entrance", taken ) );
    SW_REQUIRE_TRUE( _facility.tryTakeScrap( placed, "entrance", taken ) );
    SW_EXPECT_TRUE( taken._areaID.empty() );
    SW_EXPECT_EQUAL( 25, taken._value );
    SW_EXPECT_TRUE( _facility.findGroundScrap( placed ) == nullptr );
}

/**
 * @brief [ScavengerFacilityTest] 잠긴 문은 플래그로 연다. 안 잠긴 문 · 이미 연 문 · 플래그를 빌리지 않은 시설은 열리지 않는다. 하루가 끝나면 이 시설이 둔 플래그만 지운다
 */
SW_TEST_CASE_F( ScavengerFacilityTest, LockedDoorsUseBorrowedFlags )
{
    sw::ScavengerCatalog locked;
    SW_REQUIRE_TRUE( locked.loadFromXMLText( kScavengerLockedXML, "ScavengerTest" ) );
    sw::GameFlags flags;
    flags.setFlag( "story.beat" );
    _facility.bindFlags( flags );
    SW_REQUIRE_TRUE( _facility.createLayout( locked, *locked.findMoon( "rust" ), 7u, 1.0f ) );
    SW_EXPECT_EQUAL( 2, _facility.getLockedDoorCount() );

    SW_EXPECT_FALSE( _facility.canTraverse( "entrance", "room1" ) );
    SW_EXPECT_FALSE( _facility.unlockDoor( "entrance" ) ); // 잠기지 않았다
    SW_EXPECT_FALSE( _facility.unlockDoor( "nowhere" ) );
    SW_EXPECT_TRUE( _facility.unlockDoor( "room1" ) );
    SW_EXPECT_TRUE( _facility.canTraverse( "entrance", "room1" ) );
    SW_EXPECT_FALSE( _facility.unlockDoor( "room1" ) ); // 이미 열었다
    SW_EXPECT_TRUE( flags.hasFlag( "unlocked.room1" ) );

    _facility.clear();
    SW_EXPECT_FALSE( flags.hasFlag( "unlocked.room1" ) );
    SW_EXPECT_TRUE( flags.hasFlag( "story.beat" ) );

    sw::ScavengerFacility noFlags;
    SW_REQUIRE_TRUE( noFlags.createLayout( locked, *locked.findMoon( "rust" ), 7u, 1.0f ) );
    SW_EXPECT_FALSE( noFlags.unlockDoor( "room1" ) );
    SW_EXPECT_FALSE( noFlags.canTraverse( "entrance", "room1" ) );
}

/**
 * @brief [ScavengerFacilityUnknownScrapTest] 결함: 위성 목록의 모르는 고철은 읽을 때 오류만 내고 남아, 내리는 순간 빈 포인터를 읽었다 — 뽑히지 않는다
 */
SW_TEST_CASE( ScavengerFacilityUnknownScrapTest, UnknownMoonScrapIsNeverPicked )
{
    SW_EXPECT_LOG( "XMLCatalog.UnknownReference" );
    constexpr const utf8* kUnknownScrapXML = R"(<ScavengerCatalog>
  <Facility rooms="2" roomsMax="2"/>
  <Scrap id="bolt" min="10" max="10"/>
  <Moon id="mixed" name="Mixed" scrap="4" scrapMax="4" scraps="ghost,bolt"/>
  <Moon id="haunted" name="Haunted" scrap="4" scrapMax="4" scraps="ghost"/>
</ScavengerCatalog>)";
    sw::ScavengerCatalog  catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( kUnknownScrapXML, "ScavengerTest" ) ); // 필수 참조가 깨져 로드는 실패다 — 읽은 항목으로 판이 모르는 고철을 고르지 않는지만 본다

    sw::ScavengerFacility mixed;
    SW_REQUIRE_TRUE( mixed.createLayout( catalog, *catalog.findMoon( "mixed" ), 3u, 1.0f ) );
    SW_REQUIRE_EQUAL( size_t( 4 ), mixed.getGroundScrap().size() );
    for ( const sw::ScavengerScrap& scrap : mixed.getGroundScrap() )
    {
        SW_EXPECT_EQUAL( sw::hashed_string( "bolt" ), scrap._scrapID );
    }

    sw::ScavengerFacility haunted;
    SW_REQUIRE_TRUE( haunted.createLayout( catalog, *catalog.findMoon( "haunted" ), 3u, 1.0f ) );
    SW_EXPECT_TRUE( haunted.getGroundScrap().empty() );
}

/**
 * @brief [ScavengerFacilityTest] 왕복은 그래프를 다시 만들고 바닥 · 다음 번호를 잇는다. 카탈로그 없이 지도가 있는 상태는 거절한다
 */
SW_TEST_CASE_F( ScavengerFacilityTest, StateRoundTripRebuildsGraph )
{
    SW_REQUIRE_TRUE( _facility.createLayout( _catalog, *_catalog.findMoon( "rust" ), 7u, 1.0f ) );
    sw::Archive written;
    _facility.writeState( written );

    sw::ScavengerFacility restored;
    restored.bindCatalog( &_catalog );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 3, restored.getRoomCount() );
    SW_EXPECT_EQUAL( 30, restored.computeGroundValue() );
    SW_EXPECT_TRUE( restored.canTraverse( "outside", "entrance" ) );
    SW_EXPECT_EQUAL( _facility.placeScrap( ScavengerTestUtil::makeScrap( 0, 1, 1.0f, false ), "entrance" ), restored.placeScrap( ScavengerTestUtil::makeScrap( 0, 1, 1.0f, false ), "entrance" ) );

    sw::ScavengerFacility noCatalog;
    sw::Archive           readingAgain( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noCatalog.readState( readingAgain ) );
}

/**
 * @brief [ScavengerFacilityTest] 결함: 번호가 0 이하이거나 다음 번호 이상인 고철(새로 놓는 것과 겹친다), 시드 0 의 지도(난수 단언), 음수 무게 · 가치를 받았다 — 거절한다
 */
SW_TEST_CASE_F( ScavengerFacilityTest, ReadStateRefusesBrokenScrap )
{
    sw::ScavengerFacility restored;
    restored.bindCatalog( &_catalog );
    const sw::ScavengerScrap good = ScavengerTestUtil::makeScrap( 4, 10, 5.0f, false );
    SW_REQUIRE_TRUE( readFixture( restored, 7u, good, 5 ) ); // 형식은 맞다
    SW_EXPECT_EQUAL( 10, restored.computeGroundValue() );

    SW_EXPECT_FALSE( readFixture( restored, 7u, good, 4 ) );
    SW_EXPECT_FALSE( readFixture( restored, 7u, ScavengerTestUtil::makeScrap( 0, 10, 5.0f, false ), 5 ) );
    SW_EXPECT_FALSE( readFixture( restored, 7u, ScavengerTestUtil::makeScrap( 4, -10, 5.0f, false ), 5 ) );
    SW_EXPECT_FALSE( readFixture( restored, 7u, ScavengerTestUtil::makeScrap( 4, 10, -5.0f, false ), 5 ) );
    SW_EXPECT_FALSE( readFixture( restored, 0u, good, 5 ) );
    SW_EXPECT_EQUAL( 10, restored.computeGroundValue() );
}

/**
 * @brief [ScavengerExpeditionTest] 시작 거절: 사람 0 명. 대신 시작하지 않는다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, InitializeRefusesBadInput )
{
    SW_EXPECT_LOG( "ScavengerExpedition.ScavengerExpeditionRefusedCrewCountNeeds" );
    sw::ScavengerExpedition expedition;
    SW_EXPECT_FALSE( expedition.initialize( _bare.make( _catalog ), _wallet, _clock, _weather, _flags, _storage, 1, 0 ) );
    SW_EXPECT_FALSE( expedition.initialize( _bare.make( _catalog ), _wallet, _clock, _weather, _flags, _storage, 1, -3 ) );
}

/**
 * @brief [ScavengerExpeditionTest] 다시 연 원정은 새 원정과 같은 상태다 — 앞 원정의 지도 · 고유 번호 · 위협이 남지 않고, 다른 카탈로그로 열어도 저장이 읽힌다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, ReinitializedExpeditionMatchesAFreshOne )
{
    SW_REQUIRE_TRUE( _expedition.land() == sw::ScavengerActionResult::Ok );
    SW_REQUIRE_TRUE( open( _expedition, _catalog, 11 ) );
    sw::ScavengerExpedition fresh;
    SW_REQUIRE_TRUE( open( fresh, _catalog, 11 ) );
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::hasSameState( _expedition, fresh ) );

    // 앞 원정의 위성이 없는 카탈로그로 다시 열어도 저장이 읽힌다.
    sw::ScavengerCatalog other;
    SW_REQUIRE_TRUE( other.loadFromXMLText( R"(<ScavengerCatalog><Moon id="ice" name="Ice"/></ScavengerCatalog>)", "ScavengerOther" ) );
    SW_REQUIRE_TRUE( _expedition.land() == sw::ScavengerActionResult::Ok );
    SW_REQUIRE_TRUE( open( _expedition, other, 11 ) );
    sw::ScavengerExpedition restored;
    SW_REQUIRE_TRUE( open( restored, other, 11 ) );
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::roundTrips( _expedition, restored ) );
}

/**
 * @brief [ScavengerExpeditionTest] 위성 이동과 단계: 값을 내고 옮기며 모자라면 그대로다. 내린 뒤에는 옮기거나 다시 내릴 수 없고, 궤도에서는 뜰 수 없다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, RoutingAndPhases )
{
    SW_EXPECT_EQUAL( int64( 60 ), _expedition.getCredits() );
    SW_EXPECT_EQUAL( sw::hashed_string( "rust" ), _expedition.getMoon()->_id ); // 첫 위성
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::WrongPhase, _expedition.takeOff() );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::UnknownMoon, _expedition.routeTo( "nothing" ) );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::NotEnoughCredits, _expedition.routeTo( "dear" ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "rust" ), _expedition.getMoon()->_id );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::Ok, _expedition.routeTo( "rust" ) ); // 이미 있으면 공짜
    SW_EXPECT_EQUAL( int64( 60 ), _expedition.getCredits() );
    SW_EXPECT_EQUAL( 0, countEvents( sw::ScavengerEvent::Kind::Routed ) );

    SW_EXPECT_EQUAL( sw::ScavengerActionResult::Ok, _expedition.routeTo( "far" ) );
    SW_EXPECT_EQUAL( int64( 10 ), _expedition.getCredits() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::Routed ) );

    SW_EXPECT_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    SW_EXPECT_EQUAL( sw::ScavengerPhase::Landed, _expedition.getPhase() );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::WrongPhase, _expedition.land() );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::WrongPhase, _expedition.routeTo( "rust" ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::Landed ) );
}

/**
 * @brief [ScavengerExpeditionTest] 궤도에서는 몸을 움직일 수 없고, 없는 사람 · 막힌 길 · 없는 고철을 가린다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, ActionsCheckPhaseAndTargets )
{
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::WrongPhase, _expedition.movePlayer( 0, "outside" ) );
    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::Unavailable, _expedition.pickUp( 0, 1 ) );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::WrongPhase, _expedition.dropItem( 0, 1 ) );
    SW_EXPECT_NEAR( 0.0f, _expedition.applyDamage( 0, 10.0f ), kScavengerTolerance );
    _expedition.killPlayer( 0 );
    SW_EXPECT_EQUAL( 2, _expedition.countAlive() );

    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::InvalidPlayer, _expedition.movePlayer( -1, "outside" ) );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::InvalidPlayer, _expedition.movePlayer( 2, "outside" ) );
    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::Unavailable, _expedition.pickUp( 2, 1 ) );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::Blocked, _expedition.movePlayer( 0, "entrance" ) ); // 우주선에서 정문으로 바로 못 간다
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::Blocked, _expedition.movePlayer( 0, "nowhere" ) );
    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::NotFound, _expedition.pickUp( 0, _expedition.getFacility().getGroundScrap()[0]._uid ) ); // 다른 방
    SW_EXPECT_TRUE( _expedition.findCrewMember( 2 ) == nullptr );
    SW_EXPECT_TRUE( _expedition.findCrewMember( -1 ) == nullptr );
    SW_EXPECT_NEAR( 0.0f, _expedition.applyDamage( 9, 10.0f ), kScavengerTolerance );
    SW_EXPECT_EQUAL( 0, _expedition.depositToShip( 9 ) );
}

/**
 * @brief [ScavengerExpeditionTest] 하루: 해 질 녘은 한 번만 알리고, 자정에 저절로 떠나 밖에 남은 사람이 죽는다. 우주선 안의 사람이 든 고철은 실린다. 다음 날 모두 되살아난다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, DayRunsToAutomaticDeparture )
{
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    const sw::ScavengerScrap scrap = _expedition.getFacility().getGroundScrap().front();
    SW_REQUIRE_TRUE( walk( _expedition, 0, scrap._areaID ) );
    SW_REQUIRE_EQUAL( sw::ScavengerPickupResult::Ok, _expedition.pickUp( 0, scrap._uid ) );
    SW_REQUIRE_TRUE( walk( _expedition, 0, "ship" ) );
    SW_REQUIRE_TRUE( walk( _expedition, 1, "outside" ) );

    _expedition.update( 0.0f );
    _expedition.update( -10.0f );
    SW_EXPECT_NEAR( 0.0f, _expedition.getHoursOnMoon(), kScavengerTolerance );

    _expedition.update( 100.0f ); // 10 시간 — 16 시
    SW_EXPECT_NEAR( 10.0f, _expedition.getHoursOnMoon(), kScavengerTolerance );
    SW_EXPECT_EQUAL( 0, countEvents( sw::ScavengerEvent::Kind::Dusk ) );
    _expedition.update( 20.0f ); // 18 시 — 경계에서 알린다
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::Dusk ) );
    _expedition.update( 10.0f );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::Dusk ) );
    SW_EXPECT_EQUAL( sw::ScavengerPhase::Landed, _expedition.getPhase() );

    _expedition.update( 50.0f ); // 24 시 — 떠난다
    SW_EXPECT_EQUAL( sw::ScavengerPhase::InOrbit, _expedition.getPhase() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::ShipDeparted ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::PlayerDied ) );
    SW_EXPECT_EQUAL( size_t( 1 ), _expedition.getShipScrap().size() );
    SW_EXPECT_EQUAL( 10, _expedition.computeShipValue() );
    SW_EXPECT_EQUAL( int64( 48 ), _expedition.getCredits() ); // 시신 없는 죽음 하나 — 20%
    SW_EXPECT_EQUAL( 2, _expedition.countAlive() );
    SW_EXPECT_EQUAL( sw::hashed_string( "ship" ), _expedition.findCrewMember( 1 )->_areaID );

    _expedition.update( 10000.0f ); // 궤도에서는 시간이 흐르지 않는다
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::ShipDeparted ) );
}

/**
 * @brief [ScavengerExpeditionTest] 죽으면 든 것이 그 자리에 떨어지고 시신이 남는다. 동료가 양손으로 들어 실으면 벌금이 줄어든다. 죽은 사람은 다시 맞거나 움직이지 못한다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, BodyRecoveryLowersFine )
{
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    const sw::ScavengerScrap scrap = _expedition.getFacility().getGroundScrap().front();
    SW_REQUIRE_TRUE( walk( _expedition, 0, scrap._areaID ) );
    SW_REQUIRE_EQUAL( sw::ScavengerPickupResult::Ok, _expedition.pickUp( 0, scrap._uid ) );

    SW_EXPECT_NEAR( 40.0f, _expedition.applyDamage( 0, 40.0f ), kScavengerTolerance );
    SW_EXPECT_EQUAL( 2, _expedition.countAlive() );
    SW_EXPECT_NEAR( 60.0f, _expedition.applyDamage( 0, 500.0f ), kScavengerTolerance ); // 남은 체력만큼
    SW_EXPECT_EQUAL( 1, _expedition.countAlive() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::PlayerDied ) );
    SW_EXPECT_NEAR( 0.0f, _expedition.applyDamage( 0, 10.0f ), kScavengerTolerance );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::InvalidPlayer, _expedition.movePlayer( 0, "outside" ) );
    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::Unavailable, _expedition.pickUp( 0, scrap._uid ) );

    SW_EXPECT_TRUE( _expedition.getFacility().findGroundScrap( scrap._uid ) != nullptr ); // 든 것이 떨어졌다(번호 그대로)
    const sw::ScavengerScrap* pBody = nullptr;
    for ( const sw::ScavengerScrap& ground : _expedition.getFacility().getGroundScrap() )
    {
        pBody = ground.isBody() ? &ground : pBody;
    }
    SW_REQUIRE_TRUE( pBody != nullptr );
    SW_EXPECT_EQUAL( 0, pBody->_bodyOf );
    SW_EXPECT_EQUAL( scrap._areaID, pBody->_areaID );
    SW_EXPECT_EQUAL( 30, _expedition.getFacility().computeGroundValue() ); // 시신은 값이 없다
    const int32 bodyUid = pBody->_uid;

    SW_REQUIRE_TRUE( walk( _expedition, 1, scrap._areaID ) );
    SW_REQUIRE_EQUAL( sw::ScavengerPickupResult::Ok, _expedition.pickUp( 1, bodyUid ) );
    SW_EXPECT_TRUE( _expedition.findCrewMember( 1 )->_carry.isHoldingTwoHanded() );
    SW_EXPECT_EQUAL( sw::ScavengerPickupResult::HandsFull, _expedition.pickUp( 1, scrap._uid ) );
    SW_REQUIRE_TRUE( walk( _expedition, 1, "ship" ) );
    SW_EXPECT_EQUAL( 1, _expedition.depositToShip( 1 ) );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::BodyRecovered ) );
    SW_EXPECT_TRUE( _expedition.getShipScrap().empty() ); // 시신은 고철이 아니다

    SW_EXPECT_EQUAL( sw::ScavengerActionResult::Ok, _expedition.takeOff() );
    SW_EXPECT_EQUAL( int64( 57 ), _expedition.getCredits() ); // 5%
    SW_EXPECT_EQUAL( 0, countEvents( sw::ScavengerEvent::Kind::CrewWiped ) );
}

/**
 * @brief [ScavengerExpeditionTest] 결함: 시신을 든 채 우주선에서 죽으면 시신이 실린 알림이 없었다(내려놓기 · 이륙 길에는 있다). 모두 죽으면 저절로 떠나고, 우주선 안의 죽음은 시신이 실린 것이다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, DyingOnShipWithBodyRecoversIt )
{
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    SW_REQUIRE_TRUE( walk( _expedition, 0, "outside" ) );
    _expedition.killPlayer( 0 );
    const sw::ScavengerScrap* pBody = nullptr;
    for ( const sw::ScavengerScrap& ground : _expedition.getFacility().getGroundScrap() )
    {
        pBody = ground.isBody() ? &ground : pBody;
    }
    SW_REQUIRE_TRUE( pBody != nullptr );
    const int32 bodyUid = pBody->_uid;

    SW_REQUIRE_TRUE( walk( _expedition, 1, "outside" ) );
    SW_REQUIRE_EQUAL( sw::ScavengerPickupResult::Ok, _expedition.pickUp( 1, bodyUid ) );
    SW_REQUIRE_TRUE( walk( _expedition, 1, "ship" ) );
    _expedition.killPlayer( 1 );

    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::BodyRecovered ) );
    SW_EXPECT_EQUAL( sw::ScavengerPhase::InOrbit, _expedition.getPhase() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::CrewWiped ) );
    SW_EXPECT_EQUAL( int64( 54 ), _expedition.getCredits() ); // 5% 둘
}

/**
 * @brief [ScavengerExpeditionTest] 전멸: 우주선 고철의 비율을 올림으로 잃고 남는 것은 실은 순서를 지킨다. 시드가 같으면 같은 것을 잃는다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, WipeLosesShipScrapDeterministically )
{
    sw::ScavengerExpedition twin;
    SW_REQUIRE_TRUE( open( twin, _catalog, 11 ) );
    const auto wipe = [this]( sw::ScavengerExpedition& expedition )
    {
        (void)expedition.land();
        (void)haulEverything( expedition, 0 );
        expedition.killPlayer( 0 );
        expedition.killPlayer( 1 );
    };
    wipe( _expedition );
    wipe( twin );

    SW_REQUIRE_EQUAL( size_t( 1 ), _expedition.getShipScrap().size() ); // 셋의 50% 를 올림해 둘을 잃는다
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::CrewWiped ) );
    for ( const sw::ScavengerEvent& event : _listEvent )
    {
        if ( event._kind == sw::ScavengerEvent::Kind::CrewWiped )
            SW_EXPECT_EQUAL( 2, event._value );
    }
    SW_REQUIRE_EQUAL( size_t( 1 ), twin.getShipScrap().size() );
    SW_EXPECT_EQUAL( _expedition.getShipScrap()[0]._uid, twin.getShipScrap()[0]._uid );
}

/**
 * @brief [ScavengerExpeditionTest] 전멸해도 최소한 정한 수는 남긴다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, WipeKeepsMinimum )
{
    sw::ScavengerCatalog keep;
    SW_REQUIRE_TRUE( keep.loadFromXMLText( kScavengerKeepXML, "ScavengerTest" ) );
    sw::ScavengerExpedition expedition;
    SW_REQUIRE_TRUE( open( expedition, keep, 11 ) );
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, expedition.land() );
    SW_REQUIRE_TRUE( haulEverything( expedition, 0 ) );
    SW_REQUIRE_EQUAL( size_t( 3 ), expedition.getShipScrap().size() );
    expedition.killPlayer( 0 );
    expedition.killPlayer( 1 );
    SW_EXPECT_EQUAL( size_t( 2 ), expedition.getShipScrap().size() );
}

/**
 * @brief [ScavengerExpeditionTest] 회사에서만 판다. 값은 고철 합 × 마감에 가까운 매입률이고 할당량을 채운다. 넘긴 만큼 보너스를 받고 할당량이 오른다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, SellingFulfilsQuota )
{
    sw::ScavengerActionResult result = sw::ScavengerActionResult::Ok;
    SW_EXPECT_EQUAL( 0, _expedition.sellAllShipScrap( result ) );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::WrongPhase, result );

    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    SW_REQUIRE_TRUE( haulEverything( _expedition, 0 ) );
    SW_EXPECT_EQUAL( 0, _expedition.sellAllShipScrap( result ) );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::NotCompany, result );
    SW_EXPECT_EQUAL( 30, _expedition.computeShipValue() );
    SW_EXPECT_EQUAL( 22, _expedition.computeSellValue() ); // 남은 1 일 — 매입률 0.75, 22.5 를 버린다

    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.takeOff() );
    SW_EXPECT_EQUAL( 0, _expedition.getQuota().getDaysLeft() );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( _expedition.getShipScrap().size() ) );
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.routeTo( "company" ) );
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    SW_EXPECT_EQUAL( 0, _expedition.getFacility().getRoomCount() );

    SW_EXPECT_EQUAL( 30, _expedition.sellAllShipScrap( result ) );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::Ok, result );
    SW_EXPECT_TRUE( _expedition.getShipScrap().empty() );
    SW_EXPECT_EQUAL( 30, _expedition.getQuota().getFulfilled() );
    SW_EXPECT_EQUAL( int64( 90 ), _expedition.getCredits() );

    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.takeOff() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::QuotaMet ) );
    SW_EXPECT_EQUAL( int64( 92 ), _expedition.getCredits() ); // 넘긴 10 / 5
    SW_EXPECT_EQUAL( 73, _expedition.getQuota().getQuota() ); // 20 + 50 × (1 + 1/16)
    SW_EXPECT_EQUAL( 1, _expedition.getQuota().getDaysLeft() );
}

/**
 * @brief [ScavengerExpeditionTest] 마감에 못 채우면 게임 오버이고 그 뒤로는 내리지도 사지도 못한다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, MissingQuotaEndsTheGame )
{
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.takeOff() );
    SW_EXPECT_EQUAL( sw::ScavengerPhase::InOrbit, _expedition.getPhase() );
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.takeOff() );
    SW_EXPECT_EQUAL( sw::ScavengerPhase::GameOver, _expedition.getPhase() );
    SW_EXPECT_EQUAL( 1, countEvents( sw::ScavengerEvent::Kind::GameOver ) );

    SW_EXPECT_EQUAL( sw::ScavengerActionResult::WrongPhase, _expedition.land() );
    SW_EXPECT_EQUAL( sw::ScavengerActionResult::WrongPhase, _expedition.routeTo( "rust" ) );
    SW_EXPECT_EQUAL( sw::ShopResult::UnknownShop, _expedition.buyFromTerminal( "bolt", 1 ) );
}

/**
 * @brief [ScavengerExpeditionTest] 내린 채로 쓰고 읽으면 사람 · 고철 · 지도 · 시간이 이어지고 같은 시각에 떠난다. 사람 수가 다르면 거절한다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, StateRoundTripsWhileLanded )
{
    SW_REQUIRE_EQUAL( sw::ScavengerActionResult::Ok, _expedition.land() );
    const sw::ScavengerScrap scrap = _expedition.getFacility().getGroundScrap().front();
    SW_REQUIRE_TRUE( walk( _expedition, 0, scrap._areaID ) );
    SW_REQUIRE_EQUAL( sw::ScavengerPickupResult::Ok, _expedition.pickUp( 0, scrap._uid ) );
    _expedition.update( 100.0f );

    sw::Archive written;
    _expedition.writeState( written );

    sw::ScavengerExpedition restored;
    SW_REQUIRE_TRUE( open( restored, _catalog, 99 ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( sw::ScavengerPhase::Landed, restored.getPhase() );
    SW_EXPECT_NEAR( 10.0f, restored.getHoursOnMoon(), kScavengerTolerance );
    SW_EXPECT_EQUAL( 1, restored.findCrewMember( 0 )->_carry.getCount() );
    SW_EXPECT_EQUAL( scrap._areaID, restored.findCrewMember( 0 )->_areaID );
    SW_EXPECT_EQUAL( _expedition.getFacility().computeGroundValue(), restored.getFacility().computeGroundValue() );
    SW_EXPECT_TRUE( restored.getFacility().canTraverse( "outside", "entrance" ) );

    restored.update( 70.0f ); // 내린 지 17 시간
    SW_EXPECT_EQUAL( sw::ScavengerPhase::Landed, restored.getPhase() );
    restored.update( 10.0f ); // 18 시간 — 6 시에 내렸으니 자정이다
    SW_EXPECT_EQUAL( sw::ScavengerPhase::InOrbit, restored.getPhase() );

    sw::ScavengerExpedition solo;
    SW_REQUIRE_TRUE( solo.initialize( _bare.make( _catalog ), _wallet, _clock, _weather, _flags, _storage, 1, 1 ) );
    sw::Archive readingAgain( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( solo.readState( readingAgain ) );
}

/**
 * @brief [ScavengerExpeditionTest] 결함: 사람 수 밖의 시신 번호(시신을 싣는 순간 범위 밖을 쓴다)와 NaN · 음수 내린 시간(자정 비교가 늘 거짓이라 우주선이 뜨지 않는다)을 받았다 — 거절하고 상태를 둔다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, ReadStateRefusesBrokenBodiesAndTime )
{
    sw::ScavengerScrap body = ScavengerTestUtil::makeScrap( 5, 0, 60.0f, true );
    body._bodyOf            = 1;
    SW_REQUIRE_TRUE( readFixture( body, 0.0f ) ); // 형식은 맞다
    SW_EXPECT_EQUAL( 1, _expedition.findCrewMember( 0 )->_carry.getCount() );

    sw::ScavengerScrap stray = body;
    stray._bodyOf            = 2;
    SW_EXPECT_FALSE( readFixture( stray, 0.0f ) );
    stray._bodyOf = 9999;
    SW_EXPECT_FALSE( readFixture( stray, 0.0f ) );
    stray._bodyOf = -2;
    SW_EXPECT_FALSE( readFixture( stray, 0.0f ) );
    SW_EXPECT_FALSE( readFixture( body, std::numeric_limits<float32>::quiet_NaN() ) );
    SW_EXPECT_FALSE( readFixture( body, -1.0f ) );
    SW_EXPECT_NEAR( 0.0f, _expedition.getHoursOnMoon(), kScavengerTolerance );
}

/**
 * @brief [ScavengerFacilityTest] 매입률은 적은 순서와 상관없이 점 사이를 보간하고 끝 밖은 끝 값이다. 점이 없으면 1 이다
 */
SW_TEST_CASE_F( ScavengerFacilityTest, BuyRateInterpolates )
{
    SW_EXPECT_NEAR( 1.0f, _catalog.computeBuyRate( -5 ), kScavengerTolerance );
    SW_EXPECT_NEAR( 1.0f, _catalog.computeBuyRate( 0 ), kScavengerTolerance );
    SW_EXPECT_NEAR( 0.75f, _catalog.computeBuyRate( 1 ), kScavengerTolerance );
    SW_EXPECT_NEAR( 0.5f, _catalog.computeBuyRate( 2 ), kScavengerTolerance );
    SW_EXPECT_NEAR( 0.5f, _catalog.computeBuyRate( 99 ), kScavengerTolerance );

    sw::ScavengerCatalog empty;
    SW_EXPECT_NEAR( 1.0f, empty.computeBuyRate( 3 ), kScavengerTolerance );
}

/**
 * @brief [ScavengerCatalogTest] 깨진 원소를 알린다: 같은 날의 매입률(앞의 것이 남는다), 범위 밖 값(앞의 설정이 남는다), 모르는 원소, 위성 없음
 */
SW_TEST_CASE( ScavengerCatalogTest, ReportsBrokenElements )
{
    SW_EXPECT_LOG( "ScavengerCatalog.BuyRateDaysLeft" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "ScavengerCatalog.NoMoonsNowhereLand" );
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    constexpr const utf8* kBrokenXML = R"(<ScavengerCatalog>
  <Quota start="50"/>
  <Quota start="0"/>
  <BuyRate daysLeft="1" rate="0.25"/>
  <BuyRate daysLeft="1" rate="0.75"/>
  <Scrap id="bolt" max="30"/>
  <Mystery/>
</ScavengerCatalog>)";
    sw::ScavengerCatalog  catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrokenXML, "ScavengerTest" ) );
    SW_EXPECT_EQUAL( 50, catalog.getQuotaSettings()._startQuota );
    SW_EXPECT_NEAR( 0.25f, catalog.computeBuyRate( 1 ), kScavengerTolerance );
    SW_EXPECT_TRUE( catalog.findScrap( "bolt" ) != nullptr );
}

namespace
{
    /** @brief 원정과 빌려 준 지갑 · 시계 · 날씨 · 플래그 · 창고를 한 판으로 묶습니다. 지갑 · 시계는 원정 밖(주인이 저장한다)이라 결정성 테스트가 함께 싣습니다. */
    struct ScavengerDeterminismRun
    {
        sw::Wallet              _wallet{};
        sw::WorldClock          _clock{};
        sw::WeatherSystem       _weather{};
        sw::GameFlags           _flags{};
        sw::Inventory           _storage{};
        sw::ScavengerExpedition _expedition{};

        /** @brief 원정과 지갑을 함께 싣습니다 — 벌금 · 매입이 지갑을 바꾸고, 벌금은 지갑 잔액에서 계산한다. */
        SW_DECLARE_STATE_IO_IN_PLACE()
        {
            io.value( _wallet );
            io.value( _clock );
            io.value( _expedition );
        }
    };
} // namespace

/**
 * @brief [ScavengerExpeditionTest] 결정성: 같은 입력이면 시설 생성(시드 난수) · 줍기 · 죽음 · 시신 · 자정 이륙 · 벌금이 같은 바이트를 내고, 중간 저장에서 이어도 같다
 */
SW_TEST_CASE_F( ScavengerExpeditionTest, ExpeditionDeterminism )
{
    const auto setup = [this]( ScavengerDeterminismRun& run ) -> bool
    {
        return run._clock.initialize( sw::WorldClockSettings{} ) && run._expedition.initialize( _bare.make( _catalog ), run._wallet, run._clock, run._weather, run._flags, run._storage, 11, 2 );
    };
    const auto step = []( ScavengerDeterminismRun& run, int32 stepIndex )
    {
        sw::ScavengerExpedition& expedition = run._expedition;
        const sw::hashed_string  arrRoute[] = { "outside", "entrance", "room1", "room2" };
        if ( stepIndex == 0 )
            (void)expedition.land();
        if ( 2 <= stepIndex && stepIndex < 6 )
            (void)expedition.movePlayer( 1, arrRoute[stepIndex - 2] ); // 시설 연결은 시드가 정한다 — 막힌 문은 거절된다
        if ( 20 <= stepIndex && stepIndex < 22 )
            (void)expedition.movePlayer( 0, arrRoute[stepIndex - 20] );
        for ( int32 player = 0; player < 2; ++player )
        {
            const sw::ScavengerCrewMember* pMember = expedition.findCrewMember( player );
            for ( const sw::ScavengerScrap& scrap : expedition.getFacility().getGroundScrap() )
            {
                if ( stepIndex % 3 == player && scrap._areaID == pMember->_areaID )
                {
                    (void)expedition.pickUp( player, scrap._uid );
                    break;
                }
            }
        }
        if ( stepIndex == 30 )
            (void)expedition.applyDamage( 0, 30.0f );
        if ( stepIndex == 40 )
            (void)expedition.applyDamage( 1, 500.0f ); // 든 것이 떨어지고 시신이 남는다
        if ( 50 <= stepIndex && stepIndex < 52 )
            (void)expedition.movePlayer( 0, arrRoute[51 - stepIndex] );
        if ( stepIndex == 52 )
        {
            (void)expedition.movePlayer( 0, sw::hashed_string{ sw::ScavengerExpedition::kShipAreaID } );
            (void)expedition.depositToShip( 0 );
        }
        expedition.update( 2.0f ); // 하루 240 초 — 내린 지 90 걸음에 자정이라 저절로 뜬다
    };
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::isDeterministic<ScavengerDeterminismRun>( setup, step, 160 ) );
}
