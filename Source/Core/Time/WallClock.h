/**
 * @file WallClock.h
 * @brief UTC 벽시계 — 유닉스 시각(1970-01-01 00:00 UTC 부터) 밀리초입니다. 서버의 기간 · 만료 · 기록 시각처럼 기준점(epoch)이 있어야 하는 값에 씁니다.
 * @details 시스템 시계는 거꾸로 갈 수 있다(NTP 보정) — 경과 시간 · 시한은 `MonotonicClock` 으로 잰다. 서비스 코드는 이 값을 직접 읽지 않고
 *          `nowMs` 매개변수로 받는다(시험이 가짜 시각을 넣는다). `std::chrono::system_clock` 을 읽는 곳은 이 파일 하나다(`CheckClockReads`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct WallClock
     * @brief UTC 벽시계입니다.
     */
    struct SW_API WallClock
    {
        /** @brief 지금 UTC 유닉스 시각(밀리초)입니다. */
        static int64 nowUnixMilliseconds() noexcept;
    };
} // namespace sw
