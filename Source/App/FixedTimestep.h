/**
 * @file FixedTimestep.h
 * @brief 프레임 시간 정책입니다. 가변 델타의 상한과 고정 스텝 분할을 한 곳에서 정합니다.
 *
 * @details 루프 본문에 흩어져 있던 시간 값(최대 델타 · 고정 스텝 길이 · 누산기)을 한 타입으로 모읍니다. 상용 엔진이 이 값들을
 *          설정으로 노출하는 이유는 두 가지입니다. 프로젝트마다 시뮬레이션 주기가 다르고, 고정 스텝에는 **프레임당 상한**이
 *          반드시 필요하기 때문입니다. 상한이 없으면 느린 프레임이 더 많은 스텝을 부르고, 그래서 더 느려지는 악순환(고정 스텝
 *          스파이럴)에 빠져 영원히 따라잡지 못합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Time/GameTimer.h"

namespace sw
{
    /** @brief 한 프레임의 시간 분해 결과입니다. */
    struct FrameTime
    {
        /** @brief 최대 델타로 자른 이번 프레임의 가변 델타(초)입니다. */
        float32 _deltaTime;
        /** @brief 고정 스텝 하나의 길이(초)입니다. */
        float32 _fixedDeltaTime;
        /** @brief 최대 델타로 자른 이번 프레임의 실제 경과(초) — 시간 배율 · 정지를 곱하지 않은 값입니다(UI · 화면 전환). */
        float32 _unscaledDeltaTime;
        /** @brief 이번 프레임에 돌려야 하는 고정 스텝 수입니다. 상한에서 잘립니다. */
        uint32 _fixedStepCount;

        FrameTime()
            : _deltaTime{ 0.0f }
            , _fixedDeltaTime{ 0.0f }
            , _unscaledDeltaTime{ 0.0f }
            , _fixedStepCount{ 0 }
        {
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @class FixedTimestep
     * @brief 실시간 경과를 가변 델타와 고정 스텝 수로 나눕니다.
     */
    class FixedTimestep
    {
    public:
        FixedTimestep();

        /** @brief 시간 정책을 설정합니다. 0 이하의 값은 기본값으로 되돌립니다. */
        void configure( float32 maxFrameDeltaTime, float32 fixedDeltaTime, uint32 maxFixedStepPerFrame );
        /** @brief 타이머와 누산기를 0 에서 다시 시작합니다. 루프 진입 직전에 한 번 부릅니다. */
        void start();

        /**
         * @brief 한 프레임을 진행하고 그 시간 분해 결과를 반환합니다.
         * @param timeScale            게임 시간 배율(`GameTimeScale`). 최대 델타로 자른 **뒤에** 곱하므로 빨리 감기는 한 프레임에 상한보다 긴
         *                             시간을 흘리고, 고정 스텝 수도 그만큼 늘어납니다(스텝 상한은 그대로). 0 이면 시간이 멈춥니다.
         * @param overrideFrameSeconds 0 보다 크면 벽시계 대신 이 시간을 이번 프레임의 경과로 씁니다(`-gv_fixedFrameDelta` — 결정적 실행).
         */
        FrameTime advance( float32 timeScale = 1.0f, float32 overrideFrameSeconds = 0.0f );

    private:
        GameTimer _timer;
        /** @brief 아직 고정 스텝으로 쓰지 못한 남은 시간(초)입니다. */
        float32 _accumulator;
        float32 _maxFrameDeltaTime;
        float32 _fixedDeltaTime;
        uint32  _maxFixedStepPerFrame;
    };
} // namespace sw
