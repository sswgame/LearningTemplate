/**
 * @file CpuClock.h
 * @brief 엔진의 단조 시계 하나입니다 — 지금 시각(`CpuClock`) · 경과 시간(`CpuStopwatch`) · 기한(`CpuDeadline`).
 * @details `CpuTimer`(프레임 델타)와 같은 OS 카운터(Windows QPC · Linux `CLOCK_MONOTONIC` · macOS `mach_absolute_time`)를 읽습니다.
 *          엔진 코드에서 `std::chrono::steady_clock::now()` 를 직접 읽지 말고 이것을 씁니다 — 시계가 하나여야 프로파일러 · 로그 ·
 *          기한이 같은 시각을 봅니다. 로거를 include 하지 않는 가벼운 헤더라 동시성 · 컨테이너 헤더에서도 씁니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct CpuClock
     * @brief 단조 시계의 지금 시각입니다. 기준점(0)은 정해져 있지 않으므로 두 시각의 차이로만 씁니다.
     */
    struct SW_API CpuClock
    {
        /** @brief OS 카운터의 현재 값입니다(단위는 `getCountsPerSecond`). */
        static int64 readCounter() noexcept;
        /** @brief 카운터가 1 초에 몇 번 오르는지입니다(프로세스에서 한 번 읽어 둡니다). */
        static int64 getCountsPerSecond() noexcept;

        /** @brief 지금 시각(나노초)입니다. */
        static int64 nowNanoseconds() noexcept;
        /**
         * @brief 카운터 값을 나노초로 바꿉니다. `counts * 1e9` 를 그대로 곱하면 며칠 가동 만에 int64 를 넘으므로 초와 나머지로 나눠 곱합니다.
         */
        static constexpr int64 countsToNanoseconds( int64 counts, int64 countsPerSecond ) noexcept
        {
            if ( countsPerSecond == constant::kNanosecondsPerSecond )
                return counts;
            return ( counts / countsPerSecond ) * constant::kNanosecondsPerSecond + ( counts % countsPerSecond ) * constant::kNanosecondsPerSecond / countsPerSecond;
        }
        /** @brief 지금 시각(마이크로초)입니다. */
        static int64 nowMicroseconds() noexcept { return nowNanoseconds() / 1000; }
    };

    /**
     * @class CpuStopwatch
     * @brief 만들 때(또는 `restart` 때)부터 지난 시간을 정수로 잽니다. 프레임 델타 · 일시정지가 필요하면 `CpuTimer` 를 씁니다.
     */
    class CpuStopwatch
    {
    public:
        /** @brief 지금부터 잽니다. */
        CpuStopwatch() noexcept
            : _startNanoseconds{ CpuClock::nowNanoseconds() }
        {
        }

        /** @brief 기준을 지금으로 옮깁니다. */
        void restart() noexcept { _startNanoseconds = CpuClock::nowNanoseconds(); }

        /** @brief 지난 시간(나노초)입니다. */
        int64 getElapsedNanoseconds() const noexcept { return CpuClock::nowNanoseconds() - _startNanoseconds; }
        /** @brief 지난 시간(마이크로초)입니다. */
        int64 getElapsedMicroseconds() const noexcept { return getElapsedNanoseconds() / 1000; }
        /** @brief 지난 시간(밀리초)입니다. */
        int64 getElapsedMilliseconds() const noexcept { return getElapsedNanoseconds() / 1000000; }

    private:
        int64 _startNanoseconds;
    };

    /**
     * @class CpuDeadline
     * @brief "이 시각까지" 기다리는 루프의 기한입니다. 기다림은 횟수가 아니라 시간으로 묶는다(느린 머신에서 횟수 상한은 정상을 실패로 만든다).
     */
    class CpuDeadline
    {
    public:
        /** @brief 지금부터 @p milliseconds 뒤가 기한입니다. */
        static CpuDeadline afterMilliseconds( int64 milliseconds ) noexcept { return CpuDeadline{ CpuClock::nowNanoseconds() + milliseconds * 1000000 }; }

        /** @brief 기한이 지났으면 true 입니다. */
        bool isExpired() const noexcept { return CpuClock::nowNanoseconds() >= _deadlineNanoseconds; }
        /** @brief 기한까지 남은 시간(밀리초, 지났으면 0)입니다. */
        int64 getRemainingMilliseconds() const noexcept
        {
            const int64 remaining = _deadlineNanoseconds - CpuClock::nowNanoseconds();
            return remaining > 0 ? remaining / 1000000 : 0;
        }

    private:
        explicit CpuDeadline( int64 deadlineNanoseconds ) noexcept
            : _deadlineNanoseconds{ deadlineNanoseconds }
        {
        }

        int64 _deadlineNanoseconds;
    };
} // namespace sw
