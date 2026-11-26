// ShooterServerCommandTest — 매치 서버가 명령 하나를 푸는 차례와 시각: 걸은 자리에서 쏘고, 발동은 자기가 보던 시각으로 되감아 판정하며, 쓰러진 몸의 명령은 걷지도 쏘지도 않고, 눈 높이는 설정값이다(nogpu, 에셋 없음).
// 무기는 퍼짐 없는 한 방(피해 200)이고 두 자리의 몸은 반경 0.35 m · 높이 1.8 m 의 캡슐로 맞는다.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Games/Playground/Shooter3D/Tests/ShooterServerTestWorld.h"

using namespace sw;

namespace
{
    using World  = ShooterServerTestWorld;
    using Client = ShooterServerTestClient;

    ControlIntent makeFireIntent( const float2& move, float32 yaw, ControlViewTick viewTick )
    {
        ControlIntent intent;
        intent._move = move;
        intent.addTrigger( World::kFireButton, yaw, 0.0f, viewTick );
        return intent;
    }

    ControlIntent makeWalkIntent()
    {
        ControlIntent intent;
        intent._move = float2{ 1.0f, 0.0f };
        return intent;
    }
} // namespace

/** @brief [ShooterServerCommandTest] 명령 하나는 걷고 나서 쏜다 — 걸은 자리에서만 맞는 표적이 맞는다(판정 뒤에 걸으면 빗나간다) */
SW_TEST_CASE( ShooterServerCommandTest, ShotsLeaveFromWhereTheCommandWalked )
{
    ShooterServerTestWorld::expectHitSound();
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    World                 world{ World::WeaponTable::Present };
    game::ScopedGameScope scopedScope( world._scope );
    world._manager.beginPlay();
    Client target{ world, 3, 11 };
    world.advance();
    Client shooter{ world, 5, 22 };
    world.advance();
    ShooterPlayerComponent* pTarget  = world.findPlayer( 0 );
    ShooterPlayerComponent* pShooter = world.findPlayer( 1 );
    SW_REQUIRE_TRUE( pTarget != nullptr && pShooter != nullptr );
    SW_EXPECT_NEAR( 4.0f, pShooter->getFeetPosition()._value._x, 1.0e-3f );

    // 표적의 캡슐은 x 4.35 ~ 5.05 다 — 걷기 전 자리(4.0)의 +Z 광선은 지나가고 걸은 자리(4.5)의 광선은 맞는다.
    (void)shooter.send( makeFireIntent( float2{ 1.0f, 0.0f }, 0.0f, ControlViewTick::makePresent() ) );
    world.advance();
    SW_EXPECT_NEAR( 4.0f + World::kWalkStep, pShooter->getFeetPosition()._value._x, 1.0e-3f );
    SW_EXPECT_EQUAL( 1u, pShooter->getShotCount() );
    SW_EXPECT_EQUAL( 1u, pShooter->getHitCount() );
    SW_EXPECT_FALSE( pTarget->isAlive() );
}

/** @brief [ShooterServerCommandTest] 발동은 자기가 보던 시각의 표적을 맞힌다 — 같은 조준이라도 지금을 보고 쏘면 이미 지나간 자리라 빗나간다(의도의 시각이 아니라 발동의 시각이다) */
SW_TEST_CASE( ShooterServerCommandTest, EachTriggerIsJudgedAtTheTimeItSaw )
{
    ShooterServerTestWorld::expectHitSound();
    constexpr bool kArrSeenPast[] = { true, false };
    for ( const bool bSeenPast : kArrSeenPast )
    {
        SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
        World                 world{ World::WeaponTable::Present };
        game::ScopedGameScope scopedScope( world._scope );
        world._manager.beginPlay();
        Client target{ world, 3, 11 };
        world.advance();
        Client shooter{ world, 5, 22 };
        world.advance();
        ShooterPlayerComponent* pTarget  = world.findPlayer( 0 );
        ShooterPlayerComponent* pShooter = world.findPlayer( 1 );
        SW_REQUIRE_TRUE( pTarget != nullptr && pShooter != nullptr );
        const float64 seenTick = static_cast<float64>( world.computeServerTick() ) - 3.0; // 표적이 서 있던 때다
        const float3  before   = pTarget->getFeetPosition()._value;
        const float32 yaw      = MathUtil::atan2( before._x - 4.0f, before._z + 16.0f );
        for ( int32 command = 0; command < 8; ++command )
        {
            (void)target.send( makeWalkIntent() );
            world.advanceFrame();
        }
        SW_REQUIRE_TRUE( pTarget->getFeetPosition()._value._x > before._x + 3.0f );

        (void)shooter.send( makeFireIntent( float2{}, yaw, bSeenPast ? ControlViewTick::makeSeen( seenTick ) : ControlViewTick::makePresent() ) );
        world.advance();
        SW_EXPECT_EQUAL( 1u, pShooter->getShotCount() );
        SW_EXPECT_EQUAL( bSeenPast ? 1u : 0u, pShooter->getHitCount() );
        SW_EXPECT_EQUAL( bSeenPast, pTarget->isAlive() == false );
    }
}

/** @brief [ShooterServerCommandTest] 쓰러진 몸이 보낸 명령은 걷지도 쏘지도 않는다 */
SW_TEST_CASE( ShooterServerCommandTest, CommandsOfADownedBodyAreRefused )
{
    ShooterServerTestWorld::expectHitSound();
    SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
    World                 world{ World::WeaponTable::Present };
    game::ScopedGameScope scopedScope( world._scope );
    world._manager.beginPlay();
    Client target{ world, 3, 11 };
    world.advance();
    Client shooter{ world, 5, 22 };
    world.advance();
    ShooterPlayerComponent* pTarget  = world.findPlayer( 0 );
    ShooterPlayerComponent* pShooter = world.findPlayer( 1 );
    SW_REQUIRE_TRUE( pTarget != nullptr && pShooter != nullptr );
    (void)shooter.send( makeFireIntent( float2{ 1.0f, 0.0f }, 0.0f, ControlViewTick::makePresent() ) );
    // 같은 프레임에 온 표적의 명령이다 — 자리 차례가 앞인 표적은 아직 살아 있어 걷고, 다음 프레임의 명령부터 거절된다.
    world.advance();
    SW_REQUIRE_FALSE( pTarget->isAlive() );
    const float3 downAt = pTarget->getFeetPosition()._value;

    (void)target.send( makeFireIntent( float2{ 1.0f, 0.0f }, 0.0f, ControlViewTick::makePresent() ) );
    world.advanceFrame();
    SW_EXPECT_EQUAL( 0u, pTarget->getShotCount() );
    SW_EXPECT_NEAR( downAt._x, pTarget->getFeetPosition()._value._x, 1.0e-3f );
}

/** @brief [ShooterServerCommandTest] 서버는 설정한 눈 높이에서 쏜다 — 머리 위(2.2 m)의 눈이면 선 사람 위로 지나간다 */
SW_TEST_CASE( ShooterServerCommandTest, ShotsLeaveFromTheEyeHeightSetting )
{
    ShooterServerTestWorld::expectHitSound();
    constexpr float32 kArrEyeHeight[] = { 1.5f, 2.2f };
    for ( const float32 eyeHeight : kArrEyeHeight )
    {
        SW_EXPECT_LOG( "PrefabPool.PrefabPoolCannotSpawn" );
        World                 world{ World::WeaponTable::Present, eyeHeight };
        game::ScopedGameScope scopedScope( world._scope );
        world._manager.beginPlay();
        Client target{ world, 3, 11 };
        world.advance();
        Client shooter{ world, 5, 22 };
        world.advance();
        ShooterPlayerComponent* pShooter = world.findPlayer( 1 );
        SW_REQUIRE_NOT_NULL( pShooter );
        (void)shooter.send( makeFireIntent( float2{ 1.0f, 0.0f }, 0.0f, ControlViewTick::makePresent() ) );
        world.advance();
        SW_EXPECT_EQUAL( 1u, pShooter->getShotCount() );
        SW_EXPECT_EQUAL( eyeHeight < 2.0f ? 1u : 0u, pShooter->getHitCount() );
    }
}

/** @brief [ShooterServerCommandTest] 네트가 움직이는 몸의 눈 높이는 보이는 몸과 상관없는 설정값이다 — 혼자 하는 몸만 소켓 높이를 쓴다 */
SW_TEST_CASE( ShooterServerCommandTest, NetDrivenEyeHeightIgnoresTheVisibleBody )
{
    SW_EXPECT_NEAR( 1.5f, ShooterPlayerComponent::chooseEyeHeight( 1.5f, 1.7f, 0.1f, true ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.8f, ShooterPlayerComponent::chooseEyeHeight( 1.5f, 1.7f, 0.1f, false ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.5f, ShooterPlayerComponent::chooseEyeHeight( 1.5f, 0.0f, 0.0f, false ), 1.0e-6f ); // 아직 조립 전이다
}
