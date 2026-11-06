#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatEvents.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Rule/ActionRoom.h"
#include "GameFramework/Runtime/Foundation/Framework/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include <cstring>
#include <limits>

// ActionRoomTest — 룸 전투: 공격 방향 · 방어 · 쿨다운, 닿음과 탄 피해, 무적과 대시, 사격 간격과 탄 방향, 클리어 · 패배 이벤트, 상태 왕복.
// 결함: 적을 세우지 못한 보스 룸이 "보스 처치" 를 알렸다 · 클리어 뒤 남은 탄이 플레이어를 때렸다 · 음수 시간이 적을 거꾸로 움직였다 · NaN · 최대 초과 체력의 상태를 받았다.
// MonsterCatalogTest — 카탈로그: 사격 · 보상 읽기, 공격 간격 0 인 사격 종(결함), 이번 읽기에 하나도 없으면 실패(결함). 키트 GF_ActionCombat(nogpu).

namespace
{
    // 시간은 0.25 초 스텝이라 float 오차가 없다. 공격 피해 34, 공격 상자는 앞 0.85 중심 반지름 0.45, 몸은 반지름 0.28 이다.
    constexpr const utf8* kRoomMonsterXML = R"(<MonsterCatalog>
  <Monster id="grunt" name="Grunt"><Stats hp="50" maxHP="50" atk="7" def="4" speed="0" radius="0.5"/></Monster>
  <Monster id="walker" name="Walker"><Stats hp="50" maxHP="50" atk="7" speed="2" radius="0.5"/></Monster>
  <Monster id="gunner" name="Gunner"><Stats hp="10" maxHP="10" speed="0" radius="0.5"/><AI coolTime="1" firstDelay="0.5"/><Shot speed="2" life="1" radius="0.25" damage="9"/></Monster>
  <Monster id="sideGunner" name="SideGunner"><Stats hp="10" maxHP="10" speed="0" radius="0.5"/><AI coolTime="4" firstDelay="0.25"/><Shot angle="1.5707963" speed="2" life="2" radius="0.25" damage="9"/></Monster>
  <Monster id="slowGunner" name="SlowGunner"><Stats hp="10" maxHP="10" speed="0" radius="0.5"/><AI coolTime="4" firstDelay="0.25"/><Shot speed="1" life="8" radius="0.1" damage="9"/></Monster>
</MonsterCatalog>)";

    constexpr float32 kRoomStep      = 0.25f;
    constexpr float32 kRoomTolerance = 1.0e-3f;

    const sw::hashed_string kGrunt{ "grunt" };
    const sw::hashed_string kWalker{ "walker" };
    const sw::hashed_string kGunner{ "gunner" };
    const sw::hashed_string kSideGunner{ "sideGunner" };
    const sw::hashed_string kSlowGunner{ "slowGunner" };

    /** @brief 카탈로그를 건 게임 범위입니다. 테스트 본문이 `ScopedGameScope` 로 겁니다. */
    struct RoomBench
    {
        sw::MonsterCatalog _catalog;
        sw::SubsystemScope _scope;

        [[nodiscard]] bool setUp() { return _catalog.loadFromXMLText( kRoomMonsterXML, "ActionRoomTest" ) && _scope.bind<sw::MonsterCatalog>( &_catalog ); }
    };

    struct ActionRoomTestUtil
    {
        static sw::ActionRoomFrameInput makeInput( const sw::float2& position = sw::float2{ 0.0f, 0.0f }, sw::FacingDir facing = sw::FacingDir::Right, bool bAttack = false, bool bDash = false )
        {
            sw::ActionRoomFrameInput input;
            input._playerPos      = position;
            input._facing         = facing;
            input._bAttackPressed = bAttack ? SW_TRUE : SW_FALSE;
            input._bDashPressed   = bDash ? SW_TRUE : SW_FALSE;
            return input;
        }

        static sw::ActionRoomFrameInput makeAttack( sw::FacingDir facing = sw::FacingDir::Right ) { return makeInput( sw::float2{ 0.0f, 0.0f }, facing, true ); }

        /** @brief 적 하나를 세운 보스 룸을 시작합니다. 보스 룸은 `getBossHPFill` 로 체력이 보인다. */
        static void beginBossWith( sw::ActionRoom& room, const sw::hashed_string& monsterID, const sw::float2& position )
        {
            sw::ActionRoomLayout   layout;
            sw::ActionRoomSpawnDef spawn;
            spawn._monsterID = monsterID;
            spawn._position  = position;
            layout._listBossSpawn.push_back( spawn );
            room.setLayout( layout );
            room.beginBoss();
        }

        static void beginHallWith( sw::ActionRoom& room, const sw::hashed_string& monsterID, const sw::float2& position )
        {
            sw::ActionRoomLayout   layout;
            sw::ActionRoomSpawnDef spawn;
            spawn._monsterID = monsterID;
            spawn._position  = position;
            layout._listHallSpawn.push_back( spawn );
            room.setLayout( layout );
            room.beginHall();
        }
    };
} // namespace

/**
 * @brief [ActionRoomTest] 공격은 바라보는 쪽 상자에 닿은 적에게만 들어가고, 방어를 뺀 값이며, 쿨다운이 끝나야 다시 나간다
 */
SW_TEST_CASE( ActionRoomTest, AttackHitsFacingBoxWithArmorAndCooldown )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionRoom            room;
    ActionRoomTestUtil::beginBossWith( room, kGrunt, sw::float2{ 1.0f, 0.0f } );
    SW_EXPECT_NEAR( 1.0f, room.getBossHPFill(), kRoomTolerance );

    (void)room.update( kRoomStep, ActionRoomTestUtil::makeAttack( sw::FacingDir::Left ) ); // 등 뒤로 휘두른다 — 쿨다운만 돈다
    SW_EXPECT_NEAR( 1.0f, room.getBossHPFill(), kRoomTolerance );
    (void)room.update( kRoomStep, ActionRoomTestUtil::makeInput() );

    (void)room.update( kRoomStep, ActionRoomTestUtil::makeAttack() ); // 34 - 방어 4 = 30
    SW_EXPECT_NEAR( 0.4f, room.getBossHPFill(), kRoomTolerance );
    (void)room.update( kRoomStep, ActionRoomTestUtil::makeAttack() ); // 쿨다운 0.28 초가 아직 남았다
    SW_EXPECT_NEAR( 0.4f, room.getBossHPFill(), kRoomTolerance );

    const sw::ActionRoomFrameResult result = room.update( kRoomStep, ActionRoomTestUtil::makeAttack() ); // 20 - 30 은 0 에서 멈춘다
    SW_EXPECT_NEAR( 0.0f, room.getBossHPFill(), kRoomTolerance );
    SW_EXPECT_EQUAL( 0, room.getAliveEnemyCount() );
    SW_EXPECT_TRUE( result._bClearedThisFrame == SW_TRUE );
    SW_EXPECT_TRUE( result._bBossDefeated == SW_TRUE );
    SW_EXPECT_TRUE( room.isCleared() );
}

/**
 * @brief [ActionRoomTest] 위 · 아래 방향은 Y 축을 따라 상자를 놓고(아래가 +Y), 방어가 공격보다 커도 최소 1 은 들어간다
 */
SW_TEST_CASE( ActionRoomTest, AttackDirectionsAndMinimumDamage )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionRoom         room;
    sw::ActionRoomSettings settings;
    settings._playerAttackDamage    = 1.0f;
    settings._attackCooldownSeconds = 0.125f;
    room.setSettings( settings );
    ActionRoomTestUtil::beginBossWith( room, kGrunt, sw::float2{ 0.0f, 1.0f } );

    (void)room.update( kRoomStep, ActionRoomTestUtil::makeAttack( sw::FacingDir::Up ) );
    SW_EXPECT_NEAR( 1.0f, room.getBossHPFill(), kRoomTolerance );
    (void)room.update( kRoomStep, ActionRoomTestUtil::makeAttack( sw::FacingDir::Down ) ); // 방어 4 > 피해 1 이어도 1
    SW_EXPECT_NEAR( 49.0f / 50.0f, room.getBossHPFill(), kRoomTolerance );
}

/**
 * @brief [ActionRoomTest] 클리어는 한 번만 알리고 홀은 보스 처치가 아니다. 클리어 전에는 게이지가 그대로고 비활성 룸은 아무 일도 없다
 */
SW_TEST_CASE( ActionRoomTest, ClearReportsOnce )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionRoom idle;
    SW_EXPECT_FALSE( idle.isActive() );
    const sw::ActionRoomFrameResult idleResult = idle.update( kRoomStep, ActionRoomTestUtil::makeAttack() );
    SW_EXPECT_TRUE( idleResult._bClearedThisFrame == SW_FALSE );
    SW_EXPECT_NEAR( 0.0f, idle.getBossHPFill(), kRoomTolerance );

    sw::ActionRoom room;
    ActionRoomTestUtil::beginHallWith( room, kGunner, sw::float2{ 1.0f, 0.0f } );
    SW_EXPECT_TRUE( room.getKind() == sw::ActionRoomKind::Hall );
    SW_EXPECT_NEAR( 0.0f, room.getBossHPFill(), kRoomTolerance ); // 보스 룸이 아니다
    const sw::ActionRoomFrameResult cleared = room.update( kRoomStep, ActionRoomTestUtil::makeAttack() );
    SW_EXPECT_TRUE( cleared._bClearedThisFrame == SW_TRUE );
    SW_EXPECT_TRUE( cleared._bBossDefeated == SW_FALSE );
    const sw::ActionRoomFrameResult after = room.update( kRoomStep, ActionRoomTestUtil::makeInput() );
    SW_EXPECT_TRUE( after._bClearedThisFrame == SW_FALSE );
}

/**
 * @brief [ActionRoomTest] 결함: 배치가 비었거나 카탈로그에 없는 종뿐인 보스 룸이 다음 프레임에 "보스 처치" 를 알렸다. 비어서 끝나도 처치는 아니다
 */
SW_TEST_CASE( ActionRoomTest, EmptyBossRoomIsNotABossKill )
{
    SW_EXPECT_LOG( "Action.RoomLayoutNoSpawns" );
    SW_EXPECT_LOG( "Action.RoomMonsterNotInCatalog" );
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionRoom empty;
    empty.beginBoss();
    SW_EXPECT_TRUE( empty.isActive() );
    const sw::ActionRoomFrameResult emptyResult = empty.update( kRoomStep, ActionRoomTestUtil::makeInput() );
    SW_EXPECT_TRUE( emptyResult._bClearedThisFrame == SW_TRUE );
    SW_EXPECT_TRUE( emptyResult._bBossDefeated == SW_FALSE );

    sw::ActionRoom unknown;
    ActionRoomTestUtil::beginBossWith( unknown, sw::hashed_string( "ghost" ), sw::float2{} );
    SW_EXPECT_EQUAL( 0, unknown.getAliveEnemyCount() );
    const sw::ActionRoomFrameResult unknownResult = unknown.update( kRoomStep, ActionRoomTestUtil::makeInput() );
    SW_EXPECT_TRUE( unknownResult._bBossDefeated == SW_FALSE );
}

/**
 * @brief [ActionRoomTest] 카탈로그 서비스가 없으면 적을 세우지 않고 오류를 남긴다(코드 폴백이 없다). 그런 상태 바이트도 읽지 않는다
 */
SW_TEST_CASE( ActionRoomTest, MissingCatalogServiceSpawnsNothing )
{
    SW_EXPECT_LOG( "ActionRoom.MonsterNoMonsterCatalogRegistered" );
    sw::Archive written;
    {
        RoomBench bench;
        SW_REQUIRE_TRUE( bench.setUp() );
        sw::game::ScopedGameScope scopedScope( bench._scope );
        sw::ActionRoom            source;
        ActionRoomTestUtil::beginHallWith( source, kGrunt, sw::float2{ 1.0f, 0.0f } );
        source.writeState( written );
    }
    sw::ActionRoom room;
    ActionRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 1.0f, 0.0f } );
    SW_EXPECT_EQUAL( 0, room.getAliveEnemyCount() );

    sw::Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( room.readState( reading ) );
    SW_EXPECT_EQUAL( 0, room.getAliveEnemyCount() );
}

/**
 * @brief [ActionRoomTest] 닿으면 공격력만큼 피해를 주고, 무적이 끝날 때까지 다시 맞지 않는다
 */
SW_TEST_CASE( ActionRoomTest, ContactDamageRespectsInvulnerability )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionRoom            room;
    ActionRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 0.0f, 0.0f } );
    const sw::ActionRoomFrameInput input = ActionRoomTestUtil::makeInput();

    SW_EXPECT_EQUAL( 7, room.update( kRoomStep, input )._damageToPlayer );
    SW_EXPECT_TRUE( room.isPlayerInvulnerable() );
    SW_EXPECT_EQUAL( 0, room.update( kRoomStep, input )._damageToPlayer ); // 0.45 남음
    SW_EXPECT_EQUAL( 0, room.update( kRoomStep, input )._damageToPlayer ); // 0.2 남음
    SW_EXPECT_EQUAL( 7, room.update( kRoomStep, input )._damageToPlayer ); // 끝났다

    // 닿지 않으면 맞지 않는다: 적 상자 끝 0.5 와 몸 끝 0.28 사이가 벌어진 곳.
    sw::ActionRoom apart;
    ActionRoomTestUtil::beginHallWith( apart, kGrunt, sw::float2{ 0.79f, 0.0f } );
    SW_EXPECT_EQUAL( 0, apart.update( kRoomStep, input )._damageToPlayer );
}

/**
 * @brief [ActionRoomTest] 대시는 쿨다운 동안 다시 안 나가고, 잠깐 무적을 주되 맞아서 얻은 더 긴 무적을 줄이지 않는다. 게이지는 쿨다운을 따른다
 */
SW_TEST_CASE( ActionRoomTest, DashGrantsInvulnerabilityWithoutShorteningIt )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionRoomSettings settings;
    settings._dashCooldownSeconds     = 1.0f;
    settings._dashInvulnerableSeconds = 0.5f;
    settings._hitInvulnerableSeconds  = 1.5f;
    sw::ActionRoom room;
    room.setSettings( settings );
    ActionRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 0.0f, 0.0f } );
    const sw::ActionRoomFrameInput idle = ActionRoomTestUtil::makeInput();
    const sw::ActionRoomFrameInput dash = ActionRoomTestUtil::makeInput( sw::float2{ 0.0f, 0.0f }, sw::FacingDir::Right, false, true );
    SW_EXPECT_NEAR( 1.0f, room.getDashFill(), kRoomTolerance );

    sw::ActionRoomFrameResult result = room.update( kRoomStep, dash ); // 대시한 프레임에는 맞지 않는다
    SW_EXPECT_TRUE( result._bDashStarted == SW_TRUE );
    SW_EXPECT_EQUAL( 0, result._damageToPlayer );
    SW_EXPECT_NEAR( 0.0f, room.getDashFill(), kRoomTolerance );
    result = room.update( kRoomStep, dash ); // 쿨다운 중
    SW_EXPECT_TRUE( result._bDashStarted == SW_FALSE );
    SW_EXPECT_NEAR( 0.25f, room.getDashFill(), kRoomTolerance );

    SW_EXPECT_EQUAL( 7, room.update( kRoomStep, idle )._damageToPlayer ); // 대시 무적 0.5 초 끝, 맞아서 1.5 초 무적
    SW_EXPECT_EQUAL( 0, room.update( kRoomStep, idle )._damageToPlayer ); // 1.25 남음, 대시 쿨다운 0.25 남음
    SW_EXPECT_EQUAL( 0, room.update( kRoomStep, idle )._damageToPlayer ); // 1.0 남음, 쿨다운 끝
    result = room.update( kRoomStep, dash );                              // 0.75 남음에 대시 0.5 — 줄지 않는다
    SW_EXPECT_TRUE( result._bDashStarted == SW_TRUE );
    SW_EXPECT_EQUAL( 0, room.update( kRoomStep, idle )._damageToPlayer ); // 0.5
    SW_EXPECT_EQUAL( 0, room.update( kRoomStep, idle )._damageToPlayer ); // 0.25
    SW_EXPECT_EQUAL( 7, room.update( kRoomStep, idle )._damageToPlayer ); // 0

    settings._dashCooldownSeconds = 0.0f; // 분모 0 이어도 게이지는 가득이다
    sw::ActionRoom noCooldown;
    noCooldown.setSettings( settings );
    SW_EXPECT_NEAR( 1.0f, noCooldown.getDashFill(), kRoomTolerance );
}

/**
 * @brief [ActionRoomTest] 적은 플레이어 쪽으로 걸어와 닿는다. 결함: 음수 시간이 적을 거꾸로 움직였다 — 음수 시간은 아무 일도 하지 않는다
 */
SW_TEST_CASE( ActionRoomTest, WalkersApproachAndNegativeTimeIsIgnored )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionRoom            room;
    ActionRoomTestUtil::beginHallWith( room, kWalker, sw::float2{ 5.0f, 0.0f } );
    const sw::ActionRoomFrameInput input = ActionRoomTestUtil::makeInput();

    // 정의대로면 프레임마다 0.5 씩 다가와 상자 끝이 몸에 닿는 x <= 0.78 은 9 번째 프레임이다.
    for ( int32 frame = 0; frame < 100; ++frame )
    {
        (void)room.update( -1.0f, input );
    }
    for ( int32 frame = 0; frame < 8; ++frame )
    {
        SW_EXPECT_EQUAL( 0, room.update( kRoomStep, input )._damageToPlayer );
    }
    SW_EXPECT_EQUAL( 7, room.update( kRoomStep, input )._damageToPlayer );

    // 플레이어 바로 위에 선 적의 방향은 0 이라 움직이지도 깨지지도 않는다.
    sw::ActionRoom onTop;
    ActionRoomTestUtil::beginHallWith( onTop, kWalker, sw::float2{ 0.0f, 0.0f } );
    SW_EXPECT_EQUAL( 7, onTop.update( kRoomStep, input )._damageToPlayer );
    SW_EXPECT_EQUAL( 1, onTop.getAliveEnemyCount() );
}

/**
 * @brief [ActionRoomTest] 쏘는 종만 쏘고, 첫 발은 지연 뒤, 그다음은 간격마다 한 번(탄 수와 상관없이 한 번으로 센다)이다
 */
SW_TEST_CASE( ActionRoomTest, VolleysFollowDelayAndCoolTime )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionRoom            room;
    ActionRoomTestUtil::beginHallWith( room, kGunner, sw::float2{ 20.0f, 0.0f } ); // 탄이 닿지 않는 거리
    const sw::ActionRoomFrameInput input = ActionRoomTestUtil::makeInput();

    SW_EXPECT_EQUAL( 0, room.update( kRoomStep, input )._enemyVolleyCount ); // 0.25
    SW_EXPECT_EQUAL( 1, room.update( kRoomStep, input )._enemyVolleyCount ); // 0.5 — 첫 발
    for ( int32 frame = 0; frame < 3; ++frame )
    {
        SW_EXPECT_EQUAL( 0, room.update( kRoomStep, input )._enemyVolleyCount );
    }
    SW_EXPECT_EQUAL( 1, room.update( kRoomStep, input )._enemyVolleyCount ); // 1 초 뒤

    sw::ActionRoom melee;
    ActionRoomTestUtil::beginHallWith( melee, kGrunt, sw::float2{ 20.0f, 0.0f } );
    for ( int32 frame = 0; frame < 8; ++frame )
    {
        SW_EXPECT_EQUAL( 0, melee.update( kRoomStep, input )._enemyVolleyCount );
    }
}

/**
 * @brief [ActionRoomTest] 탄은 겨눈 쪽으로 날아 맞으면 정의의 피해를 주고 사라지며, 수명 안에 못 닿으면 피해가 없다
 */
SW_TEST_CASE( ActionRoomTest, ProjectilesHitOrExpire )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope      scopedScope( bench._scope );
    const sw::ActionRoomFrameInput input = ActionRoomTestUtil::makeInput();

    // x = 1.5 에서 쏜 탄(속도 2)은 쏜 프레임에 1.0, 다음 프레임에 0.5 로, 그 상자 끝 0.25 가 몸 끝 0.28 안이다.
    sw::ActionRoom near_;
    ActionRoomTestUtil::beginHallWith( near_, kGunner, sw::float2{ 1.5f, 0.0f } );
    SW_EXPECT_EQUAL( 0, near_.update( kRoomStep, input )._damageToPlayer );
    SW_EXPECT_EQUAL( 0, near_.update( kRoomStep, input )._damageToPlayer ); // 쏜 프레임
    SW_EXPECT_EQUAL( 9, near_.update( kRoomStep, input )._damageToPlayer );
    SW_EXPECT_TRUE( near_.isPlayerInvulnerable() );

    // x = 4 에서는 수명 1 초에 2 만큼만 날아 사라진다(다음 탄도 같다).
    sw::ActionRoom distant;
    ActionRoomTestUtil::beginHallWith( distant, kGunner, sw::float2{ 4.0f, 0.0f } );
    for ( int32 frame = 0; frame < 16; ++frame )
    {
        SW_EXPECT_EQUAL( 0, distant.update( kRoomStep, input )._damageToPlayer );
    }
}

/**
 * @brief [ActionRoomTest] 각도 π/2 인 탄은 겨눈 방향 (x, y) 를 (−y, x) 로 돌린 쪽으로 난다
 */
SW_TEST_CASE( ActionRoomTest, ShotAngleRotatesTheAim )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    // 적 (0, 1.5), 플레이어 (0, 0): 겨눈 방향 (0, −1), 돌리면 (1, 0). 탄은 x = 0.5 → 1.0 으로 간다.
    const sw::float2 arrSpot[] = {
        sw::float2{ 1.0f, 1.5f},
        sw::float2{-1.0f, 1.5f}
    };
    const int32 arrExpected[] = { 9, 0 };
    for ( int32 caseIndex = 0; caseIndex < 2; ++caseIndex )
    {
        sw::ActionRoom room;
        ActionRoomTestUtil::beginHallWith( room, kSideGunner, sw::float2{ 0.0f, 1.5f } );
        SW_EXPECT_EQUAL( 1, room.update( kRoomStep, ActionRoomTestUtil::makeInput() )._enemyVolleyCount );
        const sw::ActionRoomFrameInput moved = ActionRoomTestUtil::makeInput( arrSpot[caseIndex] );
        SW_EXPECT_EQUAL( arrExpected[caseIndex], room.update( kRoomStep, moved )._damageToPlayer );
    }
}

/**
 * @brief [ActionRoomTest] 결함: 클리어한 뒤에도 날아오던 탄이 플레이어를 때려, 이긴 방에서 질 수 있었다. 클리어하면 탄이 사라진다
 */
SW_TEST_CASE( ActionRoomTest, ClearRemovesRemainingProjectiles )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionRoom            room;
    ActionRoomTestUtil::beginHallWith( room, kSlowGunner, sw::float2{ 1.0f, 0.0f } );

    SW_REQUIRE_TRUE( room.update( kRoomStep, ActionRoomTestUtil::makeInput() )._enemyVolleyCount == 1 );  // 탄은 x = 0.75
    const sw::ActionRoomFrameResult cleared = room.update( kRoomStep, ActionRoomTestUtil::makeAttack() ); // 쏜 적을 쓰러뜨렸다, 탄은 x = 0.5
    SW_EXPECT_TRUE( cleared._bClearedThisFrame == SW_TRUE );
    for ( int32 frame = 0; frame < 8; ++frame )
    {
        SW_EXPECT_EQUAL( 0, room.update( kRoomStep, ActionRoomTestUtil::makeInput() )._damageToPlayer ); // 지우지 않으면 다음 프레임에 맞는다
    }
}

/**
 * @brief [ActionRoomTest] 전투 시작은 게이트를 닫고, 클리어는 클리어 알림 뒤 게이트를 열며, 패배는 패배 알림 뒤 게이트를 열고 룸을 비운다. 위치 정보가 실린다
 */
SW_TEST_CASE( ActionRoomTest, EventsFollowTheFight )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::EventDispatcher dispatcher;
    SW_REQUIRE_TRUE( bench._scope.bind<sw::EventDispatcher>( &dispatcher ) );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::vector<sw::string> listLog;
    sw::string             lastZone;
    sw::string             lastMap;
    sw::string             lastReturn;
    uint8                  bLastBoss = SW_FALSE;

    const sw::Subscription gateSubscription   = dispatcher.subscribe<sw::ClearGateStateChangedEvent>( sw::gameEventChannel(), SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::ClearGateStateChangedEvent& )>, [&]( const sw::ClearGateStateChangedEvent& event )
      {
        listLog.push_back( event._bLocked == SW_TRUE ? "locked" : "open" );
        lastZone = event._zoneID;
    } ) );
    const sw::Subscription clearSubscription  = dispatcher.subscribe<sw::RoomClearedEvent>( sw::gameEventChannel(), SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::RoomClearedEvent& )>, [&]( const sw::RoomClearedEvent& event )
     {
        listLog.push_back( "cleared" );
        lastMap   = event._mapPath;
        bLastBoss = event._bBossDefeated;
    } ) );
    const sw::Subscription defeatSubscription = dispatcher.subscribe<sw::PlayerDefeatedInRoomEvent>( sw::gameEventChannel(), SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::PlayerDefeatedInRoomEvent& )>, [&]( const sw::PlayerDefeatedInRoomEvent& event )
    {
        listLog.push_back( "defeated" );
        lastReturn = event._returnMapPath;
    } ) );

    sw::ActionRoomSite site;
    site._mapPath       = "map/boss";
    site._zoneID        = "zone1";
    site._returnMapPath = "map/field";
    sw::ActionRoom room;
    room.setSite( site );
    room.onPlayerDefeated(); // 비활성이면 아무 일도 없다
    SW_EXPECT_TRUE( listLog.empty() );

    ActionRoomTestUtil::beginBossWith( room, kGunner, sw::float2{ 1.0f, 0.0f } );
    SW_REQUIRE_EQUAL( size_t( 1 ), listLog.size() );
    SW_EXPECT_EQUAL( sw::string( "locked" ), listLog[0] );
    SW_EXPECT_EQUAL( sw::string( "zone1" ), lastZone );

    (void)room.update( kRoomStep, ActionRoomTestUtil::makeAttack() );
    SW_REQUIRE_EQUAL( size_t( 3 ), listLog.size() );
    SW_EXPECT_EQUAL( sw::string( "cleared" ), listLog[1] );
    SW_EXPECT_EQUAL( sw::string( "open" ), listLog[2] );
    SW_EXPECT_EQUAL( sw::string( "map/boss" ), lastMap );
    SW_EXPECT_TRUE( bLastBoss == SW_TRUE );

    listLog.clear();
    ActionRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 1.0f, 0.0f } );
    room.onPlayerDefeated();
    SW_REQUIRE_EQUAL( size_t( 3 ), listLog.size() );
    SW_EXPECT_EQUAL( sw::string( "defeated" ), listLog[1] );
    SW_EXPECT_EQUAL( sw::string( "open" ), listLog[2] );
    SW_EXPECT_EQUAL( sw::string( "map/field" ), lastReturn );
    SW_EXPECT_FALSE( room.isActive() );
    SW_EXPECT_EQUAL( 0, room.getAliveEnemyCount() );
    SW_EXPECT_EQUAL( sw::string( "zone1" ), room.getSite()._zoneID ); // 비워도 위치 · 설정 · 배치는 남는다
    SW_EXPECT_EQUAL( size_t( 1 ), room.getLayout()._listHallSpawn.size() );
}

/**
 * @brief [ActionRoomTest] 쓰고 읽은 룸은 체력 · 공격 쿨다운 · 날아가는 탄까지 이어져 원본과 같이 굴러가고, 읽기는 룸 이벤트를 내지 않는다
 */
SW_TEST_CASE( ActionRoomTest, StateRoundTripResumes )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::EventDispatcher dispatcher;
    SW_REQUIRE_TRUE( bench._scope.bind<sw::EventDispatcher>( &dispatcher ) );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    int32                  gateCount        = 0;
    const sw::Subscription gateSubscription = dispatcher.subscribe<sw::ClearGateStateChangedEvent>( sw::gameEventChannel(), SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::ClearGateStateChangedEvent& )>, [&]( const sw::ClearGateStateChangedEvent& )
    { ++gateCount; } ) );

    sw::ActionRoom original;
    ActionRoomTestUtil::beginBossWith( original, kGunner, sw::float2{ 1.5f, 0.0f } );
    const sw::ActionRoomFrameInput input = ActionRoomTestUtil::makeInput();
    (void)original.update( kRoomStep, input );
    (void)original.update( kRoomStep, input ); // 탄이 x = 1.0 에 있다

    sw::Archive written;
    original.writeState( written );
    const int32 gateCountBefore = gateCount;

    sw::ActionRoom restored;
    sw::Archive    reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( gateCountBefore, gateCount );
    SW_EXPECT_TRUE( restored.getKind() == sw::ActionRoomKind::Boss );
    SW_EXPECT_EQUAL( 1, restored.getAliveEnemyCount() );
    SW_EXPECT_NEAR( 1.0f, restored.getBossHPFill(), kRoomTolerance );

    for ( int32 frame = 0; frame < 6; ++frame )
    {
        const sw::ActionRoomFrameResult expected = original.update( kRoomStep, input );
        const sw::ActionRoomFrameResult actual   = restored.update( kRoomStep, input );
        SW_EXPECT_EQUAL( expected._damageToPlayer, actual._damageToPlayer );
        SW_EXPECT_EQUAL( expected._enemyVolleyCount, actual._enemyVolleyCount );
        SW_EXPECT_TRUE( original.isPlayerInvulnerable() == restored.isPlayerInvulnerable() );
    }
}

/**
 * @brief [ActionRoomTest] 결함: NaN · 음수 · 최대를 넘는 체력, NaN 위치, 음수 피해 탄을 받아들였다(NaN 체력은 죽지 않는 적이 된다). 범위 밖 인덱스 · 종류 · 표시 · 잘린 바이트와 함께 거절하고 룸을 그대로 둔다
 */
SW_TEST_CASE( ActionRoomTest, ReadRejectsBrokenBytes )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionRoom source;
    ActionRoomTestUtil::beginHallWith( source, kGunner, sw::float2{ 20.0f, 0.0f } );
    (void)source.update( kRoomStep, ActionRoomTestUtil::makeInput() );
    (void)source.update( kRoomStep, ActionRoomTestUtil::makeInput() ); // 탄 하나가 난다
    sw::Archive written;
    source.writeState( written );
    const size_t size = static_cast<size_t>( written.getSize() );

    // 머리: 종류(1) 클리어(1) 쿨다운 셋(12) = 14, 종 수(4), 이름 길이(4) + "gunner"(6) = 28, 적 수(4) = 32.
    // 적: 위치(8) 체력(4) 사격(4) 종 인덱스(2) 생존(1). 탄은 공통 발사체의 바이트다(`ProjectileSimulation::writeState`): 다음 id(4) 수(4), 맨 끝 탄 105 —
    // 빈 정의 이름(4) 위치(12) 속도(12) 피해 · 나이 · 거리 · 수명 · 사거리 · 중력 · 저항 · 유도 · 반지름 · 폭발 · 반발(44) id · 반응 번호(8) 정수 여섯(24) 깃발(1).
    constexpr size_t kActorAt    = 32;
    constexpr size_t kProjectile = 105;
    SW_REQUIRE_TRUE( size == kActorAt + 19 + 4 + 4 + kProjectile );

    sw::ActionRoom target;
    const auto     expectRejected = [&]( size_t offset, auto value )
    {
        sw::vector<uint8> listByte( written.getData(), written.getData() + size );
        std::memcpy( listByte.data() + offset, &value, sizeof( value ) );
        sw::Archive reading( listByte.data(), listByte.size() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_FALSE( target.isActive() );
    };
    const float32 nan      = std::numeric_limits<float32>::quiet_NaN();
    const float32 infinity = std::numeric_limits<float32>::infinity();
    expectRejected( 0, static_cast<uint8>( 9 ) );              // 종류
    expectRejected( 1, static_cast<uint8>( 2 ) );              // 클리어 표시
    expectRejected( kActorAt, nan );                           // 위치
    expectRejected( kActorAt + 4, infinity );                  // 위치
    expectRejected( kActorAt + 8, nan );                       // 체력
    expectRejected( kActorAt + 8, -1.0f );                     // 체력
    expectRejected( kActorAt + 8, 11.0f );                     // 최대 10 초과
    expectRejected( kActorAt + 16, static_cast<uint16>( 1 ) ); // 종 인덱스 범위 밖
    expectRejected( kActorAt + 18, static_cast<uint8>( 2 ) );  // 생존 표시
    expectRejected( size - 101, nan );                         // 탄 위치
    expectRejected( size - 93, 1.0f );                         // 탄 위치 Z(룸 평면 밖)
    expectRejected( size - 89, infinity );                     // 탄 속도
    expectRejected( size - 77, -1.0f );                        // 탄 피해
    expectRejected( size - 45, -1.0f );                        // 탄 반지름
    expectRejected( size - 1, static_cast<uint8>( 2 ) );       // 탄 깃발(살아 있지 않다)

    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target.readState( truncated ) );
    sw::Archive whole( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( target.readState( whole ) );
    SW_EXPECT_EQUAL( 1, target.getAliveEnemyCount() );
}

/**
 * @brief [ActionRoomTest] 카탈로그에 없는 종의 상태는 오류를 남기고 거절한다
 */
SW_TEST_CASE( ActionRoomTest, ReadRejectsUnknownMonster )
{
    SW_EXPECT_LOG( "Action.RoomMonsterNotInCatalog" );
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionRoom source;
    ActionRoomTestUtil::beginHallWith( source, kGrunt, sw::float2{ 5.0f, 0.0f } );
    sw::Archive written;
    source.writeState( written );

    sw::vector<uint8> listByte( written.getData(), written.getData() + static_cast<size_t>( written.getSize() ) );
    listByte[22] = static_cast<uint8>( 'x' ); // 이름 "grunt" 의 첫 글자(머리 14 + 종 수 4 + 길이 4)
    sw::ActionRoom target;
    sw::Archive    reading( listByte.data(), listByte.size() );
    SW_EXPECT_FALSE( target.readState( reading ) );
    SW_EXPECT_FALSE( target.isActive() );
}

/**
 * @brief [MonsterCatalogTest] 스탯 · AI · 사격 줄 · 보상을 읽는다. 적지 않은 보상은 0 이다
 */
SW_TEST_CASE( MonsterCatalogTest, ReadsAllSections )
{
    sw::MonsterCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MonsterCatalog>
  <Monster id="boss" name="Boss" archetype="RangedShooter">
    <Stats hp="220" maxHP="300" atk="12" def="16" speed="0.9" radius="0.7"/>
    <AI coolTime="1.5" firstDelay="1.25"/>
    <Shot angle="0" speed="4.5" life="2.5" radius="0.25" damage="10"/>
    <Shot angle="1.5" speed="3.25" life="1.75" radius="0.25" damage="11"/>
    <Prefab path="prefabs/boss"/>
    <Drop exp="10" souls="3"/>
  </Monster>
</MonsterCatalog>)",
                                              "reads" ) );
    const sw::MonsterDef* pBoss = catalog.findMonster( sw::hashed_string( "boss" ) );
    SW_REQUIRE_NOT_NULL( pBoss );
    SW_EXPECT_TRUE( pBoss->_archetype == sw::MonsterArchetype::RangedShooter );
    SW_EXPECT_EQUAL( 220, pBoss->_stats._hp );
    SW_EXPECT_EQUAL( 300, pBoss->_stats._maxHP );
    SW_EXPECT_NEAR( 1.25f, pBoss->_ai._firstAttackDelay, kRoomTolerance );
    SW_REQUIRE_EQUAL( size_t( 2 ), pBoss->_listShot.size() );
    SW_EXPECT_EQUAL( 11, pBoss->_listShot[1]._damage );
    SW_EXPECT_EQUAL( sw::string( "prefabs/boss" ), pBoss->_prefabPath );
    SW_EXPECT_EQUAL( 3, pBoss->getDrop( sw::hashed_string( "souls" ) ) );
    SW_EXPECT_EQUAL( 0, pBoss->getDrop( sw::hashed_string( "gold" ) ) );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "ghost" ) ) == nullptr );

    catalog.clear();
    SW_EXPECT_TRUE( catalog.getAllMonsters().empty() );
}

/**
 * @brief [MonsterCatalogTest] 결함: 공격 간격 0 인 사격 종을 받아 프레임마다 쐈다. 모르는 자식 · 음수 보상과 함께 그 몬스터만 넣지 않는다
 */
SW_TEST_CASE( MonsterCatalogTest, BrokenMonstersAreSkipped )
{
    SW_EXPECT_LOG( "MonsterCatalog.ShooterNeedsCoolTime" );
    SW_EXPECT_LOG( "MonsterCatalog.UnknownChild" );
    SW_EXPECT_LOG( "MonsterCatalog.DropNeedsIntegerMore" );
    SW_EXPECT_LOG( "MonsterCatalog.UnknownElementSkipped" );
    SW_EXPECT_LOG( "MonsterCatalog.ElementNotLoadedListed" );

    sw::MonsterCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MonsterCatalog>
  <Monster id="rapid" name="r"><AI coolTime="0"/><Shot/></Monster>
  <Monster id="odd" name="o"><Mystery/></Monster>
  <Monster id="debt" name="d"><Drop gold="-1"/></Monster>
  <Beast/>
  <Monster id="still" name="s"><AI coolTime="0"/></Monster>
</MonsterCatalog>)",
                                              "broken" ) );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "rapid" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "odd" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "debt" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "still" ) ) != nullptr ); // 쏘지 않는 종은 간격 0 이어도 된다
}

/**
 * @brief [MonsterCatalogTest] 결함: 앞서 읽은 몬스터가 있으면 이번 글에 하나도 없어도 성공이라 했다. 이번 읽기에 하나도 없으면 실패다(`<monster>` 오타)
 */
SW_TEST_CASE( MonsterCatalogTest, ReadingNothingFails )
{
    SW_EXPECT_LOG( "MonsterCatalog.UnknownElementSkipped" );
    SW_EXPECT_LOG( "MonsterCatalog.ElementNotLoadedListed" );
    SW_EXPECT_LOG( "MonsterCatalog.NoMonsterEntries" );

    sw::MonsterCatalog catalog;
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<MonsterCatalog><monster id="typo" name="t"/></MonsterCatalog>)", "typo" ) );
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MonsterCatalog><Monster id="one" name="o"/></MonsterCatalog>)", "one" ) );
    SW_EXPECT_FALSE( catalog.loadFromXMLText( R"(<MonsterCatalog/>)", "empty" ) );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "one" ) ) != nullptr );
}

/** @brief [MonsterCatalogTest] 결함: 순찰 · 감지 · 공격 거리를 읽고 액션 룸이 버렸다. 칸을 지웠으므로 적으면 그 몬스터를 넣지 않는다 - 적은 값이 말없이 버려지지 않는다 */
SW_TEST_CASE( MonsterCatalogTest, UnusedAIFieldIsRejected )
{
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_LOG( "MonsterCatalog.ElementNotLoadedListed" );

    sw::MonsterCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MonsterCatalog><Monster id="a" name="a"><AI patrolRange="4"/></Monster><Monster id="b" name="b"><AI coolTime="1"/></Monster></MonsterCatalog>)", "dropped" ) );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "a" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "b" ) ) != nullptr );
}
