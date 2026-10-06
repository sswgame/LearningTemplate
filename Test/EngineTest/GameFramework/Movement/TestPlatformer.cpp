#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Movement/PlatformerMotor2D.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 2D 플랫포머 몸 — 점프 높이, 짧은 점프, 코요테 시간, 점프 미리 누르기, 벽 미끄러짐 · 벽 점프, 대시, 2단 점프, 한쪽 발판 · 내려가기, 사다리, 위험 칸.

using namespace sw;

namespace
{
    constexpr float32 kPlatformerStep = 1.0f / 60.0f;

    //         x: 0123456789012345678901
    constexpr const utf8* kPlatformerLevelText = R"(
######################
#                    #
#        ----        #
#                 H  #
#                 H ##
#                 H ##
#                 H ##
#  ^              H ##
######################
)";

    struct PlatformerScene
    {
        PlatformTileMap   _map;
        PlatformerMotor2D _motor;

        PlatformerScene()
        {
            _map.loadFromText( kPlatformerLevelText, 1.0f, float2{ 0.0f, 0.0f } );
            _motor.setSettings( PlatformerSettings{} );
            _motor.setPosition( float2{ 5.5f, 1.45f } );
        }

        uint32 run( const PlatformerInput& input, int32 frameCount, float32* pOutMaxY = nullptr )
        {
            uint32 events = 0;
            for ( int32 frame = 0; frame < frameCount; ++frame )
            {
                _motor.update( _map, input, kPlatformerStep );
                events |= _motor.getEvents();
                if ( pOutMaxY != nullptr )
                    *pOutMaxY = MathUtil::max( *pOutMaxY, _motor.getPosition()._y );
            }
            return events;
        }

        PlatformerInput press( float32 moveX, bool bJump, bool bHeld = true )
        {
            PlatformerInput input;
            input._move         = float2{ moveX, 0.0f };
            input._bJumpPressed = bJump ? SW_TRUE : SW_FALSE;
            input._bJumpHeld    = bHeld ? SW_TRUE : SW_FALSE;
            return input;
        }
    };
} // namespace

SW_TEST_CASE( PlatformerTest, JumpHeightCutJumpsCoyoteAndBuffer )
{
    PlatformerScene scene;
    (void)scene.run( PlatformerInput{}, 5 );
    SW_EXPECT_TRUE( scene._motor.isGrounded() );
    const float32 groundY = scene._motor.getPosition()._y;
    SW_EXPECT_NEAR_EQUAL( 1.45f, groundY, 1.0e-3f );

    // 꾹 누른 점프 — 정한 높이(3.2)까지.
    float32 maxY  = groundY;
    uint32  event = scene.run( scene.press( 0.0f, true ), 1, &maxY );
    SW_EXPECT_TRUE( ( event & PlatformerEvent::kJumped ) != 0 );
    event = scene.run( scene.press( 0.0f, false ), 80, &maxY );
    SW_EXPECT_NEAR_EQUAL( 3.2f, maxY - groundY, 0.15f );
    SW_EXPECT_TRUE( ( event & PlatformerEvent::kLanded ) != 0 );

    // 짧은 점프 — 바로 떼면 훨씬 낮다.
    maxY = groundY;
    (void)scene.run( scene.press( 0.0f, true, false ), 1, &maxY );
    (void)scene.run( scene.press( 0.0f, false, false ), 80, &maxY );
    SW_EXPECT_TRUE( maxY - groundY < 1.5f );

    // 점프 미리 누르기 — 땅에 닿기 직전에 눌러도 닿자마자 뛴다.
    (void)scene.run( scene.press( 0.0f, true ), 1 );
    (void)scene.run( scene.press( 0.0f, false ), 30 );
    bool bLandedThenJumped = false;
    for ( int32 frame = 0; frame < 60 && bLandedThenJumped == false; ++frame )
    {
        const float32 height = scene._motor.getPosition()._y - groundY;
        const bool    bPress = height < 0.3f && scene._motor.getVelocity()._y < 0.0f;
        (void)scene.run( scene.press( 0.0f, bPress ), 1 );
        if ( bPress )
        {
            (void)scene.run( scene.press( 0.0f, false ), 8 );
            bLandedThenJumped = scene._motor.getPosition()._y - groundY > 0.5f;
        }
    }
    SW_EXPECT_TRUE( bLandedThenJumped );
}

SW_TEST_CASE( PlatformerTest, WallsDashesDoubleJumpsPlatformsLaddersAndHazards )
{
    PlatformerScene scene;
    (void)scene.run( PlatformerInput{}, 30 );
    const float32 groundY = scene._motor.getPosition()._y;

    // 코요테 — 발판 끝에서 걸어 나간 직후에도 땅 점프가 된다(한쪽 발판 위에서).
    scene._motor.setPosition( float2{ 11.5f, 7.5f } ); // 발판(x 9..12, y 6) 위
    (void)scene.run( PlatformerInput{}, 10 );
    SW_EXPECT_TRUE( scene._motor.isGrounded() );
    for ( int32 frame = 0; frame < 60 && scene._motor.isGrounded(); ++frame )
        (void)scene.run( scene.press( 1.0f, false ), 1 );
    SW_EXPECT_FALSE( scene._motor.isGrounded() );
    (void)scene.run( scene.press( 1.0f, false ), 2 );
    const uint32 coyoteEvent = scene.run( scene.press( 1.0f, true ), 1 );
    SW_EXPECT_TRUE( ( coyoteEvent & PlatformerEvent::kJumped ) != 0 );

    // 발판 아래로 — 아래 + 점프로 내려간다.
    scene._motor.setPosition( float2{ 10.5f, 7.5f } );
    (void)scene.run( PlatformerInput{}, 10 );
    PlatformerInput drop;
    drop._move         = float2{ 0.0f, -1.0f };
    drop._bJumpPressed = SW_TRUE;
    (void)scene.run( drop, 1 );
    (void)scene.run( PlatformerInput{}, 40 );
    SW_EXPECT_NEAR_EQUAL( groundY, scene._motor.getPosition()._y, 1.0e-2f );

    // 벽 미끄러짐 · 벽 점프 — 왼쪽 벽(x 1)에 붙어 내려오다 뛰면 오른쪽으로 튄다.
    scene._motor.setPosition( float2{ 2.0f, 6.0f } );
    (void)scene.run( scene.press( -1.0f, false ), 20 );
    SW_EXPECT_EQUAL( -1, scene._motor.getWallSide() );
    SW_EXPECT_TRUE( scene._motor.getVelocity()._y >= -scene._motor.getSettings()._wallSlideSpeed - 1.0e-3f );
    const uint32 wallEvent = scene.run( scene.press( -1.0f, true ), 1 );
    SW_EXPECT_TRUE( ( wallEvent & PlatformerEvent::kWallJumped ) != 0 );
    SW_EXPECT_TRUE( scene._motor.getVelocity()._x > 0.0f && scene._motor.getVelocity()._y > 0.0f );

    // 대시 — 땅에서 오른쪽으로 대시 길이(18 × 0.15 ≈ 2.7)만큼.
    scene._motor.setPosition( float2{ 4.5f, groundY } );
    (void)scene.run( PlatformerInput{}, 10 );
    PlatformerInput dash;
    dash._bDashPressed   = SW_TRUE;
    const float32 startX = scene._motor.getPosition()._x;
    SW_EXPECT_TRUE( ( scene.run( dash, 1 ) & PlatformerEvent::kDashed ) != 0 );
    (void)scene.run( PlatformerInput{}, 9 );
    SW_EXPECT_NEAR_EQUAL( 2.7f, scene._motor.getPosition()._x - startX, 0.4f );
    SW_EXPECT_FALSE( ( scene.run( dash, 1 ) & PlatformerEvent::kDashed ) != 0 ); // 쿨다운

    // 2단 점프.
    PlatformerSettings doubleJump;
    doubleJump._extraJumpCount = 1;
    scene._motor.setSettings( doubleJump );
    scene._motor.setPosition( float2{ 6.5f, groundY } );
    (void)scene.run( PlatformerInput{}, 5 );
    (void)scene.run( scene.press( 0.0f, true ), 1 );
    (void)scene.run( scene.press( 0.0f, false, true ), 20 );
    SW_EXPECT_TRUE( ( scene.run( scene.press( 0.0f, true ), 1 ) & PlatformerEvent::kAirJumped ) != 0 );
    SW_EXPECT_FALSE( ( scene.run( scene.press( 0.0f, true ), 1 ) & ( PlatformerEvent::kAirJumped | PlatformerEvent::kJumped ) ) != 0 );

    // 사다리 — 위를 누르면 잡고 오른다.
    scene._motor.setSettings( PlatformerSettings{} );
    scene._motor.setPosition( float2{ 18.5f, groundY } );
    PlatformerInput climb;
    climb._move = float2{ 0.0f, 1.0f };
    (void)scene.run( climb, 30 );
    SW_EXPECT_TRUE( scene._motor.isClimbing() );
    SW_EXPECT_TRUE( scene._motor.getPosition()._y > groundY + 1.5f );

    // 위험 칸.
    scene._motor.setPosition( float2{ 3.5f, groundY } );
    SW_EXPECT_TRUE( ( scene.run( PlatformerInput{}, 1 ) & PlatformerEvent::kTouchedHazard ) != 0 );
}
