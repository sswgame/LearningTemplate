#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Catalog/WesternCatalog.h"
#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Rule/WesternOutlaw.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
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
  <Animal id="deer" size="Medium" quality="3" pelt="30" carcass="10" decayHours="48"/>
  <HuntWeapon id="rifle" sizes="Medium,Large"/>
  <HitZone id="head" penalty="0"/>
  <PeltGrade scales="0,0.3,0.6,1"/>
</WesternCatalog>)";

    /** @brief 판 상태 대신 자기 객체를 빌려 주는 판입니다. */
    struct OutlawRig
    {
        WesternCatalog  _catalog{};
        Wallet          _wallet{};
        ReputationState _reputation{};
        GameFlags       _flags{};
        WorldClock      _clock{};
        WesternOutlaw   _outlaw{};

        OutlawRig()
        {
            SW_REQUIRE_TRUE( _catalog.loadFromXMLText( kOutlawXML, "WesternOutlawTest" ) );
            _reputation.initialize( &_catalog.getHonorReputation() );
            SW_REQUIRE_TRUE( _clock.initialize( WorldClockSettings{} ) );
            SW_REQUIRE_TRUE( _outlaw.initialize( &_catalog, makeRefs(), hashed_string( "frontier" ), 20.0f, AIPerceptionSettings{} ) );
        }

        SessionStateRefs makeRefs()
        {
            SessionStateRefs refs;
            refs._pWallet     = &_wallet;
            refs._pReputation = &_reputation;
            refs._pClock      = &_clock;
            refs._pFlags      = &_flags;
            return refs;
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
