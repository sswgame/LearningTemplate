#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternCarcass.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternHonor.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternHorse.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternLaw.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternSurvival.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

#include <limits>

// 서부극 키트의 규칙: 법(목격 · 신고 시간 · 입막음 · 수배 상한과 식기 · 현상금), 명예, 말(유대 · 코어 · 겁 · 질주), 생존(온도 · 데드아이), 사냥(가죽 등급 · 부패 · 값), 카탈로그 오류와 상태 바이트의 범위 검사.
// 상태 바이트는 형식을 맞춰 손으로 쓴 뒤(정상 값이 읽히는 것을 함께 본다) 범위 밖 값만 바꿔 넣는다. 키트 GF_OpenWorldWestern(nogpu).

namespace
{
    constexpr const utf8* kWesternXML = R"(<WesternCatalog currency="Dollar" extraHitPenalty="1">
  <Crime id="theft" reportTime="10" bounty="5" wanted="1" honor="-5"/>
  <Crime id="murder" reportTime="20" bounty="51" wanted="2" honor="-40"/>
  <Crime id="littering" reportTime="0" bounty="1" wanted="0"/>
  <Region id="lemoyne" wantedCooldown="60" disguiseScale="2" maskedBountyScale="0.5" bountyDecayPerDay="1" maxWanted="3"/>
  <Pursuit level="3" name="Manhunt" lawmen="8" shootOnSight="true"/>
  <Pursuit level="1" name="Search" lawmen="2"/>
  <Honor min="-1000" max="1000" start="0">
    <Tier name="Outlaw" min="-1000" discount="-0.1" flag="honor_low"/>
    <Tier name="Neutral" min="-200"/>
    <Tier name="Hero" min="500" discount="0.2" flag="honor_high"/>
    <Action id="help" delta="300"/>
    <Action id="brand" delta="-900"/>
  </Honor>
  <Horse id="steady" health="100" stamina="100" courage="0.5" gallopDrain="12" coreDrain="2" gallopCoreDrain="6" fearDecay="0.25" buckChance="0"/>
  <Horse id="wild" courage="0" buckChance="1"/>
  <Horse id="coin" courage="0" buckChance="0.5"/>
  <Bond level="1" xp="0"/>
  <Bond level="2" xp="10" unlocks="rear" stamina="10" fearResist="0.1"/>
  <Bond level="3" xp="30" unlocks="drift,piaffe" stamina="20" health="5" fearResist="0.3"/>
  <BondExperience ride="0.1" brush="15" feed="10" calm="5" brushCooldown="6"/>
  <Food id="hay" healthCore="25" staminaCore="15" health="10" bond="5"/>
  <Food id="apple" healthCore="5"/>
  <Food id="stew" healthCore="30" staminaCore="40" deadEyeCore="50" health="10" stamina="20"/>
  <Clothing id="coat" warmth="15"/>
  <Survival comfortMin="5" comfortMax="30" coreDrain="1.5" temperatureDrain="0.5" minRegenScale="0.2" staminaRegen="15" deadEyeDrain="20" deadEyeMinimum="10"/>
  <DeadEye level="2" timeScale="0.4" marks="3"/>
  <DeadEye level="1" timeScale="0.5" marks="0"/>
  <Animal id="deer" size="Medium" quality="3" pelt="1.5" carcass="3" decayHours="48" loot="deer_parts"/>
  <Animal id="rabbit" size="Small" quality="1" pelt="1" carcass="1"/>
  <HuntWeapon id="rifle" sizes="Medium,Large"/>
  <HuntWeapon id="varmint" sizes="Small"/>
  <HuntWeapon id="dynamite" ruinsPelt="true"/>
  <HitZone id="head" penalty="0"/>
  <HitZone id="body" penalty="1"/>
  <HitZone id="gut" penalty="5"/>
  <PeltGrade scales="0,0.3,0.6,1"/>
</WesternCatalog>)";

    constexpr float32 kWesternTolerance = 1.0e-3f;

    /** @brief 카탈로그만 읽어 둔 판입니다. 스위트마다 이것을 물려받습니다. */
    struct WesternCatalogBase
    {
        sw::WesternCatalog _catalog{};

        WesternCatalogBase() { SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kWesternXML, "WesternTest" ) ); }
    };

    /** @brief 지정한 목격자만 못 보는 시야입니다. */
    class WesternTestSight final : public sw::IWesternWitnessSight
    {
    public:
        sw::vector<uint64> _listBlind{};

        bool canWitnessSee( const sw::WesternWitness& witness, const sw::float3& crimePosition ) const override
        {
            (void)crimePosition;
            for ( const uint64 blindID : _listBlind )
            {
                if ( blindID == witness._id )
                    return false;
            }
            return true;
        }
    };

    struct WesternLawTest : WesternCatalogBase
    {
        sw::WesternLawState _law{};
        WesternTestSight    _sight{};
        sw::hashed_string   _region{ "lemoyne" };

        WesternLawTest() { _law.initialize( &_catalog ); }

        static sw::WesternWitness makeWitness( uint64 witnessID, bool bLawman )
        {
            sw::WesternWitness witness;
            witness._id      = witnessID;
            witness._bLawman = bLawman ? SW_TRUE : SW_FALSE;
            return witness;
        }

        uint32 commit( const sw::hashed_string& crimeID, const sw::vector<sw::WesternWitness>& listWitness, bool bMasked = false )
        {
            return _law.commitCrime( crimeID, _region, sw::float3{}, listWitness, _sight, bMasked );
        }

        /** @brief 쌓인 알림을 꺼내 @p kind 의 수를 셉니다. */
        int32 popCount( sw::WesternLawEvent::Kind kind )
        {
            sw::vector<sw::WesternLawEvent> listEvent;
            _law.pollEvents( listEvent );
            int32 count = 0;
            for ( const sw::WesternLawEvent& event : listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }

        bool readForgedRecords( float32 unseenTime, int32 bounty, int32 wantedLevel, bool bTwice )
        {
            sw::Archive written;
            written << static_cast<uint32>( bTwice ? 2 : 1 );
            for ( int32 record = 0; record < ( bTwice ? 2 : 1 ); ++record )
            {
                sw::StateIO::writeName( written, _region );
                written << unseenTime;
                written << bounty;
                written << wantedLevel;
                written << static_cast<uint8>( SW_FALSE );
            }
            written << static_cast<uint32>( 0 );
            written << static_cast<uint32>( 5 );
            written << static_cast<uint8>( SW_FALSE );
            sw::Archive reading( written.getData(), written.getSize() );
            return _law.readState( reading );
        }

        bool readForgedPending( uint32 incidentID, uint32 nextIncidentID )
        {
            sw::Archive written;
            written << static_cast<uint32>( 0 );
            written << static_cast<uint32>( 1 );
            sw::StateIO::writeName( written, "theft" );
            sw::StateIO::writeName( written, _region );
            written << static_cast<uint64>( 77 );
            written << incidentID;
            sw::StateIO::writeCountdown( written, sw::Countdown( 3.0f ) );
            written << static_cast<uint8>( SW_FALSE );
            written << nextIncidentID;
            written << static_cast<uint8>( SW_FALSE );
            sw::Archive reading( written.getData(), written.getSize() );
            return _law.readState( reading );
        }
    };

    struct WesternHorseTest : WesternCatalogBase
    {
        sw::WesternHorse _horse{};

        WesternHorseTest() { SW_REQUIRE_TRUE( _horse.initialize( &_catalog, "steady", 7 ) ); }

        int32 popCount( sw::WesternHorse& horse, sw::WesternHorseEvent::Kind kind )
        {
            sw::vector<sw::WesternHorseEvent> listEvent;
            horse.pollEvents( listEvent );
            int32 count = 0;
            for ( const sw::WesternHorseEvent& event : listEvent )
            {
                count += event._kind == kind ? 1 : 0;
            }
            return count;
        }

        bool readForged( const sw::hashed_string& horseID, float32 healthCore, float32 staminaCore, float32 hoursSinceBrush, const sw::vector<sw::hashed_string>& listAbility = {}, int32 bondLevel = 1 )
        {
            sw::GameRandom random;
            random.setSeed( 7 );
            sw::Archive written;
            sw::StateIO::writeName( written, horseID );
            written << static_cast<uint32>( listAbility.size() );
            for ( const sw::hashed_string& ability : listAbility )
            {
                sw::StateIO::writeName( written, ability );
            }
            _horse.getHealth().writeState( written );
            _horse.getStamina().writeState( written );
            sw::StateIO::writeRandom( written, random );
            written << healthCore;
            written << staminaCore;
            sw::LevelProgress bond;
            bond.setLevel( _catalog.getBondCurve(), bondLevel );
            bond.writeState( written );
            written << 0.0f;
            written << 0.0f;
            written << hoursSinceBrush;
            written << static_cast<uint8>( SW_FALSE );
            written << static_cast<uint8>( SW_FALSE );
            sw::Archive reading( written.getData(), written.getSize() );
            return _horse.readState( reading );
        }
    };

    struct WesternSurvivalTest : WesternCatalogBase
    {
        sw::WesternSurvival _survival{};

        WesternSurvivalTest() { SW_REQUIRE_TRUE( _survival.initialize( &_catalog ) ); }

        bool readForged( float32 healthCore, uint8 bDeadEyeActive, const sw::vector<uint64>& listMark = {}, int32 deadEyeLevel = 2 )
        {
            sw::Archive written;
            written << static_cast<uint32>( 0 );
            written << static_cast<uint32>( listMark.size() );
            for ( const uint64 targetID : listMark )
            {
                written << targetID;
            }
            _survival.getGauge( sw::WesternCore::Health ).writeState( written );
            written << healthCore;
            _survival.getGauge( sw::WesternCore::Stamina ).writeState( written );
            written << 100.0f;
            _survival.getGauge( sw::WesternCore::DeadEye ).writeState( written );
            written << 100.0f;
            written << deadEyeLevel;
            written << bDeadEyeActive;
            sw::Archive reading( written.getData(), written.getSize() );
            return _survival.readState( reading );
        }
    };

    struct WesternHuntingTest : WesternCatalogBase
    {
        int32 stars( const sw::hashed_string& animal, const sw::hashed_string& weapon, const sw::hashed_string& zone, int32 hitCount ) const
        {
            return sw::WesternCarcass::computePeltStars( _catalog, sw::WesternKill{ animal, weapon, zone, hitCount } );
        }

        sw::WesternCarcass carcass( int32 carcassStars, float32 ageHours ) const
        {
            sw::WesternCarcass made;
            made._animalID = "deer";
            made._stars    = carcassStars;
            made._ageHours = ageHours;
            return made;
        }
    };

    struct WesternHonorTest : WesternCatalogBase
    {
        sw::ReputationState _reputation{};
        sw::WesternHonor    _honor{};

        WesternHonorTest()
        {
            _reputation.initialize( &_catalog.getHonorReputation() );
            sw::SessionStateRefs refs;
            refs._pReputation = &_reputation;
            _honor.initialize( &_catalog, refs );
        }
    };
} // namespace

/**
 * @brief [WesternLawTest] 범죄 · 지역을 모르면 번호를 쓰지 않는다. 아무도 못 보면 번호만 붙고 현상금은 없다
 */
SW_TEST_CASE_F( WesternLawTest, CrimeNumbersAndUnwitnessedCrimes )
{
    SW_EXPECT_EQUAL( uint32( 0 ), commit( "nothing", {} ) );
    SW_EXPECT_EQUAL( uint32( 0 ), _law.commitCrime( "theft", "nowhere", sw::float3{}, {}, _sight, false ) );

    SW_EXPECT_EQUAL( uint32( 1 ), commit( "theft", {} ) );
    SW_EXPECT_EQUAL( 1, popCount( sw::WesternLawEvent::Kind::Unwitnessed ) );

    _sight._listBlind.push_back( 9 );
    SW_EXPECT_EQUAL( uint32( 2 ), commit( "theft", { makeWitness( 9, true ) } ) ); // 보안관이어도 못 보면 없는 일이다
    SW_EXPECT_EQUAL( 1, popCount( sw::WesternLawEvent::Kind::Unwitnessed ) );

    _law.update( 100.0f );
    SW_EXPECT_EQUAL( 0, _law.getBounty( _region ) );
    SW_EXPECT_EQUAL( 0, _law.getWantedLevel( _region ) );
}

/**
 * @brief [WesternLawTest] 시민은 신고 시간이 다 되어야 신고하고, 여럿이 보았어도 한 번만 오른다. 멈춘 시간에는 신고가 다가오지 않는다
 */
SW_TEST_CASE_F( WesternLawTest, CitizensReportOnceAfterReportTime )
{
    const uint32 incident = commit( "theft", { makeWitness( 1, false ), makeWitness( 2, false ) } );
    SW_EXPECT_EQUAL( uint32( 1 ), incident );
    SW_EXPECT_EQUAL( 2, _law.countPendingReports() );

    _law.update( 0.0f );
    _law.update( -5.0f );
    _law.update( 9.0f );
    SW_EXPECT_EQUAL( 0, _law.getBounty( _region ) );
    SW_EXPECT_EQUAL( 2, _law.countPendingReports() );

    _law.update( 1.0f ); // 정확히 다 된 걸음
    SW_EXPECT_EQUAL( 5, _law.getBounty( _region ) );
    SW_EXPECT_EQUAL( 1, _law.getWantedLevel( _region ) );
    SW_EXPECT_EQUAL( 0, _law.countPendingReports() );
    SW_EXPECT_EQUAL( 1, popCount( sw::WesternLawEvent::Kind::Reported ) );
}

/**
 * @brief [WesternLawTest] 보안관은 목록 어디에 있어도 그 자리에서 신고하고, 먼저 줄 선 시민의 신고는 지운다. 같은 범죄가 두 번 오르지 않는다
 */
SW_TEST_CASE_F( WesternLawTest, LawmanReportsImmediatelyAndClearsCitizens )
{
    _law.setDisguised( true );
    commit( "murder", { makeWitness( 1, false ), makeWitness( 2, true ), makeWitness( 3, false ) } );
    SW_EXPECT_EQUAL( 51, _law.getBounty( _region ) );
    SW_EXPECT_EQUAL( 2, _law.getWantedLevel( _region ) );
    SW_EXPECT_EQUAL( 0, _law.countPendingReports() );
    SW_EXPECT_FALSE( _law.isDisguised() ); // 새로 신고된 모습이다

    _law.update( 100.0f );
    SW_EXPECT_EQUAL( 51, _law.getBounty( _region ) );
}

/**
 * @brief [WesternLawTest] 복면 범죄의 현상금은 배율을 곱해 반올림한다. 수배가 없는 범죄는 수배를 올리지 않고, 신고 시간 0 은 다음 걸음에 신고한다
 */
SW_TEST_CASE_F( WesternLawTest, MaskedBountyAndWantedlessCrime )
{
    commit( "murder", { makeWitness( 1, true ) }, true );
    SW_EXPECT_EQUAL( 26, _law.getBounty( _region ) ); // 51 × 0.5 = 25.5

    sw::WesternLawState other;
    other.initialize( &_catalog );
    (void)other.commitCrime( "littering", _region, sw::float3{}, { makeWitness( 2, false ) }, _sight, false );
    SW_EXPECT_EQUAL( 1, other.countPendingReports() );
    other.update( 0.01f );
    SW_EXPECT_EQUAL( 1, other.getBounty( _region ) );
    SW_EXPECT_EQUAL( 0, other.getWantedLevel( _region ) );
}

/**
 * @brief [WesternLawTest] 목격자를 모두 입막음하면 없던 일이다. 하나만 막으면 나머지가 신고하고, 신고한 뒤에는 늦었다. 한 목격자가 본 사건 여럿이 함께 지워진다
 */
SW_TEST_CASE_F( WesternLawTest, SilencingWitnesses )
{
    commit( "theft", { makeWitness( 1, false ), makeWitness( 2, false ) } );
    (void)popCount( sw::WesternLawEvent::Kind::Witnessed );
    SW_EXPECT_TRUE( _law.silenceWitness( 1 ) );
    SW_EXPECT_EQUAL( 0, popCount( sw::WesternLawEvent::Kind::ReportPrevented ) );
    SW_EXPECT_TRUE( _law.silenceWitness( 2 ) );
    SW_EXPECT_EQUAL( 1, popCount( sw::WesternLawEvent::Kind::ReportPrevented ) );
    SW_EXPECT_FALSE( _law.silenceWitness( 2 ) );
    _law.update( 100.0f );
    SW_EXPECT_EQUAL( 0, _law.getBounty( _region ) );

    commit( "theft", { makeWitness( 3, false ), makeWitness( 4, false ) } );
    SW_EXPECT_TRUE( _law.silenceWitness( 3 ) );
    _law.update( 10.0f );
    SW_EXPECT_EQUAL( 5, _law.getBounty( _region ) );
    SW_EXPECT_FALSE( _law.silenceWitness( 4 ) ); // 이미 신고했다
    SW_EXPECT_EQUAL( 5, _law.getBounty( _region ) );

    commit( "theft", { makeWitness( 5, false ) } );
    commit( "theft", { makeWitness( 5, false ) } );
    (void)popCount( sw::WesternLawEvent::Kind::Witnessed );
    SW_EXPECT_TRUE( _law.silenceWitness( 5 ) );
    SW_EXPECT_EQUAL( 2, popCount( sw::WesternLawEvent::Kind::ReportPrevented ) );
    SW_EXPECT_EQUAL( 0, _law.countPendingReports() );
}

/**
 * @brief [WesternLawTest] 결함: 수배가 상한이면 오르지 않는데도 "수배가 올랐다" 를 알렸다. 상한에서는 현상금만 오르고 알리지 않는다
 */
SW_TEST_CASE_F( WesternLawTest, WantedStopsAtRegionMaxWithoutEvent )
{
    commit( "murder", { makeWitness( 1, true ) } );
    SW_EXPECT_EQUAL( 1, popCount( sw::WesternLawEvent::Kind::WantedRaised ) );
    commit( "murder", { makeWitness( 1, true ) } );
    SW_EXPECT_EQUAL( 3, _law.getWantedLevel( _region ) ); // 2 + 2 를 상한 3 으로
    SW_EXPECT_EQUAL( 1, popCount( sw::WesternLawEvent::Kind::WantedRaised ) );

    commit( "murder", { makeWitness( 1, true ) } );
    SW_EXPECT_EQUAL( 3, _law.getWantedLevel( _region ) );
    SW_EXPECT_EQUAL( 153, _law.getBounty( _region ) );
    SW_EXPECT_EQUAL( 0, popCount( sw::WesternLawEvent::Kind::WantedRaised ) );
}

/**
 * @brief [WesternLawTest] 수배는 안 보이는 시간만큼 식는다: 보이면 멈추고 처음부터 세며, 변장하면 빠르고, 긴 걸음은 여러 단계를 내린다. 추적 단계가 바뀔 때 알린다
 */
SW_TEST_CASE_F( WesternLawTest, WantedCoolsWhileUnseen )
{
    commit( "theft", { makeWitness( 1, true ) } );
    SW_REQUIRE_EQUAL( 1, _law.getWantedLevel( _region ) );

    _law.setSeenByLaw( _region, true );
    _law.update( 500.0f );
    SW_EXPECT_EQUAL( 1, _law.getWantedLevel( _region ) );

    _law.setSeenByLaw( _region, false );
    _law.update( 59.0f );
    SW_EXPECT_EQUAL( 1, _law.getWantedLevel( _region ) );
    _law.setSeenByLaw( _region, true ); // 다시 보이면 처음부터 센다
    _law.setSeenByLaw( _region, false );
    _law.update( 59.0f );
    SW_EXPECT_EQUAL( 1, _law.getWantedLevel( _region ) );

    _law.setDisguised( true );
    _law.update( 1.0f ); // 59 + 2 ≥ 60
    SW_EXPECT_EQUAL( 0, _law.getWantedLevel( _region ) );
    SW_EXPECT_TRUE( _law.findPursuit( _region ) == nullptr );

    sw::WesternLawState fresh;
    fresh.initialize( &_catalog );
    (void)fresh.commitCrime( "murder", _region, sw::float3{}, { makeWitness( 1, true ) }, _sight, false );
    (void)fresh.commitCrime( "murder", _region, sw::float3{}, { makeWitness( 1, true ) }, _sight, false );
    SW_REQUIRE_TRUE( fresh.findPursuit( _region ) != nullptr );
    SW_EXPECT_EQUAL( sw::hashed_string( "Manhunt" ), fresh.findPursuit( _region )->_name );
    sw::vector<sw::WesternLawEvent> listPolled;
    fresh.pollEvents( listPolled );
    listPolled.clear();

    fresh.update( 120.0f );
    SW_EXPECT_EQUAL( 1, fresh.getWantedLevel( _region ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "Search" ), fresh.findPursuit( _region )->_name );
    fresh.pollEvents( listPolled );
    int32 lowered = 0;
    int32 changed = 0;
    for ( const sw::WesternLawEvent& event : listPolled )
    {
        lowered += event._kind == sw::WesternLawEvent::Kind::WantedLowered ? 1 : 0;
        changed += event._kind == sw::WesternLawEvent::Kind::PursuitChanged ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 2, lowered );
    SW_EXPECT_EQUAL( 1, changed ); // 3 → 2 에서 Manhunt 가 Search 로 바뀌고, 2 → 1 은 그대로 Search 다
}

/**
 * @brief [WesternLawTest] 현상금 지불의 모든 갈래. 하루마다 줄되 0 아래로 가지 않는다
 */
SW_TEST_CASE_F( WesternLawTest, PayingBounty )
{
    sw::Wallet wallet;
    SW_EXPECT_EQUAL( sw::WesternPayResult::UnknownRegion, _law.payBounty( "nowhere", wallet ) );
    SW_EXPECT_EQUAL( sw::WesternPayResult::NoBounty, _law.payBounty( _region, wallet ) );

    commit( "theft", { makeWitness( 1, true ) } );
    SW_EXPECT_EQUAL( sw::WesternPayResult::ActivelyWanted, _law.payBounty( _region, wallet ) );
    _law.update( 60.0f );
    SW_REQUIRE_EQUAL( 0, _law.getWantedLevel( _region ) );

    wallet.add( "Dollar", 4 );
    SW_EXPECT_EQUAL( sw::WesternPayResult::NotEnoughMoney, _law.payBounty( _region, wallet ) );
    SW_EXPECT_EQUAL( int64( 4 ), wallet.getBalance( "Dollar" ) );
    SW_EXPECT_EQUAL( 5, _law.getBounty( _region ) );

    _law.advanceDay();
    SW_EXPECT_EQUAL( 4, _law.getBounty( _region ) );
    SW_EXPECT_EQUAL( sw::WesternPayResult::Ok, _law.payBounty( _region, wallet ) );
    SW_EXPECT_EQUAL( int64( 0 ), wallet.getBalance( "Dollar" ) );
    SW_EXPECT_EQUAL( 0, _law.getBounty( _region ) );
    SW_EXPECT_EQUAL( sw::WesternPayResult::NoBounty, _law.payBounty( _region, wallet ) );

    _law.advanceDay();
    SW_EXPECT_EQUAL( 0, _law.getBounty( _region ) );
}

/**
 * @brief [WesternLawTest] 결함: 모르는 지역을 `setSeenByLaw` 하면 기록이 생겨, 쓴 상태를 `readState` 가 통째로 거절했다(세이브 하나가 이름 하나에 막힌다). 왕복은 신고 대기까지 잇는다
 */
SW_TEST_CASE_F( WesternLawTest, UnknownRegionDoesNotPoisonState )
{
    _law.setSeenByLaw( "nowhere", true );
    commit( "theft", { makeWitness( 1, false ) } );
    _law.update( 4.0f );

    sw::Archive written;
    _law.writeState( written );
    sw::WesternLawState restored;
    restored.initialize( &_catalog );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_EXPECT_EQUAL( 1, restored.countPendingReports() );
    restored.update( 5.0f );
    SW_EXPECT_EQUAL( 0, restored.getBounty( _region ) );
    restored.update( 1.0f ); // 남은 시간을 이었다
    SW_EXPECT_EQUAL( 5, restored.getBounty( _region ) );
}

/**
 * @brief [WesternLawTest] 결함: NaN · 음수 안 보인 시간(수배가 영영 안 식는다), 같은 지역의 두 기록, 다음 사건 번호 밖의 신고 대기(새 사건과 한 사건이 된다)를 그대로 받았다 — 거절하고 상태를 둔다
 */
SW_TEST_CASE_F( WesternLawTest, ReadStateRefusesBrokenRecords )
{
    SW_REQUIRE_TRUE( readForgedRecords( 1.0f, 8, 2, false ) ); // 형식은 맞다
    SW_EXPECT_EQUAL( 8, _law.getBounty( _region ) );

    SW_EXPECT_FALSE( readForgedRecords( std::numeric_limits<float32>::quiet_NaN(), 9, 2, false ) );
    SW_EXPECT_FALSE( readForgedRecords( -1.0f, 9, 2, false ) );
    SW_EXPECT_FALSE( readForgedRecords( 1.0f, -9, 2, false ) );
    SW_EXPECT_FALSE( readForgedRecords( 1.0f, 9, -2, false ) );
    SW_EXPECT_FALSE( readForgedRecords( 1.0f, 9, 4, false ) ); // 지역의 최대 수배(3)를 넘는다
    SW_EXPECT_FALSE( readForgedRecords( 1.0f, 9, 2, true ) );
    SW_EXPECT_EQUAL( 8, _law.getBounty( _region ) );

    SW_EXPECT_TRUE( readForgedPending( 4, 5 ) );
    SW_EXPECT_EQUAL( 1, _law.countPendingReports() );
    SW_EXPECT_FALSE( readForgedPending( 5, 5 ) );
    SW_EXPECT_FALSE( readForgedPending( 0, 5 ) );
    SW_EXPECT_FALSE( readForgedPending( 1, 0 ) );
    SW_EXPECT_EQUAL( 1, _law.countPendingReports() );
}

/**
 * @brief [WesternHonorTest] 행동과 범죄가 값을 바꾸고 범위에서 자른 실제 변화를 돌려준다. 단계에 따라 값 배율과 대사 플래그가 바뀌고, 다른 단계의 플래그는 지워진다
 */
SW_TEST_CASE_F( WesternHonorTest, TierDrivesPriceAndFlags )
{
    sw::GameFlags flags;
    SW_EXPECT_EQUAL( sw::hashed_string( "Neutral" ), _honor.getTierName() );
    SW_EXPECT_NEAR( 1.0f, _honor.computePriceScale(), kWesternTolerance );

    SW_EXPECT_EQUAL( 0, _honor.applyAction( "nothing" ) );
    SW_EXPECT_EQUAL( 0, _honor.applyCrime( "nothing" ) );
    SW_EXPECT_EQUAL( -40, _honor.applyCrime( "murder" ) );
    SW_EXPECT_EQUAL( 300, _honor.applyAction( "help" ) );
    SW_EXPECT_EQUAL( 300, _honor.applyAction( "help" ) );
    SW_EXPECT_EQUAL( 560, _honor.getValue() );
    SW_EXPECT_EQUAL( sw::hashed_string( "Hero" ), _honor.getTierName() );
    SW_EXPECT_NEAR( 0.8f, _honor.computePriceScale(), kWesternTolerance );
    _honor.applyDialogueFlags( flags );
    SW_EXPECT_EQUAL( 1, flags.getFlag( "honor_high" ) );
    SW_EXPECT_FALSE( flags.hasFlag( "honor_low" ) );

    SW_EXPECT_EQUAL( -900, _honor.applyAction( "brand" ) );
    SW_EXPECT_EQUAL( sw::hashed_string( "Outlaw" ), _honor.getTierName() );
    SW_EXPECT_NEAR( 1.1f, _honor.computePriceScale(), kWesternTolerance ); // 음수 할인은 웃돈이다
    _honor.applyDialogueFlags( flags );
    SW_EXPECT_FALSE( flags.hasFlag( "honor_high" ) );
    SW_EXPECT_EQUAL( 1, flags.getFlag( "honor_low" ) );

    SW_EXPECT_EQUAL( 1340, _honor.changeValue( 99999 ) );
    SW_EXPECT_EQUAL( 1000, _honor.getValue() );
    SW_EXPECT_EQUAL( -2000, _honor.changeValue( -99999 ) );
    SW_EXPECT_EQUAL( -1000, _honor.getValue() );
}

/**
 * @brief [WesternHonorDetachedTest] 빌린 평판이 없어도 멀쩡하다(값 0, 단계 없음, 배율 1)
 */
SW_TEST_CASE( WesternHonorDetachedTest, WorksWithoutReputation )
{
    sw::WesternHonor honor;
    SW_EXPECT_EQUAL( 0, honor.changeValue( 10 ) );
    SW_EXPECT_EQUAL( 0, honor.getValue() );
    SW_EXPECT_TRUE( honor.getTierName().empty() );
    SW_EXPECT_NEAR( 1.0f, honor.computePriceScale(), kWesternTolerance );
    SW_EXPECT_EQUAL( 0, honor.applyAction( "help" ) );
}

/**
 * @brief [WesternHorseTest] 유대 경험치는 소수를 쌓아 단계를 올리고, 능력 · 최대치 보너스 · 알림을 낸다. 한 번에 여러 단계를 건너도 능력을 빠뜨리지 않고 상한에서 멈춘다
 */
SW_TEST_CASE_F( WesternHorseTest, BondLevelsUnlockAbilities )
{
    SW_EXPECT_EQUAL( 1, _horse.getBondLevel() );
    SW_EXPECT_FALSE( _horse.hasAbility( "rear" ) );

    _horse.addBondExperience( 0.0f );
    _horse.addBondExperience( -5.0f );
    SW_EXPECT_NEAR( 0.0f, _horse.getBondExperience(), kWesternTolerance );

    _horse.addBondExperience( 9.5f );
    SW_EXPECT_EQUAL( 1, _horse.getBondLevel() );
    SW_EXPECT_NEAR( 9.5f, _horse.getBondExperience(), kWesternTolerance );
    _horse.addBondExperience( 0.5f );
    SW_EXPECT_EQUAL( 2, _horse.getBondLevel() );
    SW_EXPECT_TRUE( _horse.hasAbility( "rear" ) );
    SW_EXPECT_NEAR( 110.0f, _horse.getStamina().getMax(), kWesternTolerance );
    SW_EXPECT_EQUAL( 1, popCount( _horse, sw::WesternHorseEvent::Kind::AbilityUnlocked ) );

    _horse.addBondExperience( 1000.0f );
    SW_EXPECT_EQUAL( 3, _horse.getBondLevel() );
    SW_EXPECT_TRUE( _horse.hasAbility( "rear" ) );
    SW_EXPECT_TRUE( _horse.hasAbility( "drift" ) );
    SW_EXPECT_TRUE( _horse.hasAbility( "piaffe" ) );
    SW_EXPECT_NEAR( 120.0f, _horse.getStamina().getMax(), kWesternTolerance ); // 보너스는 그 단계의 값이고 쌓이지 않는다
    SW_EXPECT_NEAR( 105.0f, _horse.getHealth().getMax(), kWesternTolerance );
    SW_EXPECT_EQUAL( 2, popCount( _horse, sw::WesternHorseEvent::Kind::AbilityUnlocked ) );

    _horse.addBondExperience( 1000.0f );
    SW_EXPECT_EQUAL( 3, _horse.getBondLevel() );
}

/**
 * @brief [WesternHorseTest] 탄 시간은 소수 경험치로 쌓이고, 내리면 멈춘다. 손질은 대기 시간이 지나야 한다(첫 손질은 바로 된다). 먹이의 유대가 없으면 기본값이다
 */
SW_TEST_CASE_F( WesternHorseTest, BondSourcesFollowTheirRules )
{
    _horse.update( 5.0f, 0.0f );
    SW_EXPECT_NEAR( 0.0f, _horse.getBondExperience(), kWesternTolerance );
    _horse.setRidden( true );
    _horse.update( 5.0f, 0.0f );
    SW_EXPECT_NEAR( 0.5f, _horse.getBondExperience(), kWesternTolerance );
    _horse.update( 0.0f, 1.0f ); // 게임 시간만 흐르면 탄 경험치는 없다
    SW_EXPECT_NEAR( 0.5f, _horse.getBondExperience(), kWesternTolerance );
    _horse.setRidden( false );

    SW_EXPECT_TRUE( _horse.brush() );
    SW_EXPECT_NEAR( 15.5f, _horse.getBondExperience(), kWesternTolerance );
    SW_EXPECT_FALSE( _horse.brush() );
    _horse.update( 0.0f, 5.0f );
    SW_EXPECT_FALSE( _horse.brush() );
    _horse.update( 0.0f, 1.0f );
    SW_EXPECT_TRUE( _horse.brush() );
    SW_EXPECT_NEAR( 30.5f, _horse.getBondExperience(), kWesternTolerance );

    _horse.addBondExperience( 100.0f ); // 최고 단계에서는 더 쌓이지 않는다
    SW_EXPECT_NEAR( 30.5f, _horse.getBondExperience(), kWesternTolerance );

    sw::WesternHorse fed;
    SW_REQUIRE_TRUE( fed.initialize( &_catalog, "steady", 1 ) );
    SW_EXPECT_TRUE( fed.feed( "hay" ) );
    SW_EXPECT_NEAR( 5.0f, fed.getBondExperience(), kWesternTolerance );
    SW_EXPECT_TRUE( fed.feed( "apple" ) );
    SW_EXPECT_NEAR( 15.0f, fed.getBondExperience(), kWesternTolerance );
    SW_EXPECT_FALSE( fed.feed( "nothing" ) );
}

/**
 * @brief [WesternHorseTest] 코어는 게임 시간으로 줄어 바닥에 멈추고 먹이로 차되 최대를 넘지 않는다. 회복 배율은 코어에 비례하고 범위 밖 코어를 자른다
 */
SW_TEST_CASE_F( WesternHorseTest, CoresDrainFeedAndScaleRegen )
{
    _horse.update( 0.0f, 20.0f );
    SW_EXPECT_NEAR( 60.0f, _horse.getHealthCore(), kWesternTolerance );
    _horse.update( 0.0f, -5.0f );
    SW_EXPECT_NEAR( 60.0f, _horse.getHealthCore(), kWesternTolerance );

    _horse.takeDamage( 30.0f );
    SW_EXPECT_TRUE( _horse.feed( "hay" ) );
    SW_EXPECT_NEAR( 85.0f, _horse.getHealthCore(), kWesternTolerance );
    SW_EXPECT_NEAR( 75.0f, _horse.getStaminaCore(), kWesternTolerance );
    SW_EXPECT_NEAR( 80.0f, _horse.getHealth().getValue(), kWesternTolerance );
    SW_EXPECT_TRUE( _horse.feed( "hay" ) );
    SW_EXPECT_NEAR( 100.0f, _horse.getHealthCore(), kWesternTolerance );

    _horse.update( 0.0f, 1000.0f );
    SW_EXPECT_NEAR( 0.0f, _horse.getHealthCore(), kWesternTolerance );

    SW_EXPECT_NEAR( 0.2f, _horse.computeRegenScale( 0.0f ), kWesternTolerance );
    SW_EXPECT_NEAR( 0.6f, _horse.computeRegenScale( 50.0f ), kWesternTolerance );
    SW_EXPECT_NEAR( 1.0f, _horse.computeRegenScale( 100.0f ), kWesternTolerance );
    SW_EXPECT_NEAR( 1.0f, _horse.computeRegenScale( 150.0f ), kWesternTolerance );
    SW_EXPECT_NEAR( 0.2f, _horse.computeRegenScale( -10.0f ), kWesternTolerance );
}

/**
 * @brief [WesternHorseTest] 질주한 걸음은 코어가 더 줄고 다음 걸음에는 풀린다. 바닥나면 한 번 알리고 탈진한다. 결함: 탈진해 달리지 못한 걸음도 질주로 쳐 코어가 더 줄었다
 */
SW_TEST_CASE_F( WesternHorseTest, GallopDrainsAndExhausts )
{
    SW_EXPECT_TRUE( _horse.gallop( 1.0f ) );
    SW_EXPECT_NEAR( 88.0f, _horse.getStamina().getValue(), kWesternTolerance );
    _horse.update( 0.0f, 1.0f );
    SW_EXPECT_NEAR( 92.0f, _horse.getStaminaCore(), kWesternTolerance ); // 2 + 6
    _horse.update( 0.0f, 1.0f );
    SW_EXPECT_NEAR( 90.0f, _horse.getStaminaCore(), kWesternTolerance ); // 2

    SW_EXPECT_FALSE( _horse.gallop( 10.0f ) );
    SW_EXPECT_TRUE( _horse.getStamina().isExhausted() );
    SW_EXPECT_EQUAL( 1, popCount( _horse, sw::WesternHorseEvent::Kind::Exhausted ) );
    _horse.update( 0.0f, 1.0f );
    SW_EXPECT_NEAR( 82.0f, _horse.getStaminaCore(), kWesternTolerance ); // 바닥에 닿은 걸음까지는 달렸다

    SW_EXPECT_FALSE( _horse.gallop( 1.0f ) );
    SW_EXPECT_EQUAL( 0, popCount( _horse, sw::WesternHorseEvent::Kind::Exhausted ) );
    _horse.update( 0.0f, 1.0f );
    SW_EXPECT_NEAR( 80.0f, _horse.getStaminaCore(), kWesternTolerance ); // 달리지 못한 걸음
    SW_EXPECT_TRUE( _horse.getStamina().isExhausted() );

    _horse.update( 10.0f, 0.0f );
    SW_EXPECT_FALSE( _horse.getStamina().isExhausted() );
    SW_EXPECT_TRUE( _horse.gallop( 0.1f ) );
}

/**
 * @brief [WesternHorseTest] 겁은 용기와 유대만큼 덜 쌓이고 시간에 식는다. 한계를 넘으면 지워지며, 탄 사람이 없으면 떨어뜨릴 수 없고 떨어뜨리면 내린 상태다
 */
SW_TEST_CASE_F( WesternHorseTest, FearBuildsAndThrowsRider )
{
    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Calm, _horse.frighten( 0.0f ) );
    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Calm, _horse.frighten( -1.0f ) );
    SW_EXPECT_NEAR( 0.0f, _horse.getFear(), kWesternTolerance );

    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Calm, _horse.frighten( 1.0f ) ); // 용기 0.5
    SW_EXPECT_NEAR( 0.5f, _horse.getFear(), kWesternTolerance );
    _horse.update( 1.0f, 0.0f );
    SW_EXPECT_NEAR( 0.25f, _horse.getFear(), kWesternTolerance );
    _horse.calm();
    SW_EXPECT_NEAR( 0.0f, _horse.getFear(), kWesternTolerance );
    SW_EXPECT_NEAR( 5.0f, _horse.getBondExperience(), kWesternTolerance );

    _horse.addBondExperience( 100.0f ); // 3 단계 — 저항 0.5 + 0.3
    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Calm, _horse.frighten( 1.0f ) );
    SW_EXPECT_NEAR( 0.2f, _horse.getFear(), kWesternTolerance );

    sw::WesternHorse wild;
    SW_REQUIRE_TRUE( wild.initialize( &_catalog, "wild", 3 ) );
    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Rearing, wild.frighten( 1.0f ) ); // 탄 사람이 없다
    SW_EXPECT_NEAR( 0.0f, wild.getFear(), kWesternTolerance );
    SW_EXPECT_EQUAL( 1, popCount( wild, sw::WesternHorseEvent::Kind::Spooked ) );
    wild.setRidden( true );
    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Bucked, wild.frighten( 1.0f ) );
    SW_EXPECT_EQUAL( 1, popCount( wild, sw::WesternHorseEvent::Kind::ThrewRider ) );
    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Rearing, wild.frighten( 1.0f ) ); // 이미 떨어뜨렸다

    sw::WesternHorse tame;
    SW_REQUIRE_TRUE( tame.initialize( &_catalog, "steady", 3 ) );
    tame.setRidden( true );
    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Calm, tame.frighten( 1.0f ) );
    SW_EXPECT_EQUAL( sw::WesternHorseReaction::Rearing, tame.frighten( 1.0f ) ); // 확률 0 이면 떨어뜨리지 않는다
}

/**
 * @brief [WesternHorseTest] 탔는지와 상관없이 난수를 하나씩 써서 같은 시드의 수열이 어긋나지 않는다
 */
SW_TEST_CASE_F( WesternHorseTest, RandomStreamIgnoresRider )
{
    sw::WesternHorse ridden;
    sw::WesternHorse empty;
    SW_REQUIRE_TRUE( ridden.initialize( &_catalog, "coin", 11 ) );
    SW_REQUIRE_TRUE( empty.initialize( &_catalog, "coin", 11 ) );
    for ( int32 fright = 0; fright < 8; ++fright )
    {
        ridden.setRidden( true );
        (void)ridden.frighten( 1.0f );
        (void)empty.frighten( 1.0f );
    }
    ridden.setRidden( true );
    empty.setRidden( true );
    for ( int32 fright = 0; fright < 16; ++fright )
    {
        SW_EXPECT_EQUAL( ridden.frighten( 1.0f ), empty.frighten( 1.0f ) );
        ridden.setRidden( true );
        empty.setRidden( true );
    }
}

/**
 * @brief [WesternHorseTest] 품종이 다른 말에 읽어도 그 품종으로 바뀌고 유대 · 능력 · 난수 수열을 잇는다. 모르는 품종은 거절한다
 */
SW_TEST_CASE_F( WesternHorseTest, StateRoundTripsAcrossBreeds )
{
    sw::WesternHorse coin;
    SW_REQUIRE_TRUE( coin.initialize( &_catalog, "coin", 5 ) );
    coin.addBondExperience( 12.5f );
    coin.setRidden( true );

    sw::Archive written;
    coin.writeState( written );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( _horse.readState( reading ) );

    SW_EXPECT_EQUAL( sw::hashed_string( "coin" ), _horse.getDef()->_id );
    SW_EXPECT_EQUAL( 2, _horse.getBondLevel() );
    SW_EXPECT_TRUE( _horse.hasAbility( "rear" ) );
    SW_EXPECT_NEAR( 12.5f, _horse.getBondExperience(), kWesternTolerance );
    SW_EXPECT_NEAR( 110.0f, _horse.getStamina().getMax(), kWesternTolerance );
    for ( int32 fright = 0; fright < 8; ++fright )
    {
        SW_EXPECT_EQUAL( coin.frighten( 1.0f ), _horse.frighten( 1.0f ) );
    }
    SW_EXPECT_FALSE( readForged( "nothing", 50.0f, 50.0f, 0.0f ) );
}

/**
 * @brief [WesternHorseTest] 결함: 코어와 손질 시간은 범위를 보지 않아 NaN 코어(회복 배율이 영영 NaN) · 음수 손질 시간을 그대로 받았다 — 거절하고 상태를 둔다
 */
SW_TEST_CASE_F( WesternHorseTest, ReadStateRefusesOutOfRangeValues )
{
    SW_REQUIRE_TRUE( readForged( "steady", 50.0f, 60.0f, 1.0f ) ); // 형식은 맞다
    SW_EXPECT_NEAR( 50.0f, _horse.getHealthCore(), kWesternTolerance );

    const float32 nan = std::numeric_limits<float32>::quiet_NaN();
    SW_EXPECT_FALSE( readForged( "steady", nan, 60.0f, 1.0f ) );
    SW_EXPECT_FALSE( readForged( "steady", -1.0f, 60.0f, 1.0f ) );
    SW_EXPECT_FALSE( readForged( "steady", 100.5f, 60.0f, 1.0f ) );
    SW_EXPECT_FALSE( readForged( "steady", 50.0f, nan, 1.0f ) );
    SW_EXPECT_FALSE( readForged( "steady", 50.0f, -1.0f, 1.0f ) );
    SW_EXPECT_FALSE( readForged( "steady", 50.0f, 60.0f, nan ) );
    SW_EXPECT_FALSE( readForged( "steady", 50.0f, 60.0f, -1.0f ) );
    SW_EXPECT_NEAR( 50.0f, _horse.getHealthCore(), kWesternTolerance );
    SW_EXPECT_NEAR( 60.0f, _horse.getStaminaCore(), kWesternTolerance );
}

/**
 * @brief [WesternHorseTest] 결함: 능력 목록을 그대로 받아 유대 없이 능력을 열 수 있었다. 겹친 능력, 모르는 능력, 지금 단계보다 뒤에 열리는 능력을 거절하고 상태를 둔다
 */
SW_TEST_CASE_F( WesternHorseTest, ReadStateRefusesAbilitiesNotUnlocked )
{
    SW_REQUIRE_TRUE( readForged( "steady", 50.0f, 60.0f, 1.0f, { "rear" }, 2 ) ); // 형식은 맞다
    SW_EXPECT_TRUE( _horse.hasAbility( "rear" ) );

    SW_EXPECT_FALSE( readForged( "steady", 50.0f, 60.0f, 1.0f, { "rear", "rear" }, 2 ) );
    SW_EXPECT_FALSE( readForged( "steady", 50.0f, 60.0f, 1.0f, { "fly" }, 2 ) );
    SW_EXPECT_FALSE( readForged( "steady", 50.0f, 60.0f, 1.0f, { "drift" }, 2 ) ); // 3 단계에서 열린다
    SW_EXPECT_FALSE( readForged( "steady", 50.0f, 60.0f, 1.0f, { "rear" }, 1 ) );
    SW_EXPECT_TRUE( _horse.hasAbility( "rear" ) );
    SW_EXPECT_FALSE( _horse.hasAbility( "drift" ) );
}

/**
 * @brief [WesternSurvivalTest] 편한 범위 안(경계 포함)에서는 기본 감소만, 추우면 체력 코어가, 더우면 스태미나 코어가 벗어난 만큼 더 준다. 옷은 체감 온도를 올린다. 바닥 아래로 가지 않는다
 */
SW_TEST_CASE_F( WesternSurvivalTest, TemperatureDrainsTheRightCore )
{
    _survival.update( 0.0f, 2.0f, 5.0f ); // 편한 범위의 아래 경계
    SW_EXPECT_NEAR( 97.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
    SW_EXPECT_NEAR( 97.0f, _survival.getCore( sw::WesternCore::Stamina ), kWesternTolerance );
    _survival.update( 0.0f, 2.0f, 30.0f ); // 위 경계
    SW_EXPECT_NEAR( 94.0f, _survival.getCore( sw::WesternCore::Stamina ), kWesternTolerance );
    SW_EXPECT_NEAR( 94.0f, _survival.getCore( sw::WesternCore::DeadEye ), kWesternTolerance );

    _survival.update( 0.0f, 2.0f, 0.0f ); // 5 도 춥다 — 체력만 5 더
    SW_EXPECT_NEAR( 86.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
    SW_EXPECT_NEAR( 91.0f, _survival.getCore( sw::WesternCore::Stamina ), kWesternTolerance );
    _survival.update( 0.0f, 2.0f, 40.0f ); // 10 도 덥다 — 스태미나만 10 더
    SW_EXPECT_NEAR( 83.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
    SW_EXPECT_NEAR( 78.0f, _survival.getCore( sw::WesternCore::Stamina ), kWesternTolerance );

    SW_EXPECT_FALSE( _survival.setClothing( { "coat", "nothing" } ) );
    SW_EXPECT_NEAR( 0.0f, _survival.computeWarmth(), kWesternTolerance ); // 바뀌지 않았다
    SW_REQUIRE_TRUE( _survival.setClothing( { "coat" } ) );
    SW_EXPECT_NEAR( 15.0f, _survival.computeWarmth(), kWesternTolerance );
    SW_EXPECT_NEAR( 5.0f, _survival.computeFeltTemperature( -10.0f ), kWesternTolerance );
    _survival.update( 0.0f, 2.0f, -10.0f ); // 옷 덕분에 춥지 않다
    SW_EXPECT_NEAR( 80.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );

    _survival.update( 0.0f, -2.0f, -10.0f );
    SW_EXPECT_NEAR( 80.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
    _survival.update( 0.0f, 9999.0f, -10.0f );
    SW_EXPECT_NEAR( 0.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
}

/**
 * @brief [WesternSurvivalTest] 먹으면 코어가 범위 안에서 차고 게이지가 바로 찬다. 게이지 회복 속도는 코어에 비례한다
 */
SW_TEST_CASE_F( WesternSurvivalTest, EatingAndRegenScale )
{
    SW_EXPECT_FALSE( _survival.eat( "nothing" ) );
    _survival.setCore( sw::WesternCore::Health, 50.0f );
    _survival.setCore( sw::WesternCore::Stamina, 80.0f );
    _survival.setCore( sw::WesternCore::DeadEye, 0.0f );
    (void)_survival.getGauge( sw::WesternCore::Health ).reduce( 30.0f );
    SW_EXPECT_TRUE( _survival.eat( "stew" ) );
    SW_EXPECT_NEAR( 80.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
    SW_EXPECT_NEAR( 100.0f, _survival.getCore( sw::WesternCore::Stamina ), kWesternTolerance ); // 120 을 자른다
    SW_EXPECT_NEAR( 50.0f, _survival.getCore( sw::WesternCore::DeadEye ), kWesternTolerance );
    SW_EXPECT_NEAR( 80.0f, _survival.getGauge( sw::WesternCore::Health ).getValue(), kWesternTolerance );

    _survival.setCore( sw::WesternCore::Health, -5.0f );
    SW_EXPECT_NEAR( 0.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
    _survival.setCore( sw::WesternCore::Health, 500.0f );
    SW_EXPECT_NEAR( 100.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );

    _survival.setCore( sw::WesternCore::Stamina, 0.0f );
    SW_EXPECT_NEAR( 0.2f, _survival.computeRegenScale( sw::WesternCore::Stamina ), kWesternTolerance );
    (void)_survival.getGauge( sw::WesternCore::Stamina ).reduce( 50.0f );
    _survival.update( 2.0f, 0.0f, 20.0f ); // 지연 1 초 뒤 1 초 × 15 × 0.2
    SW_EXPECT_NEAR( 53.0f, _survival.getGauge( sw::WesternCore::Stamina ).getValue(), kWesternTolerance );
}

/**
 * @brief [WesternSurvivalTest] 데드아이: 최소량(같아도 된다) · 겹쳐 켜기 · 단계의 표시 개수와 시간 배율. 켠 동안은 차지 않고, 바닥나면 저절로 꺼져 표시를 지운다
 */
SW_TEST_CASE_F( WesternSurvivalTest, DeadEyeLifecycle )
{
    SW_EXPECT_NEAR( 1.0f, _survival.getTimeScale(), kWesternTolerance );
    SW_EXPECT_FALSE( _survival.markTarget( 1 ) ); // 꺼져 있다

    SW_REQUIRE_TRUE( _survival.activateDeadEye() );
    SW_EXPECT_FALSE( _survival.activateDeadEye() );
    SW_EXPECT_NEAR( 0.5f, _survival.getTimeScale(), kWesternTolerance );
    SW_EXPECT_FALSE( _survival.markTarget( 1 ) ); // 1 단계는 표시가 없다

    _survival.setDeadEyeLevel( 99 ); // 그 단계 이하에서 가장 높은 것
    SW_EXPECT_NEAR( 0.4f, _survival.getTimeScale(), kWesternTolerance );
    SW_EXPECT_EQUAL( 3, _survival.getMarkLimit() );
    SW_EXPECT_TRUE( _survival.markTarget( 1 ) );
    SW_EXPECT_TRUE( _survival.markTarget( 1 ) ); // 같은 대상에 또 표시해도 된다
    SW_EXPECT_TRUE( _survival.markTarget( 2 ) );
    SW_EXPECT_FALSE( _survival.markTarget( 3 ) );

    _survival.update( 1.0f, 0.0f, 20.0f );
    SW_EXPECT_NEAR( 80.0f, _survival.getGauge( sw::WesternCore::DeadEye ).getValue(), kWesternTolerance );
    SW_EXPECT_TRUE( _survival.isDeadEyeActive() );

    _survival.update( 10.0f, 0.0f, 20.0f );
    SW_EXPECT_FALSE( _survival.isDeadEyeActive() );
    SW_EXPECT_TRUE( _survival.getMarks().empty() );
    SW_EXPECT_NEAR( 1.0f, _survival.getTimeScale(), kWesternTolerance );

    _survival.setDeadEyeLevel( 0 );
    SW_EXPECT_EQUAL( 0, _survival.getMarkLimit() );
}

/**
 * @brief [WesternSurvivalTest] 게이지가 최소량보다 적으면 켜지 못하고, 최소량과 같으면 켠다
 */
SW_TEST_CASE_F( WesternSurvivalTest, DeadEyeNeedsMinimum )
{
    sw::ResourceGauge& gauge = _survival.getGauge( sw::WesternCore::DeadEye );
    (void)gauge.reduce( 95.0f );
    SW_EXPECT_FALSE( _survival.activateDeadEye() );
    gauge.restore( 5.0f );
    SW_EXPECT_TRUE( _survival.activateDeadEye() );
}

/**
 * @brief [WesternSurvivalTest] 카탈로그 없는 시작은 오류로 멈춘다. 상태는 범위 밖 코어와 깨진 깃발을 거절하고 그대로 둔다
 */
SW_TEST_CASE_F( WesternSurvivalTest, InitializeAndStateChecks )
{
    SW_EXPECT_LOG( "WesternSurvival.WesternSurvivalRefusedNoCatalog" );
    sw::WesternSurvival empty;
    SW_EXPECT_FALSE( empty.initialize( nullptr ) );

    SW_REQUIRE_TRUE( readForged( 40.0f, SW_TRUE ) ); // 형식은 맞다
    SW_EXPECT_NEAR( 40.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
    SW_EXPECT_TRUE( _survival.isDeadEyeActive() );

    SW_EXPECT_FALSE( readForged( std::numeric_limits<float32>::quiet_NaN(), SW_FALSE ) );
    SW_EXPECT_FALSE( readForged( -1.0f, SW_FALSE ) );
    SW_EXPECT_FALSE( readForged( 101.0f, SW_FALSE ) );
    SW_EXPECT_FALSE( readForged( 50.0f, 2 ) );
    SW_EXPECT_FALSE( readForged( 50.0f, SW_TRUE, {}, -1 ) );         // 음수 단계
    SW_EXPECT_FALSE( readForged( 50.0f, SW_FALSE, { 1 } ) );         // 꺼져 있는데 표시가 있다
    SW_EXPECT_FALSE( readForged( 50.0f, SW_TRUE, { 1, 2, 3, 4 } ) ); // 2 단계는 셋까지다
    SW_EXPECT_FALSE( readForged( 50.0f, SW_TRUE, { 1 }, 1 ) );       // 1 단계는 표시가 없다
    SW_EXPECT_NEAR( 40.0f, _survival.getCore( sw::WesternCore::Health ), kWesternTolerance );
    SW_EXPECT_TRUE( _survival.isDeadEyeActive() );
    SW_EXPECT_TRUE( _survival.getMarks().empty() );

    SW_REQUIRE_TRUE( readForged( 50.0f, SW_TRUE, { 1, 1, 2 } ) ); // 같은 대상에 또 표시해도 된다
    SW_EXPECT_EQUAL( size_t( 3 ), _survival.getMarks().size() );
}

/**
 * @brief [WesternHuntingTest] 가죽 등급 계산: 크기에 안 맞는 무기 · 부위 · 더 쏜 발 · 폭발물 · 모르는 이름, 0 과 품질로 자르기
 */
SW_TEST_CASE_F( WesternHuntingTest, PeltStarsFromKill )
{
    SW_EXPECT_EQUAL( 3, stars( "deer", "rifle", "head", 1 ) );
    SW_EXPECT_EQUAL( 2, stars( "deer", "varmint", "head", 1 ) ); // 크기가 안 맞는다
    SW_EXPECT_EQUAL( 2, stars( "deer", "rifle", "body", 1 ) );
    SW_EXPECT_EQUAL( 1, stars( "deer", "varmint", "body", 1 ) );
    SW_EXPECT_EQUAL( 1, stars( "deer", "rifle", "head", 3 ) );
    SW_EXPECT_EQUAL( 0, stars( "deer", "rifle", "gut", 1 ) ); // 음수로 가지 않는다
    SW_EXPECT_EQUAL( 0, stars( "deer", "dynamite", "head", 1 ) );
    SW_EXPECT_EQUAL( 3, stars( "deer", "nothing", "nothing", 1 ) ); // 모르는 무기 · 부위는 벌점이 없다
    SW_EXPECT_EQUAL( 3, stars( "deer", "rifle", "head", 0 ) );      // 맞힌 발이 없어도 등급은 품질을 넘지 않는다
    SW_EXPECT_EQUAL( 3, stars( "deer", "rifle", "head", -9 ) );
    SW_EXPECT_EQUAL( 0, stars( "nothing", "rifle", "head", 1 ) );
    SW_EXPECT_EQUAL( 1, stars( "rabbit", "varmint", "head", 1 ) ); // 품질 1
    SW_EXPECT_EQUAL( 0, stars( "rabbit", "rifle", "head", 1 ) );

    const sw::WesternCarcass made = sw::WesternCarcass::makeCarcass( _catalog, sw::WesternKill{ "deer", "rifle", "body", 1 } );
    SW_EXPECT_EQUAL( 2, made._stars );
    SW_EXPECT_EQUAL( sw::hashed_string( "deer" ), made._animalID );
}

/**
 * @brief [WesternHuntingTest] 사체는 부패 시간의 반에서(같아도) 한 등급 떨어지고 다 되면(같아도) 썩는다. 시간은 되돌리지 못한다
 */
SW_TEST_CASE_F( WesternHuntingTest, CarcassDecayBoundaries )
{
    sw::WesternCarcass fresh = carcass( 3, 0.0f );
    sw::WesternCarcass::ageCarcass( fresh, -100.0f );
    SW_EXPECT_NEAR( 0.0f, fresh._ageHours, kWesternTolerance );
    sw::WesternCarcass::ageCarcass( fresh, 23.5f );
    SW_EXPECT_EQUAL( 3, sw::WesternCarcass::computeCarcassStars( _catalog, fresh ) );

    SW_EXPECT_EQUAL( 2, sw::WesternCarcass::computeCarcassStars( _catalog, carcass( 3, 24.0f ) ) );
    SW_EXPECT_EQUAL( 0, sw::WesternCarcass::computeCarcassStars( _catalog, carcass( 0, 24.0f ) ) ); // 음수로 가지 않는다
    SW_EXPECT_FALSE( sw::WesternCarcass::isRotten( _catalog, carcass( 3, 47.5f ) ) );
    SW_EXPECT_TRUE( sw::WesternCarcass::isRotten( _catalog, carcass( 3, 48.0f ) ) );
    SW_EXPECT_EQUAL( 0, sw::WesternCarcass::computeCarcassStars( _catalog, carcass( 3, 48.0f ) ) );

    sw::WesternCarcass unknown;
    unknown._animalID = "nothing";
    SW_EXPECT_TRUE( sw::WesternCarcass::isRotten( _catalog, unknown ) );
}

/**
 * @brief [WesternHuntingTest] 벗기기: 지금 등급의 가죽과 전리품을 주고, 다시 벗기거나 썩은 것을 벗기면 아무것도 내지 않는다
 */
SW_TEST_CASE_F( WesternHuntingTest, SkinningOnceWithLoot )
{
    sw::LootCatalog       loot;
    constexpr const utf8* kLootXML = R"(<LootCatalog><Table id="deer_parts"><Entry item="meat" weight="1"/></Table></LootCatalog>)";
    SW_REQUIRE_TRUE( loot.loadFromXMLText( kLootXML, "WesternTest" ) );
    sw::GameRandom random;
    random.setSeed( 3 );
    sw::ItemStackList items;
    sw::WesternPelt   pelt;

    sw::WesternCarcass aged = carcass( 3, 30.0f );
    SW_REQUIRE_TRUE( sw::WesternCarcass::skin( _catalog, aged, &loot, random, pelt, items ) );
    SW_EXPECT_EQUAL( 2, pelt._stars );
    SW_EXPECT_EQUAL( sw::hashed_string( "deer" ), pelt._animalID );
    SW_EXPECT_EQUAL( 1, items.getItemCount( "meat" ) );
    SW_EXPECT_TRUE( aged._bSkinned != SW_FALSE );

    SW_EXPECT_FALSE( sw::WesternCarcass::skin( _catalog, aged, &loot, random, pelt, items ) );
    SW_EXPECT_EQUAL( 1, items.getItemCount( "meat" ) );

    sw::WesternCarcass rotten = carcass( 3, 48.0f );
    SW_EXPECT_FALSE( sw::WesternCarcass::skin( _catalog, rotten, &loot, random, pelt, items ) );
    SW_EXPECT_TRUE( rotten._bSkinned == SW_FALSE );

    sw::WesternCarcass noLoot = carcass( 1, 0.0f );
    SW_EXPECT_TRUE( sw::WesternCarcass::skin( _catalog, noLoot, nullptr, random, pelt, items ) );
    SW_EXPECT_EQUAL( 1, items.getItemCount( "meat" ) );
}

/**
 * @brief [WesternHuntingTest] 값(센트): 가죽은 등급 배율을 곱하고 범위 밖 등급을 자른다. 사체는 고기 값이 있어 0 성도 1 성 값이며 벗기면 절반, 썩으면 0 이다
 */
SW_TEST_CASE_F( WesternHuntingTest, PricesInCents )
{
    SW_EXPECT_EQUAL( 150, sw::WesternCarcass::computePeltPrice( _catalog, sw::WesternPelt{ "deer", 3 } ) );
    SW_EXPECT_EQUAL( 90, sw::WesternCarcass::computePeltPrice( _catalog, sw::WesternPelt{ "deer", 2 } ) );
    SW_EXPECT_EQUAL( 45, sw::WesternCarcass::computePeltPrice( _catalog, sw::WesternPelt{ "deer", 1 } ) );
    SW_EXPECT_EQUAL( 0, sw::WesternCarcass::computePeltPrice( _catalog, sw::WesternPelt{ "deer", 0 } ) );
    SW_EXPECT_EQUAL( 0, sw::WesternCarcass::computePeltPrice( _catalog, sw::WesternPelt{ "deer", -1 } ) );
    SW_EXPECT_EQUAL( 150, sw::WesternCarcass::computePeltPrice( _catalog, sw::WesternPelt{ "deer", 9 } ) );
    SW_EXPECT_EQUAL( 0, sw::WesternCarcass::computePeltPrice( _catalog, sw::WesternPelt{ "nothing", 3 } ) );

    SW_EXPECT_EQUAL( 300, sw::WesternCarcass::computeCarcassPrice( _catalog, carcass( 3, 0.0f ) ) );
    SW_EXPECT_EQUAL( 180, sw::WesternCarcass::computeCarcassPrice( _catalog, carcass( 3, 24.0f ) ) );
    SW_EXPECT_EQUAL( 90, sw::WesternCarcass::computeCarcassPrice( _catalog, carcass( 0, 0.0f ) ) );
    sw::WesternCarcass skinned = carcass( 3, 0.0f );
    skinned._bSkinned          = SW_TRUE;
    SW_EXPECT_EQUAL( 150, sw::WesternCarcass::computeCarcassPrice( _catalog, skinned ) );
    SW_EXPECT_EQUAL( 0, sw::WesternCarcass::computeCarcassPrice( _catalog, carcass( 3, 48.0f ) ) );
}

/**
 * @brief [WesternCatalogBase] 단계표는 적은 순서와 상관없이 그 단계 이하에서 가장 높은 것을 찾는다. 0 이하의 수배는 추적이 없다
 */
SW_TEST_CASE_F( WesternCatalogBase, LevelTablesAreSorted )
{
    SW_EXPECT_TRUE( _catalog.findPursuit( 0 ) == nullptr );
    SW_EXPECT_TRUE( _catalog.findPursuit( -1 ) == nullptr );
    SW_REQUIRE_TRUE( _catalog.findPursuit( 2 ) != nullptr );
    SW_EXPECT_EQUAL( sw::hashed_string( "Search" ), _catalog.findPursuit( 2 )->_name );
    SW_EXPECT_EQUAL( sw::hashed_string( "Manhunt" ), _catalog.findPursuit( 9 )->_name );
    SW_EXPECT_TRUE( _catalog.findDeadEyeLevel( 0 ) == nullptr );
    SW_EXPECT_EQUAL( 1, _catalog.findDeadEyeLevel( 1 )->_level );
    SW_EXPECT_EQUAL( 2, _catalog.findDeadEyeLevel( 2 )->_level );
    SW_EXPECT_NEAR( 0.0f, _catalog.getGradeScale( -1 ), kWesternTolerance );
    SW_EXPECT_NEAR( 1.0f, _catalog.getGradeScale( 9 ), kWesternTolerance );
    SW_EXPECT_EQUAL( 3, _catalog.getBondCurve().getMaxLevel() );
}

/**
 * @brief [WesternCatalogTest] 결함: 뒤집힌 명예 범위도 세력을 만들어 값을 뒤집힌 범위로 잘랐다 — 넣지 않는다. 이름 없는 단계도 찾기에서 빈 이름에 걸렸다 — 필수다
 */
SW_TEST_CASE( WesternCatalogTest, BrokenHonorIsNotAdded )
{
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    sw::WesternCatalog    inverted;
    constexpr const utf8* kInvertedXML = R"(<WesternCatalog><Honor min="5" max="1"/><Crime id="theft"/></WesternCatalog>)";
    SW_REQUIRE_TRUE( inverted.loadFromXMLText( kInvertedXML, "WesternTest" ) );
    SW_EXPECT_TRUE( inverted.getHonorReputation().find( sw::hashed_string( sw::WesternCatalog::kHonorFactionID ) ) == nullptr );
}

/**
 * @brief [WesternCatalogTest] 이름 없는 명예 단계는 넣지 않고 알린다. 다른 단계와 세력은 남는다
 */
SW_TEST_CASE( WesternCatalogTest, HonorTierNeedsName )
{
    SW_EXPECT_LOG( "XMLFields.RequiredFieldMissing" );
    sw::WesternCatalog    catalog;
    constexpr const utf8* kNamelessXML = R"(<WesternCatalog><Honor><Tier min="0"/><Tier name="Known" min="10"/></Honor><Crime id="theft"/></WesternCatalog>)";
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kNamelessXML, "WesternTest" ) );
    SW_EXPECT_TRUE( catalog.findHonorTier( sw::hashed_string{} ) == nullptr );
    SW_EXPECT_TRUE( catalog.findHonorTier( "Known" ) != nullptr );
    const sw::FactionDef* pHonor = catalog.getHonorReputation().find( sw::hashed_string( sw::WesternCatalog::kHonorFactionID ) );
    SW_REQUIRE_TRUE( pHonor != nullptr );
    SW_EXPECT_EQUAL( size_t( 1 ), pHonor->_listTier.size() );
}

/**
 * @brief [WesternCatalogTest] 결함: 음수 가죽 배율은 음수 값을, 오르지 않는 유대 경험치는 조용히 1 칸을 만들었다 — 오류로 알린다. 모자란 배율 · 모르는 크기 · 모르는 원소도 알리고 표는 그대로다
 */
SW_TEST_CASE( WesternCatalogTest, ReportsBrokenTables )
{
    SW_EXPECT_LOG( "WesternCatalog.PeltGradeScalesNeedsExactly" );
    SW_EXPECT_LOG( "WesternCatalog.PeltGradeScalesNegative" );
    SW_EXPECT_LOG( "WesternCatalog.HuntWeaponSizesUnknownSize" );
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "WesternCatalog.BondExperienceNotRising" );
    sw::WesternCatalog    catalog;
    constexpr const utf8* kBrokenXML = R"(<WesternCatalog>
  <PeltGrade scales="0,0.3,0.6"/>
  <PeltGrade scales="0,-0.3,0.6,1"/>
  <HuntWeapon id="tiny" sizes="Tiny"/>
  <Bond level="1" xp="50"/>
  <Bond level="2" xp="50"/>
  <Mystery/>
  <Crime id="theft"/>
</WesternCatalog>)";
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kBrokenXML, "WesternTest" ) );
    SW_EXPECT_NEAR( 0.3f, catalog.getGradeScale( 1 ), kWesternTolerance );
    SW_EXPECT_TRUE( catalog.findHuntWeapon( "tiny" ) == nullptr );
    SW_EXPECT_EQUAL( size_t( 2 ), catalog.getBondLevels().size() );
    SW_EXPECT_TRUE( catalog.findCrime( "theft" ) != nullptr );
}
