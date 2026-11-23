// HubPawnStateTest — 허브 폰의 소유자 상태는 칸 선언이 바이트를 정한다: 왕복이 비트까지 같고, NaN · 잘린 바이트는 거절하며, 재조정 판정은 몸(걷기 · 말)과 자리를 본다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Games/Playground/Hub/HubPawnReplicationComponent.h"

#include <limits>

using namespace sw;

namespace
{
    HubPawnState makeRidingState()
    {
        HubPawnState state;
        state._walk._motion._position      = float3{ 12.5f, 0.05f, -33.25f };
        state._walk._motion._verticalSpeed = -2.5f;
        state._walk._motion._movementMode  = static_cast<uint8>( CharacterMovementMode::Falling );
        state._walk._velocity              = float3{ 1.0f, 0.0f, 3.0f };
        state._walk._facingYaw             = -2.75f;
        state._ride._position              = float3{ 70.125f, 0.0f, 8.5f };
        state._ride._facingYaw             = 1.25f;
        state._ride._forwardSpeed          = 9.75f;
        state._ride._gait                  = static_cast<uint8>( MountGait::Gallop );
        state._mountNumber                 = 2;
        return state;
    }
} // namespace

/** @brief [HubPawnStateTest] 걷는 몸 · 말 상태가 양자화 없이 왕복한다 — 예측이 서버와 같은 값에서 시작한다 */
SW_TEST_CASE( HubPawnStateTest, OwnerStateRoundTripsExactly )
{
    const HubPawnState sent = makeRidingState();
    vector<uint8>      bytes;
    SW_REQUIRE_TRUE( HubPawnReplicationComponent::writeState( sent, bytes ) );
    SW_EXPECT_TRUE( static_cast<int32>( bytes.size() ) <= NetSnapshot::kMaxEntityBytes );
    HubPawnState received;
    SW_REQUIRE_TRUE( HubPawnReplicationComponent::readState( bytes, received ) );
    SW_EXPECT_TRUE( received._walk._motion._position == sent._walk._motion._position );
    SW_EXPECT_EQUAL( sent._walk._motion._verticalSpeed, received._walk._motion._verticalSpeed );
    SW_EXPECT_TRUE( received._walk._velocity == sent._walk._velocity );
    SW_EXPECT_EQUAL( sent._walk._facingYaw, received._walk._facingYaw );
    SW_EXPECT_TRUE( received._ride._position == sent._ride._position );
    SW_EXPECT_EQUAL( sent._ride._forwardSpeed, received._ride._forwardSpeed );
    SW_EXPECT_EQUAL( sent._ride._gait, received._ride._gait );
    SW_EXPECT_EQUAL( sent._mountNumber, received._mountNumber );
}

/** @brief [HubPawnStateTest] NaN · 범위 밖 탈것 번호는 쓰지 않고, 잘린 바이트는 읽지 않는다 */
SW_TEST_CASE( HubPawnStateTest, BrokenStatesAreRefused )
{
    vector<uint8> bytes;
    HubPawnState  broken              = makeRidingState();
    broken._walk._motion._position._x = std::numeric_limits<float32>::quiet_NaN();
    SW_EXPECT_FALSE( HubPawnReplicationComponent::writeState( broken, bytes ) );
    HubPawnState badMount = makeRidingState();
    badMount._mountNumber = static_cast<uint8>( HubPawnReplicationComponent::kMaxMount + 1u );
    SW_EXPECT_FALSE( HubPawnReplicationComponent::writeState( badMount, bytes ) );

    SW_REQUIRE_TRUE( HubPawnReplicationComponent::writeState( makeRidingState(), bytes ) );
    const vector<uint8> truncated( bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>( bytes.size() / 2 ) );
    HubPawnState        received;
    SW_EXPECT_FALSE( HubPawnReplicationComponent::readState( truncated, received ) );
    SW_EXPECT_EQUAL( uint8{ 0 }, received._mountNumber ); // 실패하면 값을 바꾸지 않는다
}

/** @brief [HubPawnStateTest] 재조정 판정 — 타고 있는 동안은 말의 자리만, 타고 내림이 갈리면 늘 다르다 */
SW_TEST_CASE( HubPawnStateTest, ClosenessFollowsTheDrivenBody )
{
    const HubPawnState riding     = makeRidingState();
    HubPawnState       walkedAway = riding;
    walkedAway._walk._motion._position._x += 5.0f;
    SW_EXPECT_TRUE( HubPawnReplicationComponent::isClose( riding, walkedAway, 0.01f ) ); // 탄 동안 걷는 몸의 자리는 뜻이 없다
    HubPawnState moved = riding;
    moved._ride._position._z += 0.5f;
    SW_EXPECT_FALSE( HubPawnReplicationComponent::isClose( riding, moved, 0.01f ) );
    HubPawnState onFoot = riding;
    onFoot._mountNumber = 0;
    SW_EXPECT_FALSE( HubPawnReplicationComponent::isClose( riding, onFoot, 0.01f ) );
}
