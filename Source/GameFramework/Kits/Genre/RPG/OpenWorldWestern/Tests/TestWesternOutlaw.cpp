#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Component/WesternDeedComponent.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternCatalog.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternOutlaw.h"
#include "GameFramework/Runtime/Character/Combat/TargetFilter.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

// WesternOutlawTest — 행동 하나가 명예 · 법 · 생존 · 사냥으로 나뉘는 길, 시계로 흐르는 코어 · 현상금, 상태 왕복. 씬 없이 돈다. 키트 GF_OpenWorldWestern(nogpu).

using namespace sw;

namespace
{
    constexpr const utf8* kOutlawXML = R"(<WesternCatalog currency="Dollar" extraHitPenalty="1">
  <Crime id="theft" reportTime="1" bounty="25" wanted="1" honor="-20"/>
  <Region id="frontier" wantedCooldown="2" disguiseScale="2" maskedBountyScale="0.5" bountyDecayPerDay="5" maxWanted="3"/>
  <Pursuit level="1" name="Search" lawmen="2"/>
  <Honor min="-1000" max="1000" start="0">
    <Tier name="Outlaw" min="-1000" discount="-0.1" flag="honor_low"/>
    <Tier name="Neutral" min="-200"/>
    <Tier name="Hero" min="200" discount="0.5" flag="honor_high"/>
    <Action id="help" delta="25"/>
    <Action id="saint" delta="300"/>
  </Honor>
  <Food id="jerky" healthCore="25" staminaCore="25"/>
  <Clothing id="duster" warmth="5"/>
  <Survival comfortMin="-100" comfortMax="100" coreDrain="1.5" temperatureDrain="0" minRegenScale="0.2"/>
  <Animal id="deer" size="Medium" quality="3" pelt="30" carcass="10" decayHours="48" loot="venison"/>
  <HuntWeapon id="rifle" sizes="Medium,Large"/>
  <HitZone id="head" penalty="0"/>
  <PeltGrade scales="0,0.3,0.6,1"/>
</WesternCatalog>)";

    constexpr const utf8* kOutlawLootXML = R"(<LootCatalog><Table id="venison"><Entry item="meat" min="2"/></Table></LootCatalog>)";

    /** @brief 판 상태 대신 자기 객체를 빌려 주는 판입니다. */
    struct OutlawRig
    {
        WesternCatalog  _catalog{};
        Wallet          _wallet{};
        ReputationState _reputation{};
        GameFlags       _flags{};
        WorldClock      _clock{};
        WeatherSystem   _weather{}; ///< 날씨 줄이 없다 — 기온은 지역 값 그대로다
        LootCatalog     _loot{};
        ItemCatalog     _items{}; ///< 아이템 정의가 없는 판(겹침 수 1)
        Inventory       _bag{};
        WesternOutlaw   _outlaw{};

        OutlawRig()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kOutlawXML, "WesternOutlawTest" ) );
            _reputation.initialize( _catalog.getHonorReputation() );
            SW_REQUIRE_TRUE( _clock.initialize( WorldClockSettings{} ) );
            SW_REQUIRE_TRUE( _loot.loadFromXMLText( kOutlawLootXML, "WesternOutlawTestLoot" ) );
            SW_REQUIRE_TRUE( _bag.initialize( _items, 4 ) );
            SW_REQUIRE_TRUE( _outlaw.initialize( &_catalog, &_loot, _wallet, _reputation, _clock, _flags, _weather, &_bag, hashed_string( "frontier" ), 20.0f, AIPerceptionSettings{} ) );
        }

        /** @brief 빌려 준 판 상태(시계 · 지갑 · 평판 · 플래그 · 가방)와 무법자를 함께 싣습니다 — 결정성 테스트가 판 전체로 쓴다. */
        SW_DECLARE_STATE_IO_IN_PLACE()
        {
            io.value( _clock );
            io.value( _wallet );
            io.value( _reputation );
            io.value( _flags );
            io.value( _bag );
            io.value( _outlaw );
        }

        [[nodiscard]] bool doDeed( WesternDeedKind kind, const utf8* pID = "", const vector<WesternWitness>& listWitness = {} )
        {
            WesternDeed deed;
            deed._kind   = kind;
            deed._id     = hashed_string( pID );
            deed._weapon = hashed_string( "rifle" );
            deed._zone   = hashed_string( "head" );
            return _outlaw.perform( deed, listWitness );
        }

        static WesternWitness makeWitness( uint64 witnessID, bool bLawman )
        {
            WesternWitness witness;
            witness._id       = witnessID;
            witness._position = float3{ 0.0f, 0.0f, 3.0f };
            witness._forward  = float3{ 0.0f, 0.0f, -1.0f }; // 원점을 본다
            witness._bLawman  = bLawman ? SW_TRUE : SW_FALSE;
            return witness;
        }
    };
} // namespace

/** @brief [WesternOutlawTest] 시민이 본 범죄는 명예를 바로 깎고, 신고 시간 뒤에 현상금과 수배가 오른다 */
SW_TEST_CASE( WesternOutlawTest, WitnessedCrimeReportsAfterItsTime )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Crime, "theft", { OutlawRig::makeWitness( 1, false ) } ) );
    SW_EXPECT_EQUAL( -20, rig._outlaw.getHonorValue() );
    SW_EXPECT_EQUAL( 1, rig._outlaw.countPendingReports() );
    SW_EXPECT_EQUAL( 0, rig._outlaw.getBounty() );
    rig._outlaw.update( 1.1f );
    SW_EXPECT_EQUAL( 25, rig._outlaw.getBounty() );
    SW_EXPECT_EQUAL( 1, rig._outlaw.getWantedLevel() );

    // 로그를 남긴 알림을 디렉터가 다시 꺼낼 수 있다(소리).
    vector<WesternLawEvent> listEvent;
    rig._outlaw.pollLawEvents( listEvent );
    const auto countKind = [&listEvent]( WesternLawEvent::Kind kind )
    {
        int32 count = 0;
        for ( const WesternLawEvent& event : listEvent )
        {
            count += event._kind == kind ? 1 : 0;
        }
        return count;
    };
    SW_EXPECT_EQUAL( 1, countKind( WesternLawEvent::Kind::Witnessed ) );
    SW_EXPECT_EQUAL( 1, countKind( WesternLawEvent::Kind::Reported ) );
    SW_EXPECT_EQUAL( 1, countKind( WesternLawEvent::Kind::WantedRaised ) );
}

/** @brief [WesternOutlawTest] 아무도 안 본 범죄도 명예는 깎이지만 현상금은 없다, 모르는 범죄는 거절한다 */
SW_TEST_CASE( WesternOutlawTest, UnseenCrimeCostsOnlyHonor )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Crime, "theft" ) );
    rig._outlaw.update( 5.0f );
    SW_EXPECT_EQUAL( -20, rig._outlaw.getHonorValue() );
    SW_EXPECT_EQUAL( 0, rig._outlaw.getBounty() );
    SW_EXPECT_FALSE( rig.doDeed( WesternDeedKind::Crime, "arson" ) );
}

/** @brief [WesternOutlawTest] 보안관이 보는 동안 수배가 식지 않고, 안 보면 식는다 */
SW_TEST_CASE( WesternOutlawTest, LawSightKeepsTheWantedLevel )
{
    OutlawRig                    rig;
    const vector<WesternWitness> listLaw{ OutlawRig::makeWitness( 2, true ) };
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Crime, "theft", listLaw ) );
    rig._outlaw.update( 0.0f );
    SW_REQUIRE_EQUAL( 1, rig._outlaw.getWantedLevel() );
    rig._outlaw.updateSeenByLaw( float3{}, listLaw );
    rig._outlaw.update( 5.0f );
    SW_EXPECT_EQUAL( 1, rig._outlaw.getWantedLevel() );
    rig._outlaw.updateSeenByLaw( float3{ 100.0f, 0.0f, 0.0f }, listLaw );
    rig._outlaw.update( 2.5f );
    SW_EXPECT_EQUAL( 0, rig._outlaw.getWantedLevel() );
}

/** @brief [WesternOutlawTest] 사냥 → 가죽 → 팔기: 센트는 달러로 내리고 명예 단계의 할인 비율만큼 더 쳐준다 */
SW_TEST_CASE( WesternOutlawTest, HuntSkinSellPaysInTheCatalogCurrency )
{
    OutlawRig rig;
    SW_EXPECT_FALSE( rig.doDeed( WesternDeedKind::Skin ) );
    SW_EXPECT_FALSE( rig.doDeed( WesternDeedKind::SellPelts ) );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Hunt, "deer" ) );
    SW_EXPECT_EQUAL( 1, rig._outlaw.getCarcassCount() );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Skin ) );
    SW_EXPECT_EQUAL( 0, rig._outlaw.getCarcassCount() );
    SW_EXPECT_EQUAL( 1, rig._outlaw.getPeltCount() );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::SellPelts ) );
    SW_EXPECT_EQUAL( int64{ 30 }, rig._outlaw.getMoney() );
    SW_EXPECT_EQUAL( 0, rig._outlaw.getPeltCount() );

    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::HonorAction, "saint" ) ); // Hero: 할인 0.5 → 값의 1.5 배
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Hunt, "deer" ) );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Skin ) );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::SellPelts ) );
    SW_EXPECT_EQUAL( int64{ 75 }, rig._outlaw.getMoney() );
}

/** @brief [WesternOutlawTest] 현상금은 수배가 식은 뒤에 내고 돈이 모자라면 거절한다 */
SW_TEST_CASE( WesternOutlawTest, PayBountyNeedsCalmAndMoney )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Crime, "theft", { OutlawRig::makeWitness( 2, true ) } ) );
    rig._outlaw.update( 0.0f );
    SW_EXPECT_FALSE( rig.doDeed( WesternDeedKind::PayBounty ) ); // 쫓기는 중
    rig._outlaw.update( 2.5f );
    SW_REQUIRE_EQUAL( 0, rig._outlaw.getWantedLevel() );
    SW_EXPECT_FALSE( rig.doDeed( WesternDeedKind::PayBounty ) ); // 돈이 없다
    rig._wallet.add( rig._catalog.getCurrency(), 40 );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::PayBounty ) );
    SW_EXPECT_EQUAL( 0, rig._outlaw.getBounty() );
    SW_EXPECT_EQUAL( int64{ 15 }, rig._outlaw.getMoney() );
}

/** @brief [WesternOutlawTest] 빌린 시계가 흐른 만큼 코어가 줄고 현상금이 날마다 식으며, 먹으면 코어가 오른다 */
SW_TEST_CASE( WesternOutlawTest, ClockDrainsCoresAndDecaysBounty )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Crime, "theft", { OutlawRig::makeWitness( 2, true ) } ) );
    rig._outlaw.update( 0.0f ); // 시계를 잡는다
    SW_REQUIRE_EQUAL( 25, rig._outlaw.getBounty() );
    const float32 coreBefore = rig._outlaw.getSurvival().getCore( WesternCore::Health );
    rig._clock.advanceMinutes( 24.0f * 60.0f );
    rig._outlaw.update( 0.0f );
    const float32 coreAfter = rig._outlaw.getSurvival().getCore( WesternCore::Health );
    SW_EXPECT_TRUE( coreAfter < coreBefore - 30.0f ); // 시간당 1.5
    SW_EXPECT_EQUAL( 20, rig._outlaw.getBounty() );

    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Eat, "jerky" ) );
    SW_EXPECT_TRUE( rig._outlaw.getSurvival().getCore( WesternCore::Health ) > coreAfter + 20.0f );
    SW_EXPECT_FALSE( rig.doDeed( WesternDeedKind::Eat, "dynamite" ) );
}

/** @brief [WesternOutlawTest] 옷을 갈아입으면 보온이 오르고 변장이 된다 */
SW_TEST_CASE( WesternOutlawTest, ChangingClothesWarmsAndDisguises )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::ChangeClothes, "duster" ) );
    SW_EXPECT_EQUAL( 5.0f, rig._outlaw.getSurvival().computeWarmth() );
    SW_EXPECT_TRUE( rig._outlaw.isDisguised() );
    SW_EXPECT_FALSE( rig.doDeed( WesternDeedKind::ChangeClothes, "kimono" ) );
}

/** @brief [WesternOutlawTest] 명예 단계에 맞춰 대사 플래그가 켜진다 */
SW_TEST_CASE( WesternOutlawTest, HonorTierSetsDialogueFlags )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::HonorAction, "saint" ) );
    rig._outlaw.update( 0.0f );
    SW_EXPECT_EQUAL( 1, rig._flags.getFlag( hashed_string( "honor_high" ) ) );
}

/** @brief [WesternOutlawTest] 상태 왕복: 수배 · 현상금 · 사체 · 가죽 · 코어가 그대로 돌아오고, 깨진 바이트는 상태를 바꾸지 않는다 */
SW_TEST_CASE( WesternOutlawTest, StateRoundTrip )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Crime, "theft", { OutlawRig::makeWitness( 2, true ) } ) );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Hunt, "deer" ) );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Hunt, "deer" ) );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Skin ) );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Hunt, "deer" ) );
    Archive written;
    rig._outlaw.writeState( written );

    OutlawRig other;
    Archive   reader( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( other._outlaw.readState( reader ) );
    SW_EXPECT_EQUAL( rig._outlaw.getBounty(), other._outlaw.getBounty() );
    SW_EXPECT_EQUAL( rig._outlaw.getWantedLevel(), other._outlaw.getWantedLevel() );
    SW_EXPECT_EQUAL( 1, other._outlaw.getCarcassCount() );
    SW_EXPECT_EQUAL( 2, other._outlaw.getPeltCount() );

    OutlawRig untouched;
    SW_REQUIRE_TRUE( untouched.doDeed( WesternDeedKind::Hunt, "deer" ) );
    const uint8 arrBroken[4] = { 1, 2, 3, 4 };
    Archive     broken( arrBroken, sizeof( arrBroken ) );
    SW_EXPECT_FALSE( untouched._outlaw.readState( broken ) );
    SW_EXPECT_EQUAL( 1, untouched._outlaw.getCarcassCount() );
}

/** @brief [WesternOutlawTest] 다시 읽힌 카탈로그에 묶고 같은 상태를 읽으면 지난 현상금은 남고 다음 범죄는 새 수치로 쌓인다 */
SW_TEST_CASE( WesternOutlawTest, RebindingTheCatalogAppliesTheNewNumbers )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Crime, "theft", { OutlawRig::makeWitness( 1, false ) } ) );
    rig._outlaw.update( 1.1f );
    SW_REQUIRE_EQUAL( 25, rig._outlaw.getBounty() );
    Archive written;
    rig._outlaw.writeState( written );

    string            text{ kOutlawXML };
    const string_view oldBounty = R"(bounty="25")";
    text.replace( text.find( oldBounty ), oldBounty.size(), R"(bounty="40")" );
    WesternCatalog reloaded;
    SW_REQUIRE_TRUE( reloaded.loadFromXMLText( text, "WesternOutlawTestReloaded" ) );
    rig._outlaw.rebindCatalog( reloaded );
    Archive reader( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( rig._outlaw.readState( reader ) );
    SW_EXPECT_EQUAL( 25, rig._outlaw.getBounty() );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Crime, "theft", { OutlawRig::makeWitness( 3, false ) } ) );
    rig._outlaw.update( 1.1f );
    SW_EXPECT_EQUAL( 65, rig._outlaw.getBounty() );
}

/**
 * @brief [WesternOutlawTest] 손질은 동물의 전리품 테이블을 굴려 빌린 가방에 넣는다 — 가죽과 함께 고기가 든다
 * @details 예전에는 전리품 카탈로그를 넘기지 않아 가죽만 얻었다. 변이: `skin` 에 전리품 카탈로그 대신 nullptr 를 넘기면 단언이 진다.
 */
SW_TEST_CASE( WesternOutlawTest, SkinningRollsTheLootTableIntoTheBag )
{
    OutlawRig rig;
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Hunt, "deer" ) );
    SW_REQUIRE_TRUE( rig.doDeed( WesternDeedKind::Skin ) );
    SW_EXPECT_EQUAL( 1, rig._outlaw.getPeltCount() );
    SW_EXPECT_EQUAL( 2, rig._bag.getItemCount( "meat" ) );
}

/**
 * @brief [WesternOutlawTest] 결정성: 범죄 · 목격 · 법의 시선 · 사냥 · 손질(전리품 난수) · 팔기 · 먹기 · 옷 · 현상금 · 흐르는 시계를 걸음 번호로 돌린 판은 같은 바이트를 내고, 중간에 쓰고 읽은 판도 같게 이어 돈다
 */
SW_TEST_CASE( WesternOutlawTest, OutlawDeterminism )
{
    SW_EXPECT_LOG( "WesternOutlaw.BagFull" ); // 120 걸음 동안 사냥 · 가죽 벗기기가 되풀이돼 가방이 찬다 — 규칙대로 남긴 고기를 알리는 경고다
    const auto setup = []( OutlawRig& )
    { return true; }; // 판은 `OutlawRig` 의 생성자가 같은 카탈로그 · 시계로 연다
    const auto step = []( OutlawRig& rig, int32 stepIndex )
    {
        const vector<WesternWitness> listWitness{ OutlawRig::makeWitness( 10 + static_cast<uint64>( stepIndex ), stepIndex % 20 == 1 ) };
        if ( stepIndex % 10 == 1 )
            (void)rig.doDeed( WesternDeedKind::Crime, "theft", listWitness );
        if ( stepIndex % 9 == 4 )
            rig._outlaw.updateSeenByLaw( stepIndex % 18 == 4 ? float3{} : float3{ 100.0f, 0.0f, 0.0f }, { OutlawRig::makeWitness( 2, true ) } );
        if ( stepIndex % 7 == 3 )
            (void)rig.doDeed( WesternDeedKind::Hunt, "deer" );
        if ( stepIndex % 7 == 5 )
            (void)rig.doDeed( WesternDeedKind::Skin );
        if ( stepIndex % 14 == 6 )
            (void)rig.doDeed( WesternDeedKind::SellPelts );
        if ( stepIndex % 11 == 8 )
            (void)rig.doDeed( WesternDeedKind::HonorAction, "help" );
        if ( stepIndex % 25 == 12 )
            (void)rig.doDeed( WesternDeedKind::Eat, "jerky" );
        if ( stepIndex == 40 )
            (void)rig.doDeed( WesternDeedKind::ChangeClothes, "duster" );
        if ( stepIndex % 30 == 29 )
            (void)rig.doDeed( WesternDeedKind::PayBounty );
        if ( stepIndex % 2 == 0 )
            rig._clock.advanceMinutes( 45.0f ); // 시계는 `update` 사이에 흐른다 — 저장이 그 사이에 걸려도 읽은 판이 같은 시간을 받는다
        rig._outlaw.update( 0.5f );
    };
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::isDeterministic<OutlawRig>( setup, step, 120 ) );
}

/** @brief [WesternOutlawTest] 전리품을 적은 동물이 있는데 전리품 카탈로그나 가방이 없으면 오류를 남기고 시작하지 않는다 */
SW_TEST_CASE( WesternOutlawTest, LootAnimalsNeedALootCatalogAndABag )
{
    SW_EXPECT_LOG( "WesternOutlaw.LootWithoutCatalogOrBag" );
    OutlawRig     rig;
    WesternOutlaw outlaw;
    SW_EXPECT_FALSE( outlaw.initialize( &rig._catalog, nullptr, rig._wallet, rig._reputation, rig._clock, rig._flags, rig._weather, &rig._bag, hashed_string( "frontier" ), 20.0f, AIPerceptionSettings{} ) );
    SW_EXPECT_FALSE( outlaw.initialize( &rig._catalog, &rig._loot, rig._wallet, rig._reputation, rig._clock, rig._flags, rig._weather, nullptr, hashed_string( "frontier" ), 20.0f, AIPerceptionSettings{} ) );
}

/** @brief [WesternOutlawTest] 쓰러진 목격자는 모으지 않는다 — 시체가 신고하거나 법 쪽의 눈이 되지 않는다 */
SW_TEST_CASE( WesternOutlawTest, DeadWitnessIsNotCollected )
{
    GameObjectManager manager;
    GameObject* const pLawman = manager.createGameObject( hashed_string( "Lawman" ) );
    (void)pLawman->addComponent<SceneComponent>();
    (void)pLawman->addComponent<WesternWitnessComponent>();
    manager.beginPlay();
    vector<WesternWitness> listWitness;
    WesternWitnessComponent::collect( manager, listWitness );
    SW_EXPECT_EQUAL( size_t{ 1 }, listWitness.size() );
    pLawman->addTag( TargetFilter::getDeadTag() );
    WesternWitnessComponent::collect( manager, listWitness );
    SW_EXPECT_TRUE( listWitness.empty() );
}
