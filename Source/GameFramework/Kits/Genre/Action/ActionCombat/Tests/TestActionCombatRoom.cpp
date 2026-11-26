#include "pch.h"

#include "Core/Event/EventDispatcher.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"
#include "Engine/Serialization/Format/Tests/StateBytesTestUtil.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionCombatRoom.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"
#include "GameFramework/Runtime/Foundation/Service/GameEvents.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/Tests/CombatAttributeTestUtil.h"

#include <cstring>
#include <limits>

// ActionCombatRoomTest — 룸 전투: 공격 방향 · 방어 · 쿨다운, 닿음과 탄 피해, 무적과 대시, 사격 간격과 탄 방향, 탄 카탈로그의 탄, 클리어 · 패배 이벤트, 상태 왕복.
// 결함: 적을 세우지 못한 보스 룸이 "보스 처치" 를 알렸다 · 클리어 뒤 남은 탄이 플레이어를 때렸다 · 음수 시간이 적을 거꾸로 움직였다 · NaN · 최대 초과 체력의 상태를 받았다.
// MonsterCatalogTest — 카탈로그: 사격 · 보상 읽기, 공격 간격 0 인 사격 종(결함), 이번 읽기에 하나도 없으면 실패(결함). 키트 GF_ActionCombat(nogpu).

namespace
{
    // 시간은 0.25 초 스텝이라 float 오차가 없다. 공격 피해 34, 공격 상자는 앞 0.85 중심 반지름 0.45, 몸은 반지름 0.28 이다.
    constexpr const utf8* kRoomMonsterXML = R"(<MonsterCatalog>
  <Monster id="grunt" name="Grunt"><Stats hp="50" maxHP="50" atk="7" def="4" speed="0" radius="0.5"/></Monster>
  <Monster id="walker" name="Walker"><Stats hp="50" maxHP="50" atk="7" speed="2" radius="0.5"/></Monster>
  <Monster id="gunner" name="Gunner"><Stats hp="10" maxHP="10" speed="0" radius="0.5"/><AI coolTime="1" firstDelay="0.5"/><Shot projectile="bolt"/></Monster>
  <Monster id="sideGunner" name="SideGunner"><Stats hp="10" maxHP="10" speed="0" radius="0.5"/><AI coolTime="4" firstDelay="0.25"/><Shot angle="90" projectile="bolt_long"/></Monster>
  <Monster id="slowGunner" name="SlowGunner"><Stats hp="10" maxHP="10" speed="0" radius="0.5"/><AI coolTime="4" firstDelay="0.25"/><Shot projectile="drift"/></Monster>
  <Monster id="forever" name="Forever"><Stats hp="10" maxHP="10" speed="0" radius="0.5"/><AI coolTime="1"/><Shot projectile="endless"/></Monster>
  <Monster id="ghostShot" name="GhostShot"><Stats hp="10" maxHP="10" speed="0" radius="0.5"/><AI coolTime="1"/><Shot projectile="ghost"/></Monster>
</MonsterCatalog>)";

    // 사격 줄이 가리키는 탄(쏘는 법은 몬스터, 날아가는 것은 탄). 룸에는 벽이 없어 수명으로 끝나고 사거리는 두지 않는다. `endless` 는 수명도 사거리도 없어 룸이 받지 않는다.
    constexpr const utf8* kRoomProjectileXML = R"(<ProjectileCatalog>
  <Projectile id="bolt" speed="2" lifetime="1" range="0" radius="0.25" damage="9"/>
  <Projectile id="bolt_long" speed="2" lifetime="2" range="0" radius="0.25" damage="9"/>
  <Projectile id="drift" speed="1" lifetime="8" range="0" radius="0.1" damage="9"/>
  <Projectile id="endless" speed="1" lifetime="0" range="0" radius="0.1" damage="9"/>
</ProjectileCatalog>)";

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
        sw::MonsterCatalog    _catalog;
        sw::ProjectileCatalog _projectiles;
        sw::SubsystemScope    _scope;

        [[nodiscard]] bool setUp()
        {
            return _catalog.loadFromXMLText( kRoomMonsterXML, "ActionCombatRoomTest" ) && _projectiles.loadFromXMLText( kRoomProjectileXML, "ActionCombatRoomTest" ) &&
                   _scope.bind<sw::MonsterCatalog>( &_catalog ) && _scope.bind<sw::ProjectileCatalog>( &_projectiles );
        }
    };

    struct ActionCombatRoomTestUtil
    {
        /** @brief 씬 데이터(`kitdemo_actioncombat.scene.xml`)와 같은 플레이어 값입니다(코드에는 기본값이 없다). */
        static sw::ActionCombatRoomSettings makeSettings()
        {
            sw::ActionCombatRoomSettings settings{};
            settings._dashCooldownSeconds     = 0.85f;
            settings._dashInvulnerableSeconds = 0.22f;
            settings._hitInvulnerableSeconds  = 0.7f;
            settings._attackCooldownSeconds   = 0.28f;
            settings._playerAttackDamage      = 34.0f;
            return settings;
        }

        static sw::ActionCombatRoomFrameInput makeInput( const sw::float2& position = sw::float2{ 0.0f, 0.0f }, sw::FacingDirection facing = sw::FacingDirection::Right, bool bAttack = false, bool bDash = false )
        {
            sw::ActionCombatRoomFrameInput input;
            input._playerPos      = position;
            input._facing         = facing;
            input._bAttackPressed = bAttack ? SW_TRUE : SW_FALSE;
            input._bDashPressed   = bDash ? SW_TRUE : SW_FALSE;
            return input;
        }

        static sw::ActionCombatRoomFrameInput makeAttack( sw::FacingDirection facing = sw::FacingDirection::Right ) { return makeInput( sw::float2{ 0.0f, 0.0f }, facing, true ); }

        /** @brief 적 하나를 세운 보스 룸을 시작합니다. 보스 룸은 `getBossHPFill` 로 체력이 보인다. */
        static void beginBossWith( sw::ActionCombatRoom& room, const sw::hashed_string& monsterID, const sw::float2& position )
        {
            sw::ActionCombatRoomLayout   layout;
            sw::ActionCombatRoomSpawnDef spawn;
            spawn._monsterID = monsterID;
            spawn._position  = position;
            layout._listBossSpawn.push_back( spawn );
            room.setLayout( layout );
            room.beginBoss();
        }

        static void beginHallWith( sw::ActionCombatRoom& room, const sw::hashed_string& monsterID, const sw::float2& position )
        {
            sw::ActionCombatRoomLayout   layout;
            sw::ActionCombatRoomSpawnDef spawn;
            spawn._monsterID = monsterID;
            spawn._position  = position;
            layout._listHallSpawn.push_back( spawn );
            room.setLayout( layout );
            room.beginHall();
        }
    };
} // namespace

/**
 * @brief [ActionCombatRoomTest] 공격은 바라보는 쪽 상자에 닿은 적에게만 들어가고, 방어를 뺀 값이며, 쿨다운이 끝나야 다시 나간다
 */
SW_TEST_CASE( ActionCombatRoomTest, AttackHitsFacingBoxWithArmorAndCooldown )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionCombatRoom      room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginBossWith( room, kGrunt, sw::float2{ 1.0f, 0.0f } );
    SW_EXPECT_NEAR( 1.0f, room.getBossHPFill(), kRoomTolerance );

    (void)room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack( sw::FacingDirection::Left ) ); // 등 뒤로 휘두른다 — 쿨다운만 돈다
    SW_EXPECT_NEAR( 1.0f, room.getBossHPFill(), kRoomTolerance );
    (void)room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() );

    (void)room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack() ); // 34 - 방어 4 = 30
    SW_EXPECT_NEAR( 0.4f, room.getBossHPFill(), kRoomTolerance );
    (void)room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack() ); // 쿨다운 0.28 초가 아직 남았다
    SW_EXPECT_NEAR( 0.4f, room.getBossHPFill(), kRoomTolerance );

    const sw::ActionCombatRoomFrameResult result = room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack() ); // 20 - 30 은 0 에서 멈춘다
    SW_EXPECT_NEAR( 0.0f, room.getBossHPFill(), kRoomTolerance );
    SW_EXPECT_EQUAL( 0, room.getAliveEnemyCount() );
    SW_EXPECT_TRUE( result._bClearedThisFrame == SW_TRUE );
    SW_EXPECT_TRUE( result._bBossDefeated == SW_TRUE );
    SW_EXPECT_TRUE( room.isCleared() );
}

/**
 * @brief [ActionCombatRoomTest] 위 · 아래 방향은 Y 축을 따라 상자를 놓고(아래가 +Y), 방어가 공격보다 커도 최소 1 은 들어간다
 */
SW_TEST_CASE( ActionCombatRoomTest, AttackDirectionsAndMinimumDamage )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionCombatRoom room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    sw::ActionCombatRoomSettings settings = ActionCombatRoomTestUtil::makeSettings();
    settings._playerAttackDamage          = 1.0f;
    settings._attackCooldownSeconds       = 0.125f;
    room.setSettings( settings );
    ActionCombatRoomTestUtil::beginBossWith( room, kGrunt, sw::float2{ 0.0f, 1.0f } );

    (void)room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack( sw::FacingDirection::Up ) );
    SW_EXPECT_NEAR( 1.0f, room.getBossHPFill(), kRoomTolerance );
    (void)room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack( sw::FacingDirection::Down ) ); // 방어 4 > 피해 1 이어도 1
    SW_EXPECT_NEAR( 49.0f / 50.0f, room.getBossHPFill(), kRoomTolerance );
}

/**
 * @brief [ActionCombatRoomTest] 클리어는 한 번만 알리고 홀은 보스 처치가 아니다. 클리어 전에는 게이지가 그대로고 비활성 룸은 아무 일도 없다
 */
SW_TEST_CASE( ActionCombatRoomTest, ClearReportsOnce )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionCombatRoom idle;
    SW_EXPECT_FALSE( idle.isActive() );
    const sw::ActionCombatRoomFrameResult idleResult = idle.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack() );
    SW_EXPECT_TRUE( idleResult._bClearedThisFrame == SW_FALSE );
    SW_EXPECT_NEAR( 0.0f, idle.getBossHPFill(), kRoomTolerance );

    sw::ActionCombatRoom room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginHallWith( room, kGunner, sw::float2{ 1.0f, 0.0f } );
    SW_EXPECT_TRUE( room.getKind() == sw::ActionCombatRoomKind::Hall );
    SW_EXPECT_NEAR( 0.0f, room.getBossHPFill(), kRoomTolerance ); // 보스 룸이 아니다
    const sw::ActionCombatRoomFrameResult cleared = room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack() );
    SW_EXPECT_TRUE( cleared._bClearedThisFrame == SW_TRUE );
    SW_EXPECT_TRUE( cleared._bBossDefeated == SW_FALSE );
    const sw::ActionCombatRoomFrameResult after = room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() );
    SW_EXPECT_TRUE( after._bClearedThisFrame == SW_FALSE );
}

/**
 * @brief [ActionCombatRoomTest] 결함: 배치가 비었거나 카탈로그에 없는 종뿐인 보스 룸이 다음 프레임에 "보스 처치" 를 알렸다. 비어서 끝나도 처치는 아니다
 */
SW_TEST_CASE( ActionCombatRoomTest, EmptyBossRoomIsNotABossKill )
{
    SW_EXPECT_LOG( "Action.RoomLayoutNoSpawns" );
    SW_EXPECT_LOG( "Action.RoomMonsterNotInCatalog" );
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionCombatRoom empty;
    empty.beginBoss();
    SW_EXPECT_TRUE( empty.isActive() );
    const sw::ActionCombatRoomFrameResult emptyResult = empty.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() );
    SW_EXPECT_TRUE( emptyResult._bClearedThisFrame == SW_TRUE );
    SW_EXPECT_TRUE( emptyResult._bBossDefeated == SW_FALSE );

    sw::ActionCombatRoom unknown;
    ActionCombatRoomTestUtil::beginBossWith( unknown, sw::hashed_string( "ghost" ), sw::float2{} );
    SW_EXPECT_EQUAL( 0, unknown.getAliveEnemyCount() );
    const sw::ActionCombatRoomFrameResult unknownResult = unknown.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() );
    SW_EXPECT_TRUE( unknownResult._bBossDefeated == SW_FALSE );
}

/**
 * @brief [ActionCombatRoomTest] 카탈로그 서비스가 없으면 적을 세우지 않고 오류를 남긴다(코드 폴백이 없다). 그런 상태 바이트도 읽지 않는다
 */
SW_TEST_CASE( ActionCombatRoomTest, MissingCatalogServiceSpawnsNothing )
{
    SW_EXPECT_LOG( "ActionCombatRoom.MonsterNoMonsterCatalogRegistered" );
    sw::Archive written;
    {
        RoomBench bench;
        SW_REQUIRE_TRUE( bench.setUp() );
        sw::game::ScopedGameScope scopedScope( bench._scope );
        sw::ActionCombatRoom      source;
        ActionCombatRoomTestUtil::beginHallWith( source, kGrunt, sw::float2{ 1.0f, 0.0f } );
        source.writeState( written );
    }
    sw::ActionCombatRoom room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 1.0f, 0.0f } );
    SW_EXPECT_EQUAL( 0, room.getAliveEnemyCount() );

    sw::Archive reading( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( room.readState( reading ) );
    SW_EXPECT_EQUAL( 0, room.getAliveEnemyCount() );
}

/**
 * @brief [ActionCombatRoomTest] 닿으면 공격력만큼 피해를 주고, 무적이 끝날 때까지 다시 맞지 않는다
 */
SW_TEST_CASE( ActionCombatRoomTest, ContactDamageRespectsInvulnerability )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionCombatRoom      room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 0.0f, 0.0f } );
    const sw::ActionCombatRoomFrameInput input = ActionCombatRoomTestUtil::makeInput();

    SW_EXPECT_EQUAL( 7, room.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer );
    SW_EXPECT_TRUE( room.isPlayerInvulnerable() );
    SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer ); // 0.45 남음
    SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer ); // 0.2 남음
    SW_EXPECT_EQUAL( 7, room.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer ); // 끝났다

    // 닿지 않으면 맞지 않는다: 적 상자 끝 0.5 와 몸 끝 0.28 사이가 벌어진 곳.
    sw::ActionCombatRoom apart;
    ActionCombatRoomTestUtil::beginHallWith( apart, kGrunt, sw::float2{ 0.79f, 0.0f } );
    SW_EXPECT_EQUAL( 0, apart.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer );
}

/**
 * @brief [ActionCombatRoomTest] 대시는 쿨다운 동안 다시 안 나가고, 잠깐 무적을 주되 맞아서 얻은 더 긴 무적을 줄이지 않는다. 게이지는 쿨다운을 따른다
 */
SW_TEST_CASE( ActionCombatRoomTest, DashGrantsInvulnerabilityWithoutShorteningIt )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionCombatRoomSettings settings = ActionCombatRoomTestUtil::makeSettings();
    settings._dashCooldownSeconds         = 1.0f;
    settings._dashInvulnerableSeconds     = 0.5f;
    settings._hitInvulnerableSeconds      = 1.5f;
    sw::ActionCombatRoom room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    room.setSettings( settings );
    ActionCombatRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 0.0f, 0.0f } );
    const sw::ActionCombatRoomFrameInput idle = ActionCombatRoomTestUtil::makeInput();
    const sw::ActionCombatRoomFrameInput dash = ActionCombatRoomTestUtil::makeInput( sw::float2{ 0.0f, 0.0f }, sw::FacingDirection::Right, false, true );
    SW_EXPECT_NEAR( 1.0f, room.getDashFill(), kRoomTolerance );

    sw::ActionCombatRoomFrameResult result = room.update( sw::GameSeconds{ kRoomStep }, dash ); // 대시한 프레임에는 맞지 않는다
    SW_EXPECT_TRUE( result._bDashStarted == SW_TRUE );
    SW_EXPECT_EQUAL( 0, result._damageToPlayer );
    SW_EXPECT_NEAR( 0.0f, room.getDashFill(), kRoomTolerance );
    result = room.update( sw::GameSeconds{ kRoomStep }, dash ); // 쿨다운 중
    SW_EXPECT_TRUE( result._bDashStarted == SW_FALSE );
    SW_EXPECT_NEAR( 0.25f, room.getDashFill(), kRoomTolerance );

    SW_EXPECT_EQUAL( 7, room.update( sw::GameSeconds{ kRoomStep }, idle )._damageToPlayer ); // 대시 무적 0.5 초 끝, 맞아서 1.5 초 무적
    SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, idle )._damageToPlayer ); // 1.25 남음, 대시 쿨다운 0.25 남음
    SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, idle )._damageToPlayer ); // 1.0 남음, 쿨다운 끝
    result = room.update( sw::GameSeconds{ kRoomStep }, dash );                              // 0.75 남음에 대시 0.5 — 줄지 않는다
    SW_EXPECT_TRUE( result._bDashStarted == SW_TRUE );
    SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, idle )._damageToPlayer ); // 0.5
    SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, idle )._damageToPlayer ); // 0.25
    SW_EXPECT_EQUAL( 7, room.update( sw::GameSeconds{ kRoomStep }, idle )._damageToPlayer ); // 0

    settings._dashCooldownSeconds = 0.0f; // 분모 0 이어도 게이지는 가득이다
    sw::ActionCombatRoom noCooldown;
    noCooldown.setSettings( settings );
    SW_EXPECT_NEAR( 1.0f, noCooldown.getDashFill(), kRoomTolerance );
}

/**
 * @brief [ActionCombatRoomTest] 적은 플레이어 쪽으로 걸어와 닿는다. 결함: 음수 시간이 적을 거꾸로 움직였다 — 음수 시간은 아무 일도 하지 않는다
 */
SW_TEST_CASE( ActionCombatRoomTest, WalkersApproachAndNegativeTimeIsIgnored )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionCombatRoom      room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginHallWith( room, kWalker, sw::float2{ 5.0f, 0.0f } );
    const sw::ActionCombatRoomFrameInput input = ActionCombatRoomTestUtil::makeInput();

    // 정의대로면 프레임마다 0.5 씩 다가와 상자 끝이 몸에 닿는 x <= 0.78 은 9 번째 프레임이다.
    for ( int32 frame = 0; frame < 100; ++frame )
    {
        (void)room.update( sw::GameSeconds{ -1.0f }, input );
    }
    for ( int32 frame = 0; frame < 8; ++frame )
    {
        SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer );
    }
    SW_EXPECT_EQUAL( 7, room.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer );

    // 플레이어 바로 위에 선 적의 방향은 0 이라 움직이지도 깨지지도 않는다.
    sw::ActionCombatRoom onTop;
    ActionCombatRoomTestUtil::beginHallWith( onTop, kWalker, sw::float2{ 0.0f, 0.0f } );
    SW_EXPECT_EQUAL( 7, onTop.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer );
    SW_EXPECT_EQUAL( 1, onTop.getAliveEnemyCount() );
}

/**
 * @brief [ActionCombatRoomTest] 쏘는 종만 쏘고, 첫 발은 지연 뒤, 그다음은 간격마다 한 번(탄 수와 상관없이 한 번으로 센다)이다
 */
SW_TEST_CASE( ActionCombatRoomTest, VolleysFollowDelayAndCoolTime )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionCombatRoom      room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginHallWith( room, kGunner, sw::float2{ 20.0f, 0.0f } ); // 탄이 닿지 않는 거리
    const sw::ActionCombatRoomFrameInput input = ActionCombatRoomTestUtil::makeInput();

    SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, input )._enemyVolleyCount ); // 0.25
    SW_EXPECT_EQUAL( 1, room.update( sw::GameSeconds{ kRoomStep }, input )._enemyVolleyCount ); // 0.5 — 첫 발
    for ( int32 frame = 0; frame < 3; ++frame )
    {
        SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, input )._enemyVolleyCount );
    }
    SW_EXPECT_EQUAL( 1, room.update( sw::GameSeconds{ kRoomStep }, input )._enemyVolleyCount ); // 1 초 뒤

    sw::ActionCombatRoom melee;
    ActionCombatRoomTestUtil::beginHallWith( melee, kGrunt, sw::float2{ 20.0f, 0.0f } );
    for ( int32 frame = 0; frame < 8; ++frame )
    {
        SW_EXPECT_EQUAL( 0, melee.update( sw::GameSeconds{ kRoomStep }, input )._enemyVolleyCount );
    }
}

/**
 * @brief [ActionCombatRoomTest] 탄은 겨눈 쪽으로 날아 맞으면 정의의 피해를 주고 사라지며, 수명 안에 못 닿으면 피해가 없다
 */
SW_TEST_CASE( ActionCombatRoomTest, ProjectilesHitOrExpire )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope            scopedScope( bench._scope );
    const sw::ActionCombatRoomFrameInput input = ActionCombatRoomTestUtil::makeInput();

    // x = 1.5 에서 쏜 탄(속도 2)은 쏜 프레임에 1.0, 다음 프레임에 0.5 로, 그 상자 끝 0.25 가 몸 끝 0.28 안이다.
    sw::ActionCombatRoom near_;
    near_.setSettings( ActionCombatRoomTestUtil::makeSettings() ); // 맞은 뒤 무적 시간은 설정 칸이다
    ActionCombatRoomTestUtil::beginHallWith( near_, kGunner, sw::float2{ 1.5f, 0.0f } );
    SW_EXPECT_EQUAL( 0, near_.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer );
    SW_EXPECT_EQUAL( 0, near_.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer ); // 쏜 프레임
    SW_EXPECT_EQUAL( 9, near_.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer );
    SW_EXPECT_TRUE( near_.isPlayerInvulnerable() );

    // x = 4 에서는 수명 1 초에 2 만큼만 날아 사라진다(다음 탄도 같다).
    sw::ActionCombatRoom distant;
    distant.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginHallWith( distant, kGunner, sw::float2{ 4.0f, 0.0f } );
    for ( int32 frame = 0; frame < 16; ++frame )
    {
        SW_EXPECT_EQUAL( 0, distant.update( sw::GameSeconds{ kRoomStep }, input )._damageToPlayer );
    }
}

/**
 * @brief [ActionCombatRoomTest] 각도 π/2 인 탄은 겨눈 방향 (x, y) 를 (−y, x) 로 돌린 쪽으로 난다
 */
SW_TEST_CASE( ActionCombatRoomTest, ShotAngleRotatesTheAim )
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
        sw::ActionCombatRoom room;
        room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
        ActionCombatRoomTestUtil::beginHallWith( room, kSideGunner, sw::float2{ 0.0f, 1.5f } );
        SW_EXPECT_EQUAL( 1, room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() )._enemyVolleyCount );
        const sw::ActionCombatRoomFrameInput moved = ActionCombatRoomTestUtil::makeInput( arrSpot[caseIndex] );
        SW_EXPECT_EQUAL( arrExpected[caseIndex], room.update( sw::GameSeconds{ kRoomStep }, moved )._damageToPlayer );
    }
}

/**
 * @brief [ActionCombatRoomTest] 사격 줄의 탄은 탄 카탈로그(게임 서비스)에서 찾는다. 없는 탄 · 수명도 사거리도 없는 탄(벽 없는 방에 영원히 남는다)을 쏘는 종과, 탄 카탈로그가 없을 때 쏘는 종은 세우지 않는다
 */
SW_TEST_CASE( ActionCombatRoomTest, ShotsResolveFromTheProjectileCatalog )
{
    SW_EXPECT_LOG( "ActionCombatRoom.ShotProjectileUnusable" );
    SW_EXPECT_LOG( "ActionCombatRoom.ShooterNoProjectileCatalog" );
    {
        RoomBench bench;
        SW_REQUIRE_TRUE( bench.setUp() );
        sw::game::ScopedGameScope scopedScope( bench._scope );
        sw::ActionCombatRoom      ghost;
        ActionCombatRoomTestUtil::beginHallWith( ghost, sw::hashed_string( "ghostShot" ), sw::float2{ 5.0f, 0.0f } );
        SW_EXPECT_EQUAL( 0, ghost.getAliveEnemyCount() );
        sw::ActionCombatRoom forever;
        ActionCombatRoomTestUtil::beginHallWith( forever, sw::hashed_string( "forever" ), sw::float2{ 5.0f, 0.0f } );
        SW_EXPECT_EQUAL( 0, forever.getAliveEnemyCount() );
    }

    sw::MonsterCatalog monsters;
    SW_REQUIRE_TRUE( monsters.loadFromXMLText( kRoomMonsterXML, "ActionCombatRoomTest" ) );
    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind<sw::MonsterCatalog>( &monsters ) );
    sw::game::ScopedGameScope scopedScope( scope );
    sw::ActionCombatRoom      room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginHallWith( room, kGunner, sw::float2{ 5.0f, 0.0f } );
    SW_EXPECT_EQUAL( 0, room.getAliveEnemyCount() );
    ActionCombatRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 5.0f, 0.0f } ); // 쏘지 않는 종은 탄 카탈로그가 없어도 선다
    SW_EXPECT_EQUAL( 1, room.getAliveEnemyCount() );
}

/**
 * @brief [ActionCombatRoomTest] 결함: 클리어한 뒤에도 날아오던 탄이 플레이어를 때려, 이긴 방에서 질 수 있었다. 클리어하면 탄이 사라진다
 */
SW_TEST_CASE( ActionCombatRoomTest, ClearRemovesRemainingProjectiles )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );
    sw::ActionCombatRoom      room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    ActionCombatRoomTestUtil::beginHallWith( room, kSlowGunner, sw::float2{ 1.0f, 0.0f } );

    SW_REQUIRE_TRUE( room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() )._enemyVolleyCount == 1 );        // 탄은 x = 0.75
    const sw::ActionCombatRoomFrameResult cleared = room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack() ); // 쏜 적을 쓰러뜨렸다, 탄은 x = 0.5
    SW_EXPECT_TRUE( cleared._bClearedThisFrame == SW_TRUE );
    for ( int32 frame = 0; frame < 8; ++frame )
    {
        SW_EXPECT_EQUAL( 0, room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() )._damageToPlayer ); // 지우지 않으면 다음 프레임에 맞는다
    }
}

/**
 * @brief [ActionCombatRoomTest] 전투 시작은 게이트를 닫고, 클리어는 클리어 알림 뒤 게이트를 열며, 패배는 패배 알림 뒤 게이트를 열고 룸을 비운다. 위치 정보가 실린다
 */
SW_TEST_CASE( ActionCombatRoomTest, EventsFollowTheFight )
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

    sw::ActionCombatRoomSite site;
    site._mapPath       = "map/boss";
    site._zoneID        = "zone1";
    site._returnMapPath = "map/field";
    sw::ActionCombatRoom room;
    room.setSettings( ActionCombatRoomTestUtil::makeSettings() );
    room.setSite( site );
    room.onPlayerDefeated(); // 비활성이면 아무 일도 없다
    SW_EXPECT_TRUE( listLog.empty() );

    ActionCombatRoomTestUtil::beginBossWith( room, kGunner, sw::float2{ 1.0f, 0.0f } );
    SW_REQUIRE_EQUAL( size_t( 1 ), listLog.size() );
    SW_EXPECT_EQUAL( sw::string( "locked" ), listLog[0] );
    SW_EXPECT_EQUAL( sw::string( "zone1" ), lastZone );

    (void)room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeAttack() );
    SW_REQUIRE_EQUAL( size_t( 3 ), listLog.size() );
    SW_EXPECT_EQUAL( sw::string( "cleared" ), listLog[1] );
    SW_EXPECT_EQUAL( sw::string( "open" ), listLog[2] );
    SW_EXPECT_EQUAL( sw::string( "map/boss" ), lastMap );
    SW_EXPECT_TRUE( bLastBoss == SW_TRUE );

    listLog.clear();
    ActionCombatRoomTestUtil::beginHallWith( room, kGrunt, sw::float2{ 1.0f, 0.0f } );
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
 * @brief [ActionCombatRoomTest] 쓰고 읽은 룸은 체력 · 공격 쿨다운 · 날아가는 탄까지 이어져 원본과 같이 굴러가고, 읽기는 룸 이벤트를 내지 않는다
 */
SW_TEST_CASE( ActionCombatRoomTest, StateRoundTripResumes )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::EventDispatcher dispatcher;
    SW_REQUIRE_TRUE( bench._scope.bind<sw::EventDispatcher>( &dispatcher ) );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    int32                  gateCount        = 0;
    const sw::Subscription gateSubscription = dispatcher.subscribe<sw::ClearGateStateChangedEvent>( sw::gameEventChannel(), SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::ClearGateStateChangedEvent& )>, [&]( const sw::ClearGateStateChangedEvent& )
    { ++gateCount; } ) );

    sw::ActionCombatRoom original;
    ActionCombatRoomTestUtil::beginBossWith( original, kGunner, sw::float2{ 1.5f, 0.0f } );
    const sw::ActionCombatRoomFrameInput input = ActionCombatRoomTestUtil::makeInput();
    (void)original.update( sw::GameSeconds{ kRoomStep }, input );
    (void)original.update( sw::GameSeconds{ kRoomStep }, input ); // 탄이 x = 1.0 에 있다

    sw::Archive written;
    original.writeState( written );
    const int32 gateCountBefore = gateCount;

    sw::ActionCombatRoom restored;
    sw::Archive          reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( gateCountBefore, gateCount );
    SW_EXPECT_TRUE( restored.getKind() == sw::ActionCombatRoomKind::Boss );
    SW_EXPECT_EQUAL( 1, restored.getAliveEnemyCount() );
    SW_EXPECT_NEAR( 1.0f, restored.getBossHPFill(), kRoomTolerance );

    for ( int32 frame = 0; frame < 6; ++frame )
    {
        const sw::ActionCombatRoomFrameResult expected = original.update( sw::GameSeconds{ kRoomStep }, input );
        const sw::ActionCombatRoomFrameResult actual   = restored.update( sw::GameSeconds{ kRoomStep }, input );
        SW_EXPECT_EQUAL( expected._damageToPlayer, actual._damageToPlayer );
        SW_EXPECT_EQUAL( expected._enemyVolleyCount, actual._enemyVolleyCount );
        SW_EXPECT_TRUE( original.isPlayerInvulnerable() == restored.isPlayerInvulnerable() );
    }
}

/**
 * @brief [ActionCombatRoomTest] 결함: NaN · 음수 · 최대를 넘는 체력, NaN 위치, 음수 피해 탄을 받아들였다(NaN 체력은 죽지 않는 적이 된다). 범위 밖 인덱스 · 종류 · 표시 · 잘린 바이트와 함께 거절하고 룸을 그대로 둔다
 */
SW_TEST_CASE( ActionCombatRoomTest, ReadRejectsBrokenBytes )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionCombatRoom source;
    ActionCombatRoomTestUtil::beginHallWith( source, kGunner, sw::float2{ 20.0f, 0.0f } );
    (void)source.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() );
    (void)source.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput() ); // 탄 하나가 난다
    sw::Archive written;
    source.writeState( written );
    const size_t size = static_cast<size_t>( written.getSize() );

    // 리플렉션 머리 뒤 종류(열거는 int64, 8) 클리어(1), 손 본문의 쿨다운 셋(12), 종 수(4), 이름 길이(4) + "gunner"(6), 적 수(4).
    // 적: 위치(8) 체력(4) 사격(4) 종 인덱스(2) 생존(1). 탄은 공통 발사체의 바이트다: 다음 id(4) 수(4), 맨 끝 탄 한 줄(이름 "bolt" 4 글자 + 칸 자리는 `StateOffsetFromEnd`).
    using ProjectileAt           = sw::ProjectileSimulation::StateOffsetFromEnd;
    const size_t     kindAt      = sw::StateIO::kReflectedHeaderBytes;
    const size_t     clearedAt   = kindAt + sizeof( int64 );
    const size_t     kActorAt    = clearedAt + 1 + 12 + 4 + 4 + 6 + 4;
    constexpr size_t kProjectile = ProjectileAt::kNameLength + 4;
    SW_REQUIRE_EQUAL( kActorAt + 19 + 4 + 4 + kProjectile, size );

    sw::ActionCombatRoom target;
    const auto           expectRejected = [&]( size_t offset, auto value )
    {
        sw::vector<uint8> listByte( written.getData(), written.getData() + size );
        std::memcpy( listByte.data() + offset, &value, sizeof( value ) );
        sw::Archive reading( listByte.data(), listByte.size() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_FALSE( target.isActive() );
    };
    const float32 nan      = std::numeric_limits<float32>::quiet_NaN();
    const float32 infinity = std::numeric_limits<float32>::infinity();
    expectRejected( kindAt, static_cast<int64>( 9 ) );                      // 종류
    expectRejected( clearedAt, static_cast<uint8>( 2 ) );                   // 클리어 표시
    expectRejected( kActorAt, nan );                                        // 위치
    expectRejected( kActorAt + 4, infinity );                               // 위치
    expectRejected( kActorAt + 8, nan );                                    // 체력
    expectRejected( kActorAt + 8, -1.0f );                                  // 체력
    expectRejected( kActorAt + 8, 11.0f );                                  // 최대 10 초과
    expectRejected( kActorAt + 16, static_cast<uint16>( 1 ) );              // 종 인덱스 범위 밖
    expectRejected( kActorAt + 18, static_cast<uint8>( 2 ) );               // 생존 표시
    expectRejected( size - ProjectileAt::kPosition, nan );                  // 탄 위치
    expectRejected( size - ProjectileAt::kPosition + 8, 1.0f );             // 탄 위치 Z(룸 평면 밖)
    expectRejected( size - ProjectileAt::kVelocity, infinity );             // 탄 속도
    expectRejected( size - ProjectileAt::kDamage, -1.0f );                  // 탄 피해
    expectRejected( size - ProjectileAt::kRadius, -1.0f );                  // 탄 반지름
    expectRejected( size - ProjectileAt::kFlags, static_cast<uint8>( 2 ) ); // 탄 깃발(살아 있지 않다)

    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target.readState( truncated ) );
    sw::Archive whole( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( target.readState( whole ) );
    SW_EXPECT_EQUAL( 1, target.getAliveEnemyCount() );
}

/**
 * @brief [ActionCombatRoomTest] 카탈로그에 없는 종의 상태는 오류를 남기고 거절한다
 */
SW_TEST_CASE( ActionCombatRoomTest, ReadRejectsUnknownMonster )
{
    SW_EXPECT_LOG( "Action.RoomMonsterNotInCatalog" );
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    sw::ActionCombatRoom source;
    ActionCombatRoomTestUtil::beginHallWith( source, kGrunt, sw::float2{ 5.0f, 0.0f } );
    sw::Archive written;
    source.writeState( written );

    sw::vector<uint8> listByte( written.getData(), written.getData() + static_cast<size_t>( written.getSize() ) );
    // 이름 "grunt" 의 첫 글자: 리플렉션 머리 뒤 종류(int64) 클리어(1), 손 본문의 쿨다운 셋(12) 종 수(4) 이름 길이(4) 다음
    listByte[sw::StateIO::kReflectedHeaderBytes + sizeof( int64 ) + 1 + 12 + 4 + 4] = static_cast<uint8>( 'x' );
    sw::ActionCombatRoom target;
    sw::Archive          reading( listByte.data(), listByte.size() );
    SW_EXPECT_FALSE( target.readState( reading ) );
    SW_EXPECT_FALSE( target.isActive() );
}

/**
 * @brief [ActionCombatRoomTest] 결정성: 걷는 적 · 쏘는 적 · 서 있는 적의 홀에서 걸음 번호로 정한 공격 · 대시 · 이동이면 상태 바이트가 같고, 중간 세이브를 읽어 이어 돌려도 같다
 */
SW_TEST_CASE( ActionCombatRoomTest, RoomDeterminism )
{
    RoomBench bench;
    SW_REQUIRE_TRUE( bench.setUp() );
    sw::game::ScopedGameScope scopedScope( bench._scope );

    const auto setup = []( sw::ActionCombatRoom& room )
    {
        sw::ActionCombatRoomLayout layout;
        for ( const sw::ActionCombatRoomSpawnDef& spawn : {
                  sw::ActionCombatRoomSpawnDef{    kWalker,  sw::float2{ 3.0f, 1.0f }},
                  sw::ActionCombatRoomSpawnDef{    kGunner, sw::float2{ -2.0f, 2.0f }},
                  sw::ActionCombatRoomSpawnDef{kSideGunner, sw::float2{ 2.0f, -2.0f }},
                  sw::ActionCombatRoomSpawnDef{     kGrunt,  sw::float2{ 0.8f, 0.0f }}
        } )
        {
            layout._listHallSpawn.push_back( spawn );
        }
        room.setLayout( layout );
        room.beginHall();
        return room.getAliveEnemyCount() == 4;
    };
    const auto step = []( sw::ActionCombatRoom& room, int32 stepIndex )
    {
        const sw::float2          position{ static_cast<float32>( stepIndex % 8 ) * 0.25f - 1.0f, static_cast<float32>( stepIndex % 5 ) * 0.2f - 0.4f };
        const sw::FacingDirection facing = stepIndex % 4 < 2 ? sw::FacingDirection::Right : sw::FacingDirection::Left;
        (void)room.update( sw::GameSeconds{ kRoomStep }, ActionCombatRoomTestUtil::makeInput( position, facing, stepIndex % 2 == 0, stepIndex % 7 == 3 ) );
    };
    SW_EXPECT_TRUE( sw::StateBytesTestUtil::isDeterministic<sw::ActionCombatRoom>( setup, step, 120 ) );
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
    <Shot angle="0" projectile="orb"/>
    <Shot angle="85.94367" projectile="orb_slow"/>
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
    sw::GameObjectManager       manager;
    sw::AbilitySystemComponent* pUnit = manager.createGameObject( sw::hashed_string( "Boss" ) )->addComponent<sw::AbilitySystemComponent>();
    SW_REQUIRE_NOT_NULL( pUnit );
    SW_REQUIRE_NOT_NULL( pUnit->addAttributeSet( sw::test::makeCombatAttributeSet() ) );
    SW_REQUIRE_TRUE( pBoss->_stats.applyTo( *pUnit ) ); // 카탈로그의 스탯이 전투 세트에 그대로 옮는다
    SW_EXPECT_NEAR( 220.0f, pUnit->getAttributeValue( sw::CombatAttributes::health() ).value_or( 0.0f ), kRoomTolerance );
    SW_EXPECT_NEAR( 300.0f, pUnit->getAttributeValue( sw::CombatAttributes::maxHealth() ).value_or( 0.0f ), kRoomTolerance );
    SW_EXPECT_NEAR( 1.25f, pBoss->_ai._firstAttackDelay, kRoomTolerance );
    SW_REQUIRE_EQUAL( size_t( 2 ), pBoss->_listShot.size() );
    SW_EXPECT_NEAR( 1.5f, pBoss->_listShot[1]._angle.radians(), kRoomTolerance );
    SW_EXPECT_TRUE( pBoss->_listShot[1]._projectileID.key() == sw::hashed_string( "orb_slow" ) );
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
    SW_EXPECT_LOG( "Xml.UnknownElement" );

    sw::MonsterCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MonsterCatalog>
  <Monster id="rapid" name="r"><AI coolTime="0"/><Shot projectile="bolt"/></Monster>
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
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    SW_EXPECT_LOG( "XMLCatalog.NoEntries" );

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

    sw::MonsterCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<MonsterCatalog><Monster id="a" name="a"><AI patrolRange="4"/></Monster><Monster id="b" name="b"><AI coolTime="1"/></Monster></MonsterCatalog>)", "dropped" ) );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "a" ) ) == nullptr );
    SW_EXPECT_TRUE( catalog.findMonster( sw::hashed_string( "b" ) ) != nullptr );
}
