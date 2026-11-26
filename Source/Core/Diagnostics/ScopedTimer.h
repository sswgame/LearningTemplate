/**
 * @file ScopedTimer.h
 * @brief 스코프 동안의 시간을 재고 소멸할 때 로그 한 줄로 남기는 진단 도구입니다.
 * @details 로그를 쓰므로 진단 층에 둔다 — 시계 층(Time)에 두면 로그 한 줄 때문에 Time 이 Log 위에 서고, 그 아래 층(Subsystem · Concurrency)이
 *          시계를 함수 포인터 · `std::chrono` 로 우회했다. 프레임 루프에는 넣지 않는다(스코프마다 한 줄, `FrameProfiler` 참고).
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

namespace sw
{
    /** @brief 스코프 동안의 CPU 시간을 재고, 소멸할 때 로그로 남깁니다. */
    class SW_API ScopedTimer final
    {
    public:
        /** @brief 태그를 저장하고 지금을 시작으로 합니다. */
        explicit ScopedTimer( const utf8* pTag ) noexcept
            : _pTag{ pTag }
            , _startMicro{ MonotonicClock::nowMicroseconds() }
        {
        }

        /** @brief 경과 시간을 밀리초 단위로 Info 로그에 남깁니다. */
        ~ScopedTimer() noexcept
        {
            (void)_pTag;
            SW_LOG_INFO_ID( "GameTimer.Ms", "'%#': %# ms", _pTag, getElapsedTimeInSeconds() * constant::time::kMillisecondsPerSecond );
        }

        /**
         * @brief 생성 이후 경과한 시간(초)입니다. 몇 번을 불러도 같은 시작에서 잰다.
         * @details 예전에는 게임 타이머(`GameTimer::updateTimer`)로 재 부를 때마다 기준점이 옮겨, 두 번째 호출 · 소멸자의 로그가 0 ms 였다. 단조 시계의 시작 시각 하나로 잰다.
         */
        [[nodiscard]] float32 getElapsedTimeInSeconds() const noexcept
        {
            return static_cast<float32>( MonotonicClock::nowMicroseconds() - _startMicro ) / static_cast<float32>( constant::time::kMicrosecondsPerSecond );
        }

        /** @brief 복사를 금지합니다. */
        ScopedTimer( const ScopedTimer& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        ScopedTimer& operator=( const ScopedTimer& ) = delete;

    private:
        const utf8* _pTag;
        int64       _startMicro;
    };
} // namespace sw
