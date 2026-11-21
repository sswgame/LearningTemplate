/**
 * @file GameTimer.h
 * @brief 고해상도 타이머로 프레임 델타를 측정합니다. 스코프 시간을 로그로 남기는 `ScopedTimer` 는 진단 층(`Core/Diagnostics/ScopedTimer.h`)이다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/Clock.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) GameTimer — resetTimer → startTimer → updateTimer(매 프레임) → stopTimer
    //    getDeltaTime 은 직전 updateTimer 와의 간격, getTotalTime 은 일시정지를 뺀 누적 시간
    // ------------------------------------------------------------------------------
    /**
     * @brief 단조 시계(`IClock`)로 누적 · 델타 시간을 측정합니다.
     * @details 시계는 받는다 — 운영은 실제 단조 시계(`SystemMonotonicClock`), 테스트는 손으로 미는 시계(`ManualClock`)를 줘 잠들지 않고 경과를 정한다
     *          (언리얼 자동화의 고정 시각 · 유니티 테스트의 시간 고정과 같은 갈래).
     */
    class SW_API GameTimer
    {
    public:
        /** @brief 실제 단조 시계로 재며 중지 상태로 시작합니다. */
        GameTimer() noexcept;
        /** @brief @p clock 으로 재며 중지 상태로 시작합니다. 시계는 빌려 쓰므로 이 객체보다 오래 살아야 합니다. */
        explicit GameTimer( const IClock& clock ) noexcept;

        /**
         * @brief 일시정지를 뺀 누적 시간(초)입니다.
         */
        float64 getTotalTime() const noexcept;
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
        const IClock* _pClock; ///< 시각(나노초)을 주는 시계. 빌려 쓴다
        float64       _deltaTime;

        int64 _baseTime; ///< 아래 시각은 모두 `_pClock` 의 나노초다
        int64 _pausedTime;
        int64 _stopTime;
        int64 _prevTime;
        int64 _currentTime;

        uint8 _bStopped : 1;
    };
} // namespace sw
