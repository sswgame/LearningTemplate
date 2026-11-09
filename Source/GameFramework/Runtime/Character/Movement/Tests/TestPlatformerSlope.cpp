#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Movement/PlatformerMotor2D.h"

namespace
{
    struct PlatformerSlopeTestUtil
    {
        static constexpr float32 kStep = 1.0f / 60.0f;

        /** @brief 바닥(맨 아래 줄) 위 오른쪽으로 오르는 경사 두 칸, 그 위 높은 바닥이다. 칸 크기 1, 원점 (0,0). */
        static sw::PlatformTileMap makeRamp()
        {
            sw::PlatformTileMap map;
            const bool          bLoaded = map.loadFromXMLText( "..........\n"
                                                                        "..........\n"
                                                                        "..........\n"
                                                                        "....../###\n"
                                                                        "...../####\n"
                                                                        "##########\n",
                                                               1.0f, sw::float2{ 0.0f, 0.0f } );
            SW_EXPECT_TRUE( bLoaded );
            return map;
        }

        /** @brief 아래 바닥 위에 서 있는 몸입니다(발이 y = 1). */
        static sw::PlatformerMotor2D makeStanding( const sw::PlatformTileMap& map, float32 x, const sw::PlatformerSettings& settings )
        {
            sw::PlatformerMotor2D motor;
            motor.setSettings( settings );
            motor.setPosition( sw::float2{ x, 1.0f + settings._halfExtents._y } );
            motor.update( map, sw::PlatformerInput{}, kStep );
            return motor;
        }

        static void run( sw::PlatformerMotor2D& motor, const sw::PlatformTileMap& map, float32 moveX, int32 tickCount, bool& outAlwaysGrounded )
        {
            sw::PlatformerInput input;
            input._move = sw::float2{ moveX, 0.0f };
            for ( int32 tick = 0; tick < tickCount; ++tick )
            {
                motor.update( map, input, kStep );
                outAlwaysGrounded = outAlwaysGrounded && motor.isGrounded();
            }
        }
    };
} // namespace

// 경사를 걸어 오르면 발이 빗변을 따라 올라 높은 바닥에 닿고, 되돌아 내려오는 동안 한 틱도 뜨지 않는다(내리막에서 공중 판정이면 점프 · 코요테가 어긋난다).
SW_TEST_CASE( PlatformerSlopeTest, WalkingUpAndDownFollowsTheSlope )
{
    const sw::PlatformTileMap map   = PlatformerSlopeTestUtil::makeRamp();
    sw::PlatformerSettings    tune  = {};
    sw::PlatformerMotor2D     motor = PlatformerSlopeTestUtil::makeStanding( map, 2.5f, tune );
    SW_REQUIRE_TRUE( motor.isGrounded() );

    bool bGroundedUp = true;
    PlatformerSlopeTestUtil::run( motor, map, 1.0f, 90, bGroundedUp );
    SW_EXPECT_TRUE( bGroundedUp );
    SW_EXPECT_NEAR( 3.0f, motor.getPosition()._y - tune._halfExtents._y, 0.01f ); // 높은 바닥 윗면(y = 3)

    bool bGroundedDown = true;
    PlatformerSlopeTestUtil::run( motor, map, -1.0f, 90, bGroundedDown );
    SW_EXPECT_TRUE( bGroundedDown );
    SW_EXPECT_NEAR( 1.0f, motor.getPosition()._y - tune._halfExtents._y, 0.01f ); // 아래 바닥으로 돌아왔다
}

// 경사 중간에서 상자 몸은 높은 쪽 모서리로 빗변에 선다 — 오르막 첫 칸 가운데(바닥 + 0.5)에서 오른쪽 모서리(반폭 0.35 − 틈)만큼 높다.
SW_TEST_CASE( PlatformerSlopeTest, StandsOnTheSlopeSurface )
{
    const sw::PlatformTileMap map = PlatformerSlopeTestUtil::makeRamp();
    sw::PlatformerMotor2D     motor;
    motor.setSettings( sw::PlatformerSettings{} );
    motor.setPosition( sw::float2{ 5.5f, 3.0f } ); // 경사 첫 칸(x 5) 위에서 떨어뜨린다
    bool bIgnored = true;
    PlatformerSlopeTestUtil::run( motor, map, 0.0f, 60, bIgnored );
    SW_EXPECT_TRUE( motor.isGrounded() );
    SW_EXPECT_NEAR( 1.5f + motor.getSettings()._halfExtents._x, motor.getPosition()._y - motor.getSettings()._halfExtents._y, 0.01f );
}

// 기본은 경사가 당기지 않는다(멈춰 선다). 관성의 경사 당김을 켜고 땅 가속을 끄면 오르막에서 아래로 미끄러진다 — 탈것 · 썰매와 같은 식이다.
SW_TEST_CASE( PlatformerSlopeTest, SlopePullIsOptIn )
{
    const sw::PlatformTileMap map = PlatformerSlopeTestUtil::makeRamp();

    sw::PlatformerMotor2D still;
    still.setSettings( sw::PlatformerSettings{} );
    still.setPosition( sw::float2{ 5.5f, 3.0f } );
    bool bIgnored = true;
    PlatformerSlopeTestUtil::run( still, map, 0.0f, 60, bIgnored );
    SW_EXPECT_NEAR( 5.5f, still.getPosition()._x, 1.0e-4f );

    sw::PlatformerSettings sled;
    sled._groundAcceleration       = 0.0f;
    sled._slopeInertia._slopeScale = 1.0f;
    sw::PlatformerMotor2D sliding;
    sliding.setSettings( sled );
    sliding.setPosition( sw::float2{ 6.5f, 4.0f } );
    PlatformerSlopeTestUtil::run( sliding, map, 0.0f, 30, bIgnored );
    SW_EXPECT_TRUE( sliding.getVelocity()._x < -0.5f ); // 오른쪽으로 오르는 경사 — 왼쪽(내리막)으로 빨라진다
    SW_EXPECT_TRUE( sliding.getPosition()._x < 6.5f );
}
