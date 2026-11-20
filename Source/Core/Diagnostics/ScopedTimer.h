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
#include "Core/Time/GameTimer.h"

namespace sw
{
    /** @brief 스코프 동안의 CPU 시간을 재고, 소멸할 때 로그로 남깁니다. */
    class SW_API ScopedTimer final
    {
    public:
        /** @brief 태그를 저장하고 타이머를 리셋한 뒤 시작합니다. */
        explicit ScopedTimer( const utf8* pTag ) noexcept
            : _pTag{ pTag }
        {
            _timer.resetTimer();
            _timer.startTimer();
        }

        /** @brief 경과 시간을 갱신하고 밀리초 단위로 Info 로그를 남깁니다. */
        ~ScopedTimer() noexcept
        {
            (void)_pTag;
            SW_LOG_INFO_ID( "GameTimer.Ms", "'%#': %# ms", _pTag, getElapsedTimeInSeconds() * constant::time::kMillisecondsPerSecond );
        }

        /**
         * @brief 생성 이후 경과한 시간(초)입니다. **부를 때마다 타이머를 한 번 갱신하므로** const 가 아닙니다.
         * @details `updateTimer()` 는 **직전 갱신 이후**의 델타를 재고 기준점을 지금으로 옮깁니다. 그래서 두 번 연달아 부르면
         *          두 번째는 그 사이의 시간(≈0)만 반환합니다. `updateTimer()` 뒤에 이 함수를 또 부르면 **스코프 길이와 상관없이 0 ms 를
         *          기록합니다.** 갱신은 한 번만 합니다.
         *
         *          그래서 `const` 로 선언하지 않습니다. "읽기만 한다" 고 말하면서 상태를 바꾸면, 두 번 부르면 안 된다는 사실이
         *          드러나지 않습니다.
         */
        float32 getElapsedTimeInSeconds() noexcept
        {
            _timer.updateTimer();
            return _timer.getDeltaTime();
        }

        /** @brief 복사를 금지합니다. */
        ScopedTimer( const ScopedTimer& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        ScopedTimer& operator=( const ScopedTimer& ) = delete;

    private:
        const utf8* _pTag;
        GameTimer   _timer;
    };
} // namespace sw
