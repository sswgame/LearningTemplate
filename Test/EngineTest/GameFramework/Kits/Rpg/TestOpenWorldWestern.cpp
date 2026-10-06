#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Inventory/ItemStackList.h"
#include "GameFramework/Base/Inventory/LootTable.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/Base/World/GameFlags.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternCatalog.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternHonor.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternHorse.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternHunting.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternLaw.h"
#include "GameFramework/Kits/Rpg/OpenWorldWestern/WesternSurvival.h"

#include "TestFramework/TestFramework.h"

// 오픈월드 서부극 키트 — 카탈로그, 목격자 신고 · 처치로 막기 · 복면, 수배 감쇠 · 변장 · 추적 단계 · 현상금 지불, 명예 단계 · 할인 · 대사 플래그,
// 말 유대 · 능력 해금 · 코어에 따른 회복 · 질주 탈진 · 겁, 플레이어 코어의 추위 · 옷 · 음식과 데드아이, 가죽 등급 · 부패 · 매입 값.

using namespace sw;

namespace
{
    constexpr const utf8* kWesternTestXml = R"(
<WesternCatalog currency="Dollar" extraHitPenalty="1">
  <Crime id="murder" bounty="50" wanted="2" honor="-40" reportTime="20"/>
  <Crime id="theft" bounty="10" wanted="1" honor="-5" reportTime="10"/>
  <Region id="lemoyne" wantedCooldown="30" disguiseScale="3" maskedBountyScale="0.5" bountyDecayPerDay="5" maxWanted="3"/>
  <Pursuit level="1" name="Search" lawmen="2"/>
  <Pursuit level="3" name="Manhunt" lawmen="8" shootOnSight="true" bountyHunter="true"/>
  <Honor min="-1000" max="1000" start="0">
    <Tier name="Outlaw" min="-1000" discount="-0.1" flag="honor_low"/>
    <Tier name="Neutral" min="-200" flag="honor_neutral"/>
    <Tier name="Honorable" min="300" discount="0.3" flag="honor_high"/>
    <Action id="help_stranger" delta="200"/>
  </Honor>
  <Horse id="arabian" health="100" stamina="100" courage="0.2" gallopDrain="20" coreDrain="2" gallopCoreDrain="8"/>
  <Bond level="1" xp="0"/>
  <Bond level="2" xp="20" unlocks="rear" stamina="10"/>
  <Bond level="3" xp="50" unlocks="drift,skid" stamina="20" fearResist="0.8"/>
  <BondExperience ride="1" brush="15" feed="10" calm="5" brushCooldown="6"/>
  <Food id="hay" healthCore="25" staminaCore="50" bond="15"/>
  <Food id="steak" healthCore="40" staminaCore="10" deadEyeCore="10" health="20"/>
  <Clothing id="shirt" warmth="2"/>
  <Clothing id="sheepskin_coat" warmth="15"/>
  <Survival comfortMin="5" comfortMax="30" coreDrain="1" temperatureDrain="1" minRegenScale="0.2" deadEyeDrain="25" deadEyeMinimum="10"/>
  <DeadEye level="1" timeScale="0.5" marks="0"/>
  <DeadEye level="2" timeScale="0.35" marks="3"/>
  <Animal id="rabbit" size="Small" quality="3" pelt="0.5" carcass="1" decayHours="24"/>
  <Animal id="deer" size="Medium" quality="3" pelt="1.5" carcass="3" decayHours="48" loot="deer_parts"/>
  <Animal id="old_buck" size="Medium" quality="2" pelt="1.5" carcass="3" decayHours="48"/>
  <HuntWeapon id="varmint_rifle" sizes="Small"/>
  <HuntWeapon id="bow" sizes="Small,Medium,Large"/>
  <HuntWeapon id="dynamite" ruinsPelt="true"/>
  <HitZone id="head" penalty="0"/>
  <HitZone id="body" penalty="1"/>
  <PeltGrade scales="0,0.3,0.6,1"/>
</WesternCatalog>
)";

    /** @brief 시험용 시야 — id 목록에 든 목격자만 본다. */
    class ListedSight : public IWesternWitnessSight
    {
    public:
        explicit ListedSight( vector<uint64> listSeeing )
            : _listSeeing{ listSeeing }
        {
        }

        bool canWitnessSee( const WesternWitness& witness, const float3& crimePosition ) const override
        {
            (void)crimePosition;
            for ( uint64 id : _listSeeing )
            {
                if ( id == witness._id )
                    return true;
            }
            return false;
        }

    private:
        vector<uint64> _listSeeing;
    };

    WesternWitness makeWitness( uint64 id, bool bLawman, float32 x = 0.0f, float32 z = 0.0f )
    {
        WesternWitness witness;
        witness._id       = id;
        witness._bLawman  = bLawman ? SW_TRUE : SW_FALSE;
        witness._position = float3{ x, 0.0f, z };
        return witness;
    }

    int32 countLawEvents( const vector<WesternLawEvent>& listEvent, WesternLawEvent::Kind kind )
    {
        int32 count = 0;
        for ( const WesternLawEvent& event : listEvent )
            count += event._kind == kind ? 1 : 0;
        return count;
    }

    /** @brief 상태 하나의 바이트입니다. */
    template <typename TState>
    vector<uint8> captureWesternBytes( const TState& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }
} // namespace

SW_TEST_CASE( OpenWorldWesternTest, CatalogReadsLawHorseSurvivalAndHunting )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );
    SW_EXPECT_TRUE( catalog.getCurrency() == hashed_string( "Dollar" ) );
    const WesternCrimeDef* pMurder = catalog.findCrime( hashed_string( "murder" ) );
    SW_ASSERT_NOT_NULL( pMurder );
    SW_EXPECT_EQUAL( 50, pMurder->_bounty );
    SW_EXPECT_EQUAL( -40, pMurder->_honor );
    SW_EXPECT_TRUE( catalog.findPursuit( 0 ) == nullptr );
    SW_EXPECT_TRUE( catalog.findPursuit( 2 )->_name == hashed_string( "Search" ) );
    SW_EXPECT_TRUE( catalog.findPursuit( 3 )->_bShootOnSight != SW_FALSE );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( catalog.getBondLevels().size() ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( catalog.getBondLevels()[2]._listUnlock.size() ) );
    SW_EXPECT_TRUE( catalog.findAnimal( hashed_string( "rabbit" ) )->_size == WesternAnimalSize::Small );
    SW_EXPECT_TRUE( catalog.findHuntWeapon( hashed_string( "dynamite" ) )->_bRuinsPelt != SW_FALSE );
    SW_EXPECT_EQUAL( 3, catalog.findDeadEyeLevel( 5 )->_markCount );
    SW_EXPECT_NEAR_EQUAL( 0.6f, catalog.getGradeScale( 2 ), 1.0e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, catalog.getGradeScale( 0 ), 1.0e-5f );
    SW_EXPECT_TRUE( catalog.getHonorReputation().findFaction( hashed_string( WesternCatalog::kHonorFactionId ) ) != nullptr );
}

SW_TEST_CASE( OpenWorldWesternTest, WitnessesReportAfterDelayUnlessSilencedAndLawmenReportAtOnce )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );
    WesternLawState law;
    law.initialize( &catalog );
    const hashed_string    region( "lemoyne" );
    vector<WesternWitness> listCandidate{ makeWitness( 1, false ), makeWitness( 2, false ), makeWitness( 3, false ) };

    // 아무도 못 보았다 — 현상금이 붙지 않는다.
    const ListedSight blind( vector<uint64>{} );
    SW_EXPECT_TRUE( law.commitCrime( hashed_string( "theft" ), region, float3{}, listCandidate, blind, false ) > 0 );
    law.update( 60.0f );
    SW_EXPECT_EQUAL( 0, law.getBounty( region ) );

    // 둘이 보았다 — 신고 시간 전에 둘 다 처치하면 신고되지 않는다.
    const ListedSight twoSee( vector<uint64>{ 1, 2 } );
    (void)law.commitCrime( hashed_string( "murder" ), region, float3{}, listCandidate, twoSee, false );
    SW_EXPECT_EQUAL( 2, law.countPendingReports() );
    law.update( 10.0f );
    SW_EXPECT_TRUE( law.silenceWitness( 1 ) );
    SW_EXPECT_FALSE( law.silenceWitness( 3 ) ); // 보지 않은 사람
    law.update( 5.0f );
    SW_EXPECT_TRUE( law.silenceWitness( 2 ) );
    law.update( 60.0f );
    SW_EXPECT_EQUAL( 0, law.getBounty( region ) );
    vector<WesternLawEvent> listEvent;
    law.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countLawEvents( listEvent, WesternLawEvent::Kind::ReportPrevented ) );
    SW_EXPECT_EQUAL( 0, countLawEvents( listEvent, WesternLawEvent::Kind::Reported ) );

    // 한 명만 남기면 신고 시간(20 초)이 지나야 한 번 신고된다 — 경계 바로 앞에서는 아직이다.
    (void)law.commitCrime( hashed_string( "murder" ), region, float3{}, listCandidate, twoSee, false );
    SW_EXPECT_TRUE( law.silenceWitness( 1 ) );
    law.update( 19.9f );
    SW_EXPECT_EQUAL( 0, law.getBounty( region ) );
    law.update( 0.2f );
    SW_EXPECT_EQUAL( 50, law.getBounty( region ) );
    SW_EXPECT_EQUAL( 2, law.getWantedLevel( region ) );
    SW_EXPECT_FALSE( law.silenceWitness( 2 ) ); // 이미 신고했다
    listEvent.clear();
    law.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 1, countLawEvents( listEvent, WesternLawEvent::Kind::Reported ) );

    // 보안관이 보면 그 자리에서 신고 — 복면이면 현상금이 반, 수배는 최대(3)에서 멈춘다.
    vector<WesternWitness> listLawman{ makeWitness( 7, false ), makeWitness( 9, true ) };
    const ListedSight      lawSees( vector<uint64>{ 7, 9 } );
    (void)law.commitCrime( hashed_string( "murder" ), region, float3{}, listLawman, lawSees, true );
    SW_EXPECT_EQUAL( 75, law.getBounty( region ) );
    SW_EXPECT_EQUAL( 3, law.getWantedLevel( region ) );
    SW_EXPECT_EQUAL( 0, law.countPendingReports() ); // 같은 사건의 시민 신고 대기는 지워졌다
    SW_EXPECT_EQUAL( 8, law.findPursuit( region )->_lawmen );
    SW_EXPECT_EQUAL( 0, law.commitCrime( hashed_string( "arson" ), region, float3{}, listLawman, lawSees, false ) ); // 모르는 범죄
}

SW_TEST_CASE( OpenWorldWesternTest, WantedCoolsWhenUnseenFasterInDisguiseAndBountyIsPaidAfterwards )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );
    const hashed_string    region( "lemoyne" );
    const ListedSight      lawSees( vector<uint64>{ 9 } );
    vector<WesternWitness> listLawman{ makeWitness( 9, true ) };

    // 같은 일을 변장 없이 · 변장하고 — 변장한 쪽이 먼저 따돌린다.
    float32 arrClearTime[2] = { 0.0f, 0.0f };
    for ( int32 trial = 0; trial < 2; ++trial )
    {
        WesternLawState law;
        law.initialize( &catalog );
        (void)law.commitCrime( hashed_string( "murder" ), region, float3{}, listLawman, lawSees, false );
        SW_EXPECT_EQUAL( 2, law.getWantedLevel( region ) );
        law.setSeenByLaw( region, true );
        law.update( 100.0f ); // 보이는 동안은 식지 않는다
        SW_EXPECT_EQUAL( 2, law.getWantedLevel( region ) );
        law.setSeenByLaw( region, false );
        law.setDisguised( trial == 1 );
        float32 elapsed = 0.0f;
        while ( law.getWantedLevel( region ) > 0 && elapsed < 1000.0f )
        {
            law.update( 1.0f );
            elapsed += 1.0f;
        }
        arrClearTime[trial] = elapsed;

        // 쫓기는 중에는 못 내고, 돈이 모자라도 못 낸다.
        Wallet wallet;
        wallet.add( hashed_string( "Dollar" ), 30 );
        SW_EXPECT_TRUE( law.payBounty( region, wallet ) == WesternPayResult::NotEnoughMoney );
        law.advanceDay(); // 하루 5 씩 준다
        SW_EXPECT_EQUAL( 45, law.getBounty( region ) );
        wallet.add( hashed_string( "Dollar" ), 20 );
        SW_EXPECT_TRUE( law.payBounty( region, wallet ) == WesternPayResult::Ok );
        SW_EXPECT_EQUAL( 5, static_cast<int32>( wallet.getBalance( hashed_string( "Dollar" ) ) ) );
        SW_EXPECT_EQUAL( 0, law.getBounty( region ) );
        SW_EXPECT_TRUE( law.payBounty( region, wallet ) == WesternPayResult::NoBounty );
    }
    SW_EXPECT_NEAR_EQUAL( 60.0f, arrClearTime[0], 1.0e-3f ); // 30 초마다 한 단계
    SW_EXPECT_NEAR_EQUAL( 20.0f, arrClearTime[1], 1.0e-3f ); // 변장 3 배

    WesternLawState wanted;
    wanted.initialize( &catalog );
    (void)wanted.commitCrime( hashed_string( "theft" ), region, float3{}, listLawman, lawSees, false );
    Wallet rich;
    rich.add( hashed_string( "Dollar" ), 1000 );
    SW_EXPECT_TRUE( wanted.payBounty( region, rich ) == WesternPayResult::ActivelyWanted );
    SW_EXPECT_TRUE( wanted.payBounty( hashed_string( "nowhere" ), rich ) == WesternPayResult::UnknownRegion );
}

SW_TEST_CASE( OpenWorldWesternTest, HonorTiersGiveDiscountsAndDialogueFlags )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );
    ReputationState reputation; // 공유 평판 — 그 카탈로그에 명예 세력이 있어야 단계가 선다
    reputation.initialize( &catalog.getHonorReputation() );
    GameStateRefs refs;
    refs._pReputation = &reputation;
    WesternHonor honor;
    honor.initialize( &catalog, refs );
    GameFlags flags;
    SW_EXPECT_EQUAL( 0, honor.getValue() );
    SW_EXPECT_TRUE( honor.getTierName() == hashed_string( "Neutral" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0f, honor.computePriceScale(), 1.0e-5f );
    honor.applyDialogueFlags( flags );
    SW_EXPECT_TRUE( flags.evaluate( "honor_neutral && !honor_high" ) );

    SW_EXPECT_EQUAL( 200, honor.applyAction( hashed_string( "help_stranger" ) ) );
    SW_EXPECT_EQUAL( 0, honor.applyAction( hashed_string( "unknown" ) ) );
    SW_EXPECT_TRUE( honor.getTierName() == hashed_string( "Neutral" ) ); // 300 바로 아래
    SW_EXPECT_EQUAL( 200, honor.applyAction( hashed_string( "help_stranger" ) ) );
    SW_EXPECT_TRUE( honor.getTierName() == hashed_string( "Honorable" ) );
    SW_EXPECT_NEAR_EQUAL( 0.7f, honor.computePriceScale(), 1.0e-5f );
    honor.applyDialogueFlags( flags );
    SW_EXPECT_TRUE( flags.evaluate( "honor_high && !honor_neutral" ) );

    for ( int32 index = 0; index < 20; ++index )
        (void)honor.applyCrime( hashed_string( "murder" ) );
    SW_EXPECT_EQUAL( -400, honor.getValue() );
    SW_EXPECT_TRUE( honor.getTierName() == hashed_string( "Outlaw" ) );
    SW_EXPECT_NEAR_EQUAL( 1.1f, honor.computePriceScale(), 1.0e-5f ); // 무법자는 웃돈
    vector<ReputationEvent> listEvent;
    reputation.drainEvents( listEvent );                          // 단계 알림은 빌린 평판에 쌓인다(게임 화면이 꺼낸다)
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listEvent.size() ) ); // 중립 → 명예 → 중립 → 무법
}

/**
 * @brief [OpenWorldWesternTest] 유대 단계는 카탈로그의 누적 경험치 문턱에서 오른다 — 타기의 소수 경험치는 버려지지 않고 쌓인다, 한 번에 두 단계를 넘으면 둘 다 열린다,
 *        세이브 왕복 뒤에도 같은 걸음에서 오른다
 */
SW_TEST_CASE( OpenWorldWesternTest, HorseBondLevelsAtTheCatalogThresholds )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );
    WesternHorse horse;
    SW_ASSERT_TRUE( horse.initialize( &catalog, hashed_string( "arabian" ), 7u ) );
    horse.setRidden( true ); // 타면 초당 1
    for ( int32 step = 0; step < 39; ++step )
        horse.update( 0.5f, 0.0f ); // 19.5
    SW_EXPECT_EQUAL( 1, horse.getBondLevel() );
    horse.update( 0.5f, 0.0f ); // 20 — 단계 2 문턱
    SW_EXPECT_EQUAL( 2, horse.getBondLevel() );
    for ( int32 step = 0; step < 59; ++step )
        horse.update( 0.25f, 0.0f ); // 34.75
    SW_EXPECT_EQUAL( 2, horse.getBondLevel() );

    Archive archive;
    horse.writeState( archive );
    vector<uint8> bytes;
    archive.writeData( bytes );
    WesternHorse restored;
    SW_ASSERT_TRUE( restored.initialize( &catalog, hashed_string( "arabian" ), 7u ) );
    Archive reader( bytes.data(), bytes.size() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    restored.setRidden( true );
    for ( int32 step = 0; step < 60; ++step )
        restored.update( 0.25f, 0.0f ); // 49.75
    SW_EXPECT_EQUAL( 2, restored.getBondLevel() );
    restored.update( 0.25f, 0.0f ); // 50 — 단계 3 문턱
    SW_EXPECT_EQUAL( 3, restored.getBondLevel() );

    // 한 번에 두 단계 — 둘 다의 능력이 열린다
    WesternHorse fresh;
    SW_ASSERT_TRUE( fresh.initialize( &catalog, hashed_string( "arabian" ), 7u ) );
    fresh.addBondExperience( 60.0f );
    SW_EXPECT_EQUAL( 3, fresh.getBondLevel() );
    SW_EXPECT_TRUE( fresh.hasAbility( hashed_string( "rear" ) ) && fresh.hasAbility( hashed_string( "drift" ) ) );
}

SW_TEST_CASE( OpenWorldWesternTest, HorseBondUnlocksAbilitiesCoresSlowRegenAndFearIsDeterministic )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );
    WesternHorse horse;
    SW_EXPECT_FALSE( horse.initialize( &catalog, hashed_string( "unicorn" ), 1u ) );
    SW_ASSERT_TRUE( horse.initialize( &catalog, hashed_string( "arabian" ), 7u ) );
    SW_EXPECT_EQUAL( 1, horse.getBondLevel() );
    SW_EXPECT_FALSE( horse.hasAbility( hashed_string( "rear" ) ) );

    // 유대: 손질은 대기 시간마다 한 번, 타면 초당, 먹이는 그 먹이의 몫.
    SW_EXPECT_TRUE( horse.brush() );
    SW_EXPECT_FALSE( horse.brush() );
    horse.setRidden( true );
    horse.update( 5.0f, 0.0f );
    SW_EXPECT_EQUAL( 2, horse.getBondLevel() );
    SW_EXPECT_TRUE( horse.hasAbility( hashed_string( "rear" ) ) );
    SW_EXPECT_NEAR_EQUAL( 110.0f, horse.getStamina().getMax(), 1.0e-3f ); // 단계 보너스
    horse.update( 0.0f, 6.0f );
    SW_EXPECT_TRUE( horse.brush() );
    SW_EXPECT_TRUE( horse.feed( hashed_string( "hay" ) ) );
    SW_EXPECT_FALSE( horse.feed( hashed_string( "rock" ) ) );
    SW_EXPECT_EQUAL( 3, horse.getBondLevel() );
    SW_EXPECT_TRUE( horse.hasAbility( hashed_string( "drift" ) ) && horse.hasAbility( hashed_string( "skid" ) ) );

    // 질주로 바닥내면 탈진, 코어가 낮으면 회복이 느리다.
    int32 frames = 0;
    while ( horse.gallop( 0.1f ) && frames < 1000 )
        ++frames;
    SW_EXPECT_TRUE( horse.getStamina().isExhausted() );
    vector<WesternHorseEvent> listEvent;
    horse.drainEvents( listEvent );
    int32 exhausted = 0;
    for ( const WesternHorseEvent& event : listEvent )
        exhausted += event._kind == WesternHorseEvent::Kind::Exhausted ? 1 : 0;
    SW_EXPECT_EQUAL( 1, exhausted );

    WesternHorse starved;
    SW_ASSERT_TRUE( starved.initialize( &catalog, hashed_string( "arabian" ), 7u ) );
    WesternHorse fed;
    SW_ASSERT_TRUE( fed.initialize( &catalog, hashed_string( "arabian" ), 7u ) );
    starved.update( 0.0f, 50.0f ); // 코어 0
    SW_EXPECT_NEAR_EQUAL( 0.0f, starved.getStaminaCore(), 1.0e-3f );
    for ( WesternHorse* pHorse : { &starved, &fed } )
    {
        (void)pHorse->gallop( 2.0f );
        pHorse->update( 3.0f, 0.0f );
    }
    SW_EXPECT_TRUE( fed.getStamina().getValue() > starved.getStamina().getValue() + 10.0f );
    SW_EXPECT_TRUE( starved.feed( hashed_string( "hay" ) ) );
    SW_EXPECT_NEAR_EQUAL( 50.0f, starved.getStaminaCore(), 1.0e-3f );

    // 겁: 같은 씨앗이면 같은 반응. 유대 3 단계(저항 0.2 + 0.8)는 겁먹지 않는다.
    vector<WesternHorseReaction> arrReaction[2];
    for ( int32 trial = 0; trial < 2; ++trial )
    {
        WesternHorse rookie;
        SW_ASSERT_TRUE( rookie.initialize( &catalog, hashed_string( "arabian" ), 99u ) );
        for ( int32 index = 0; index < 12; ++index )
        {
            rookie.setRidden( true );
            arrReaction[trial].push_back( rookie.frighten( 1.5f ) );
        }
    }
    int32 bucked = 0;
    for ( size_t index = 0; index < arrReaction[0].size(); ++index )
    {
        SW_EXPECT_TRUE( arrReaction[0][index] == arrReaction[1][index] );
        SW_EXPECT_TRUE( arrReaction[0][index] != WesternHorseReaction::Calm );
        bucked += arrReaction[0][index] == WesternHorseReaction::Bucked ? 1 : 0;
    }
    SW_EXPECT_TRUE( bucked > 0 && bucked < 12 );
    SW_EXPECT_TRUE( horse.frighten( 5.0f ) == WesternHorseReaction::Calm );
}

SW_TEST_CASE( OpenWorldWesternTest, PlayerCoresDrainWithColdUnlessDressedAndDeadEyeSlowsAndMarks )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );
    WesternSurvival naked;
    naked.initialize( &catalog );
    WesternSurvival dressed;
    dressed.initialize( &catalog );
    dressed.setClothing( vector<hashed_string>{ hashed_string( "shirt" ), hashed_string( "sheepskin_coat" ) } );
    SW_EXPECT_NEAR_EQUAL( 17.0f, dressed.computeWarmth(), 1.0e-4f );

    // 기온 −5 도에서 10 시간: 편한 범위(5)보다 10 도 낮다 — 옷이 없으면 체력 코어가 시간당 1 + 10 씩 준다.
    naked.update( 0.0f, 10.0f, -5.0f );
    dressed.update( 0.0f, 10.0f, -5.0f ); // 체감 12 도 — 기본 감소만
    SW_EXPECT_NEAR_EQUAL( 0.0f, naked.getCore( WesternCore::Health ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 90.0f, naked.getCore( WesternCore::Stamina ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 90.0f, dressed.getCore( WesternCore::Health ), 1.0e-3f );
    // 더위는 스태미나 코어.
    dressed.update( 0.0f, 1.0f, 20.0f ); // 체감 37 도 — 7 도 넘쳤다
    SW_EXPECT_NEAR_EQUAL( 82.0f, dressed.getCore( WesternCore::Stamina ), 1.0e-3f );

    // 음식은 코어를 채우고 범위를 넘지 않는다.
    SW_EXPECT_TRUE( naked.eat( hashed_string( "steak" ) ) );
    SW_EXPECT_FALSE( naked.eat( hashed_string( "air" ) ) );
    SW_EXPECT_NEAR_EQUAL( 40.0f, naked.getCore( WesternCore::Health ), 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, naked.getCore( WesternCore::Stamina ), 1.0e-3f );

    // 데드아이: 1 단계는 느려지기만, 2 단계는 셋까지 표시. 바닥나면 꺼진다.
    WesternSurvival shooter;
    shooter.initialize( &catalog );
    SW_EXPECT_NEAR_EQUAL( 1.0f, shooter.getTimeScale(), 1.0e-5f );
    SW_EXPECT_TRUE( shooter.activateDeadEye() );
    SW_EXPECT_FALSE( shooter.activateDeadEye() );
    SW_EXPECT_NEAR_EQUAL( 0.5f, shooter.getTimeScale(), 1.0e-5f );
    SW_EXPECT_FALSE( shooter.markTarget( 1 ) );
    shooter.deactivateDeadEye();
    shooter.setDeadEyeLevel( 2 );
    SW_EXPECT_TRUE( shooter.activateDeadEye() );
    SW_EXPECT_NEAR_EQUAL( 0.35f, shooter.getTimeScale(), 1.0e-5f );
    SW_EXPECT_TRUE( shooter.markTarget( 1 ) && shooter.markTarget( 2 ) && shooter.markTarget( 2 ) );
    SW_EXPECT_FALSE( shooter.markTarget( 3 ) );
    for ( int32 index = 0; index < 50 && shooter.isDeadEyeActive(); ++index )
        shooter.update( 0.1f, 0.0f, 20.0f );
    SW_EXPECT_FALSE( shooter.isDeadEyeActive() ); // 100 / 초당 25 → 4 초
    SW_EXPECT_FALSE( shooter.activateDeadEye() ); // 최소량 아래
    SW_EXPECT_NEAR_EQUAL( 1.0f, shooter.getTimeScale(), 1.0e-5f );
}

SW_TEST_CASE( OpenWorldWesternTest, PeltStarsDependOnWeaponZoneAndHitsAndCarcassesRot )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );
    const auto stars = [&]( const utf8* pAnimal, const utf8* pWeapon, const utf8* pZone, int32 hits )
    {
        WesternKill kill;
        kill._animalId = hashed_string( pAnimal );
        kill._weaponId = hashed_string( pWeapon );
        kill._zoneId   = hashed_string( pZone );
        kill._hitCount = hits;
        return WesternHunting::computePeltStars( catalog, kill );
    };
    SW_EXPECT_EQUAL( 3, stars( "deer", "bow", "head", 1 ) );
    SW_EXPECT_EQUAL( 2, stars( "deer", "varmint_rifle", "head", 1 ) ); // 크기에 맞지 않는 무기
    SW_EXPECT_EQUAL( 3, stars( "rabbit", "varmint_rifle", "head", 1 ) );
    SW_EXPECT_EQUAL( 2, stars( "deer", "bow", "body", 1 ) );
    SW_EXPECT_EQUAL( 1, stars( "deer", "bow", "body", 2 ) );
    SW_EXPECT_EQUAL( 0, stars( "deer", "varmint_rifle", "body", 3 ) ); // 0 아래로 내려가지 않는다
    SW_EXPECT_EQUAL( 0, stars( "deer", "dynamite", "head", 1 ) );
    SW_EXPECT_EQUAL( 2, stars( "old_buck", "bow", "head", 1 ) ); // 원래 품질이 상한
    SW_EXPECT_EQUAL( 0, stars( "dragon", "bow", "head", 1 ) );

    LootCatalog loot;
    SW_ASSERT_TRUE( loot.loadFromXmlText( R"(<LootCatalog><Table id="deer_parts" rolls="0"><Always item="venison" min="2" max="2" chance="1"/></Table></LootCatalog>)",
                                          "OpenWorldWesternTest" ) );
    WesternKill kill;
    kill._animalId         = hashed_string( "deer" );
    kill._weaponId         = hashed_string( "bow" );
    kill._zoneId           = hashed_string( "head" );
    WesternCarcass carcass = WesternHunting::makeCarcass( catalog, kill );
    SW_EXPECT_EQUAL( 300, WesternHunting::computeCarcassPrice( catalog, carcass ) );
    WesternHunting::ageCarcass( carcass, 23.9f );
    SW_EXPECT_EQUAL( 3, WesternHunting::computeCarcassStars( catalog, carcass ) );
    WesternHunting::ageCarcass( carcass, 0.2f ); // 부패 시간의 반을 넘었다
    SW_EXPECT_EQUAL( 2, WesternHunting::computeCarcassStars( catalog, carcass ) );
    SW_EXPECT_EQUAL( 180, WesternHunting::computeCarcassPrice( catalog, carcass ) );

    GameRandom    random( 5u );
    WesternPelt   pelt;
    ItemStackList items;
    SW_ASSERT_TRUE( WesternHunting::skin( catalog, carcass, &loot, random, pelt, items ) );
    SW_EXPECT_FALSE( WesternHunting::skin( catalog, carcass, &loot, random, pelt, items ) ); // 두 번은 못 벗긴다
    SW_EXPECT_EQUAL( 2, pelt._stars );
    SW_EXPECT_EQUAL( 90, WesternHunting::computePeltPrice( catalog, pelt ) );       // 1.5 달러 × 0.6
    SW_EXPECT_EQUAL( 90, WesternHunting::computeCarcassPrice( catalog, carcass ) ); // 벗긴 사체는 반값
    SW_EXPECT_TRUE( items.getItemCount( hashed_string( "venison" ) ) >= 2 );

    WesternHunting::ageCarcass( carcass, 30.0f );
    SW_EXPECT_TRUE( WesternHunting::isRotten( catalog, carcass ) );
    SW_EXPECT_EQUAL( 0, WesternHunting::computeCarcassPrice( catalog, carcass ) );
    WesternCarcass rotten = WesternHunting::makeCarcass( catalog, kill );
    WesternHunting::ageCarcass( rotten, 48.0f );
    SW_EXPECT_FALSE( WesternHunting::skin( catalog, rotten, nullptr, random, pelt, items ) );
    SW_EXPECT_EQUAL( 0, WesternHunting::computePeltPrice( catalog, WesternPelt{ hashed_string( "deer" ), 0 } ) );
}

/**
 * @brief [OpenWorldWesternTest] 말 · 법 · 생존 상태 바이트 — 유대 · 능력 · 게이지 · 코어 · 겁 · 난수, 지역 기록 · 신고 대기 남은 시간, 옷 · 표시 · 데드아이가 그대로 와서 같은 걸음이 이어진다. 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( OpenWorldWesternTest, StateRoundTripContinuesTheSameRide )
{
    WesternCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWesternTestXml, "OpenWorldWesternTest" ) );

    // 말 — 유대 2 단계 · 질주 중 · 겁이 조금 쌓였다.
    WesternHorse horse;
    SW_ASSERT_TRUE( horse.initialize( &catalog, hashed_string( "arabian" ), 7u ) );
    SW_EXPECT_TRUE( horse.brush() );
    horse.setRidden( true );
    horse.update( 5.0f, 0.0f );
    SW_ASSERT_EQUAL( 2, horse.getBondLevel() );
    for ( int32 frame = 0; frame < 3; ++frame )
        (void)horse.gallop( 0.1f );
    SW_EXPECT_TRUE( horse.frighten( 0.5f ) == WesternHorseReaction::Calm );

    const vector<uint8> horseBytes = captureWesternBytes( horse );
    WesternHorse        restoredHorse;
    SW_ASSERT_TRUE( restoredHorse.initialize( &catalog, hashed_string( "arabian" ), 1u ) );
    Archive horseReader( horseBytes.data(), horseBytes.size() );
    SW_ASSERT_TRUE( restoredHorse.readState( horseReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, horseReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWesternBytes( restoredHorse ) == horseBytes );
    SW_EXPECT_TRUE( restoredHorse.hasAbility( hashed_string( "rear" ) ) );
    SW_EXPECT_NEAR_EQUAL( horse.getStamina().getMax(), restoredHorse.getStamina().getMax(), 1.0e-3f );

    // 같은 걸음을 둘 다 더 돌리면 바이트가 같다(겁 문턱을 넘어 난수를 하나 쓴다).
    SW_EXPECT_TRUE( horse.frighten( 1.5f ) == restoredHorse.frighten( 1.5f ) );
    horse.update( 1.0f, 1.0f );
    restoredHorse.update( 1.0f, 1.0f );
    SW_EXPECT_TRUE( captureWesternBytes( horse ) == captureWesternBytes( restoredHorse ) );

    WesternHorse truncatedHorse;
    SW_ASSERT_TRUE( truncatedHorse.initialize( &catalog, hashed_string( "arabian" ), 1u ) );
    Archive horseCut( horseBytes.data(), horseBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedHorse.readState( horseCut ) );
    SW_EXPECT_EQUAL( 1, truncatedHorse.getBondLevel() );

    // 법 — 보안관이 본 절도는 현상금, 시민 둘이 본 살인은 신고 대기 중.
    const hashed_string    region( "lemoyne" );
    vector<WesternWitness> listCandidate{ makeWitness( 1, false ), makeWitness( 2, false ) };
    vector<WesternWitness> listLawman{ makeWitness( 9, true ) };
    const ListedSight      twoSee( vector<uint64>{ 1, 2 } );
    const ListedSight      lawSees( vector<uint64>{ 9 } );
    WesternLawState        law;
    law.initialize( &catalog );
    (void)law.commitCrime( hashed_string( "theft" ), region, float3{}, listLawman, lawSees, false );
    (void)law.commitCrime( hashed_string( "murder" ), region, float3{}, listCandidate, twoSee, false );
    law.setDisguised( true );
    law.update( 5.0f );
    SW_ASSERT_EQUAL( 2, law.countPendingReports() );

    const vector<uint8> lawBytes = captureWesternBytes( law );
    WesternLawState     restoredLaw;
    restoredLaw.initialize( &catalog );
    Archive lawReader( lawBytes.data(), lawBytes.size() );
    SW_ASSERT_TRUE( restoredLaw.readState( lawReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, lawReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWesternBytes( restoredLaw ) == lawBytes );
    SW_EXPECT_EQUAL( law.getBounty( region ), restoredLaw.getBounty( region ) );
    SW_EXPECT_TRUE( restoredLaw.isDisguised() );

    law.update( 20.0f ); // 신고 시간이 지나 살인이 신고된다
    restoredLaw.update( 20.0f );
    SW_EXPECT_EQUAL( 0, restoredLaw.countPendingReports() );
    SW_EXPECT_EQUAL( law.getBounty( region ), restoredLaw.getBounty( region ) );
    SW_EXPECT_TRUE( captureWesternBytes( law ) == captureWesternBytes( restoredLaw ) );

    WesternLawState truncatedLaw;
    truncatedLaw.initialize( &catalog );
    Archive lawCut( lawBytes.data(), lawBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedLaw.readState( lawCut ) );
    SW_EXPECT_EQUAL( 0, truncatedLaw.countPendingReports() );

    // 생존 — 옷 · 추위로 준 코어, 2 단계 데드아이로 하나를 표시했다.
    WesternSurvival survival;
    survival.initialize( &catalog );
    survival.setClothing( vector<hashed_string>{ hashed_string( "shirt" ) } );
    survival.update( 0.0f, 2.0f, -5.0f );
    survival.setDeadEyeLevel( 2 );
    SW_ASSERT_TRUE( survival.activateDeadEye() );
    SW_ASSERT_TRUE( survival.markTarget( 1 ) );
    survival.update( 0.5f, 0.0f, 20.0f );

    const vector<uint8> survivalBytes = captureWesternBytes( survival );
    WesternSurvival     restoredSurvival;
    restoredSurvival.initialize( &catalog );
    Archive survivalReader( survivalBytes.data(), survivalBytes.size() );
    SW_ASSERT_TRUE( restoredSurvival.readState( survivalReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, survivalReader.getRemainingBytes() );
    SW_EXPECT_TRUE( captureWesternBytes( restoredSurvival ) == survivalBytes );
    SW_EXPECT_NEAR_EQUAL( survival.computeWarmth(), restoredSurvival.computeWarmth(), 1.0e-5f );
    SW_EXPECT_TRUE( restoredSurvival.isDeadEyeActive() );

    survival.update( 1.0f, 1.0f, -5.0f );
    restoredSurvival.update( 1.0f, 1.0f, -5.0f );
    SW_EXPECT_TRUE( captureWesternBytes( survival ) == captureWesternBytes( restoredSurvival ) );

    WesternSurvival truncatedSurvival;
    truncatedSurvival.initialize( &catalog );
    Archive survivalCut( survivalBytes.data(), survivalBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedSurvival.readState( survivalCut ) );
    SW_EXPECT_FALSE( truncatedSurvival.isDeadEyeActive() );
}
