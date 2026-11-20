// PlaybackSegmentTest — 재생 커서 구간의 문턱 규칙(정방향 (from, to], 역방향 [to, from), 빈 구간은 아무것도 지나지 않음)과 꺾이는 끝의 한 번(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/PlaybackSegment.h"

using namespace sw;

/** @brief [PlaybackSegmentTest] 정방향은 from 을 빼고 to 를 넣고, 역방향은 to 를 넣고 from 을 뺀다 — 꺾인 끝을 공유하는 두 구간이 그 문턱을 한 번만 지난다 */
SW_TEST_CASE( PlaybackSegmentTest, EachThresholdIsCrossedOnce )
{
    const PlaybackSegment<int32> forward{ 5, 10 };
    SW_EXPECT_TRUE( forward.isForward() );
    SW_EXPECT_FALSE( forward.hasCrossed( 5 ) );
    SW_EXPECT_TRUE( forward.hasCrossed( 6 ) );
    SW_EXPECT_TRUE( forward.hasCrossed( 10 ) );
    SW_EXPECT_FALSE( forward.hasCrossed( 11 ) );

    const PlaybackSegment<int32> turnBack{ 10, 7 }; // 왕복이 10 에서 꺾여 돌아왔다
    SW_EXPECT_FALSE( turnBack.isForward() );
    SW_EXPECT_FALSE( turnBack.hasCrossed( 10 ) ); // 꺾인 끝은 앞 구간이 이미 지났다
    SW_EXPECT_TRUE( turnBack.hasCrossed( 7 ) );
    SW_EXPECT_TRUE( turnBack.hasCrossed( 9 ) );
    SW_EXPECT_FALSE( turnBack.hasCrossed( 6 ) );

    const PlaybackSegment<int32> still{ 4, 4 };
    SW_EXPECT_FALSE( still.isForward() );
    SW_EXPECT_FALSE( still.hasCrossed( 4 ) ); // 멈춘 갱신은 아무것도 지나지 않는다
}

/** @brief [PlaybackSegmentTest] 초 구간도 같은 규칙이다 — 이어진 걸음들이 문턱 0.5 를 정확히 한 번 지난다(누적한 시각이 문턱 바로 위 · 아래여도) */
SW_TEST_CASE( PlaybackSegmentTest, ContiguousSecondStepsCrossOnce )
{
    float32 time       = 0.0f;
    uint32  crossCount = 0;
    for ( int32 stepIndex = 0; stepIndex < 120; ++stepIndex )
    {
        const float32                  next = time + 1.0f / 60.0f;
        const PlaybackSegment<float32> step{ time, next };
        crossCount += step.hasCrossed( 0.5f ) ? 1u : 0u;
        time = next;
    }
    SW_EXPECT_EQUAL( 1u, crossCount );
}
