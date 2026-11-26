/**
 * @file MonotonicClock.h
 * @brief 엔진의 단조 시계 하나입니다 — 지금 시각(`MonotonicClock`) · 경과 시간(`Stopwatch`) · 기한(`Deadline`).
 * @details OS 카운터(Windows QPC · Linux `CLOCK_MONOTONIC`)를 읽습니다(언리얼 `FPlatformTime::Seconds` · 유니티 `Time.realtimeSinceStartupAsDouble` 의 자리).
 *          엔진 코드에서 `std::chrono::steady_clock::now()` 를 직접 읽지 말고 이것을 씁니다 — 시계가 하나여야 프로파일러 · 로그 ·
 *          기한이 같은 시각을 봅니다. 로거를 include 하지 않는 가벼운 헤더라 동시성 · 컨테이너 헤더에서도 씁니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct MonotonicClock
     * @brief 단조 시계의 지금 시각입니다. 기준점(0)은 정해져 있지 않으므로 두 시각의 차이로만 씁니다.
     */
    struct SW_API MonotonicClock
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
            if ( countsPerSecond == constant::time::kNanosecondsPerSecond )
                return counts;
            return ( counts / countsPerSecond ) * constant::time::kNanosecondsPerSecond + ( counts % countsPerSecond ) * constant::time::kNanosecondsPerSecond / countsPerSecond;
        }
        /** @brief 지금 시각(마이크로초)입니다. */
        static int64 nowMicroseconds() noexcept { return nowNanoseconds() / constant::time::kNanosecondsPerMicrosecond; }
        /** @brief 지금 시각(초, 실수)입니다. 초 단위 시한 · 경과를 실수로 다루는 쪽(자동화 벽시계, 네트워크 갱신)이 변환식을 호출부마다 두지 않는다. */
        static float64 nowSeconds() noexcept { return static_cast<float64>( nowNanoseconds() ) / static_cast<float64>( constant::time::kNanosecondsPerSecond ); }
        /** @brief 설정 · 시한의 초(@p seconds)를 이 시계의 나노초 폭으로 바꿉니다(소수 아래는 버린다). 변환식을 호출부마다 두지 않는다. */
        static constexpr int64 secondsToNanoseconds( float64 seconds ) noexcept { return static_cast<int64>( seconds * static_cast<float64>( constant::time::kNanosecondsPerSecond ) ); }
        /**
         * @brief `nowNanoseconds()` 기준 @p deadlineNanoseconds 까지 이 스레드를 재웁니다(이미 지났으면 바로 돌아온다). 고정 틱 루프의 잠자기입니다.
         * @details Windows 는 고해상도 대기 타이머(`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` — 없으면 `Sleep` 밀리초)로 1 ms 해상도 한계를 피하고,
         *          리눅스는 `clock_nanosleep` 상대 대기입니다. 일찍 깨거나 신호에 깨면 남은 시간을 다시 측정해서 마감까지 다시 잡니다.
         */
        static void sleepUntilNanoseconds( int64 deadlineNanoseconds ) noexcept;
    };
} // namespace sw

namespace sw
{
    /**
     * @class Stopwatch
     * @brief 만들 때(또는 `restart` · `lapSeconds` 때)부터 지난 시간을 잽니다. 구간 하나 · 구간마다의 길이(랩)를 잰다 — 프레임 시간 정책은 `FrameClock` 이다.
     */
    class Stopwatch
    {
    public:
        /** @brief 지금부터 측정합니다. */
        Stopwatch() noexcept
            : _startNanoseconds{ MonotonicClock::nowNanoseconds() }
        {
        }

        /** @brief 기준을 지금으로 옮깁니다. */
        void restart() noexcept { _startNanoseconds = MonotonicClock::nowNanoseconds(); }

        /** @brief 지난 시간(나노초)입니다. */
        int64 getElapsedNanoseconds() const noexcept { return MonotonicClock::nowNanoseconds() - _startNanoseconds; }
        /** @brief 지난 시간(마이크로초)입니다. */
        int64 getElapsedMicroseconds() const noexcept { return getElapsedNanoseconds() / constant::time::kNanosecondsPerMicrosecond; }
        /** @brief 지난 시간(밀리초)입니다. */
        int64 getElapsedMilliseconds() const noexcept { return getElapsedNanoseconds() / constant::time::kNanosecondsPerMillisecond; }
        /** @brief 지난 시간(초)입니다. 읽기만 한다 — 몇 번 불러도 기준은 그대로다. */
        float64 getElapsedSeconds() const noexcept { return static_cast<float64>( getElapsedNanoseconds() ) / static_cast<float64>( constant::time::kNanosecondsPerSecond ); }
        /** @brief 지난 시간(초)을 돌려주고 기준을 지금으로 옮깁니다 — 단계마다 · 프레임마다의 길이(랩)를 잰다. 음수는 없다. */
        float64 lapSeconds() noexcept
        {
            const int64 now     = MonotonicClock::nowNanoseconds();
            const int64 elapsed = now - _startNanoseconds;
            _startNanoseconds   = now;
            return elapsed > 0 ? static_cast<float64>( elapsed ) / static_cast<float64>( constant::time::kNanosecondsPerSecond ) : 0.0;
        }

    private:
        int64 _startNanoseconds;
    };
} // namespace sw

namespace sw
{
    /**
     * @class Deadline
     * @brief "이 시각까지" 기다리는 루프의 기한입니다. 기다림은 횟수가 아니라 시간으로 묶는다(느린 머신에서 횟수 상한은 정상을 실패로 만든다).
     */
    class Deadline
    {
    public:
        /** @brief 지금부터 @p milliseconds 뒤가 기한입니다. */
        static Deadline afterMilliseconds( int64 milliseconds ) noexcept { return Deadline{ MonotonicClock::nowNanoseconds() + milliseconds * constant::time::kNanosecondsPerMillisecond }; }
        /** @brief 지금부터 @p seconds 초 뒤입니다. */
        static Deadline afterSeconds( int64 seconds ) noexcept { return Deadline{ MonotonicClock::nowNanoseconds() + seconds * constant::time::kNanosecondsPerSecond }; }

        /** @brief 기한이 지났으면 true 입니다. */
        bool isExpired() const noexcept { return MonotonicClock::nowNanoseconds() >= _deadlineNanoseconds; }
        /** @brief 기한까지 남은 시간(밀리초, 지났으면 0)입니다. */
        int64 getRemainingMilliseconds() const noexcept
        {
            const int64 remaining = _deadlineNanoseconds - MonotonicClock::nowNanoseconds();
            return remaining > 0 ? remaining / constant::time::kNanosecondsPerMillisecond : 0;
        }

    private:
        explicit Deadline( int64 deadlineNanoseconds ) noexcept
            : _deadlineNanoseconds{ deadlineNanoseconds }
        {
        }

    private:
        int64 _deadlineNanoseconds;
    };
} // namespace sw
