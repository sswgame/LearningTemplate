#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionCombatSimulation.h"
#include "GameFramework/Kits/Genre/Action/ActionPlatformer/ActionPlatformerCatalog.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"
#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"

#include <cstring>
#include <limits>

// ActionCombatSimulationTest — 플랫포머 싸움: 콤보 잇기(캔슬 창 · 공격 기억 · 끝난 뒤 시작), 히트스톱이 세상을 멈춤, 패리 반사 경계, 탄 수명 · 벽 · 맞은 탄 꺼내기, 총, 상태 왕복과 깨진 탄 거절.
// 결함: 기술 표에 없는 기술을 든 콤보가 공격을 말없이 삼킴 · 음수 반경이 맞음 · 겹치는 탄 id · 수치가 깨진 탄을 받음. 키트 GF_ActionPlatformer(nogpu).

namespace
{
    constexpr const utf8* kRigCatalogXML = R"(<ActionPlatformer>
  <Motor halfExtents="0.35,0.45" runSpeed="7" groundAcceleration="70" airAcceleration="40" jumpHeight="3.2" timeToApex="0.38" fallGravityScale="1.6" jumpCutRatio="0.45" maxFallSpeed="18" coyoteTime="0.1" jumpBufferTime="0.12" wallSlideSpeed="2.5" wallJumpSpeedX="7.5" wallJumpSpeedY="12" wallJumpLockTime="0.15" dashSpeed="18" dashDuration="0.15" dashCooldown="0.3" climbSpeed="4" dropThroughTime="0.25" extraJumpCount="0" airDashCount="1" wallJump="true"/>
  <Grading><Grade id="S" min="90"/></Grading>
  <Parry window="4" radius="2" reflectSpeed="2" reflectDamage="3" hitstop="5" buffer="3"/>
  <Combo id="sword" moves="slash1,slash2"/>
  <Combo id="long" moves="heavy"/>
  <Combo id="ghost" moves="slash1,missing"/>
</ActionPlatformer>)";

    // slash1 전체 6 프레임(캔슬 창 2..6), slash2 전체 2 프레임, heavy 전체 9 프레임.
    constexpr const utf8* kRigMoveXML = R"(<MoveCatalog>
  <Move id="slash1" startup="2" active="1" recovery="4" hitstop="3"><Cancel from="2" to="6" moves="slash2"/></Move>
  <Move id="slash2" startup="1" active="1" recovery="1"/>
  <Move id="heavy" startup="1" active="1" recovery="8"/>
</MoveCatalog>)";

    const sw::hashed_string kSword{ "sword" };
    const sw::hashed_string kLong{ "long" };
    const sw::hashed_string kSlash1{ "slash1" };
    const sw::hashed_string kSlash2{ "slash2" };

    const sw::float2  kRigPlayer{ 0.0f, 0.0f };
    constexpr float32 kRigTolerance = 1.0e-3f;

    struct ActionCombatSimulationTestUtil
    {
        static sw::ProjectileState makeShot( const sw::float2& position, const sw::float2& velocity, sw::ActionTeam team = sw::ActionTeam::Enemy, float32 damage = 5.0f, float32 lifetime = 3.0f )
        {
            return sw::ActionCombatSimulation::makeProjectile( position, velocity, team, damage, lifetime );
        }

        static bool isTeam( const sw::ProjectileState& projectile, sw::ActionTeam team ) { return projectile._team == static_cast<int32>( team ); }

        /** @brief 퍼짐이 없어 탄 방향이 정확한 산탄총입니다(3 알, 탄창 2, 예비 0). 탄은 `makePellet` 입니다. */
        static sw::WeaponDef makeShotgun()
        {
            sw::WeaponDef weapon;
            weapon._id             = sw::hashed_string( "gun" );
            weapon._projectileID   = sw::CatalogKey<sw::ProjectileDef>( sw::hashed_string( "pellet" ) );
            weapon._minSpread      = sw::Radians{};
            weapon._maxSpread      = sw::Radians{};
            weapon._spreadPerShot  = sw::Radians{};
            weapon._pelletCount    = 3;
            weapon._magazineSize   = 2;
            weapon._maxReserveAmmo = 0;
            weapon._fireInterval   = 0.1f;
            return weapon;
        }

        /** @brief 탄속 30, 피해 7, 사거리 60 인 알입니다. */
        static sw::ProjectileDef makePellet()
        {
            sw::ProjectileDef pellet;
            pellet._id     = sw::hashed_string( "pellet" );
            pellet._speed  = 30.0f;
            pellet._damage = 7.0f;
            pellet._range  = 60.0f;
            return pellet;
        }
    };
} // namespace

/**
 * @brief [ActionCombatSimulationTest] 첫 기술은 누른 다음 프레임에 나가고, 다음 기술은 캔슬 창이 열린 프레임에 기억한 공격으로 이어지며, 콤보가 끝나면 이어 낸 수를 알린다
 */
SW_TEST_CASE( ActionCombatSimulationTest, ComboChainsThroughTheCancelWindow )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRigCatalogXML, "ActionCombatSimulationTest" ) );
    sw::MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kRigMoveXML, "ActionCombatSimulationTest" ) );
    sw::ActionCombatSimulation rig;
    SW_REQUIRE_TRUE( rig.initialize( &catalog, &moves, kSword ) );

    rig.pressAttack();
    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_EQUAL( 0, rig.getComboIndex() );
    SW_EXPECT_EQUAL( 1, rig.getTimeline().getFrame() );

    rig.pressAttack();
    rig.advanceFrame( nullptr, kRigPlayer ); // 1 프레임은 창 밖 — 기억만 줄고 기술은 2 프레임으로
    SW_EXPECT_EQUAL( 0, rig.getComboIndex() );
    SW_EXPECT_EQUAL( 2, rig.getTimeline().getFrame() );
    rig.advanceFrame( nullptr, kRigPlayer ); // 2 프레임은 창 안
    SW_EXPECT_EQUAL( 1, rig.getComboIndex() );
    SW_EXPECT_EQUAL( kSlash2, rig.getTimeline().getMove()._id );
    SW_EXPECT_EQUAL( 1, rig.getTimeline().getFrame() );

    rig.advanceFrame( nullptr, kRigPlayer );
    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_FALSE( rig.getTimeline().isPlaying() );
    SW_EXPECT_EQUAL( -1, rig.getComboIndex() );

    sw::vector<sw::ActionCombatEvent> listEvent;
    rig.pollEvents( listEvent );
    SW_REQUIRE_EQUAL( size_t( 3 ), listEvent.size() );
    SW_EXPECT_TRUE( listEvent[0]._type == sw::ActionCombatEventKind::MoveStarted );
    SW_EXPECT_EQUAL( kSlash1, listEvent[0]._id );
    SW_EXPECT_EQUAL( 0, listEvent[0]._value );
    SW_EXPECT_EQUAL( kSlash2, listEvent[1]._id );
    SW_EXPECT_EQUAL( 1, listEvent[1]._value );
    SW_EXPECT_TRUE( listEvent[2]._type == sw::ActionCombatEventKind::ComboEnded );
    SW_EXPECT_EQUAL( 2, listEvent[2]._value );
}

/**
 * @brief [ActionCombatSimulationTest] 공격 기억은 `buffer` 프레임 안에 기술이 끝나면 다음 콤보로 이어지고, 넘기면 사라진다
 */
SW_TEST_CASE( ActionCombatSimulationTest, AttackBufferExpires )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRigCatalogXML, "ActionCombatSimulationTest" ) );
    sw::MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kRigMoveXML, "ActionCombatSimulationTest" ) );

    sw::ActionCombatSimulation late; // 8 프레임에서 누르면 기억 3 안에 기술이 끝난다
    SW_REQUIRE_TRUE( late.initialize( &catalog, &moves, kLong ) );
    late.pressAttack();
    late.advanceFrame( nullptr, kRigPlayer );
    while ( late.getTimeline().getFrame() < 8 )
    {
        late.advanceFrame( nullptr, kRigPlayer );
    }
    late.pressAttack();
    late.advanceFrame( nullptr, kRigPlayer ); // 9 프레임, 기억 2
    late.advanceFrame( nullptr, kRigPlayer ); // 끝남, 기억 1
    SW_EXPECT_FALSE( late.getTimeline().isPlaying() );
    late.advanceFrame( nullptr, kRigPlayer ); // 다시 시작
    SW_EXPECT_TRUE( late.getTimeline().isPlaying() );
    SW_EXPECT_EQUAL( 1, late.getTimeline().getFrame() );

    sw::ActionCombatSimulation early; // 2 프레임에서 누르면 기억이 끝날 때까지 기술이 안 끝난다
    SW_REQUIRE_TRUE( early.initialize( &catalog, &moves, kLong ) );
    early.pressAttack();
    early.advanceFrame( nullptr, kRigPlayer );
    early.advanceFrame( nullptr, kRigPlayer );
    early.pressAttack();
    while ( early.getTimeline().isPlaying() )
    {
        early.advanceFrame( nullptr, kRigPlayer );
    }
    early.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_FALSE( early.getTimeline().isPlaying() );
}

/**
 * @brief [ActionCombatSimulationTest] 결함: 기술 표에 없는 기술을 든 콤보를 받아, 그 차례의 공격이 말없이 사라졌다. 모르는 콤보 · 기술 표 없음과 함께 초기화가 거절하고, 총과 패리는 그대로 쓴다
 */
SW_TEST_CASE( ActionCombatSimulationTest, ComboWithUnknownMoveIsRejected )
{
    SW_EXPECT_LOG( "ActionCombatSimulation.ComboMoveUnknown" );
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRigCatalogXML, "ActionCombatSimulationTest" ) );
    sw::MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kRigMoveXML, "ActionCombatSimulationTest" ) );

    sw::ActionCombatSimulation rig;
    SW_EXPECT_FALSE( rig.initialize( &catalog, nullptr, kSword ) );
    SW_EXPECT_FALSE( rig.initialize( nullptr, &moves, kSword ) );
    SW_EXPECT_FALSE( rig.initialize( &catalog, &moves, sw::hashed_string( "nothing" ) ) );
    SW_EXPECT_FALSE( rig.initialize( &catalog, &moves, sw::hashed_string( "ghost" ) ) );

    rig.pressAttack();
    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_FALSE( rig.getTimeline().isPlaying() );
    rig.pressParry();
    SW_EXPECT_TRUE( rig.isParryActive() );
}

/**
 * @brief [ActionCombatSimulationTest] 닿으면 기술의 히트스톱만큼 탄 · 패리 창 · 기술이 함께 멈췄다가 같이 풀린다. 기술이 없으면 멈추지 않는다
 */
SW_TEST_CASE( ActionCombatSimulationTest, HitstopFreezesTheWorld )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRigCatalogXML, "ActionCombatSimulationTest" ) );
    sw::MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kRigMoveXML, "ActionCombatSimulationTest" ) );
    sw::ActionCombatSimulation rig;
    SW_REQUIRE_TRUE( rig.initialize( &catalog, &moves, kSword ) );

    rig.registerMeleeContact( false ); // 기술이 없다
    SW_EXPECT_FALSE( rig.isFrozen() );

    rig.pressAttack();
    rig.advanceFrame( nullptr, kRigPlayer );
    (void)rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 50.0f, 0.0f }, sw::float2{ 60.0f, 0.0f } ) );
    rig.pressParry();
    rig.registerMeleeContact( false );
    SW_EXPECT_TRUE( rig.isFrozen() );
    SW_EXPECT_EQUAL( 3, rig.getHitstopFrames() );

    for ( int32 frame = 0; frame < 3; ++frame )
    {
        rig.advanceFrame( nullptr, kRigPlayer );
        SW_EXPECT_NEAR( 50.0f, rig.getProjectiles()[0]._position._x, kRigTolerance );
        SW_EXPECT_EQUAL( 1, rig.getTimeline().getFrame() );
        SW_EXPECT_TRUE( rig.isParryActive() );
    }
    SW_EXPECT_FALSE( rig.isFrozen() );

    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_EQUAL( 2, rig.getTimeline().getFrame() );
    SW_EXPECT_NEAR( 51.0f, rig.getProjectiles()[0]._position._x, kRigTolerance );

    // 패리 창 4 프레임 중 하나를 썼다 — 멈춘 프레임은 세지 않았다
    rig.advanceFrame( nullptr, kRigPlayer );
    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_TRUE( rig.isParryActive() );
    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_FALSE( rig.isParryActive() );
}

/**
 * @brief [ActionCombatSimulationTest] 패리는 반경 경계(포함) 안의 적 탄만 뒤집어 내 편으로 만들고 속도 · 피해 배율을 건다. 내 편 탄과 먼 탄은 그대로이고, 더 짧은 히트스톱은 긴 것을 줄이지 않는다
 */
SW_TEST_CASE( ActionCombatSimulationTest, ParryReflectsOnlyEnemyShotsInRadius )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRigCatalogXML, "ActionCombatSimulationTest" ) );
    sw::MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kRigMoveXML, "ActionCombatSimulationTest" ) );
    sw::ActionCombatSimulation rig;
    SW_REQUIRE_TRUE( rig.initialize( &catalog, &moves, kSword ) );

    const uint32 edgeID = rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 2.0f, 0.0f }, sw::float2{ -6.0f, 0.0f } ) );
    (void)rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 2.01f, 0.0f }, sw::float2{ 0.0f, 0.0f } ) );
    (void)rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 1.0f, 0.0f }, sw::float2{ 0.0f, 0.0f }, sw::ActionTeam::Player ) );

    rig.pressAttack();
    rig.pressParry();
    rig.advanceFrame( nullptr, kRigPlayer );

    const sw::vector<sw::ProjectileState>& listShot = rig.getProjectiles();
    SW_REQUIRE_EQUAL( size_t( 3 ), listShot.size() );
    SW_EXPECT_TRUE( ActionCombatSimulationTestUtil::isTeam( listShot[0], sw::ActionTeam::Player ) );
    SW_EXPECT_TRUE( listShot[0]._bReflected == SW_TRUE );
    SW_EXPECT_NEAR( 12.0f, listShot[0]._velocity._x, kRigTolerance );
    SW_EXPECT_NEAR( 15.0f, listShot[0]._damage, kRigTolerance );
    SW_EXPECT_TRUE( ActionCombatSimulationTestUtil::isTeam( listShot[1], sw::ActionTeam::Enemy ) );
    SW_EXPECT_TRUE( listShot[1]._bReflected == SW_FALSE );
    SW_EXPECT_TRUE( listShot[2]._bReflected == SW_FALSE );
    SW_EXPECT_NEAR( 5.0f, listShot[2]._damage, kRigTolerance );

    SW_EXPECT_EQUAL( 5, rig.getHitstopFrames() );
    rig.registerMeleeContact( false ); // 기술의 히트스톱 3 < 남은 5
    SW_EXPECT_EQUAL( 5, rig.getHitstopFrames() );

    sw::vector<sw::ActionCombatEvent> listEvent;
    rig.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::ActionCombatEvent::_type>( listEvent, sw::ActionCombatEventKind::ParryStarted ) );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::ActionCombatEvent::_type>( listEvent, sw::ActionCombatEventKind::ProjectileReflected ) );
    SW_EXPECT_EQUAL( 1, sw::VectorUtil::countBy<&sw::ActionCombatEvent::_type>( listEvent, sw::ActionCombatEventKind::HitstopStarted ) );
    for ( const sw::ActionCombatEvent& event : listEvent )
    {
        if ( event._type == sw::ActionCombatEventKind::ProjectileReflected )
            SW_EXPECT_EQUAL( static_cast<int32>( edgeID ), event._value );
    }

    // 다 풀어도 기술은 자기 히트스톱만큼 줄었고 프레임은 그대로다
    for ( int32 frame = 0; frame < 5; ++frame )
    {
        rig.advanceFrame( nullptr, kRigPlayer );
    }
    SW_EXPECT_FALSE( rig.isFrozen() );
    SW_EXPECT_FALSE( rig.getTimeline().isInHitstop() );
    SW_EXPECT_EQUAL( 1, rig.getTimeline().getFrame() );
}

/**
 * @brief [ActionCombatSimulationTest] 패리 창이 닫힌 뒤에 들어온 탄은 되받아치지 못한다
 */
SW_TEST_CASE( ActionCombatSimulationTest, ParryWindowCloses )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRigCatalogXML, "ActionCombatSimulationTest" ) );
    sw::ActionCombatSimulation rig;
    SW_EXPECT_FALSE( rig.initialize( &catalog, nullptr, sw::hashed_string{} ) );
    rig.pressParry();
    for ( int32 frame = 0; frame < 4; ++frame )
    {
        SW_EXPECT_TRUE( rig.isParryActive() );
        rig.advanceFrame( nullptr, kRigPlayer );
    }
    SW_EXPECT_FALSE( rig.isParryActive() );
    (void)rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 0.5f, 0.0f }, sw::float2{ 0.0f, 0.0f } ) );
    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_TRUE( ActionCombatSimulationTestUtil::isTeam( rig.getProjectiles()[0], sw::ActionTeam::Enemy ) );
}

/**
 * @brief [ActionCombatSimulationTest] 탄은 수명이 다하면 사라지고, 벽 칸에 들어서면 사라진다(지형을 주지 않으면 벽을 보지 않는다)
 */
SW_TEST_CASE( ActionCombatSimulationTest, ProjectilesExpireAndHitWalls )
{
    sw::ActionCombatSimulation rig;
    (void)rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 0.0f, 0.0f }, sw::float2{ 0.0f, 0.0f }, sw::ActionTeam::Enemy, 1.0f, 1.5f * sw::ActionCombatSimulation::kFrameTime ) );
    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.getProjectiles().size() );
    rig.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_TRUE( rig.getProjectiles().empty() );

    sw::PlatformTileMap map;
    SW_REQUIRE_TRUE( map.loadFromXMLText( "    \n  # \n    \n", 1.0f, sw::float2{ 0.0f, 0.0f } ) );
    const sw::float2           start{ 1.5f, 1.5f };
    const sw::float2           fast{ 60.0f, 0.0f }; // 한 프레임에 한 칸
    sw::ActionCombatSimulation walled;
    (void)walled.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( start, fast ) );
    walled.advanceFrame( &map, kRigPlayer );
    SW_EXPECT_TRUE( walled.getProjectiles().empty() );

    sw::ActionCombatSimulation open;
    (void)open.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( start, fast ) );
    open.advanceFrame( nullptr, kRigPlayer );
    SW_EXPECT_EQUAL( size_t( 1 ), open.getProjectiles().size() );
}

/**
 * @brief [ActionCombatSimulationTest] 맞은 탄 꺼내기는 그 편만, 반경 경계를 포함해 꺼내고 결과 목록을 먼저 비운다. 결함: 음수 반경이 제곱되어 양수처럼 맞았다
 */
SW_TEST_CASE( ActionCombatSimulationTest, TakeHitsFiltersTeamAndRadius )
{
    sw::ActionCombatSimulation rig;
    (void)rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 3.0f, 4.0f }, sw::float2{ 0.0f, 0.0f } ) );                         // 거리 5
    (void)rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 6.0f, 0.0f }, sw::float2{ 0.0f, 0.0f } ) );                         // 거리 6
    (void)rig.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 1.0f, 0.0f }, sw::float2{ 0.0f, 0.0f }, sw::ActionTeam::Player ) ); // 다른 편

    sw::vector<sw::ProjectileState> listHit;
    listHit.push_back( sw::ProjectileState{} );
    SW_EXPECT_EQUAL( 0, rig.takeProjectileHits( sw::ActionTeam::Enemy, kRigPlayer, -10.0f, listHit ) );
    SW_EXPECT_TRUE( listHit.empty() );
    SW_EXPECT_EQUAL( size_t( 3 ), rig.getProjectiles().size() );

    SW_EXPECT_EQUAL( 1, rig.takeProjectileHits( sw::ActionTeam::Enemy, kRigPlayer, 5.0f, listHit ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listHit.size() );
    SW_EXPECT_NEAR( 3.0f, listHit[0]._position._x, kRigTolerance );
    SW_EXPECT_EQUAL( size_t( 2 ), rig.getProjectiles().size() );

    SW_EXPECT_EQUAL( 1, rig.takeProjectileHits( sw::ActionTeam::Player, sw::float2{ 1.0f, 0.0f }, 0.0f, listHit ) ); // 반경 0 은 정확히 그 자리
    SW_EXPECT_TRUE( ActionCombatSimulationTestUtil::isTeam( listHit[0], sw::ActionTeam::Player ) );
}

/**
 * @brief [ActionCombatSimulationTest] 총은 알마다 내 편 탄을 겨눈 쪽으로 내고(탄속 · 피해 · 사거리는 장전한 탄), 쿨다운 · 빈 탄창 · 미장비를 알린다. 길이 0 인 겨냥은 오른쪽이다. 광선 탄(속도 0)은 2D 총에 넣지 않는다
 */
SW_TEST_CASE( ActionCombatSimulationTest, GunSpawnsPelletsAndReportsWhyItCannot )
{
    sw::ActionCombatSimulation rig;
    SW_EXPECT_TRUE( rig.fireGun( sw::float2{ 1.0f, 2.0f }, sw::float2{ 0.0f, 5.0f }, true ) == sw::WeaponFireResult::OutOfAmmo );
    SW_EXPECT_TRUE( rig.getProjectiles().empty() );

    sw::ProjectileDef ray = ActionCombatSimulationTestUtil::makePellet();
    ray._speed            = 0.0f;
    SW_EXPECT_LOG( "ActionCombatSimulation.GunNeedsFlyingProjectile" );
    SW_EXPECT_FALSE( rig.equipGun( ActionCombatSimulationTestUtil::makeShotgun(), ray, 0 ) );

    SW_REQUIRE_TRUE( rig.equipGun( ActionCombatSimulationTestUtil::makeShotgun(), ActionCombatSimulationTestUtil::makePellet(), 0 ) );
    SW_EXPECT_TRUE( rig.fireGun( sw::float2{ 1.0f, 2.0f }, sw::float2{ 0.0f, 5.0f }, true ) == sw::WeaponFireResult::Fired );
    const sw::vector<sw::ProjectileState>& listShot = rig.getProjectiles();
    SW_REQUIRE_EQUAL( size_t( 3 ), listShot.size() );
    SW_EXPECT_NEAR( 1.0f, listShot[0]._position._x, kRigTolerance );
    SW_EXPECT_NEAR( 0.0f, listShot[0]._velocity._x, kRigTolerance );
    SW_EXPECT_NEAR( 30.0f, listShot[0]._velocity._y, kRigTolerance );
    SW_EXPECT_NEAR( 7.0f, listShot[0]._damage, kRigTolerance );
    SW_EXPECT_NEAR( 60.0f, listShot[0]._range, kRigTolerance );
    SW_EXPECT_TRUE( ActionCombatSimulationTestUtil::isTeam( listShot[0], sw::ActionTeam::Player ) );
    SW_EXPECT_NOT_EQUAL( listShot[0]._id, listShot[1]._id );

    SW_EXPECT_TRUE( rig.fireGun( sw::float2{}, sw::float2{ 1.0f, 0.0f }, true ) == sw::WeaponFireResult::Cooling );
    SW_EXPECT_EQUAL( size_t( 3 ), listShot.size() );

    for ( int32 frame = 0; frame < 7; ++frame ) // 간격 0.1 초 = 6 프레임 + 여유
    {
        rig.advanceFrame( nullptr, kRigPlayer );
    }
    SW_EXPECT_TRUE( rig.fireGun( sw::float2{}, sw::float2{ 0.0f, 0.0f }, true ) == sw::WeaponFireResult::Fired );
    SW_REQUIRE_EQUAL( size_t( 6 ), listShot.size() );
    SW_EXPECT_NEAR( 30.0f, listShot[5]._velocity._x, kRigTolerance );

    for ( int32 frame = 0; frame < 7; ++frame )
    {
        rig.advanceFrame( nullptr, kRigPlayer );
    }
    SW_EXPECT_TRUE( rig.fireGun( sw::float2{}, sw::float2{ 1.0f, 0.0f }, true ) == sw::WeaponFireResult::OutOfAmmo ); // 탄창 2 를 다 썼고 예비가 없다

    sw::vector<sw::ActionCombatEvent> listEvent;
    rig.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 2, sw::VectorUtil::countBy<&sw::ActionCombatEvent::_type>( listEvent, sw::ActionCombatEventKind::GunFired ) );
}

/**
 * @brief [ActionCombatSimulationTest] 쓰고 읽은 판은 원본과 같이 굴러간다(기술 진행 · 탄 · 총). 초기화 전 · 모르는 콤보 · 총 장비가 다른 쪽은 거절한다
 */
SW_TEST_CASE( ActionCombatSimulationTest, StateRoundTripResumesIdentically )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRigCatalogXML, "ActionCombatSimulationTest" ) );
    sw::MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kRigMoveXML, "ActionCombatSimulationTest" ) );

    sw::ActionCombatSimulation original;
    SW_REQUIRE_TRUE( original.initialize( &catalog, &moves, kSword ) );
    const sw::WeaponDef     gun    = ActionCombatSimulationTestUtil::makeShotgun();
    const sw::ProjectileDef pellet = ActionCombatSimulationTestUtil::makePellet();
    SW_REQUIRE_TRUE( original.equipGun( gun, pellet, 0 ) );
    original.pressAttack();
    original.advanceFrame( nullptr, kRigPlayer );
    original.pressParry();
    SW_REQUIRE_TRUE( original.fireGun( sw::float2{}, sw::float2{ 1.0f, 0.0f }, true ) == sw::WeaponFireResult::Fired );

    sw::Archive written;
    original.writeState( written );

    sw::ActionCombatSimulation uninitialized;
    sw::Archive                forUninitialized( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( uninitialized.readState( forUninitialized ) );

    sw::ActionCombatSimulation noGun;
    SW_REQUIRE_TRUE( noGun.initialize( &catalog, &moves, kSword ) );
    sw::Archive forNoGun( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( noGun.readState( forNoGun ) );

    sw::ActionPlatformerCatalog otherCatalog;
    SW_REQUIRE_TRUE( otherCatalog.loadFromXMLText( R"(<ActionPlatformer><Motor halfExtents="0.35,0.45" runSpeed="7" groundAcceleration="70" airAcceleration="40" jumpHeight="3.2" timeToApex="0.38" fallGravityScale="1.6" jumpCutRatio="0.45" maxFallSpeed="18" coyoteTime="0.1" jumpBufferTime="0.12" wallSlideSpeed="2.5" wallJumpSpeedX="7.5" wallJumpSpeedY="12" wallJumpLockTime="0.15" dashSpeed="18" dashDuration="0.15" dashCooldown="0.3" climbSpeed="4" dropThroughTime="0.25" extraJumpCount="0" airDashCount="1" wallJump="true"/><Grading><Grade id="S" min="90"/></Grading></ActionPlatformer>)", "other" ) );
    sw::ActionCombatSimulation otherCombo;
    SW_EXPECT_FALSE( otherCombo.initialize( &otherCatalog, &moves, kSword ) );
    SW_REQUIRE_TRUE( otherCombo.equipGun( gun, pellet, 0 ) );
    sw::Archive forOther( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( otherCombo.readState( forOther ) );

    sw::ActionCombatSimulation restored;
    SW_REQUIRE_TRUE( restored.initialize( &catalog, &moves, kLong ) );
    SW_REQUIRE_TRUE( restored.equipGun( gun, pellet, 0 ) );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );

    SW_EXPECT_EQUAL( original.getComboIndex(), restored.getComboIndex() );
    SW_EXPECT_EQUAL( original.getGun().getMagazineAmmo(), restored.getGun().getMagazineAmmo() );
    for ( int32 frame = 0; frame < 4; ++frame )
    {
        original.advanceFrame( nullptr, kRigPlayer );
        restored.advanceFrame( nullptr, kRigPlayer );
        SW_EXPECT_EQUAL( original.getTimeline().getFrame(), restored.getTimeline().getFrame() );
        SW_EXPECT_TRUE( original.isParryActive() == restored.isParryActive() );
        SW_REQUIRE_EQUAL( original.getProjectiles().size(), restored.getProjectiles().size() );
        SW_EXPECT_NEAR( original.getProjectiles()[0]._position._x, restored.getProjectiles()[0]._position._x, kRigTolerance );
    }

    // 다음 탄 id 도 이어진다
    const uint32 nextOriginal = original.spawnProjectile( sw::ProjectileState{} );
    const uint32 nextRestored = restored.spawnProjectile( sw::ProjectileState{} );
    SW_EXPECT_EQUAL( nextOriginal, nextRestored );
}

/**
 * @brief [ActionCombatSimulationTest] 결함: 다음 id 이상인 탄 id(새 탄과 겹친다)와 NaN · 무한 수치의 탄을 받아들였다. 편 · 표시 범위와 잘린 바이트까지 거절하고 지금 상태를 그대로 둔다
 */
SW_TEST_CASE( ActionCombatSimulationTest, ReadRejectsBrokenProjectiles )
{
    sw::ActionPlatformerCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( kRigCatalogXML, "ActionCombatSimulationTest" ) );
    sw::MoveCatalog moves;
    SW_REQUIRE_TRUE( moves.loadFromXMLText( kRigMoveXML, "ActionCombatSimulationTest" ) );

    sw::ActionCombatSimulation source;
    SW_REQUIRE_TRUE( source.initialize( &catalog, &moves, kSword ) );
    (void)source.spawnProjectile( ActionCombatSimulationTestUtil::makeShot( sw::float2{ 1.0f, 1.0f }, sw::float2{ 1.0f, 0.0f } ) );
    sw::Archive written;
    source.writeState( written );

    // 탄 하나는 맨 끝 줄이다(빈 정의 이름). 칸 자리는 끝에서 잰 `StateOffsetFromEnd` 를 쓴다.
    using ProjectileAt = sw::ProjectileSimulation::StateOffsetFromEnd;
    const size_t size  = static_cast<size_t>( written.getSize() );
    SW_REQUIRE_TRUE( size > ProjectileAt::kNameLength );

    sw::ActionCombatSimulation target;
    SW_REQUIRE_TRUE( target.initialize( &catalog, &moves, kSword ) );
    const auto expectRejected = [&]( size_t offsetFromEnd, const void* pValue, size_t valueSize )
    {
        sw::vector<uint8> listByte( written.getData(), written.getData() + size );
        std::memcpy( listByte.data() + size - offsetFromEnd, pValue, valueSize );
        sw::Archive reading( listByte.data(), listByte.size() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_TRUE( target.getProjectiles().empty() );
    };

    const uint32  idAtNext = 2; // 다음 id 와 같다(원본의 탄은 1 번)
    const int32   badTeam  = 2;
    const uint8   badFlags = 2; // 살아 있지 않다
    const float32 nan      = std::numeric_limits<float32>::quiet_NaN();
    const float32 infinity = std::numeric_limits<float32>::infinity();
    const float32 depth    = 1.0f; // 화면 평면 밖
    expectRejected( ProjectileAt::kID, &idAtNext, sizeof( idAtNext ) );
    expectRejected( ProjectileAt::kTeam, &badTeam, sizeof( badTeam ) );
    expectRejected( ProjectileAt::kFlags, &badFlags, sizeof( badFlags ) );
    expectRejected( ProjectileAt::kPosition, &nan, sizeof( nan ) );           // 위치 X
    expectRejected( ProjectileAt::kPosition - 8, &depth, sizeof( depth ) );   // 위치 Z
    expectRejected( ProjectileAt::kVelocity, &infinity, sizeof( infinity ) ); // 속도 X
    expectRejected( ProjectileAt::kDamage, &nan, sizeof( nan ) );             // 피해
    expectRejected( ProjectileAt::kLifetime, &nan, sizeof( nan ) );           // 수명

    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target.readState( truncated ) );
    SW_EXPECT_TRUE( target.getProjectiles().empty() );

    sw::Archive whole( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( target.readState( whole ) );
    SW_EXPECT_EQUAL( size_t( 1 ), target.getProjectiles().size() );
}
