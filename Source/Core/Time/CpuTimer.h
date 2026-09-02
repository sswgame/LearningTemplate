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
    // 1) CpuTimer — reset → start → update(매 프레임) → stop
    //    getDeltaTime 은 직전 update 간격, getTotalTime 은 일시정지 제외 누적
    // ------------------------------------------------------------------------------
    /** @brief QPC 등으로 누적·델타 시간을 잽니다. */
    class SW_API CpuTimer
    {
    public:
        /** @brief 초당 카운트를 읽고 중지 상태로 둡니다. */
        CpuTimer() noexcept;

        /**
         * @brief 일시정지를 뺀 누적 초입니다.
         */
        float32 getTotalTime() const noexcept;
        /**
         * @brief 직전 updateTimer 호출 이후의 초입니다.
         */
        float32 getDeltaTime() const noexcept;

        /**
         * @brief 기준 시각을 지금으로 맞추고 누적·델타를 0으로 둡니다.
         */
        void resetTimer() noexcept;
        /**
         * @brief 중지 중이면 일시정지 구간을 빼고 다시 돕니다.
         */
        void startTimer() noexcept;
        /**
         * @brief 갱신을 멈추고 일시정지 시작 시각을 기록합니다.
         */
        void stopTimer() noexcept;
        /**
         * @brief 현재 카운트를 읽어 델타를 갱신합니다. 중지 중이면 델타는 0입니다.
         */
        void updateTimer() noexcept;

        /** @brief stopTimer 이후 startTimer 전이면 true입니다. */
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
    // 2) ScopeCpuTimer — 생성 시 start, 소멸 시 경과 ms 를 로그
    // ------------------------------------------------------------------------------
    /** @brief 스코프 동안의 CPU 시간을 재고 소멸 시 로그로 남깁니다. */
    class SW_API ScopeCpuTimer final
    {
    public:
        /** @brief 태그를 저장하고 타이머를 리셋·시작합니다. */
        explicit ScopeCpuTimer( const utf8* pTag ) noexcept
            : _pTag{ pTag }
        {
            _timer.resetTimer();
            _timer.startTimer();
        }

        /** @brief 경과를 갱신하고 밀리초를 Info 로그로 남깁니다. */
        ~ScopeCpuTimer() noexcept
        {
            _timer.updateTimer();
            (void)_pTag;
            SW_LOG_INFO( "'%#': %# ms", _pTag, getElapsedTimeInSeconds() * 1000.0f );
        }

        /** @brief 생성 이후 경과 초입니다. 호출 시 타이머를 한 번 갱신합니다. */
        float32 getElapsedTimeInSeconds() const noexcept
        {
            const_cast<CpuTimer&>( _timer ).updateTimer();
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

using CpuTimer      = sw::CpuTimer;
using ScopeCpuTimer = sw::ScopeCpuTimer;
