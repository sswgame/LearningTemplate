#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Catalog/BattleRoyaleCatalog.h"
#include "GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BattleRoyaleMatch.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/LootTable.h"

#include <cstring>
#include <limits>

// BattleRoyaleMatchTest — 한 판: 피해 거절, 방어구, 기절 · 출혈 · 처치 귀속, 부활 진행과 끊김, 팀 전멸과 순위, 자기장 피해, 보급 상자, 상태 왕복.
// 결함: 없는 공격자 번호가 `_downedBy` 에 남아 세이브를 못 읽었다 · NaN 피해를 받았다 · 자기를 기절시켜도 기절 수에 셌다 · 판이 끝난 틱에 이긴 사람까지 깎았다 · 깨진 위치 · 보급 상자 값을 읽었다. 키트 GF_BattleRoyale(nogpu).

namespace
{
    constexpr const utf8* kMatchXML = R"(<BattleRoyaleCatalog mapSize="100">
  <Player health="100" downedHealth="50" bleedout="10" reviveTime="2" reviveHealthRatio="0.5" revivers="2" slots="4"/>
  <Zone startRadius="1000"><Phase wait="1000" shrink="1" ratio="1" damage="0"/></Zone>
  <Armor id="helmet" slot="Helmet" reduction="0.5" durability="30"/>
  <Armor id="vest" slot="Vest" reduction="0.25" durability="1000"/>
  <SupplyDrop table="gold" times="0.5,0.2"/>
</BattleRoyaleCatalog>)";

    // 맵 구석(0, 0)은 원 밖이다. 초당 피해만 다르다.
    constexpr const utf8* kBurnXML = R"(<BattleRoyaleCatalog mapSize="100">
  <Player health="100" downedHealth="50" bleedout="10"/>
  <Zone startRadius="10"><Phase wait="1000" shrink="1" ratio="1" damage="10"/></Zone>
  <Armor id="vest" slot="Vest" reduction="0.5" durability="1000"/>
</BattleRoyaleCatalog>)";

    constexpr const utf8* kLethalXML = R"(<BattleRoyaleCatalog mapSize="100">
  <Zone startRadius="10"><Phase wait="1000" shrink="1" ratio="1" damage="2000"/></Zone>
</BattleRoyaleCatalog>)";

    constexpr const utf8* kMatchItemXML = R"(<ItemCatalog><Item id="gold" maxStack="10"/></ItemCatalog>)";
    constexpr const utf8* kMatchLootXML = R"(<LootCatalog><Table id="gold"><Entry item="gold" min="2"/></Table></LootCatalog>)";

    constexpr float32 kMatchStep      = sw::BattleRoyaleMatch::kFixedStep;
    constexpr float32 kMatchTolerance = 1.0e-2f;

    using EventKind = sw::BattleRoyaleEvent::Kind;

    /** @brief 카탈로그와 판을 묶어 둡니다. 팀 A(0번 사람부터 @p teamASize 명), 팀 B(그다음 한 명)로 시작합니다. */
    struct MatchBench
    {
        sw::BattleRoyaleCatalog _catalog;
        sw::ItemCatalog         _items;
        sw::LootCatalog         _loot;
        sw::BattleRoyaleMatch   _match;

        [[nodiscard]] bool loadCatalogs( const utf8* pCatalogXML )
        {
            return _catalog.loadFromXMLText( pCatalogXML, "BattleRoyaleMatchTest" ) && _items.loadFromXMLText( kMatchItemXML, "items" ) && _loot.loadFromXMLText( kMatchLootXML, "loot" );
        }

        /** @brief 사람을 세우고 판을 엽니다. 적은 늘 마지막 번호입니다. */
        [[nodiscard]] bool setUp( const utf8* pCatalogXML, int32 teamASize, bool bStart = true )
        {
            if ( loadCatalogs( pCatalogXML ) == false || _match.initialize( &_catalog, &_items, &_loot, 5, nullptr ) == false )
                return false;
            const int32 teamA = _match.addTeam( sw::hashed_string( "A" ) );
            const int32 teamB = _match.addTeam( sw::hashed_string( "B" ) );
            for ( int32 index = 0; index < teamASize; ++index )
            {
                if ( _match.addPlayer( teamA ) != index )
                    return false;
            }
            if ( _match.addPlayer( teamB ) != teamASize )
                return false;
            if ( bStart )
                _match.start();
            return true;
        }

        void stepFrames( int32 steps )
        {
            for ( int32 step = 0; step < steps; ++step )
            {
                _match.update( kMatchStep );
            }
        }

        const sw::BattleRoyalePlayer& player( int32 index ) const { return *_match.findPlayer( index ); }

        /** @brief 지금까지의 알림 중 @p kind 를 모두 꺼냅니다. 나머지는 버립니다. */
        sw::vector<sw::BattleRoyaleEvent> drainKind( EventKind kind )
        {
            sw::vector<sw::BattleRoyaleEvent> listAll;
            _match.pollEvents( listAll );
            sw::vector<sw::BattleRoyaleEvent> listKind;
            for ( const sw::BattleRoyaleEvent& event : listAll )
            {
                if ( event._kind == kind )
                    listKind.push_back( event );
            }
            return listKind;
        }
    };
} // namespace

/**
 * @brief [BattleRoyaleMatchTest] 시작 전 · 없는 대상 · 0 · 음수 · 같은 팀의 피해는 들어가지 않는다. 결함: NaN 피해를 받았다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, DamageIsRejectedWhenItShouldNotCount )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 2, false ) );
    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( 0, 2, 10.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance ); // 시작 전
    bench._match.start();

    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( 99, 2, 10.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance );
    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( -1, 2, 10.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance );
    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( 0, 2, 0.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance );
    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( 0, 2, -5.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance );
    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( 0, 2, std::numeric_limits<float32>::quiet_NaN(), sw::BattleRoyaleHitZone::Limb ), kMatchTolerance );
    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( 0, 1, 10.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance ); // 같은 팀
    SW_EXPECT_NEAR( 100.0f, bench.player( 0 )._healthMachine.getHealth(), kMatchTolerance );

    SW_EXPECT_NEAR( 40.0f, bench._match.applyDamage( 0, 2, 40.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance );
    SW_EXPECT_NEAR( 60.0f, bench.player( 0 )._healthMachine.getHealth(), kMatchTolerance );
    SW_EXPECT_NEAR( 10.0f, bench._match.applyDamage( 0, 0, 10.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance ); // 자기 자신은 같은 팀이 아니라 받는다
}

/**
 * @brief [BattleRoyaleMatchTest] 피해는 맞은 부위의 방어구를 거쳐 들어가고, 부서지면 알린다. 기절한 몸은 방어구가 막지 않는다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, ArmorFiltersDamageUntilDowned )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 2 ) );
    sw::BattleRoyalePlayer* pPlayer = bench._match.findPlayerMutable( 0 );
    SW_REQUIRE_NOT_NULL( pPlayer );
    SW_REQUIRE_TRUE( pPlayer->_loadout.tryEquipArmor( sw::hashed_string( "helmet" ) ) );
    SW_REQUIRE_TRUE( pPlayer->_loadout.tryEquipArmor( sw::hashed_string( "vest" ) ) );

    SW_EXPECT_NEAR( 10.0f, bench._match.applyDamage( 0, 2, 20.0f, sw::BattleRoyaleHitZone::Head ), kMatchTolerance );
    SW_EXPECT_NEAR( 10.0f, bench._match.applyDamage( 0, 2, 20.0f, sw::BattleRoyaleHitZone::Head ), kMatchTolerance ); // 내구도 30 을 다 썼다
    SW_EXPECT_TRUE( bench.player( 0 )._loadout.getHelmet().isEmpty() );
    const sw::vector<sw::BattleRoyaleEvent> listBroken = bench.drainKind( EventKind::ArmorBroken );
    SW_REQUIRE_EQUAL( size_t( 1 ), listBroken.size() );
    SW_EXPECT_EQUAL( static_cast<int32>( sw::BattleRoyaleHitZone::Head ), listBroken[0]._value );
    SW_EXPECT_TRUE( listBroken[0]._other == 2 );

    SW_EXPECT_NEAR( 30.0f, bench._match.applyDamage( 0, 2, 40.0f, sw::BattleRoyaleHitZone::Body ), kMatchTolerance );
    SW_EXPECT_NEAR( 50.0f, bench.player( 0 )._healthMachine.getHealth(), kMatchTolerance );
    (void)bench._match.applyDamage( 0, 2, 1000.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( bench.player( 0 ).isDowned() );
    SW_EXPECT_NEAR( 8.0f, bench._match.applyDamage( 0, 2, 8.0f, sw::BattleRoyaleHitZone::Body ), kMatchTolerance ); // 조끼를 지나지 않는다
    SW_EXPECT_NEAR( 42.0f, bench.player( 0 )._healthMachine.getDownedHealth(), kMatchTolerance );
}

/**
 * @brief [BattleRoyaleMatchTest] 체력이 0 이면 기절하고 출혈로 죽으며, 그 처치는 기절시킨 사람의 것이다. 남은 인원을 알린다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, DownedPlayersBleedOutAndCreditTheKnocker )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 2 ) );
    SW_EXPECT_EQUAL( 3, bench._match.countRemainingPlayers() );
    SW_EXPECT_NEAR( 100.0f, bench._match.applyDamage( 0, 2, 150.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance );
    SW_EXPECT_TRUE( bench.player( 0 ).isDowned() );
    SW_EXPECT_EQUAL( 2, bench.player( 0 )._downedBy );
    SW_EXPECT_EQUAL( 1, bench.player( 2 )._knocks );
    SW_EXPECT_EQUAL( 0, bench.player( 2 )._kills );
    SW_EXPECT_EQUAL( 3, bench._match.countRemainingPlayers() ); // 기절도 남은 인원이다
    SW_EXPECT_EQUAL( size_t( 1 ), bench.drainKind( EventKind::PlayerDowned ).size() );

    bench.stepFrames( 40 ); // 출혈 체력 50, 초당 10 — 4 초에는 아직 산다
    SW_EXPECT_TRUE( bench.player( 0 ).isDowned() );
    bench.stepFrames( 15 );
    SW_REQUIRE_TRUE( bench.player( 0 ).isDead() );
    SW_EXPECT_EQUAL( 1, bench.player( 2 )._kills );
    SW_EXPECT_EQUAL( 2, bench._match.countRemainingPlayers() );
    const sw::vector<sw::BattleRoyaleEvent> listKilled = bench.drainKind( EventKind::PlayerKilled );
    SW_REQUIRE_EQUAL( size_t( 1 ), listKilled.size() );
    SW_EXPECT_TRUE( listKilled[0]._player == 0 );
    SW_EXPECT_TRUE( listKilled[0]._other == 2 );
    SW_EXPECT_EQUAL( 2, listKilled[0]._value );
    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( 0, 2, 10.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance ); // 죽은 사람
}

/**
 * @brief [BattleRoyaleMatchTest] 부활은 서 있는 팀원만, 한 번에 한 명에게 붙는다. 붙어 있는 동안 출혈이 멈추고, 다 차면 정해진 체력으로 일어나 자리를 비운다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, ReviveRestoresTheDownedTeammate )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 2 ) );
    SW_EXPECT_FALSE( bench._match.beginRevive( 1, 0 ) ); // 아직 기절하지 않았다
    (void)bench._match.applyDamage( 0, 2, 150.0f, sw::BattleRoyaleHitZone::Limb );

    SW_EXPECT_FALSE( bench._match.beginRevive( 2, 0 ) ); // 적
    SW_EXPECT_FALSE( bench._match.beginRevive( 0, 0 ) ); // 자기 자신
    SW_EXPECT_FALSE( bench._match.beginRevive( 99, 0 ) );
    SW_EXPECT_FALSE( bench._match.beginRevive( 1, 99 ) );
    SW_EXPECT_TRUE( bench._match.beginRevive( 1, 0 ) );
    SW_EXPECT_FALSE( bench._match.beginRevive( 1, 0 ) ); // 이미 붙었다
    SW_EXPECT_EQUAL( 0, bench.player( 1 )._revivingTarget );
    SW_EXPECT_EQUAL( size_t( 1 ), bench.drainKind( EventKind::ReviveStarted ).size() );

    bench.stepFrames( 15 ); // 부활 2 초 중 1.5 초
    SW_EXPECT_TRUE( bench.player( 0 ).isDowned() );
    SW_EXPECT_NEAR( 50.0f, bench.player( 0 )._healthMachine.getDownedHealth(), kMatchTolerance );

    bench.stepFrames( 10 );
    SW_REQUIRE_TRUE( bench.player( 0 ).isAlive() );
    SW_EXPECT_NEAR( 50.0f, bench.player( 0 )._healthMachine.getHealth(), kMatchTolerance ); // 최대 × 0.5
    SW_EXPECT_EQUAL( -1, bench.player( 1 )._revivingTarget );
    SW_EXPECT_EQUAL( -1, bench.player( 0 )._downedBy );
    const sw::vector<sw::BattleRoyaleEvent> listRevived = bench.drainKind( EventKind::PlayerRevived );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRevived.size() );
    SW_EXPECT_TRUE( listRevived[0]._player == 0 );
    SW_EXPECT_TRUE( listRevived[0]._other == 1 );
}

/**
 * @brief [BattleRoyaleMatchTest] 둘이 붙으면 혼자보다 빨리 일어난다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, TwoRevivesAreFaster )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 3 ) );
    (void)bench._match.applyDamage( 0, 3, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( bench._match.beginRevive( 1, 0 ) );
    SW_REQUIRE_TRUE( bench._match.beginRevive( 2, 0 ) );
    bench.stepFrames( 15 ); // 혼자면 2 초가 걸린다
    SW_EXPECT_TRUE( bench.player( 0 ).isAlive() );
}

/**
 * @brief [BattleRoyaleMatchTest] 적에게 맞으면 부활이 처음부터고 살리던 사람이 풀린다. 환경 피해는 끊지 않는다. 모두 떠나도 처음부터이고 출혈이 다시 흐른다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, ReviveInterruptions )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 2 ) );
    (void)bench._match.applyDamage( 0, 2, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( bench._match.beginRevive( 1, 0 ) );
    bench.stepFrames( 5 );
    SW_EXPECT_TRUE( bench.player( 0 )._revive.getProgress() > 0.0f );

    (void)bench._match.applyDamage( 0, -1, 1.0f, sw::BattleRoyaleHitZone::Limb ); // 환경
    SW_EXPECT_EQUAL( 1, bench.player( 0 )._revive.getParticipantCount() );

    (void)bench._match.applyDamage( 0, 2, 1.0f, sw::BattleRoyaleHitZone::Limb ); // 적
    SW_EXPECT_EQUAL( 0, bench.player( 0 )._revive.getParticipantCount() );
    SW_EXPECT_NEAR( 0.0f, bench.player( 0 )._revive.getProgress(), kMatchTolerance );
    SW_EXPECT_EQUAL( -1, bench.player( 1 )._revivingTarget );
    SW_EXPECT_EQUAL( size_t( 1 ), bench.drainKind( EventKind::ReviveInterrupted ).size() );

    SW_EXPECT_TRUE( bench._match.beginRevive( 1, 0 ) );
    bench.stepFrames( 5 );
    bench._match.endRevive( 1 );
    bench._match.endRevive( 99 ); // 없는 사람은 아무 일도 없다
    SW_EXPECT_EQUAL( -1, bench.player( 1 )._revivingTarget );
    SW_EXPECT_NEAR( 0.0f, bench.player( 0 )._revive.getProgress(), kMatchTolerance );
    const float32 bleedBefore = bench.player( 0 )._healthMachine.getDownedHealth();
    bench.stepFrames( 5 );
    SW_EXPECT_TRUE( bench.player( 0 )._healthMachine.getDownedHealth() < bleedBefore );
}

/**
 * @brief [BattleRoyaleMatchTest] 살리던 사람이 기절하면 부활이 멈추고 출혈이 흐른다. 서 있는 팀원이 남아 전멸은 아니다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, DownedReviverStops )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 3 ) );
    (void)bench._match.applyDamage( 0, 3, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( bench._match.beginRevive( 1, 0 ) );
    (void)bench._match.applyDamage( 1, 3, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( bench.player( 1 ).isDowned() );
    SW_EXPECT_TRUE( bench.player( 0 ).isDowned() );
    SW_EXPECT_EQUAL( -1, bench.player( 1 )._revivingTarget );
    SW_EXPECT_EQUAL( 0, bench.player( 0 )._revive.getParticipantCount() );
    bench.stepFrames( 5 );
    SW_EXPECT_TRUE( bench.player( 0 )._healthMachine.getDownedHealth() < 50.0f );
}

/**
 * @brief [BattleRoyaleMatchTest] 서 있는 팀원이 없으면 기절한 팀원이 모두 죽어 처치가 기절시킨 쪽에 가고, 팀 순위와 이긴 팀이 정해지며, 끝난 판은 피해도 시간도 받지 않는다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, TeamWipeEndsTheMatch )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 2 ) );
    (void)bench._match.applyDamage( 0, 2, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_EXPECT_EQUAL( 3, bench._match.countRemainingPlayers() );
    (void)bench._match.applyDamage( 1, 2, 150.0f, sw::BattleRoyaleHitZone::Limb );

    SW_EXPECT_TRUE( bench.player( 0 ).isDead() );
    SW_EXPECT_TRUE( bench.player( 1 ).isDead() );
    SW_EXPECT_EQUAL( 2, bench.player( 2 )._knocks );
    SW_EXPECT_EQUAL( 2, bench.player( 2 )._kills );
    SW_EXPECT_EQUAL( 1, bench._match.countRemainingPlayers() );
    SW_EXPECT_TRUE( bench._match.getMatchState().getPhase() == sw::MatchPhase::Ended );
    SW_EXPECT_EQUAL( 1, bench._match.getMatchState().getWinningTeam() );

    sw::vector<sw::BattleRoyaleEvent> listAll;
    bench._match.pollEvents( listAll );
    int32               eliminatedPlacement = 0;
    sw::optional<int32> endedTeam{};
    for ( const sw::BattleRoyaleEvent& event : listAll )
    {
        if ( event._kind == EventKind::TeamEliminated )
            eliminatedPlacement = event._value;
        else if ( event._kind == EventKind::MatchEnded )
            endedTeam = event._team;
    }
    SW_EXPECT_EQUAL( 2, eliminatedPlacement );
    SW_EXPECT_TRUE( endedTeam == 1 );

    SW_EXPECT_NEAR( 0.0f, bench._match.applyDamage( 2, 0, 10.0f, sw::BattleRoyaleHitZone::Limb ), kMatchTolerance );
    const float32 timeBefore = bench._match.getTime();
    bench.stepFrames( 5 );
    SW_EXPECT_NEAR( timeBefore, bench._match.getTime(), kMatchTolerance );
}

/**
 * @brief [BattleRoyaleMatchTest] 혼자인 팀은 기절이 곧 죽음이다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, SoloPlayerDiesOnKnock )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 1 ) );
    (void)bench._match.applyDamage( 0, 1, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_EXPECT_TRUE( bench.player( 0 ).isDead() );
    SW_EXPECT_EQUAL( 1, bench.player( 1 )._kills );
    SW_EXPECT_EQUAL( 1, bench._match.getMatchState().getWinningTeam() );
}

/**
 * @brief [BattleRoyaleMatchTest] 결함: 없는 공격자 번호가 `_downedBy` 에 남아 그 세이브를 읽지 못했다. 없는 번호는 환경으로 치고, 왕복이 된다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, UnknownAttackerCountsAsEnvironment )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 2 ) );
    (void)bench._match.applyDamage( 0, 99, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( bench.player( 0 ).isDowned() );
    SW_EXPECT_EQUAL( -1, bench.player( 0 )._downedBy );
    const sw::vector<sw::BattleRoyaleEvent> listDowned = bench.drainKind( EventKind::PlayerDowned );
    SW_REQUIRE_EQUAL( size_t( 1 ), listDowned.size() );
    SW_EXPECT_FALSE( listDowned[0]._other.has_value() );

    sw::Archive written;
    bench._match.writeState( written );
    MatchBench restored;
    SW_REQUIRE_TRUE( restored.loadCatalogs( kMatchXML ) );
    SW_REQUIRE_TRUE( restored._match.initialize( &restored._catalog, &restored._items, &restored._loot, 5, nullptr ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( restored._match.readState( reading ) );
}

/**
 * @brief [BattleRoyaleMatchTest] 결함: 자기를 기절시켜도 기절시킨 수에 셌다. 처치와 같이 세지 않는다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, SelfKnockIsNotCounted )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 2 ) );
    (void)bench._match.applyDamage( 0, 0, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( bench.player( 0 ).isDowned() );
    SW_EXPECT_EQUAL( 0, bench.player( 0 )._knocks );
    bench.stepFrames( 55 );
    SW_REQUIRE_TRUE( bench.player( 0 ).isDead() );
    SW_EXPECT_EQUAL( 0, bench.player( 0 )._kills );
}

/**
 * @brief [BattleRoyaleMatchTest] 자기장 피해는 원 밖에서만, 초당 양대로, 방어구 없이 들어가고 부활을 끊지 않는다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, ZoneDamageIgnoresArmorAndRevives )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kBurnXML, 2 ) );
    SW_REQUIRE_TRUE( bench._match.findPlayerMutable( 0 )->_loadout.tryEquipArmor( sw::hashed_string( "vest" ) ) );
    const sw::float2 center{ 50.0f, 50.0f };
    bench._match.setPlayerPosition( 1, center );
    bench._match.setPlayerPosition( 2, center );
    bench._match.setPlayerPosition( 99, center ); // 없는 사람은 아무 일도 없다

    bench.stepFrames( 10 ); // 1 초
    SW_EXPECT_NEAR( 90.0f, bench.player( 0 )._healthMachine.getHealth(), kMatchTolerance );
    SW_EXPECT_NEAR( 100.0f, bench.player( 1 )._healthMachine.getHealth(), kMatchTolerance );
    SW_EXPECT_NEAR( 1000.0f, bench.player( 0 )._loadout.getVest()._durability, kMatchTolerance );

    (void)bench._match.applyDamage( 0, 2, 200.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( bench.player( 0 ).isDowned() );
    SW_REQUIRE_TRUE( bench._match.beginRevive( 1, 0 ) );
    bench.stepFrames( 10 );
    SW_EXPECT_EQUAL( 1, bench.player( 0 )._revive.getParticipantCount() );
    SW_EXPECT_NEAR( 40.0f, bench.player( 0 )._healthMachine.getDownedHealth(), kMatchTolerance ); // 출혈은 멈췄고 자기장만 깎는다
}

/**
 * @brief [BattleRoyaleMatchTest] 기절 뒤 자기장에 죽어도 처치는 기절시킨 사람의 것이다. 자기장이 기절시켰으면 귀속이 없다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, ZoneDeathKeepsTheKnockCredit )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kBurnXML, 2 ) );
    const sw::float2 center{ 50.0f, 50.0f };
    bench._match.setPlayerPosition( 1, center );
    bench._match.setPlayerPosition( 2, center );
    (void)bench._match.applyDamage( 0, 2, 200.0f, sw::BattleRoyaleHitZone::Limb );
    bench.stepFrames( 40 ); // 출혈 10 + 자기장 10 = 초당 20, 체력 50
    SW_REQUIRE_TRUE( bench.player( 0 ).isDead() );
    SW_EXPECT_EQUAL( 1, bench.player( 2 )._kills );

    MatchBench environment;
    SW_REQUIRE_TRUE( environment.setUp( kBurnXML, 2 ) );
    environment._match.setPlayerPosition( 1, center );
    environment._match.setPlayerPosition( 2, center );
    (void)environment.drainKind( EventKind::PlayerKilled );
    environment.stepFrames( 150 ); // 자기장 10 초에 기절, 그 뒤 2.5 초에 죽는다
    SW_REQUIRE_TRUE( environment.player( 0 ).isDead() );
    SW_EXPECT_EQUAL( -1, environment.player( 0 )._downedBy );
    SW_EXPECT_EQUAL( 0, environment.player( 2 )._kills );
    const sw::vector<sw::BattleRoyaleEvent> listKilled = environment.drainKind( EventKind::PlayerKilled );
    SW_REQUIRE_EQUAL( size_t( 1 ), listKilled.size() );
    SW_EXPECT_FALSE( listKilled[0]._other.has_value() );
}

/**
 * @brief [BattleRoyaleMatchTest] 결함: 판이 끝난 틱에도 남은 사람을 계속 깎아, 이긴 사람이 끝난 뒤에 죽었다. 끝난 틱에서 멈추고 한 팀이 이긴다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, MatchEndStopsTheTick )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kLethalXML, 1 ) ); // 둘 다 원 밖, 한 틱에 200 피해
    (void)bench.drainKind( EventKind::PlayerKilled );
    bench.stepFrames( 1 );
    SW_EXPECT_TRUE( bench.player( 0 ).isDead() );
    SW_EXPECT_TRUE( bench.player( 1 ).isAlive() );
    SW_EXPECT_EQUAL( 1, bench._match.getMatchState().getWinningTeam() );
    SW_EXPECT_EQUAL( size_t( 1 ), bench.drainKind( EventKind::PlayerKilled ).size() );
}

/**
 * @brief [BattleRoyaleMatchTest] 자기장에 한 틱에 함께 죽으면 체력이 적은 쪽이 먼저 쓰러진다 - 입장 순서로 이기지 않는다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, ZoneTieLosesByLowerHealth )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kLethalXML, 1 ) );
    (void)bench._match.findPlayerMutable( 1 )->_healthMachine.applyDamage( 10.0f, 0.0f, sw::nullopt );
    bench.stepFrames( 1 );
    SW_EXPECT_TRUE( bench.player( 1 ).isDead() );
    SW_EXPECT_TRUE( bench.player( 0 ).isAlive() );
    SW_EXPECT_EQUAL( 0, bench._match.getMatchState().getWinningTeam() );
}

/**
 * @brief [BattleRoyaleMatchTest] 보급 상자는 적힌 시각이 지나면 오름차순으로 떨어져 내용물과 위치를 갖고, 시작 전에는 떨어지지 않는다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, SupplyDropsFollowTheSchedule )
{
    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 1, false ) );
    bench.stepFrames( 10 );
    SW_EXPECT_TRUE( bench._match.getSupplyDrops().empty() );
    SW_EXPECT_NEAR( 0.0f, bench._match.getTime(), kMatchTolerance );
    bench._match.start();

    bench.stepFrames( 3 ); // 0.3 초
    SW_REQUIRE_EQUAL( size_t( 1 ), bench._match.getSupplyDrops().size() );
    const sw::BattleRoyaleSupplyDrop& first = bench._match.getSupplyDrops()[0];
    SW_REQUIRE_EQUAL( size_t( 1 ), first._listItem.size() );
    SW_EXPECT_EQUAL( sw::hashed_string( "gold" ), first._listItem[0]._itemID );
    SW_EXPECT_EQUAL( 2, first._listItem[0]._count );
    SW_EXPECT_EQUAL( -1, first._listItem[0]._spotIndex );
    SW_EXPECT_TRUE( bench._match.getZone().isInside( first._position ) );

    const sw::vector<sw::BattleRoyaleEvent> listLanded = bench.drainKind( EventKind::SupplyDropLanded );
    SW_REQUIRE_EQUAL( size_t( 1 ), listLanded.size() );
    SW_EXPECT_EQUAL( 0, listLanded[0]._value );

    bench.stepFrames( 3 ); // 0.6 초
    SW_EXPECT_EQUAL( size_t( 2 ), bench._match.getSupplyDrops().size() );
    bench.stepFrames( 10 );
    SW_EXPECT_EQUAL( size_t( 2 ), bench._match.getSupplyDrops().size() );
}

/**
 * @brief [BattleRoyaleMatchTest] 없는 팀 · 카탈로그 없는 판에는 사람을 넣지 못한다. 시작하면 남은 인원을 알린다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, AddPlayerNeedsAValidTeam )
{
    sw::BattleRoyaleMatch noCatalog;
    SW_REQUIRE_TRUE( noCatalog.initialize( nullptr, nullptr, nullptr, 1, nullptr ) );
    const int32 team = noCatalog.addTeam( sw::hashed_string( "A" ) );
    SW_EXPECT_FALSE( noCatalog.addPlayer( team ).has_value() );

    MatchBench bench;
    SW_REQUIRE_TRUE( bench.setUp( kMatchXML, 1, false ) );
    SW_EXPECT_FALSE( bench._match.addPlayer( 7 ).has_value() );
    SW_EXPECT_FALSE( bench._match.addPlayer( -1 ).has_value() );
    SW_EXPECT_EQUAL( 2, bench._match.getPlayerCount() );
    SW_EXPECT_TRUE( bench._match.findPlayer( 7 ) == nullptr );
    SW_EXPECT_TRUE( bench._match.findPlayer( -1 ) == nullptr );
    bench._match.start();
    const sw::vector<sw::BattleRoyaleEvent> listRemaining = bench.drainKind( EventKind::PlayersRemaining );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRemaining.size() );
    SW_EXPECT_EQUAL( 2, listRemaining[0]._value );
}

/**
 * @brief [BattleRoyaleMatchTest] 진행 중에 쓰고 읽은 판은 부활 진행 · 장비 · 보급 상자까지 이어져 원본과 같이 굴러간다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, StateRoundTripResumes )
{
    MatchBench original;
    SW_REQUIRE_TRUE( original.setUp( kMatchXML, 2 ) );
    SW_REQUIRE_TRUE( original._match.findPlayerMutable( 1 )->_loadout.tryEquipArmor( sw::hashed_string( "vest" ), 500.0f ) );
    original._match.setPlayerPosition( 2, sw::float2{ 20.0f, 30.0f } );
    (void)original._match.applyDamage( 0, 2, 150.0f, sw::BattleRoyaleHitZone::Limb );
    SW_REQUIRE_TRUE( original._match.beginRevive( 1, 0 ) );
    original.stepFrames( 3 );

    sw::Archive written;
    original._match.writeState( written );

    sw::BattleRoyaleMatch uninitialized;
    sw::Archive           forUninitialized( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( uninitialized.readState( forUninitialized ) );

    MatchBench restored;
    SW_REQUIRE_TRUE( restored.loadCatalogs( kMatchXML ) );
    SW_REQUIRE_TRUE( restored._match.initialize( &restored._catalog, &restored._items, &restored._loot, 999, nullptr ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored._match.readState( reading ) );

    SW_EXPECT_EQUAL( 3, restored._match.getPlayerCount() );
    SW_EXPECT_TRUE( restored.player( 0 ).isDowned() );
    SW_EXPECT_EQUAL( 2, restored.player( 0 )._downedBy );
    SW_EXPECT_EQUAL( 0, restored.player( 1 )._revivingTarget );
    SW_EXPECT_NEAR( 500.0f, restored.player( 1 )._loadout.getVest()._durability, kMatchTolerance );
    SW_EXPECT_NEAR( 20.0f, restored.player( 2 )._position._x, kMatchTolerance );
    SW_EXPECT_EQUAL( size_t( 1 ), restored._match.getSupplyDrops().size() );
    SW_EXPECT_NEAR( original._match.getTime(), restored._match.getTime(), kMatchTolerance );
    sw::vector<sw::BattleRoyaleEvent> listEvent;
    restored._match.pollEvents( listEvent );
    SW_EXPECT_TRUE( listEvent.empty() );

    for ( int32 step = 0; step < 30; ++step )
    {
        original.stepFrames( 1 );
        restored.stepFrames( 1 );
        SW_EXPECT_TRUE( original.player( 0 ).isAlive() == restored.player( 0 ).isAlive() );
        SW_EXPECT_NEAR( original.player( 0 )._revive.getProgress(), restored.player( 0 )._revive.getProgress(), kMatchTolerance );
    }
    SW_EXPECT_TRUE( restored.player( 0 ).isAlive() );
    SW_EXPECT_EQUAL( original._match.getSupplyDrops().size(), restored._match.getSupplyDrops().size() );
}

/**
 * @brief [BattleRoyaleMatchTest] 결함: NaN 위치 · 수가 0 이하인 보급 무더기 · 깨진 상자 값을 받아들였다. 범위 밖 처치 · 번호와 잘린 바이트까지 거절하고 판을 그대로 둔다
 */
SW_TEST_CASE( BattleRoyaleMatchTest, ReadRejectsBrokenBytes )
{
    MatchBench noDrop;
    SW_REQUIRE_TRUE( noDrop.setUp( kMatchXML, 1 ) );
    sw::Archive written;
    noDrop._match.writeState( written );

    MatchBench target;
    SW_REQUIRE_TRUE( target.setUp( kMatchXML, 3 ) );
    const auto expectRejected = [&target]( const sw::Archive& bytes, size_t offsetFromEnd, auto value )
    {
        const size_t      size = static_cast<size_t>( bytes.getSize() );
        sw::vector<uint8> listByte( bytes.getData(), bytes.getData() + size );
        std::memcpy( listByte.data() + size - offsetFromEnd, &value, sizeof( value ) );
        sw::Archive reading( listByte.data(), listByte.size() );
        SW_EXPECT_FALSE( target._match.readState( reading ) );
        SW_EXPECT_EQUAL( 4, target._match.getPlayerCount() );
    };

    // 끝에서: 상자 수(4), 그 앞이 마지막 사람의 살리는 대상(4) 기절시킨 사람(4) 기절 수(4) 처치(4) 위치(8).
    const float32 nan = std::numeric_limits<float32>::quiet_NaN();
    expectRejected( written, 28, nan );
    expectRejected( written, 24, std::numeric_limits<float32>::infinity() );
    expectRejected( written, 20, static_cast<int32>( -1 ) );
    expectRejected( written, 16, static_cast<int32>( -1 ) );
    expectRejected( written, 12, static_cast<int32>( 2 ) ); // 사람은 둘(0, 1)
    expectRejected( written, 12, static_cast<int32>( -2 ) );
    expectRejected( written, 8, static_cast<int32>( 2 ) );

    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target._match.readState( truncated ) );

    // 끝에서: 시각(4), 상자 위치(8), 무더기의 지점(4), 개수(4).
    MatchBench withDrop;
    SW_REQUIRE_TRUE( withDrop.setUp( kMatchXML, 1 ) );
    withDrop.stepFrames( 3 );
    SW_REQUIRE_EQUAL( size_t( 1 ), withDrop._match.getSupplyDrops().size() );
    sw::Archive dropWritten;
    withDrop._match.writeState( dropWritten );
    expectRejected( dropWritten, 4, nan );
    expectRejected( dropWritten, 4, -1.0f );
    expectRejected( dropWritten, 12, nan );
    expectRejected( dropWritten, 20, static_cast<int32>( 0 ) );
    expectRejected( dropWritten, 20, static_cast<int32>( -3 ) );

    sw::Archive whole( dropWritten.getData(), dropWritten.getSize() );
    SW_EXPECT_TRUE( target._match.readState( whole ) );
    SW_EXPECT_EQUAL( 2, target._match.getPlayerCount() );
}
