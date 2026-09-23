/**
 * @file CpuTimer.h
 * @brief 고해상도 타이머로 프레임 델타와 스코프 프로파일링을 측정합니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Log/Logger.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) CpuTimer — resetTimer → startTimer → updateTimer(매 프레임) → stopTimer
    //    getDeltaTime 은 직전 updateTimer 와의 간격, getTotalTime 은 일시정지를 뺀 누적 시간
    // ------------------------------------------------------------------------------
    /** @brief QPC 같은 고해상도 카운터로 누적 · 델타 시간을 잽니다. */
    class SW_API CpuTimer
    {
    public:
        /** @brief 초당 카운트를 읽고 중지 상태로 시작합니다. */
        CpuTimer() noexcept;

        /**
         * @brief 일시정지를 뺀 누적 시간(초)입니다.
         */
        float32 getTotalTime() const noexcept;
        /**
         * @brief 직전 updateTimer 호출 이후의 시간(초)입니다.
         */
        float32 getDeltaTime() const noexcept;

        /**
         * @brief 기준 시각을 지금으로 맞추고 누적 · 델타를 0 으로 둡니다.
         */
        void resetTimer() noexcept;
        /**
         * @brief 중지 상태면 일시정지 구간을 빼고 다시 돌기 시작합니다.
         */
        void startTimer() noexcept;
        /**
         * @brief 갱신을 멈추고 일시정지 시작 시각을 기록합니다.
         */
        void stopTimer() noexcept;
        /**
         * @brief 현재 카운트를 읽어 델타를 갱신합니다. 중지 상태면 델타는 0 입니다.
         */
        void updateTimer() noexcept;

        /** @brief stopTimer 뒤 아직 startTimer 를 부르지 않았으면 true 입니다. */
        bool isStopped() const noexcept { return _bStopped; }

    private:
        float64 _secondsPerCount;
        float64 _deltaTime;

        int64 _baseTime;
        int64 _pausedTime;
        int64 _stopTime;
        int64 _prevTime;
        int64 _currentTime;

        bool _bStopped;
    };

    // ------------------------------------------------------------------------------
    // 2) ScopeCpuTimer — 생성할 때 시작하고, 소멸할 때 경과 시간(ms)을 로그로 남긴다
    // ------------------------------------------------------------------------------
    /** @brief 스코프 동안의 CPU 시간을 재고, 소멸할 때 로그로 남깁니다. */
    class SW_API ScopeCpuTimer final
    {
    public:
        /** @brief 태그를 저장하고 타이머를 리셋한 뒤 시작합니다. */
        explicit ScopeCpuTimer( const utf8* pTag ) noexcept
            : _pTag{ pTag }
        {
            _timer.resetTimer();
            _timer.startTimer();
        }

        /** @brief 경과 시간을 갱신하고 밀리초 단위로 Info 로그를 남깁니다. */
        ~ScopeCpuTimer() noexcept
        {
            (void)_pTag;
            SW_LOG_INFO( "'%#': %# ms", _pTag, getElapsedTimeInSeconds() * 1000.0f );
        }

        /**
         * @brief 생성 이후 경과한 시간(초)입니다. **부를 때마다 타이머를 한 번 갱신하므로** const 가 아닙니다.
         * @details `updateTimer()` 는 **직전 갱신 이후**의 델타를 재고 기준점을 지금으로 옮깁니다. 그래서 두 번 연달아 부르면
         *          두 번째는 그 사이의 시간(≈0)만 반환합니다. 예전 소멸자는 `updateTimer()` 를 부른 뒤 이 함수를 불러 또
         *          갱신했고, 그 결과 **스코프 길이와 상관없이 0 ms 를 기록했습니다.** 갱신은 한 번만 합니다.
         *
         *          그런 사연이 있는 함수인데도 `const` 로 선언하고 `const_cast` 로 타이머를 돌리고 있었습니다. "읽기만 한다" 고
         *          말하면서 상태를 바꾸면, 두 번 부르면 안 된다는 사실이 드러나지 않습니다.
         */
        float32 getElapsedTimeInSeconds() noexcept
        {
            _timer.updateTimer();
            return _timer.getDeltaTime();
        }

        /** @brief 복사를 금지합니다. */
        ScopeCpuTimer( const ScopeCpuTimer& ) = delete;
        /** @brief 복사 대입을 금지합니다. */
        ScopeCpuTimer& operator=( const ScopeCpuTimer& ) = delete;

    private:
        const utf8* _pTag;
        CpuTimer    _timer;
    };
} // namespace sw
