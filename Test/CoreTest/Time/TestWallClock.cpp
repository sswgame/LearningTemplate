#include "pch.h"

#include "Core/Time/WallClock.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [WallClockTest] UTC 벽시계는 유닉스 밀리초다
 * @details 서버의 만료 · 기록 시각이 이 값이다 — 초나 100 ns 틱을 돌려주면 시한이 1000 배 · 10000 배 어긋난다. 2020-01-01 과 2200-01-01 사이면 단위가 밀리초다.
 */
SW_TEST_CASE( WallClockTest, ReturnsUnixMilliseconds )
{
    constexpr int64 kYear2020Ms = 1577836800000;
    constexpr int64 kYear2200Ms = 7258118400000;
    const int64     nowMs       = sw::WallClock::nowUnixMilliseconds();
    SW_EXPECT_TRUE( kYear2020Ms < nowMs && nowMs < kYear2200Ms );
}
