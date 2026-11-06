// LockOnSelectorTest — 록온: 후보 고르기(거리 · 각 · 가림 · 우선도), 좌우 넘기기, 유지 · 해제(거리 · 사라짐 · 가림 유예), 저장(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Combat/LockOnSelector.h"

using namespace sw;

namespace
{
    const float3 kEye{ 0.0f, 0.0f, 0.0f };
    const float3 kForward{ 0.0f, 0.0f, 1.0f };

    LockOnCandidate makeCandidate( uint64 id, float32 x, float32 z, float32 priority = 0.0f, uint8 bVisible = SW_TRUE )
    {
        LockOnCandidate candidate;
        candidate._position = float3{ x, 0.0f, z };
        candidate._id       = id;
        candidate._priority = priority;
        candidate._bVisible = bVisible;
        return candidate;
    }
} // namespace

/** @brief [LockOnSelectorTest] 고르기: 가깝고 앞에 있는 후보가 먼저다. 범위 · 각 밖과 가려진 후보, id 0 은 뺀다. 후보가 없으면 풀린다 */
SW_TEST_CASE( LockOnSelectorTest, PickBestFiltersAndScores )
{
    LockOnSelector selector;
    SW_EXPECT_EQUAL( uint64( 2 ), selector.pickBest( kEye, kForward, { makeCandidate( 1, 0.0f, 20.0f ), makeCandidate( 2, 0.0f, 5.0f ) } ) );
    SW_EXPECT_EQUAL( uint64( 1 ), selector.pickBest( kEye, kForward, { makeCandidate( 1, 0.0f, 5.0f ), makeCandidate( 2, 3.0f, 5.0f ) } ) ); // 거리가 비슷하면 앞쪽이다

    SW_EXPECT_EQUAL( uint64( 0 ), selector.pickBest( kEye, kForward,
                                                     {
                                                         makeCandidate( 0, 0.0f, 5.0f ),                 // id 없음
                                                         makeCandidate( 3, 0.0f, 26.0f ),                // 멀다
                                                         makeCandidate( 4, 10.0f, 1.0f ),                // 옆이다
                                                         makeCandidate( 5, 0.0f, -5.0f ),                // 뒤다
                                                         makeCandidate( 6, 0.0f, 5.0f, 0.0f, SW_FALSE ), // 가려졌다
                                                     } ) );
    SW_EXPECT_FALSE( selector.hasTarget() );

    float3          aerial{ 0.0f, 30.0f, 1.0f };
    LockOnCandidate above = makeCandidate( 7, 0.0f, 1.0f );
    above._position       = aerial; // 높이 차도 거리에 든다
    SW_EXPECT_EQUAL( uint64( 0 ), selector.pickBest( kEye, kForward, { above } ) );
}

/** @brief [LockOnSelectorTest] 우선도가 크면 더 먼 후보를 먼저 잡는다 */
SW_TEST_CASE( LockOnSelectorTest, PriorityWins )
{
    LockOnSelector selector;
    SW_EXPECT_EQUAL( uint64( 2 ), selector.pickBest( kEye, kForward, { makeCandidate( 1, 0.0f, 5.0f ), makeCandidate( 2, 0.0f, 20.0f, 5.0f ) } ) );
}

/** @brief [LockOnSelectorTest] 넘기기: 오른쪽 · 왼쪽으로 가장 가까운 각의 후보다. 그쪽에 없으면 그대로이고, 가려졌거나 범위 밖이면 건너뛴다 */
SW_TEST_CASE( LockOnSelectorTest, CycleMovesSideways )
{
    LockOnSelector                selector;
    const vector<LockOnCandidate> listCandidate = {
        makeCandidate( 1, -4.0f, 10.0f ),
        makeCandidate( 2, 0.0f, 10.0f ),
        makeCandidate( 3, 2.0f, 10.0f ),
        makeCandidate( 4, 5.0f, 10.0f ),
        makeCandidate( 5, 1.0f, 10.0f, 0.0f, SW_FALSE ),
    };
    SW_REQUIRE_EQUAL( uint64( 2 ), selector.pickBest( kEye, kForward, listCandidate ) );

    SW_EXPECT_EQUAL( uint64( 3 ), selector.cycle( kEye, kForward, listCandidate, 1 ) );
    SW_EXPECT_EQUAL( uint64( 4 ), selector.cycle( kEye, kForward, listCandidate, 1 ) );
    SW_EXPECT_EQUAL( uint64( 4 ), selector.cycle( kEye, kForward, listCandidate, 1 ) ); // 더 오른쪽이 없다
    SW_EXPECT_EQUAL( uint64( 3 ), selector.cycle( kEye, kForward, listCandidate, -1 ) );
    SW_EXPECT_EQUAL( uint64( 2 ), selector.cycle( kEye, kForward, listCandidate, -1 ) ); // 가려진 5 를 건너뛴다
    SW_EXPECT_EQUAL( uint64( 1 ), selector.cycle( kEye, kForward, listCandidate, -1 ) );
    SW_EXPECT_EQUAL( uint64( 1 ), selector.cycle( kEye, kForward, listCandidate, -1 ) );
}

/** @brief [LockOnSelectorTest] 유지: 잡은 뒤에는 더 먼 거리까지 유지하고, 넘거나 사라지면 풀린다 */
SW_TEST_CASE( LockOnSelectorTest, UpdateKeepsUntilBreakDistance )
{
    LockOnSelector selector;
    SW_EXPECT_FALSE( selector.update( kEye, {}, 0.1f ) ); // 대상이 없다

    SW_REQUIRE_EQUAL( uint64( 1 ), selector.pickBest( kEye, kForward, { makeCandidate( 1, 0.0f, 5.0f ) } ) );
    SW_EXPECT_TRUE( selector.update( kEye, { makeCandidate( 1, 0.0f, 30.0f ) }, 0.1f ) ); // 새로 잡는 거리 25 밖, 유지 거리 32 안
    SW_EXPECT_FALSE( selector.update( kEye, { makeCandidate( 1, 0.0f, 33.0f ) }, 0.1f ) );
    SW_EXPECT_FALSE( selector.hasTarget() );

    SW_REQUIRE_EQUAL( uint64( 1 ), selector.pickBest( kEye, kForward, { makeCandidate( 1, 0.0f, 5.0f ) } ) );
    SW_EXPECT_FALSE( selector.update( kEye, { makeCandidate( 2, 0.0f, 5.0f ) }, 0.1f ) ); // 사라졌다
    SW_EXPECT_FALSE( selector.hasTarget() );
}

/** @brief [LockOnSelectorTest] 가림: 유예 안에는 유지하고, 다시 보이면 처음부터 세며, 넘으면 풀린다 */
SW_TEST_CASE( LockOnSelectorTest, HiddenGrace )
{
    LockOnSelector                selector;
    const vector<LockOnCandidate> listHidden  = { makeCandidate( 1, 0.0f, 5.0f, 0.0f, SW_FALSE ) };
    const vector<LockOnCandidate> listVisible = { makeCandidate( 1, 0.0f, 5.0f ) };
    SW_REQUIRE_EQUAL( uint64( 1 ), selector.pickBest( kEye, kForward, listVisible ) );

    SW_EXPECT_TRUE( selector.update( kEye, listHidden, 0.9f ) );
    SW_EXPECT_TRUE( selector.update( kEye, listVisible, 0.1f ) );
    SW_EXPECT_TRUE( selector.update( kEye, listHidden, 0.9f ) ); // 다시 센다
    SW_EXPECT_FALSE( selector.update( kEye, listHidden, 0.2f ) );
    SW_EXPECT_FALSE( selector.hasTarget() );
}

/** @brief [LockOnSelectorTest] 저장: 표적과 숨은 시간이 되돌아온다. 깨진 값은 거절하고 그대로 둔다 */
SW_TEST_CASE( LockOnSelectorTest, StateRoundTrips )
{
    LockOnSelector selector;
    SW_REQUIRE_EQUAL( uint64( 1 ), selector.pickBest( kEye, kForward, { makeCandidate( 1, 0.0f, 5.0f ) } ) );
    SW_EXPECT_TRUE( selector.update( kEye, { makeCandidate( 1, 0.0f, 5.0f, 0.0f, SW_FALSE ) }, 0.9f ) );

    Archive archive;
    selector.writeState( archive );
    LockOnSelector restored;
    Archive        reading( archive.getData(), archive.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( uint64( 1 ), restored.getTarget() );
    SW_EXPECT_FALSE( restored.update( kEye, { makeCandidate( 1, 0.0f, 5.0f, 0.0f, SW_FALSE ) }, 0.2f ) ); // 숨은 시간을 이어 간다

    Archive broken;
    broken << uint64( 9 );
    broken << -1.0f;
    Archive brokenReading( broken.getData(), broken.getSize() );
    SW_EXPECT_FALSE( selector.readState( brokenReading ) );
    SW_EXPECT_EQUAL( uint64( 1 ), selector.getTarget() );
}
