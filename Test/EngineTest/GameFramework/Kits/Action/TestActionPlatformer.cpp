// 스테이지형 액션 플랫포머 키트 — 체크포인트 · 목숨 · 비밀 확정 · 등급, 우산 활공 · 갈고리 진자 · 드릴 굴착과 도약, 근접 콤보 캔슬 · 히트스톱, 총 · 패리 반사 · 적 패턴.
#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Combat/FrameData.h"
#include "GameFramework/Base/Actor/Combat/Weapon/Weapon.h"
#include "GameFramework/Base/Actor/Movement/PlatformerMotor2D.h"
#include "GameFramework/Kits/Action/ActionPlatformer/Catalog/ActionPlatformerCatalog.h"
#include "GameFramework/Kits/Action/ActionPlatformer/Rule/ActionCombatRig.h"
#include "GameFramework/Kits/Action/ActionPlatformer/Rule/ActionEnemyBrain.h"
#include "GameFramework/Kits/Action/ActionPlatformer/Rule/ActionPlatformerBody.h"
#include "GameFramework/Kits/Action/ActionPlatformer/Rule/ActionStageRun.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr float32 kActionPlatformerStep = 1.0f / 60.0f;

    constexpr const utf8* kActionPlatformerCatalogXml = R"(
<ActionPlatformer>
  <Grading time="2" hits="1" collect="1"><Grade id="C" min="0"/><Grade id="S" min="90"/><Grade id="B" min="50"/><Grade id="A" min="70"/></Grading>
  <Body glideFallSpeed="2" glideGravityScale="0.3" grappleRange="6" grappleMinLength="1" grappleSwingAcceleration="14" grappleMaxSpeed="24"
        drillSpeed="11" drillExitSpeed="11" drillJumpSpeed="16" drillEntryTime="0.2"/>
  <Parry window="8" radius="1.5" reflectSpeed="1.5" reflectDamage="2" hitstop="6" buffer="8"/>
  <Stage id="1-1" name="Rooftops" parTime="60" lives="3" hitTolerance="4" checkpoints="cp1,cp2" secrets="gem1,gem2"/>
  <Combo id="umbrella" moves="slash1,slash2,slash3"/>
  <Pattern id="gunner" start="patrol">
    <State id="patrol" frames="120" next="patrol" onNear="aim" near="6" moveX="1" onHit="stagger"/>
    <State id="aim" frames="10" next="shoot"/>
    <State id="shoot" frames="1" next="cooldown" fire="true" fireSpeed="12"/>
    <State id="cooldown" frames="30" next="patrol"/>
    <State id="stagger" frames="20" next="patrol"/>
  </Pattern>
</ActionPlatformer>
)";

    constexpr const utf8* kMoveXml = R"(
<MoveCatalog>
  <Move id="slash1" startup="3" active="2" recovery="10" damage="10" hitstop="4"><Cancel from="4" to="12" moves="slash2" onHit="true"/></Move>
  <Move id="slash2" startup="3" active="2" recovery="12" damage="12" hitstop="4"><Cancel from="4" to="14" moves="slash3"/></Move>
  <Move id="slash3" startup="5" active="3" recovery="20" damage="20" hitstop="8"/>
</MoveCatalog>
)";

    //         x: 012345678901234567890123
    constexpr const utf8* kRoomText = R"(
########################
#                      #
#                      #
#           O          #
#                      #
#                      #
#                      #
#                      #
#                      #
#       DDDD####       #
#       DDDD####       #
#       DDDD####       #
########################
)";

    struct ActionScene
    {
        ActionPlatformerCatalog _catalog;
        MoveCatalog             _moves;
        PlatformTileMap         _map;
        ActionTerrainGrid       _terrain;
        ActionPlatformerBody    _body;
        bool                    _bLoaded{ false };

        ActionScene()
        {
            _bLoaded = _catalog.loadFromXmlText( kActionPlatformerCatalogXml, "action" ) && _moves.loadFromXmlText( kMoveXml, "moves" );
            _terrain.loadFromText( kRoomText, 1.0f, float2{ 0.0f, 0.0f }, _map );
            _body.initialize( PlatformerSettings{}, _catalog.getBodySettings() );
        }

        uint32 run( const ActionBodyInput& input, int32 frameCount, float32* pOutMaxY = nullptr )
        {
            uint32 events = 0;
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _body.update( _map, _terrain, input, kActionPlatformerStep );
                events |= _body.getEvents();
                if ( pOutMaxY != nullptr )
                    *pOutMaxY = MathUtil::max( *pOutMaxY, _body.getPosition()._y );
            }
            return events;
        }
    };

    WeaponDef makePistol()
    {
        WeaponDef pistol;
        pistol._id              = hashed_string( "pistol" );
        pistol._fireInterval    = 0.25f;
        pistol._reloadTime      = 0.5f;
        pistol._damage          = 5.0f;
        pistol._range           = 40.0f;
        pistol._minSpread       = 0.0f;
        pistol._maxSpread       = 0.0f;
        pistol._spreadPerShot   = 0.0f;
        pistol._projectileSpeed = 20.0f;
        pistol._magazineSize    = 2;
        pistol._maxReserveAmmo  = 10;
        pistol._bAutomatic      = SW_FALSE;
        return pistol;
    }

    /** @brief 사수가 쏜 탄을 플레이어가 패리로 되받아쳐 사수를 맞히는 한 판입니다. 되받아친 프레임(없으면 −1)과 사수가 맞은 피해를 냅니다. */
    struct ParryDuel
    {
        int32   _reflectFrame{ -1 };
        int32   _playerHitCount{ 0 };
        float32 _enemyDamage{ 0.0f };
        int32   _shotCount{ 0 };
        int32   _frozenFrameCount{ 0 };
    };

    ParryDuel runParryDuel( const ActionScene& scene, bool bParry )
    {
        ParryDuel        duel;
        ActionCombatRig  rig;
        ActionEnemyBrain brain;
        (void)rig.initialize( &scene._catalog, &scene._moves, "umbrella" );
        (void)brain.initialize( scene._catalog.findPattern( "gunner" ), -1 );
        const float2             playerPosition{ 4.0f, 6.0f };
        float2                   enemyPosition{ 14.0f, 6.0f };
        bool                     bParryPressed = false;
        vector<ActionProjectile> listHit;
        for ( int32 frame = 0; frame < 240; ++frame )
        {
            if ( rig.isFrozen() )
                ++duel._frozenFrameCount;
            else
            {
                const ActionEnemyAction action = brain.advanceFrame( float2::getDistance( playerPosition, enemyPosition ) );
                enemyPosition._x += action._moveX * 3.0f * kActionPlatformerStep;
                if ( action._bFire == SW_TRUE && duel._shotCount == 0 )
                {
                    ActionProjectile shot;
                    shot._position = enemyPosition;
                    shot._velocity = float2{ -action._fireSpeed, 0.0f };
                    shot._damage   = 10.0f;
                    (void)rig.spawnProjectile( shot ); // id 는 쓰지 않는다 — 탄은 getProjectiles 로 다시 찾는다
                    ++duel._shotCount;
                }
            }
            // 탄이 가까워지면 패리.
            for ( const ActionProjectile& projectile : rig.getProjectiles() )
            {
                if ( bParry && bParryPressed == false && projectile._team == ActionTeam::Enemy && float2::getDistance( projectile._position, playerPosition ) < 2.5f )
                {
                    rig.pressParry();
                    bParryPressed = true;
                }
            }
            rig.advanceFrame( &scene._map, playerPosition );
            vector<ActionCombatEvent> listEvent;
            rig.drainEvents( listEvent );
            for ( const ActionCombatEvent& event : listEvent )
            {
                if ( event._type == ActionCombatEventType::ProjectileReflected )
                    duel._reflectFrame = frame;
            }
            duel._playerHitCount += rig.takeProjectileHits( ActionTeam::Enemy, playerPosition, 0.5f, listHit );
            if ( rig.takeProjectileHits( ActionTeam::Player, enemyPosition, 0.5f, listHit ) > 0 )
                duel._enemyDamage += listHit[0]._damage;
        }
        return duel;
    }

    /** @brief 상태 바이트를 꺼냅니다. */
    template <typename StateType>
    vector<uint8> capturePlatformerBytes( const StateType& state )
    {
        Archive archive;
        state.writeState( archive );
        vector<uint8> bytes;
        archive.writeData( bytes );
        return bytes;
    }
} // namespace

SW_TEST_CASE( ActionPlatformerTest, CheckpointsCommitSecretsAndLivesRunOut )
{
    ActionScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    ActionStageRun run;
    SW_EXPECT_FALSE( run.start( &scene._catalog, "9-9" ) );
    SW_ASSERT_TRUE( run.start( &scene._catalog, "1-1" ) );
    SW_EXPECT_EQUAL( 3, run.getLives() );

    // 체크포인트 전에 주운 비밀은 죽으면 제자리로 돌아간다.
    SW_EXPECT_TRUE( run.collectSecret( "gem1" ) );
    SW_EXPECT_FALSE( run.collectSecret( "gem1" ) );
    SW_EXPECT_FALSE( run.collectSecret( "otherStageGem" ) );
    SW_EXPECT_TRUE( run.die().empty() ); // 시작점에서
    SW_EXPECT_FALSE( run.isSecretFound( "gem1" ) );
    SW_EXPECT_EQUAL( 2, run.getLives() );

    // 다시 줍고 체크포인트를 지나면 확정 — 죽어도 남는다.
    SW_EXPECT_TRUE( run.collectSecret( "gem1" ) );
    SW_EXPECT_TRUE( run.reachCheckpoint( "cp1" ) );
    SW_EXPECT_FALSE( run.reachCheckpoint( "cp1" ) );
    SW_EXPECT_EQUAL( 1, run.getSecretCommittedCount() );
    SW_EXPECT_TRUE( run.die() == hashed_string( "cp1" ) );
    SW_EXPECT_TRUE( run.isSecretFound( "gem1" ) );

    // 뒤의 체크포인트를 지난 뒤 앞의 것으로 되돌아가지 않는다.
    SW_EXPECT_TRUE( run.reachCheckpoint( "cp2" ) );
    SW_EXPECT_FALSE( run.reachCheckpoint( "cp1" ) );
    SW_EXPECT_EQUAL( 1, run.getCheckpointIndex() );

    // 목숨이 다 하면 게임 오버 — 그 뒤로는 아무것도 바뀌지 않는다.
    SW_EXPECT_TRUE( run.collectSecret( "gem2" ) );
    (void)run.die();
    SW_EXPECT_TRUE( run.getState() == ActionStageState::GameOver );
    SW_EXPECT_EQUAL( 0, run.getLives() );
    SW_EXPECT_FALSE( run.collectSecret( "gem2" ) );
    SW_EXPECT_FALSE( run.clearStage() );

    // 처음부터 — 목숨 · 수집 · 체크포인트가 모두 되돌아간다.
    run.restartStage();
    SW_EXPECT_TRUE( run.getState() == ActionStageState::Playing );
    SW_EXPECT_EQUAL( 3, run.getLives() );
    SW_EXPECT_EQUAL( -1, run.getCheckpointIndex() );
    SW_EXPECT_FALSE( run.isSecretFound( "gem1" ) );
}

SW_TEST_CASE( ActionPlatformerTest, GradeWeighsClearTimeHitsAndSecrets )
{
    ActionScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );

    // 기준 시간 안 · 무피격 · 비밀 둘 다 = 100 점 S.
    ActionStageRun perfect;
    SW_ASSERT_TRUE( perfect.start( &scene._catalog, "1-1" ) );
    for ( int32 second = 0; second < 50; ++second )
    {
        perfect.update( 1.0f );
    }
    SW_EXPECT_TRUE( perfect.collectSecret( "gem1" ) && perfect.collectSecret( "gem2" ) );
    SW_EXPECT_TRUE( perfect.clearStage() );
    perfect.update( 30.0f ); // 깬 뒤의 시간은 세지 않는다
    ActionStageResult result = perfect.computeResult();
    SW_EXPECT_NEAR_EQUAL( 50.0f, result._clearTime, 1.0e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, result._score, 1.0e-3f );
    SW_EXPECT_TRUE( result._grade == hashed_string( "S" ) );
    SW_EXPECT_EQUAL( 2, result._secretFound );

    // 기준의 두 배 시간(0.5) · 허용 4 중 2 피격(0.5) · 비밀 반(0.5) — 시간 무게 2 → (1 + 0.5 + 0.5) / 4 = 50 점 B.
    ActionStageRun sloppy;
    SW_ASSERT_TRUE( sloppy.start( &scene._catalog, "1-1" ) );
    for ( int32 second = 0; second < 120; ++second )
    {
        sloppy.update( 1.0f );
    }
    sloppy.registerHit();
    sloppy.registerHit();
    SW_EXPECT_TRUE( sloppy.collectSecret( "gem2" ) );
    SW_EXPECT_TRUE( sloppy.clearStage() );
    result = sloppy.computeResult();
    SW_EXPECT_NEAR_EQUAL( 0.5f, result._timeScore, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, result._hitScore, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, result._collectScore, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, result._score, 1.0e-3f );
    SW_EXPECT_TRUE( result._grade == hashed_string( "B" ) );

    // 죽음도 피격으로 센다 — 같은 판에서 두 번 더 죽으면 피격 점수 0, 37.5 점 C.
    ActionStageRun reckless;
    SW_ASSERT_TRUE( reckless.start( &scene._catalog, "1-1" ) );
    for ( int32 second = 0; second < 120; ++second )
    {
        reckless.update( 1.0f );
    }
    reckless.registerHit();
    reckless.registerHit();
    (void)reckless.die();
    (void)reckless.die();
    SW_EXPECT_TRUE( reckless.collectSecret( "gem2" ) );
    SW_EXPECT_TRUE( reckless.clearStage() );
    result = reckless.computeResult();
    SW_EXPECT_NEAR_EQUAL( 0.0f, result._hitScore, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 37.5f, result._score, 1.0e-3f );
    SW_EXPECT_TRUE( result._grade == hashed_string( "C" ) );
}

SW_TEST_CASE( ActionPlatformerTest, UmbrellaGlideCapsFallSpeedUntilLanding )
{
    ActionScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );

    // 우산 없이 떨어지면 빨리 떨어진다.
    scene._body.setPosition( float2{ 3.5f, 9.5f } );
    (void)scene.run( ActionBodyInput{}, 20 );
    const float32 freeFallY     = scene._body.getPosition()._y;
    const float32 freeFallSpeed = scene._body.getVelocity()._y;
    SW_EXPECT_TRUE( freeFallSpeed < -5.0f );

    // 우산을 펴면 낙하 속도가 상한(2)을 넘지 않는다.
    ActionBodyInput glide;
    glide._bGlideHeld = SW_TRUE;
    scene._body.setPosition( float2{ 3.5f, 9.5f } );
    const uint32 startEvents = scene.run( glide, 1 );
    SW_EXPECT_TRUE( ( startEvents & ActionBodyEvent::kGlideStarted ) != 0 );
    SW_EXPECT_TRUE( scene._body.getMode() == ActionMoveMode::Glide );
    for ( int32 frame = 0; frame < 19; ++frame )
    {
        (void)scene.run( glide, 1 );
        SW_EXPECT_TRUE( scene._body.getVelocity()._y >= -2.0f - 1.0e-3f );
    }
    SW_EXPECT_TRUE( scene._body.getPosition()._y > freeFallY + 1.0f );

    // 땅에 닿으면 우산은 접힌다(설정이 돌아온다).
    const uint32 landEvents = scene.run( glide, 300 );
    SW_EXPECT_TRUE( ( landEvents & ActionBodyEvent::kGlideEnded ) != 0 );
    SW_EXPECT_TRUE( scene._body.getMode() == ActionMoveMode::Normal );
    SW_EXPECT_TRUE( scene._body.getMotor().isGrounded() );
    SW_EXPECT_NEAR_EQUAL( PlatformerSettings{}._maxFallSpeed, scene._body.getMotor().getSettings()._maxFallSpeed, 1.0e-4f );
}

SW_TEST_CASE( ActionPlatformerTest, GrapplePendulumHoldsRopeAndReleaseKeepsVelocity )
{
    ActionScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    SW_ASSERT_TRUE( scene._terrain.getGrapplePoints().size() == 1 );
    const float2 anchor = scene._terrain.getGrapplePoints()[0];
    SW_EXPECT_NEAR_EQUAL( 12.5f, anchor._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 9.5f, anchor._y, 1.0e-4f );

    // 너무 먼 곳에서는 걸리지 않는다.
    ActionBodyInput grab;
    grab._bGrapplePressed = SW_TRUE;
    grab._bGrappleHeld    = SW_TRUE;
    scene._body.setPosition( float2{ 3.5f, 9.5f } );
    SW_EXPECT_FALSE( ( scene.run( grab, 1 ) & ActionBodyEvent::kGrappleAttached ) != 0 );

    // 줄과 같은 높이 왼쪽 4 칸에서 걸고 놓아두면 아래로 휘어 오른쪽으로 넘어간다.
    scene._body.setPosition( float2{ 8.5f, 9.5f } );
    SW_EXPECT_TRUE( ( scene.run( grab, 1 ) & ActionBodyEvent::kGrappleAttached ) != 0 );
    SW_EXPECT_TRUE( scene._body.getMode() == ActionMoveMode::Grapple );
    SW_EXPECT_NEAR_EQUAL( 4.0f, scene._body.getRopeLength(), 1.0e-3f );
    ActionBodyInput hold;
    hold._bGrappleHeld = SW_TRUE;
    float32 maxX       = 0.0f;
    float32 minY       = 100.0f;
    float32 maxRange   = 0.0f;
    float2  bottomVelocity{};
    for ( int32 frame = 0; frame < 60; ++frame )
    {
        (void)scene.run( hold, 1 );
        const float2 position = scene._body.getPosition();
        maxRange              = MathUtil::max( maxRange, float2::getDistance( position, anchor ) );
        maxX                  = MathUtil::max( maxX, position._x );
        if ( position._y < minY )
        {
            minY           = position._y;
            bottomVelocity = scene._body.getVelocity();
        }
    }
    SW_EXPECT_TRUE( maxRange <= 4.0f + 1.0e-3f ); // 줄은 늘어나지 않는다
    SW_EXPECT_NEAR_EQUAL( anchor._y - 4.0f, minY, 0.1f );
    SW_EXPECT_TRUE( maxX > anchor._x + 2.5f ); // 반대편으로 넘어갔다
    SW_EXPECT_TRUE( bottomVelocity.getLength() > 12.0f );

    // 맨 아래에서 놓으면 그 속도를 그대로 들고 날아간다.
    scene._body.setPosition( float2{ 8.5f, 9.5f } );
    (void)scene.run( grab, 1 );
    for ( int32 frame = 0; frame < 120 && scene._body.getPosition()._x < anchor._x; ++frame )
    {
        (void)scene.run( hold, 1 );
    }
    const float2 releaseVelocity = scene._body.getVelocity();
    const float2 releasePosition = scene._body.getPosition();
    SW_EXPECT_TRUE( releaseVelocity._x > 12.0f );
    SW_EXPECT_TRUE( ( scene.run( ActionBodyInput{}, 1 ) & ActionBodyEvent::kGrappleReleased ) != 0 );
    SW_EXPECT_TRUE( scene._body.getMode() == ActionMoveMode::Normal );
    SW_EXPECT_NEAR_EQUAL( releaseVelocity._x, scene._body.getMotor().getVelocity()._x, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( releaseVelocity._y, scene._body.getMotor().getVelocity()._y, 1.0e-4f );
    (void)scene.run( ActionBodyInput{}, 1 );
    SW_EXPECT_TRUE( scene._body.getPosition()._x - releasePosition._x > 0.18f ); // 기반 몸이 그 속도로 계속 간다
}

SW_TEST_CASE( ActionPlatformerTest, DrillDigsThroughDirtAndPopsOutWithJump )
{
    ActionScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    SW_EXPECT_TRUE( scene._terrain.isDirt( 9, 3 ) );
    SW_EXPECT_FALSE( scene._terrain.isDirt( 13, 3 ) );
    SW_EXPECT_TRUE( scene._map.getTile( 9, 3 ) == PlatformTile::Solid ); // 보통 몸에게 흙은 벽

    // 흙 위에 선다.
    scene._body.setPosition( float2{ 9.5f, 4.5f } );
    (void)scene.run( ActionBodyInput{}, 20 );
    SW_EXPECT_TRUE( scene._body.getMotor().isGrounded() );
    const float32 groundY = scene._body.getPosition()._y;

    // 드릴 없이 아래를 눌러도 파고들지 않는다. 드릴을 누르면 파고든다.
    ActionBodyInput down;
    down._motor._move = float2{ 0.0f, -1.0f };
    (void)scene.run( down, 5 );
    SW_EXPECT_NEAR_EQUAL( groundY, scene._body.getPosition()._y, 1.0e-3f );
    down._bDrillHeld = SW_TRUE;
    SW_EXPECT_TRUE( ( scene.run( down, 1 ) & ActionBodyEvent::kDrillEntered ) != 0 );
    (void)scene.run( down, 20 );
    SW_EXPECT_TRUE( scene._body.getMode() == ActionMoveMode::Drill );
    SW_EXPECT_TRUE( scene._body.getPosition()._y < 3.0f );
    SW_EXPECT_TRUE( scene._body.getPosition()._y > 1.0f ); // 아래 바위(바깥 벽)에서 멈춘다

    // 오른쪽은 바위(#) — 파지 못하고 흙 안에 남는다.
    ActionBodyInput right;
    right._bDrillHeld  = SW_TRUE;
    right._motor._move = float2{ 1.0f, 0.0f };
    (void)scene.run( right, 30 );
    SW_EXPECT_TRUE( scene._body.getMode() == ActionMoveMode::Drill );
    SW_EXPECT_TRUE( scene._body.getPosition()._x < 12.0f );

    // 위로 파 올라가 점프를 누른 채 튀어나오면 높이 솟는다.
    ActionBodyInput upJump;
    upJump._bDrillHeld       = SW_TRUE;
    upJump._motor._move      = float2{ 0.0f, 1.0f };
    upJump._motor._bJumpHeld = SW_TRUE;
    uint32 events            = 0;
    for ( int32 frame = 0; frame < 60 && scene._body.getMode() == ActionMoveMode::Drill; ++frame )
    {
        events |= scene.run( upJump, 1 );
    }
    SW_EXPECT_TRUE( ( events & ActionBodyEvent::kDrillExited ) != 0 );
    SW_EXPECT_TRUE( ( events & ActionBodyEvent::kDrillJumped ) != 0 );
    SW_EXPECT_NEAR_EQUAL( 16.0f, scene._body.getMotor().getVelocity()._y, 1.0e-3f );
    SW_EXPECT_TRUE( scene._body.getPosition()._y >= 4.45f ); // 흙 밖으로 나와 있다
    float32 jumpApex = 0.0f;
    (void)scene.run( ActionBodyInput{}, 60, &jumpApex );

    // 점프 없이 나오면 진행 속도(11)만 — 훨씬 낮다.
    scene._body.setPosition( float2{ 9.5f, 4.5f } );
    (void)scene.run( ActionBodyInput{}, 20 );
    (void)scene.run( down, 15 );
    ActionBodyInput up;
    up._bDrillHeld  = SW_TRUE;
    up._motor._move = float2{ 0.0f, 1.0f };
    events          = 0;
    for ( int32 frame = 0; frame < 60 && scene._body.getMode() == ActionMoveMode::Drill; ++frame )
    {
        events |= scene.run( up, 1 );
    }
    SW_EXPECT_TRUE( ( events & ActionBodyEvent::kDrillExited ) != 0 );
    SW_EXPECT_FALSE( ( events & ActionBodyEvent::kDrillJumped ) != 0 );
    float32 plainApex = 0.0f;
    (void)scene.run( ActionBodyInput{}, 60, &plainApex );
    SW_EXPECT_TRUE( jumpApex > plainApex + 1.0f );

    // 흙이 없는 쪽으로는 드릴이 걸리지 않는다.
    scene._body.setPosition( float2{ 3.5f, 1.5f } );
    (void)scene.run( ActionBodyInput{}, 10 );
    ActionBodyInput air;
    air._bDrillHeld  = SW_TRUE;
    air._motor._move = float2{ 0.0f, 1.0f };
    SW_EXPECT_FALSE( ( scene.run( air, 1 ) & ActionBodyEvent::kDrillEntered ) != 0 );
}

SW_TEST_CASE( ActionPlatformerTest, MeleeComboCancelsOnHitWithHitstopAndBuffer )
{
    ActionScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );
    ActionCombatRig rig;
    SW_EXPECT_FALSE( rig.initialize( &scene._catalog, &scene._moves, "noCombo" ) );
    SW_ASSERT_TRUE( rig.initialize( &scene._catalog, &scene._moves, "umbrella" ) );
    const float2 player{ 5.0f, 2.0f };

    // 헛치면 — slash1 의 캔슬 창은 맞혔을 때만 열려 콤보가 이어지지 않는다.
    rig.pressAttack();
    rig.advanceFrame( nullptr, player );
    SW_EXPECT_EQUAL( 0, rig.getComboIndex() );
    for ( int32 frame = 0; frame < 4; ++frame )
    {
        rig.advanceFrame( nullptr, player );
    }
    rig.pressAttack();
    bool bAdvanced = false;
    for ( int32 frame = 0; frame < 9; ++frame )
    {
        rig.advanceFrame( nullptr, player );
        bAdvanced = bAdvanced || rig.getComboIndex() > 0;
    }
    SW_EXPECT_FALSE( bAdvanced );
    for ( int32 frame = 0; frame < 10; ++frame )
    {
        rig.advanceFrame( nullptr, player );
    }
    SW_EXPECT_FALSE( rig.getTimeline().isPlaying() );
    SW_EXPECT_EQUAL( -1, rig.getComboIndex() );

    // 맞히면 — 히트스톱 동안 세상(탄 포함)이 멈추고, 그동안 누른 공격은 기억했다가 창이 열리면 slash2.
    ActionProjectile drifting;
    drifting._position = float2{ 15.0f, 5.0f };
    drifting._velocity = float2{ -3.0f, 0.0f };
    (void)rig.spawnProjectile( drifting ); // id 는 쓰지 않는다 — 탄이 멈췄는지는 아래 단언이 본다
    rig.pressAttack();
    rig.advanceFrame( nullptr, player ); // slash1 프레임 1
    rig.advanceFrame( nullptr, player );
    rig.advanceFrame( nullptr, player ); // 프레임 3 = 지속
    SW_EXPECT_TRUE( rig.getTimeline().getPhase() == MovePhase::Active );
    rig.registerMeleeContact( false );
    SW_EXPECT_EQUAL( 4, rig.getHitstopFrames() );
    const float32 frozenX = rig.getProjectiles()[0]._position._x;
    rig.pressAttack();
    for ( int32 frame = 0; frame < 4; ++frame )
    {
        SW_EXPECT_TRUE( rig.isFrozen() );
        rig.advanceFrame( nullptr, player );
        SW_EXPECT_NEAR_EQUAL( frozenX, rig.getProjectiles()[0]._position._x, 1.0e-5f );
        SW_EXPECT_EQUAL( 3, rig.getTimeline().getFrame() );
    }
    SW_EXPECT_FALSE( rig.isFrozen() );
    rig.advanceFrame( nullptr, player ); // 프레임 4 — 캔슬 창, 맞혔다
    rig.advanceFrame( nullptr, player );
    SW_EXPECT_EQUAL( 1, rig.getComboIndex() );
    SW_EXPECT_TRUE( rig.getProjectiles()[0]._position._x < frozenX );

    // slash2 의 창(4..)보다 일찍 눌러도 버퍼가 들고 있다가 slash3 으로.
    rig.pressAttack();
    for ( int32 frame = 0; frame < 6; ++frame )
    {
        rig.advanceFrame( nullptr, player );
    }
    SW_EXPECT_EQUAL( 2, rig.getComboIndex() );
    SW_EXPECT_TRUE( rig.getTimeline().getMove()._id == hashed_string( "slash3" ) );

    vector<ActionCombatEvent> listEvent;
    rig.drainEvents( listEvent );
    int32 startedCount = 0;
    int32 hitstopCount = 0;
    for ( const ActionCombatEvent& event : listEvent )
    {
        startedCount += event._type == ActionCombatEventType::MoveStarted ? 1 : 0;
        hitstopCount += event._type == ActionCombatEventType::HitstopStarted ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 4, startedCount ); // slash1(헛침) · slash1 · slash2 · slash3
    SW_EXPECT_EQUAL( 1, hitstopCount );
}

SW_TEST_CASE( ActionPlatformerTest, GunEnemyPatternAndParryReflectAreDeterministic )
{
    ActionScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );

    // 총 — 반자동 · 연사 간격 · 탄창 2 · 재장전. 탄은 겨눈 쪽으로 탄속만큼 간다.
    ActionCombatRig rig;
    SW_ASSERT_TRUE( rig.initialize( &scene._catalog, &scene._moves, "umbrella" ) );
    rig.equipGun( makePistol(), 4, 3u );
    SW_EXPECT_TRUE( rig.fireGun( float2{ 2.0f, 2.0f }, float2{ 1.0f, 0.0f }, true ) == WeaponFireResult::Fired );
    SW_ASSERT_TRUE( rig.getProjectiles().size() == 1 );
    SW_EXPECT_NEAR_EQUAL( 20.0f, rig.getProjectiles()[0]._velocity._x, 1.0e-3f );
    SW_EXPECT_TRUE( rig.getProjectiles()[0]._team == ActionTeam::Player );
    SW_EXPECT_TRUE( rig.fireGun( float2{ 2.0f, 2.0f }, float2{ 1.0f, 0.0f }, true ) == WeaponFireResult::Cooling );
    for ( int32 frame = 0; frame < 16; ++frame )
    {
        rig.advanceFrame( &scene._map, float2{ 2.0f, 2.0f } );
    }
    SW_EXPECT_TRUE( rig.fireGun( float2{ 2.0f, 2.0f }, float2{ 1.0f, 0.0f }, false ) == WeaponFireResult::SemiAutoHeld );
    SW_EXPECT_TRUE( rig.fireGun( float2{ 2.0f, 2.0f }, float2{ 1.0f, 0.0f }, true ) == WeaponFireResult::Fired );
    for ( int32 frame = 0; frame < 16; ++frame )
    {
        rig.advanceFrame( &scene._map, float2{ 2.0f, 2.0f } );
    }
    SW_EXPECT_TRUE( rig.fireGun( float2{ 2.0f, 2.0f }, float2{ 1.0f, 0.0f }, true ) == WeaponFireResult::EmptyMagazine );
    SW_EXPECT_TRUE( rig.getGun().isReloading() );
    // 탄은 벽(x = 23)에 닿으면 사라진다.
    for ( int32 frame = 0; frame < 120; ++frame )
    {
        rig.advanceFrame( &scene._map, float2{ 2.0f, 2.0f } );
    }
    SW_EXPECT_EQUAL( 0, static_cast<int32>( rig.getProjectiles().size() ) );

    // 적 패턴 — 멀면 순찰(바라보는 쪽으로), 가까워지면 조준 10 프레임 뒤 사격 한 번, 맞으면 경직.
    ActionEnemyBrain brain;
    SW_EXPECT_FALSE( brain.initialize( nullptr ) );
    SW_ASSERT_TRUE( brain.initialize( scene._catalog.findPattern( "gunner" ), -1 ) );
    SW_EXPECT_TRUE( brain.getStateId() == hashed_string( "patrol" ) );
    SW_EXPECT_NEAR_EQUAL( -1.0f, brain.advanceFrame( 10.0f )._moveX, 1.0e-4f );
    SW_EXPECT_TRUE( brain.advanceFrame( 5.0f )._bStateChanged != SW_FALSE );
    SW_EXPECT_TRUE( brain.getStateId() == hashed_string( "aim" ) );
    int32 fireCount = 0;
    int32 fireFrame = -1;
    for ( int32 frame = 0; frame < 40; ++frame )
    {
        const ActionEnemyAction action = brain.advanceFrame( 5.0f );
        if ( action._bFire == SW_TRUE )
        {
            ++fireCount;
            fireFrame = frame;
            SW_EXPECT_NEAR_EQUAL( 12.0f, action._fireSpeed, 1.0e-4f );
        }
    }
    SW_EXPECT_EQUAL( 1, fireCount );
    SW_EXPECT_EQUAL( 9, fireFrame ); // 조준 10 프레임(들어선 프레임 포함) 뒤
    SW_EXPECT_TRUE( brain.getStateId() == hashed_string( "cooldown" ) );
    SW_EXPECT_FALSE( brain.notifyHit() ); // 쉬는 중에는 경직이 없다
    for ( int32 frame = 0; frame < 30; ++frame )
    {
        (void)brain.advanceFrame( 10.0f );
    }
    SW_EXPECT_TRUE( brain.getStateId() == hashed_string( "patrol" ) );
    SW_EXPECT_TRUE( brain.notifyHit() );
    SW_EXPECT_TRUE( brain.getStateId() == hashed_string( "stagger" ) );

    // 패리 — 날아온 탄을 되받아쳐 사수가 두 배 피해를 받는다. 되받아칠 때 히트스톱 6 프레임.
    const ParryDuel parried = runParryDuel( scene, true );
    SW_EXPECT_EQUAL( 1, parried._shotCount );
    SW_EXPECT_TRUE( parried._reflectFrame > 0 );
    SW_EXPECT_EQUAL( 0, parried._playerHitCount );
    SW_EXPECT_NEAR_EQUAL( 20.0f, parried._enemyDamage, 1.0e-4f );
    SW_EXPECT_EQUAL( 6, parried._frozenFrameCount );

    // 패리하지 않으면 플레이어가 맞는다.
    const ParryDuel ignored = runParryDuel( scene, false );
    SW_EXPECT_EQUAL( 1, ignored._playerHitCount );
    SW_EXPECT_NEAR_EQUAL( 0.0f, ignored._enemyDamage, 1.0e-4f );
    SW_EXPECT_EQUAL( 0, ignored._frozenFrameCount );

    // 같은 입력이면 같은 프레임에 같은 결과(결정적).
    const ParryDuel again = runParryDuel( scene, true );
    SW_EXPECT_EQUAL( parried._reflectFrame, again._reflectFrame );
    SW_EXPECT_NEAR_EQUAL( parried._enemyDamage, again._enemyDamage, 0.0f );
}

/**
 * @brief [ActionPlatformerTest] 상태 바이트 — 스테이지(확정 · 지닌 비밀 · 체크포인트 · 목숨) · 활공 중인 몸 · 콤보 · 총 · 투사체 · 적 패턴이 그대로 오고,
 *        같은 걸음을 더 돌려도 바이트가 같다. 정의는 id 로 카탈로그에서 찾고, 잘린 바이트는 거절하고 그대로 둔다
 */
SW_TEST_CASE( ActionPlatformerTest, StateRoundTripContinuesTheSameRun )
{
    ActionScene scene;
    SW_ASSERT_TRUE( scene._bLoaded );

    // 스테이지 — gem1 은 확정, gem2 는 지닌 채, 한 번 맞고 5 초.
    ActionStageRun run;
    SW_ASSERT_TRUE( run.start( &scene._catalog, "1-1" ) );
    SW_EXPECT_TRUE( run.collectSecret( "gem1" ) );
    SW_EXPECT_TRUE( run.reachCheckpoint( "cp1" ) );
    SW_EXPECT_TRUE( run.collectSecret( "gem2" ) );
    run.registerHit();
    for ( int32 tick = 0; tick < 10; ++tick )
    {
        run.update( 0.5f );
    }
    const vector<uint8> runBytes = capturePlatformerBytes( run );
    ActionStageRun      restoredRun;
    restoredRun.bindCatalog( &scene._catalog );
    Archive runReader( runBytes.data(), runBytes.size() );
    SW_ASSERT_TRUE( restoredRun.readState( runReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, runReader.getRemainingBytes() );
    SW_ASSERT_NOT_NULL( restoredRun.getStage() );
    SW_EXPECT_TRUE( restoredRun.getStage()->_id == hashed_string( "1-1" ) );
    SW_EXPECT_EQUAL( 0, restoredRun.getCheckpointIndex() );
    SW_EXPECT_EQUAL( 1, restoredRun.getSecretCommittedCount() );
    SW_EXPECT_EQUAL( 1, restoredRun.getSecretPendingCount() );
    SW_EXPECT_NEAR_EQUAL( 5.0f, restoredRun.getElapsed(), 1.0e-4f );
    SW_EXPECT_TRUE( runBytes == capturePlatformerBytes( restoredRun ) );
    run.update( 1.0f );
    restoredRun.update( 1.0f );
    SW_EXPECT_TRUE( run.die() == restoredRun.die() ); // 둘 다 cp1 에서 되살아나고 gem2 를 놓는다
    SW_EXPECT_TRUE( capturePlatformerBytes( run ) == capturePlatformerBytes( restoredRun ) );
    ActionStageRun truncatedRun;
    truncatedRun.bindCatalog( &scene._catalog );
    Archive runCut( runBytes.data(), runBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedRun.readState( runCut ) );
    SW_EXPECT_TRUE( truncatedRun.getState() == ActionStageState::NotStarted );

    // 몸 — 우산을 편 채 떨어지는 중(기반 몸의 설정은 활공 것이어야 같은 속도로 이어진다).
    ActionBodyInput glide;
    glide._bGlideHeld = SW_TRUE;
    scene._body.setPosition( float2{ 3.5f, 9.5f } );
    (void)scene.run( glide, 10 );
    SW_EXPECT_TRUE( scene._body.getMode() == ActionMoveMode::Glide );
    const vector<uint8>  bodyBytes = capturePlatformerBytes( scene._body );
    ActionPlatformerBody restoredBody;
    restoredBody.initialize( PlatformerSettings{}, scene._catalog.getBodySettings() );
    Archive bodyReader( bodyBytes.data(), bodyBytes.size() );
    SW_ASSERT_TRUE( restoredBody.readState( bodyReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, bodyReader.getRemainingBytes() );
    SW_EXPECT_TRUE( restoredBody.getMode() == ActionMoveMode::Glide );
    SW_EXPECT_NEAR_EQUAL( 2.0f, restoredBody.getMotor().getSettings()._maxFallSpeed, 1.0e-4f );
    SW_EXPECT_TRUE( bodyBytes == capturePlatformerBytes( restoredBody ) );
    for ( int32 frame = 0; frame < 10; ++frame )
    {
        scene._body.update( scene._map, scene._terrain, glide, kActionPlatformerStep );
        restoredBody.update( scene._map, scene._terrain, glide, kActionPlatformerStep );
    }
    SW_EXPECT_TRUE( capturePlatformerBytes( scene._body ) == capturePlatformerBytes( restoredBody ) );
    ActionPlatformerBody truncatedBody;
    truncatedBody.initialize( PlatformerSettings{}, scene._catalog.getBodySettings() );
    Archive bodyCut( bodyBytes.data(), bodyBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedBody.readState( bodyCut ) );
    SW_EXPECT_TRUE( truncatedBody.getMode() == ActionMoveMode::Normal );

    // 싸움 — slash1 중, 총 한 발이 날고, 적 탄 하나가 다가온다.
    const float2    player{ 5.0f, 2.0f };
    ActionCombatRig rig;
    SW_ASSERT_TRUE( rig.initialize( &scene._catalog, &scene._moves, "umbrella" ) );
    rig.equipGun( makePistol(), 4, 3u );
    rig.pressAttack();
    for ( int32 frame = 0; frame < 2; ++frame )
    {
        rig.advanceFrame( nullptr, player );
    }
    SW_EXPECT_TRUE( rig.fireGun( float2{ 2.0f, 2.0f }, float2{ 1.0f, 0.0f }, true ) == WeaponFireResult::Fired );
    ActionProjectile incoming;
    incoming._position = float2{ 15.0f, 2.0f };
    incoming._velocity = float2{ -3.0f, 0.0f };
    (void)rig.spawnProjectile( incoming ); // id 는 쓰지 않는다 — 탄 상태는 아래 바이트 비교가 본다
    rig.advanceFrame( nullptr, player );
    const vector<uint8> rigBytes = capturePlatformerBytes( rig );
    ActionCombatRig     restoredRig;
    SW_ASSERT_TRUE( restoredRig.initialize( &scene._catalog, &scene._moves, "umbrella" ) );
    restoredRig.equipGun( makePistol(), 4, 3u );
    Archive rigReader( rigBytes.data(), rigBytes.size() );
    SW_ASSERT_TRUE( restoredRig.readState( rigReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, rigReader.getRemainingBytes() );
    SW_EXPECT_EQUAL( 0, restoredRig.getComboIndex() );
    SW_EXPECT_TRUE( restoredRig.getTimeline().getMove()._id == hashed_string( "slash1" ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( restoredRig.getProjectiles().size() ) );
    SW_EXPECT_EQUAL( 1, restoredRig.getGun().getMagazineAmmo() );
    SW_EXPECT_TRUE( rigBytes == capturePlatformerBytes( restoredRig ) );
    rig.registerMeleeContact( false );
    restoredRig.registerMeleeContact( false );
    rig.pressAttack();
    restoredRig.pressAttack();
    for ( int32 frame = 0; frame < 20; ++frame )
    {
        rig.advanceFrame( nullptr, player );
        restoredRig.advanceFrame( nullptr, player );
    }
    SW_EXPECT_EQUAL( rig.getComboIndex(), restoredRig.getComboIndex() );
    SW_EXPECT_TRUE( capturePlatformerBytes( rig ) == capturePlatformerBytes( restoredRig ) );
    ActionCombatRig truncatedRig;
    SW_ASSERT_TRUE( truncatedRig.initialize( &scene._catalog, &scene._moves, "umbrella" ) );
    truncatedRig.equipGun( makePistol(), 4, 3u );
    Archive rigCut( rigBytes.data(), rigBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedRig.readState( rigCut ) );
    SW_EXPECT_TRUE( truncatedRig.getProjectiles().empty() );

    // 적 — 조준 중. 패턴은 id 로 찾으니 아무것도 들지 않은 뇌에 읽어도 이어진다.
    ActionEnemyBrain brain;
    SW_ASSERT_TRUE( brain.initialize( scene._catalog.findPattern( "gunner" ), -1 ) );
    (void)brain.advanceFrame( 10.0f );
    (void)brain.advanceFrame( 5.0f );
    for ( int32 frame = 0; frame < 3; ++frame )
    {
        (void)brain.advanceFrame( 5.0f );
    }
    SW_EXPECT_TRUE( brain.getStateId() == hashed_string( "aim" ) );
    const vector<uint8> brainBytes = capturePlatformerBytes( brain );
    ActionEnemyBrain    restoredBrain;
    restoredBrain.bindCatalog( &scene._catalog );
    Archive brainReader( brainBytes.data(), brainBytes.size() );
    SW_ASSERT_TRUE( restoredBrain.readState( brainReader ) );
    SW_EXPECT_EQUAL( uint64{ 0 }, brainReader.getRemainingBytes() );
    SW_EXPECT_TRUE( restoredBrain.getStateId() == hashed_string( "aim" ) );
    SW_EXPECT_EQUAL( brain.getStateFrame(), restoredBrain.getStateFrame() );
    SW_EXPECT_EQUAL( -1, restoredBrain.getFacing() );
    SW_EXPECT_TRUE( brainBytes == capturePlatformerBytes( restoredBrain ) );
    int32 fireCount         = 0;
    int32 restoredFireCount = 0;
    for ( int32 frame = 0; frame < 12; ++frame )
    {
        fireCount += brain.advanceFrame( 5.0f )._bFire == SW_TRUE ? 1 : 0;
        restoredFireCount += restoredBrain.advanceFrame( 5.0f )._bFire == SW_TRUE ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 1, fireCount );
    SW_EXPECT_EQUAL( fireCount, restoredFireCount );
    SW_EXPECT_TRUE( capturePlatformerBytes( brain ) == capturePlatformerBytes( restoredBrain ) );
    ActionEnemyBrain truncatedBrain;
    truncatedBrain.bindCatalog( &scene._catalog );
    Archive brainCut( brainBytes.data(), brainBytes.size() - 1 );
    SW_EXPECT_FALSE( truncatedBrain.readState( brainCut ) );
    SW_EXPECT_TRUE( truncatedBrain.getStateId().empty() );
}
