#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Profiling/FrameProfileSession.h"

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

/**
 * @brief [FrameProfileSessionTest] 페이싱은 중앙 간격을 화면 주기로 보고 그 1.5 배 · 2.5 배를 넘은 프레임을 센다
 * @details 165 Hz VSync(주기 6.06 ms)에서 한 주기를 놓친 12 ms 프레임은 고정 문턱 over16ms 에 걸리지 않는다(VulkanVsyncFrameMiss).
 *          중앙 간격 기준이면 주기를 하나 놓친 프레임과 둘 놓친 프레임이 갈린다. 프레임이 없으면 모두 0 이다.
 */
SW_TEST_CASE( FrameProfileSessionTest, PacingCountsFramesMissingVsyncPeriods )
{
    using sw::FrameProfileSession;
    uint32 arrBin[FrameProfileSession::kPacingBinCount]{};

    const FrameProfileSession::FramePacing empty = FrameProfileSession::computePacing( arrBin, FrameProfileSession::kPacingBinCount );
    SW_EXPECT_EQUAL( empty._medianMicro, static_cast<int64>( 0 ) );
    SW_EXPECT_EQUAL( empty._overOneAndHalfMedian, 0u );
    SW_EXPECT_EQUAL( empty._overTwoAndHalfMedian, 0u );

    arrBin[60]                                    = 100; // 6.0 ~ 6.1 ms — 제 주기
    arrBin[121]                                   = 3;   // 12.1 ms — 주기 하나 놓침
    arrBin[182]                                   = 2;   // 18.2 ms — 주기 둘 놓침
    const FrameProfileSession::FramePacing pacing = FrameProfileSession::computePacing( arrBin, FrameProfileSession::kPacingBinCount );
    SW_EXPECT_EQUAL( pacing._medianMicro, static_cast<int64>( 6050 ) );
    SW_EXPECT_EQUAL( pacing._overOneAndHalfMedian, 5u );
    SW_EXPECT_EQUAL( pacing._overTwoAndHalfMedian, 2u );
}
