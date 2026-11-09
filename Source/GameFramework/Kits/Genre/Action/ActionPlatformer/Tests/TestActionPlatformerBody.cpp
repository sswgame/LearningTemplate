#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Genre/Action/ActionPlatformer/Rule/ActionPlatformerBody.h"

#include <cstring>
#include <limits>

// ActionPlatformerBodyTest — 이동 모드: 격자 읽기(흙 · 갈고리 지점), 갈고리 거리 경계와 늘어나지 않는 줄, 활공 낙하 상한과 설정 되돌림, 드릴 진입 · 튀어나옴 · 그만둠 · 바위, 상태 왕복.
// 결함: 흙에 닿기 전에 바위에 막히면 찾기 시간이 멈춰 드릴 모드에 갇혔다 · 수치가 깨진 상태를 받았다. 키트 GF_ActionPlatformer(nogpu).

namespace
{
    constexpr float32 kBodyStep      = 1.0f / 60.0f;
    constexpr float32 kBodyTolerance = 1.0e-3f;

    // 갈고리 지점은 (6.5, 9.5)다.
    constexpr const utf8* kOpenRoom = R"(
############
#          #
#     O    #
#          #
#          #
#          #
#          #
#          #
#          #
#          #
#          #
############
)";

    // 흙은 x 5..7, y 1..3 이다.
    constexpr const utf8* kDirtRoom = R"(
############
#          #
#          #
#    DDD   #
#    DDD   #
#    DDD   #
############
)";

    // 흙(y 2, x 5..7) 바로 오른쪽이 바위다.
    constexpr const utf8* kDirtThenRockRoom = R"(
############
#          #
#    DDD#  #
#          #
############
)";

    // 칸 0.5: 바위(x 4)가 흙(x 5) 앞을 막는다. 몸에서 앞으로 0.65 를 보면 바위 너머의 흙이 보인다.
    constexpr const utf8* kRockBeforeDirtRoom = R"(
#########
#       #
#   #D  #
#       #
#########
)";

    struct ActionPlatformerBodyTestUtil
    {
        static sw::ActionBodyInput makeDrillInput( float32 moveX, float32 moveY )
        {
            sw::ActionBodyInput input;
            input._motor._move = sw::float2{ moveX, moveY };
            input._bDrillHeld  = SW_TRUE;
            return input;
        }

        static sw::ActionBodyInput makeGrappleInput( bool bPressed, bool bHeld )
        {
            sw::ActionBodyInput input;
            input._bGrapplePressed = bPressed ? SW_TRUE : SW_FALSE;
            input._bGrappleHeld    = bHeld ? SW_TRUE : SW_FALSE;
            return input;
        }

        static sw::ActionBodyInput makeGlideInput()
        {
            sw::ActionBodyInput input;
            input._bGlideHeld = SW_TRUE;
            return input;
        }

        /** @brief @p event 가 날 때까지(또는 @p maxTicks 까지) 돌리고 났으면 true 입니다. */
        static bool runUntilEvent( sw::ActionPlatformerBody& body, const sw::PlatformTileMap& map, const sw::ActionTerrainGrid& terrain, const sw::ActionBodyInput& input, uint32 event, int32 maxTicks )
        {
            for ( int32 tick = 0; tick < maxTicks; ++tick )
            {
                body.update( map, terrain, input, kBodyStep );
                if ( body.hasEvent( event ) )
                    return true;
            }
            return false;
        }
    };
} // namespace

/**
 * @brief [ActionTerrainGridTest] 글은 첫 줄이 맨 위이고, 'D' 는 기반 지형의 벽이 되며, 'O' 는 칸 가운데 갈고리 지점이다(칸 크기 · 원점 반영). 격자 밖은 흙이 아니다
 */
SW_TEST_CASE( ActionTerrainGridTest, LoadsDirtAndGrapplePointsFromText )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid grid;
    SW_REQUIRE_TRUE( grid.loadFromXMLText( "D O\r\n\n  #\n", 2.0f, sw::float2{ 10.0f, 20.0f }, map ) );
    SW_EXPECT_EQUAL( 3, grid.getWidth() );
    SW_EXPECT_EQUAL( 2, grid.getHeight() );

    SW_EXPECT_TRUE( grid.isDirt( 0, 1 ) );
    SW_EXPECT_FALSE( grid.isDirt( 0, 0 ) );
    SW_EXPECT_FALSE( grid.isDirt( -1, 1 ) );
    SW_EXPECT_FALSE( grid.isDirt( 3, 1 ) );
    SW_EXPECT_FALSE( grid.isDirt( 0, 2 ) );
    SW_EXPECT_TRUE( grid.isDirtAt( sw::float2{ 11.0f, 23.0f } ) );
    SW_EXPECT_FALSE( grid.isDirtAt( sw::float2{ 9.9f, 23.0f } ) ); // 원점 왼쪽
    SW_EXPECT_FALSE( grid.isDirtAt( sw::float2{ 11.0f, 21.0f } ) );

    SW_EXPECT_TRUE( map.getTile( 0, 1 ) == sw::PlatformTile::Solid );
    SW_EXPECT_TRUE( map.getTile( 2, 1 ) == sw::PlatformTile::Empty );
    SW_EXPECT_TRUE( map.getTile( 2, 0 ) == sw::PlatformTile::Solid );

    SW_REQUIRE_EQUAL( size_t( 1 ), grid.getGrapplePoints().size() );
    SW_EXPECT_NEAR( 15.0f, grid.getGrapplePoints()[0]._x, kBodyTolerance );
    SW_EXPECT_NEAR( 23.0f, grid.getGrapplePoints()[0]._y, kBodyTolerance );

    SW_REQUIRE_TRUE( grid.loadFromXMLText( "   \n", 1.0f, sw::float2{}, map ) ); // 다시 읽으면 앞 내용이 남지 않는다
    SW_EXPECT_TRUE( grid.getGrapplePoints().empty() );
    SW_EXPECT_FALSE( grid.isDirt( 0, 0 ) );
}

/**
 * @brief [ActionTerrainGridTest] 칸 크기가 0 이하면 읽기를 거절하고 격자를 그대로 둔다
 */
SW_TEST_CASE( ActionTerrainGridTest, RejectsNonPositiveTileSize )
{
    SW_EXPECT_LOG( "PlatformerMotor2D.PlatformTileMapRefusedXTiles" );
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid grid;
    SW_REQUIRE_TRUE( grid.loadFromXMLText( "D\n", 1.0f, sw::float2{}, map ) );
    SW_EXPECT_FALSE( grid.loadFromXMLText( "  \n", 0.0f, sw::float2{}, map ) );
    SW_EXPECT_FALSE( grid.loadFromXMLText( "  \n", -1.0f, sw::float2{}, map ) );
    SW_EXPECT_TRUE( grid.isDirt( 0, 0 ) );
}

/**
 * @brief [ActionTerrainGridTest] 갈고리 찾기는 거리 양 끝을 포함해 가장 가까운 것을 고르고, 같으면 앞 번호이며, 범위 안에 없으면 −1 이다
 */
SW_TEST_CASE( ActionTerrainGridTest, FindGrapplePointHonorsRangeBoundsAndTies )
{
    sw::ActionTerrainGrid grid;
    SW_EXPECT_FALSE( grid.findGrapplePoint( sw::float2{}, 0.0f, 100.0f ).has_value() );
    grid.addGrapplePoint( sw::float2{ 0.5f, 0.0f } );
    grid.addGrapplePoint( sw::float2{ 3.0f, 0.0f } );
    grid.addGrapplePoint( sw::float2{ -3.0f, 0.0f } );

    SW_EXPECT_TRUE( grid.findGrapplePoint( sw::float2{}, 0.5f, 5.0f ) == 0 ); // 최소 거리 포함
    SW_EXPECT_TRUE( grid.findGrapplePoint( sw::float2{}, 1.0f, 3.0f ) == 1 ); // 최대 거리 포함, 동률은 앞 번호
    SW_EXPECT_FALSE( grid.findGrapplePoint( sw::float2{}, 1.0f, 2.99f ).has_value() );
    SW_EXPECT_FALSE( grid.findGrapplePoint( sw::float2{}, 3.01f, 100.0f ).has_value() );
}

/**
 * @brief [ActionPlatformerBodyTest] 활공은 공중에서만 켜져 낙하 속도를 상한 아래로 묶고, 떼면 원래 설정으로 돌아간다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, GlideLimitsFallSpeedAndRestoresSettings )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kOpenRoom, 1.0f, sw::float2{}, map ) );
    const sw::PlatformerSettings motorSettings{};
    const sw::ActionBodyDef      bodySettings{};

    sw::ActionPlatformerBody gliding;
    gliding.initialize( motorSettings, bodySettings );
    gliding.setPosition( sw::float2{ 3.5f, 9.5f } );
    sw::ActionPlatformerBody falling;
    falling.initialize( motorSettings, bodySettings );
    falling.setPosition( sw::float2{ 3.5f, 9.5f } );

    const sw::ActionBodyInput glideInput = ActionPlatformerBodyTestUtil::makeGlideInput();
    gliding.update( map, terrain, glideInput, kBodyStep );
    SW_EXPECT_TRUE( gliding.getMode() == sw::ActionMoveMode::Glide );
    SW_EXPECT_TRUE( gliding.hasEvent( sw::ActionBodyEvent::kGlideStarted ) );
    SW_EXPECT_NEAR( bodySettings._glideFallSpeed, gliding.getMotor().getSettings()._maxFallSpeed, kBodyTolerance );

    for ( int32 tick = 0; tick < 30; ++tick )
    {
        gliding.update( map, terrain, glideInput, kBodyStep );
        falling.update( map, terrain, sw::ActionBodyInput{}, kBodyStep );
        SW_EXPECT_TRUE( gliding.getVelocity()._y >= -bodySettings._glideFallSpeed - kBodyTolerance );
        SW_EXPECT_FALSE( gliding.hasEvent( sw::ActionBodyEvent::kGlideStarted ) ); // 켤 때 한 번만 알린다
    }
    SW_EXPECT_TRUE( falling.getVelocity()._y < -bodySettings._glideFallSpeed ); // 활공 없이는 상한을 넘는다

    gliding.update( map, terrain, sw::ActionBodyInput{}, kBodyStep );
    SW_EXPECT_TRUE( gliding.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_TRUE( gliding.hasEvent( sw::ActionBodyEvent::kGlideEnded ) );
    SW_EXPECT_NEAR( motorSettings._maxFallSpeed, gliding.getMotor().getSettings()._maxFallSpeed, kBodyTolerance );
    SW_EXPECT_NEAR( motorSettings._fallGravityScale, gliding.getMotor().getSettings()._fallGravityScale, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 땅에 닿으면 활공이 스스로 끝나고, 서 있는 동안에는 눌러도 켜지지 않는다. 위치를 옮겨도 활공이 풀린다. 0 시간 틱은 사건만 지운다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, GlideEndsOnGround )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kOpenRoom, 1.0f, sw::float2{}, map ) );
    const sw::PlatformerSettings motorSettings{};
    sw::ActionPlatformerBody     body;
    body.initialize( motorSettings, sw::ActionBodyDef{} );
    body.setPosition( sw::float2{ 3.5f, 1.8f } );

    const sw::ActionBodyInput glideInput = ActionPlatformerBodyTestUtil::makeGlideInput();
    SW_REQUIRE_TRUE( ActionPlatformerBodyTestUtil::runUntilEvent( body, map, terrain, glideInput, sw::ActionBodyEvent::kGlideEnded, 120 ) );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_TRUE( body.getMotor().isGrounded() );

    body.update( map, terrain, glideInput, kBodyStep );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_FALSE( body.hasEvent( sw::ActionBodyEvent::kGlideStarted ) );

    body.setPosition( sw::float2{ 3.5f, 9.5f } );
    body.update( map, terrain, sw::ActionBodyInput{}, kBodyStep );
    body.update( map, terrain, glideInput, kBodyStep );
    SW_REQUIRE_TRUE( body.getMode() == sw::ActionMoveMode::Glide );
    const sw::float2 before = body.getPosition();
    body.update( map, terrain, glideInput, 0.0f );
    SW_EXPECT_EQUAL( 0u, body.getEvents() );
    SW_EXPECT_NEAR( before._y, body.getPosition()._y, kBodyTolerance );
    body.setPosition( sw::float2{ 4.5f, 9.5f } );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_NEAR( motorSettings._maxFallSpeed, body.getMotor().getSettings()._maxFallSpeed, kBodyTolerance );
    SW_EXPECT_NEAR( 0.0f, body.getVelocity()._y, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 갈고리는 거리 범위 안의 지점에만 걸리고, 매달린 동안 줄이 늘어나지 않으며, 놓으면 진자 속도를 그대로 기반 몸에 넘긴다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, GrappleKeepsRopeLengthAndHandsVelocityBack )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kOpenRoom, 1.0f, sw::float2{}, map ) );
    sw::ActionPlatformerBody body;
    body.initialize( sw::PlatformerSettings{}, sw::ActionBodyDef{} );

    // 지점 바로 옆(최소 거리 0.75 안)에서는 걸리지 않는다.
    body.setPosition( sw::float2{ 6.5f, 9.0f } );
    body.update( map, terrain, ActionPlatformerBodyTestUtil::makeGrappleInput( true, true ), kBodyStep );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_FALSE( body.hasEvent( sw::ActionBodyEvent::kGrappleAttached ) );

    body.setPosition( sw::float2{ 3.5f, 6.5f } );
    body.update( map, terrain, ActionPlatformerBodyTestUtil::makeGrappleInput( true, true ), kBodyStep );
    SW_REQUIRE_TRUE( body.getMode() == sw::ActionMoveMode::Grapple );
    SW_EXPECT_TRUE( body.hasEvent( sw::ActionBodyEvent::kGrappleAttached ) );
    SW_EXPECT_NEAR( 6.5f, body.getAnchor()._x, kBodyTolerance );
    SW_EXPECT_NEAR( 9.5f, body.getAnchor()._y, kBodyTolerance );
    const float32 rope = body.getRopeLength();
    SW_EXPECT_NEAR( 4.2426f, rope, kBodyTolerance );

    const sw::ActionBodyInput hold = ActionPlatformerBodyTestUtil::makeGrappleInput( false, true );
    for ( int32 tick = 0; tick < 40; ++tick )
    {
        body.update( map, terrain, hold, kBodyStep );
        SW_EXPECT_TRUE( sw::float2::getDistance( body.getPosition(), body.getAnchor() ) <= rope + kBodyTolerance );
        SW_EXPECT_TRUE( body.getVelocity().getLength() <= sw::ActionBodyDef{}._grappleMaxSpeed + kBodyTolerance );
    }
    SW_EXPECT_TRUE( body.getPosition()._x > 3.5f ); // 중력이 진자를 움직였다

    const sw::float2 positionBefore = body.getPosition();
    const sw::float2 velocityBefore = body.getVelocity();
    SW_EXPECT_TRUE( velocityBefore.getLength() > 1.0f );
    body.update( map, terrain, sw::ActionBodyInput{}, kBodyStep );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_TRUE( body.hasEvent( sw::ActionBodyEvent::kGrappleReleased ) );
    SW_EXPECT_NEAR( positionBefore._x, body.getPosition()._x, kBodyTolerance );
    SW_EXPECT_NEAR( velocityBefore._x, body.getVelocity()._x, kBodyTolerance );
    SW_EXPECT_NEAR( velocityBefore._y, body.getVelocity()._y, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 활공 중에 걸면 활공 설정이 풀리고, 누르자마자 뗀 틱은 걸고 바로 놓는다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, GrappleFromGlideRestoresSettings )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kOpenRoom, 1.0f, sw::float2{}, map ) );
    const sw::PlatformerSettings motorSettings{};
    sw::ActionPlatformerBody     body;
    body.initialize( motorSettings, sw::ActionBodyDef{} );
    body.setPosition( sw::float2{ 3.5f, 6.5f } );
    body.update( map, terrain, ActionPlatformerBodyTestUtil::makeGlideInput(), kBodyStep );
    SW_REQUIRE_TRUE( body.getMode() == sw::ActionMoveMode::Glide );

    body.update( map, terrain, ActionPlatformerBodyTestUtil::makeGrappleInput( true, false ), kBodyStep );
    SW_EXPECT_TRUE( body.hasEvent( sw::ActionBodyEvent::kGlideEnded ) );
    SW_EXPECT_TRUE( body.hasEvent( sw::ActionBodyEvent::kGrappleAttached ) );
    SW_EXPECT_TRUE( body.hasEvent( sw::ActionBodyEvent::kGrappleReleased ) );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_NEAR( motorSettings._maxFallSpeed, body.getMotor().getSettings()._maxFallSpeed, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 드릴은 앞에 흙이 있을 때만 들어가고, 흙을 지나 튀어나오면 몸 상자가 흙에 걸치지 않게 칸 밖으로 옮겨 나온 속도로 기반 몸에 돌아간다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, DrillEntersOnlyTowardDirtAndExitsOutsideTheTile )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kDirtRoom, 1.0f, sw::float2{}, map ) );
    sw::ActionPlatformerBody body;
    body.initialize( sw::PlatformerSettings{}, sw::ActionBodyDef{} );
    const sw::ActionBodyInput right = ActionPlatformerBodyTestUtil::makeDrillInput( 1.0f, 0.0f );

    body.setPosition( sw::float2{ 3.5f, 2.5f } ); // 앞으로 0.65 를 봐도 빈 칸
    body.update( map, terrain, right, kBodyStep );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );

    body.setPosition( sw::float2{ 4.5f, 2.5f } );
    body.update( map, terrain, right, kBodyStep );
    SW_REQUIRE_TRUE( body.getMode() == sw::ActionMoveMode::Drill );
    SW_EXPECT_TRUE( body.hasEvent( sw::ActionBodyEvent::kDrillEntered ) );
    SW_EXPECT_NEAR( 11.0f, body.getVelocity()._x, kBodyTolerance );

    SW_REQUIRE_TRUE( ActionPlatformerBodyTestUtil::runUntilEvent( body, map, terrain, right, sw::ActionBodyEvent::kDrillExited, 40 ) );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_FALSE( body.hasEvent( sw::ActionBodyEvent::kDrillJumped ) );
    SW_EXPECT_NEAR( 8.0f + 0.35f + 0.001f, body.getPosition()._x, kBodyTolerance ); // 칸 x 8 왼쪽 + 몸 반폭 + 틈
    SW_EXPECT_NEAR( 2.5f, body.getPosition()._y, kBodyTolerance );
    SW_EXPECT_NEAR( 11.0f, body.getVelocity()._x, kBodyTolerance );
    SW_EXPECT_NEAR( 0.0f, body.getVelocity()._y, kBodyTolerance );
    SW_EXPECT_NEAR( body.getPosition()._x, body.getMotor().getPosition()._x, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 위로 튀어나오며 점프를 누르고 있으면 도약 속도를 받고, 빠져나온 위쪽 칸 밖에 놓인다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, DrillExitWithJumpLaunchesUpward )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kDirtRoom, 1.0f, sw::float2{}, map ) );
    sw::ActionPlatformerBody body;
    body.initialize( sw::PlatformerSettings{}, sw::ActionBodyDef{} );
    body.setPosition( sw::float2{ 6.5f, 2.5f } );

    sw::ActionBodyInput up = ActionPlatformerBodyTestUtil::makeDrillInput( 0.0f, 1.0f );
    up._motor._bJumpHeld   = SW_TRUE;
    SW_REQUIRE_TRUE( ActionPlatformerBodyTestUtil::runUntilEvent( body, map, terrain, up, sw::ActionBodyEvent::kDrillExited, 40 ) );
    SW_EXPECT_TRUE( body.hasEvent( sw::ActionBodyEvent::kDrillJumped ) );
    SW_EXPECT_NEAR( 6.5f, body.getPosition()._x, kBodyTolerance );
    SW_EXPECT_NEAR( 4.0f + 0.45f + 0.001f, body.getPosition()._y, kBodyTolerance );
    SW_EXPECT_NEAR( 16.0f, body.getVelocity()._y, kBodyTolerance ); // 나오는 속도 11 보다 도약 16 이 크다
}

/**
 * @brief [ActionPlatformerBodyTest] 흙에 닿기 전에 돌아서면 찾기 시간이 다 되어 드릴을 그만두고 절반 속도로 돌아간다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, DrillAbortsWhenItNeverReachesDirt )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kDirtRoom, 1.0f, sw::float2{}, map ) );
    sw::ActionPlatformerBody body;
    body.initialize( sw::PlatformerSettings{}, sw::ActionBodyDef{} );
    body.setPosition( sw::float2{ 4.5f, 2.5f } );
    body.update( map, terrain, ActionPlatformerBodyTestUtil::makeDrillInput( 1.0f, 0.0f ), kBodyStep );
    SW_REQUIRE_TRUE( body.getMode() == sw::ActionMoveMode::Drill );

    SW_REQUIRE_TRUE( ActionPlatformerBodyTestUtil::runUntilEvent( body, map, terrain, ActionPlatformerBodyTestUtil::makeDrillInput( -1.0f, 0.0f ), sw::ActionBodyEvent::kDrillAborted, 20 ) );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_NEAR( -5.5f, body.getVelocity()._x, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 결함: 흙에 닿기 전에 바위에 막히면 찾기 시간이 멈춰 드릴 모드에 영영 갇혔다. 막혀 있어도 시간이 흘러 그만둔다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, DrillBlockedByRockBeforeDirtStillAborts )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kRockBeforeDirtRoom, 0.5f, sw::float2{}, map ) );
    sw::ActionPlatformerBody body;
    body.initialize( sw::PlatformerSettings{}, sw::ActionBodyDef{} );
    body.setPosition( sw::float2{ 1.95f, 1.25f } );

    const sw::ActionBodyInput right = ActionPlatformerBodyTestUtil::makeDrillInput( 1.0f, 0.0f );
    body.update( map, terrain, right, kBodyStep );
    SW_REQUIRE_TRUE( body.getMode() == sw::ActionMoveMode::Drill );
    SW_EXPECT_NEAR( 1.95f, body.getPosition()._x, kBodyTolerance ); // 바위에 막혀 움직이지 않는다

    SW_REQUIRE_TRUE( ActionPlatformerBodyTestUtil::runUntilEvent( body, map, terrain, right, sw::ActionBodyEvent::kDrillAborted, 20 ) );
    SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Normal );
    SW_EXPECT_NEAR( 1.95f, body.getPosition()._x, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 흙 안에서 바위를 만나면 그만두지 않고 멈춰 있다가, 방향을 바꾸면 빠져나온다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, DrillInsideDirtWaitsAtRock )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kDirtThenRockRoom, 1.0f, sw::float2{}, map ) );
    sw::ActionPlatformerBody body;
    body.initialize( sw::PlatformerSettings{}, sw::ActionBodyDef{} );
    body.setPosition( sw::float2{ 4.5f, 2.5f } );

    const sw::ActionBodyInput right = ActionPlatformerBodyTestUtil::makeDrillInput( 1.0f, 0.0f );
    for ( int32 tick = 0; tick < 60; ++tick )
    {
        body.update( map, terrain, right, kBodyStep );
        SW_EXPECT_TRUE( body.getMode() == sw::ActionMoveMode::Drill );
        SW_EXPECT_FALSE( body.hasEvent( sw::ActionBodyEvent::kDrillAborted ) );
    }
    SW_EXPECT_TRUE( body.getPosition()._x < 8.0f );
    SW_EXPECT_TRUE( body.getPosition()._x > 7.5f );

    SW_REQUIRE_TRUE( ActionPlatformerBodyTestUtil::runUntilEvent( body, map, terrain, ActionPlatformerBodyTestUtil::makeDrillInput( -1.0f, 0.0f ), sw::ActionBodyEvent::kDrillExited, 40 ) );
    SW_EXPECT_NEAR( 5.0f - 0.35f - 0.001f, body.getPosition()._x, kBodyTolerance );
    SW_EXPECT_NEAR( -11.0f, body.getVelocity()._x, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 드릴 · 갈고리 · 활공 중에 쓰고 읽어도 원본과 같이 움직이고, 활공이면 활공 설정을 다시 건다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, StateRoundTripResumesEveryMode )
{
    sw::PlatformTileMap   map;
    sw::ActionTerrainGrid terrain;
    SW_REQUIRE_TRUE( terrain.loadFromXMLText( kDirtRoom, 1.0f, sw::float2{}, map ) );
    const sw::ActionBodyDef bodySettings{};

    sw::ActionPlatformerBody drilling;
    drilling.initialize( sw::PlatformerSettings{}, bodySettings );
    drilling.setPosition( sw::float2{ 4.5f, 2.5f } );
    const sw::ActionBodyInput right = ActionPlatformerBodyTestUtil::makeDrillInput( 1.0f, 0.0f );
    for ( int32 tick = 0; tick < 5; ++tick )
    {
        drilling.update( map, terrain, right, kBodyStep );
    }
    SW_REQUIRE_TRUE( drilling.getMode() == sw::ActionMoveMode::Drill );

    sw::Archive written;
    drilling.writeState( written );
    sw::ActionPlatformerBody restored;
    restored.initialize( sw::PlatformerSettings{}, bodySettings );
    sw::Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_TRUE( restored.getMode() == sw::ActionMoveMode::Drill );
    for ( int32 tick = 0; tick < 30; ++tick )
    {
        drilling.update( map, terrain, right, kBodyStep );
        restored.update( map, terrain, right, kBodyStep );
        SW_EXPECT_TRUE( drilling.getMode() == restored.getMode() );
        SW_EXPECT_NEAR( drilling.getPosition()._x, restored.getPosition()._x, kBodyTolerance );
        SW_EXPECT_EQUAL( drilling.getEvents(), restored.getEvents() );
    }

    sw::ActionPlatformerBody gliding;
    gliding.initialize( sw::PlatformerSettings{}, bodySettings );
    gliding.setPosition( sw::float2{ 2.5f, 5.0f } );
    gliding.update( map, terrain, ActionPlatformerBodyTestUtil::makeGlideInput(), kBodyStep );
    SW_REQUIRE_TRUE( gliding.getMode() == sw::ActionMoveMode::Glide );
    sw::Archive glideWritten;
    gliding.writeState( glideWritten );
    sw::ActionPlatformerBody glideRestored;
    glideRestored.initialize( sw::PlatformerSettings{}, bodySettings );
    sw::Archive glideReading( glideWritten.getData(), glideWritten.getSize() );
    SW_REQUIRE_TRUE( glideRestored.readState( glideReading ) );
    SW_EXPECT_TRUE( glideRestored.getMode() == sw::ActionMoveMode::Glide );
    SW_EXPECT_NEAR( bodySettings._glideFallSpeed, glideRestored.getMotor().getSettings()._maxFallSpeed, kBodyTolerance );
}

/**
 * @brief [ActionPlatformerBodyTest] 결함: NaN 위치 · 줄 끝을 받아들였다. 모르는 모드 · 음수 줄 · 깨진 표시 · 잘린 바이트와 함께 거절하고 몸을 그대로 둔다
 */
SW_TEST_CASE( ActionPlatformerBodyTest, ReadRejectsBrokenBytes )
{
    sw::ActionPlatformerBody source;
    source.initialize( sw::PlatformerSettings{}, sw::ActionBodyDef{} );
    source.setPosition( sw::float2{ 1.0f, 2.0f } );
    sw::Archive written;
    source.writeState( written );

    // 끝에서: 위치(8) 속도(8) 밧줄 끝(8) 길이(4) 드릴 시간(4) 흙 안 표시(1) = 33 바이트. 맨 앞 1 바이트는 모드다.
    const size_t size = static_cast<size_t>( written.getSize() );
    SW_REQUIRE_TRUE( size > 34 );

    sw::ActionPlatformerBody target;
    target.initialize( sw::PlatformerSettings{}, sw::ActionBodyDef{} );
    target.setPosition( sw::float2{ 7.0f, 7.0f } );
    const auto expectRejected = [&]( size_t offset, const void* pValue, size_t valueSize )
    {
        sw::vector<uint8> listByte( written.getData(), written.getData() + size );
        std::memcpy( listByte.data() + offset, pValue, valueSize );
        sw::Archive reading( listByte.data(), listByte.size() );
        SW_EXPECT_FALSE( target.readState( reading ) );
        SW_EXPECT_NEAR( 7.0f, target.getPosition()._x, kBodyTolerance );
    };

    const uint8   badMode      = 9;
    const uint8   badFlag      = 2;
    const float32 nan          = std::numeric_limits<float32>::quiet_NaN();
    const float32 infinity     = std::numeric_limits<float32>::infinity();
    const float32 negativeRope = -1.0f;
    expectRejected( 0, &badMode, sizeof( badMode ) );
    expectRejected( size - 1, &badFlag, sizeof( badFlag ) );
    expectRejected( size - 33, &nan, sizeof( nan ) );           // 위치
    expectRejected( size - 25, &infinity, sizeof( infinity ) ); // 속도
    expectRejected( size - 17, &nan, sizeof( nan ) );           // 밧줄 끝
    expectRejected( size - 9, &negativeRope, sizeof( negativeRope ) );
    expectRejected( size - 9, &nan, sizeof( nan ) );

    sw::Archive truncated( written.getData(), written.getSize() - 1 );
    SW_EXPECT_FALSE( target.readState( truncated ) );
    SW_EXPECT_NEAR( 7.0f, target.getPosition()._x, kBodyTolerance );

    sw::Archive whole( written.getData(), written.getSize() );
    SW_EXPECT_TRUE( target.readState( whole ) );
    SW_EXPECT_NEAR( 1.0f, target.getPosition()._x, kBodyTolerance );
}
