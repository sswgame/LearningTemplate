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
        /** @brief 태그를 저장하고 지금부터 잽니다. */
        explicit ScopedTimer( const utf8* pTag ) noexcept
            : _pTag{ pTag }
            , _stopwatch{}
        {
        }

        /** @brief 경과 시간을 갱신하고 밀리초 단위로 Info 로그를 남깁니다. */
        ~ScopedTimer() noexcept
        {
            (void)_pTag;
            SW_LOG_INFO_ID( "ScopedTimer.Ms", "'%#': %# ms", _pTag, getElapsedSeconds() * constant::time::kMillisecondsPerSecond );
        }

        /**
         * @brief 생성 이후 경과한 시간(초)입니다. 읽기만 한다 — 몇 번 불러도 스코프 시작부터의 시간이다.
         * @details 예전에는 프레임 타이머의 델타를 읽어(부를 때마다 기준을 옮겼다) 두 번 부르면 두 번째가 0 이었다. 이제 기준을 옮길 수 없다(const).
         */
        float64 getElapsedSeconds() const noexcept { return _stopwatch.getElapsedSeconds(); }
        /** @brief 복사를 금지합니다. */
        ScopedTimer( const ScopedTimer& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        ScopedTimer& operator=( const ScopedTimer& ) = delete;

    private:
        const utf8* _pTag;
        Stopwatch   _stopwatch;
    };
} // namespace sw
