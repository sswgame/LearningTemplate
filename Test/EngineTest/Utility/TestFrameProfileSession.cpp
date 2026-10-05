#include "pch.h"

#include "Engine/Utility/Profiling/FrameProfileSession.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [FrameProfileSessionTest] 측정 창은 프레임 목표와 시간 목표 가운데 먼저 닿는 쪽이 닫는다
 * @details 장시간 실행(`Scripts/qa/Soak.py`)은 `-gv_profileSeconds` 로 끊는다. 시간 목표만 주면 프레임 목표 0 은 "기준 없음" 이어야 하고
 *          (0 프레임에서 바로 닫히면 soak 이 워밍업 직후 끝난다), 둘 다 주면 먼저 닿는 쪽이 닫는다.
 */
SW_TEST_CASE( FrameProfileSessionTest, MeasureWindowClosesOnTheFirstTargetReached )
{
    using sw::FrameProfileSession;
    constexpr int64 kSecond = 1000000;

    // 프레임 목표만
    SW_EXPECT_FALSE( FrameProfileSession::isMeasureWindowDone( 99, 100, 3600 * kSecond, 0 ) );
    SW_EXPECT_TRUE( FrameProfileSession::isMeasureWindowDone( 100, 100, 0, 0 ) );

    // 시간 목표만 — 프레임 목표 0 은 "기준 없음" 이다
    SW_EXPECT_FALSE( FrameProfileSession::isMeasureWindowDone( 1000000, 0, 9 * kSecond, 10 ) );
    SW_EXPECT_TRUE( FrameProfileSession::isMeasureWindowDone( 1, 0, 10 * kSecond, 10 ) );

    // 둘 다 — 먼저 닿는 쪽
    SW_EXPECT_TRUE( FrameProfileSession::isMeasureWindowDone( 100, 100, 1 * kSecond, 600 ) );
    SW_EXPECT_TRUE( FrameProfileSession::isMeasureWindowDone( 5, 100, 600 * kSecond, 600 ) );
    SW_EXPECT_FALSE( FrameProfileSession::isMeasureWindowDone( 5, 100, 1 * kSecond, 600 ) );

    // 둘 다 없으면 닫지 않는다(그때는 계측 자체가 꺼져 있다)
    SW_EXPECT_FALSE( FrameProfileSession::isMeasureWindowDone( 1000000, 0, 3600 * kSecond, 0 ) );
}
