/**
 * @file FixedStepAccumulator.h
 * @brief 가변 프레임 시간을 고정 스텝 수로 바꾸는 누적기와 보간 비(alpha)입니다(Glenn Fiedler "Fix Your Timestep").
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @class FixedStepAccumulator
     * @brief 프레임마다 흐른 시간을 쌓아 고정 스텝 몇 번을 돌릴지 알려 주고, 남은 시간으로 보간 비를 냅니다.
     * @details 시뮬레이션은 언제나 같은 스텝 크기로 돌아 같은 입력이면 같은 결과입니다(프레임 속도와 무관). 화면은 마지막 두 스텝 사이를
     *          `getAlpha()` 로 보간한 자세를 그립니다(유니티 Rigidbody interpolation). 한 프레임이 너무 길면(디버거 정지 · 로딩 히치)
     *          `_maxStepsPerFrame` 까지만 돌리고 나머지 시간은 버립니다 — 따라잡으려다 프레임이 더 길어지는 악순환을 막고, 대신 그 순간은 느리게 갑니다.
     */
    class SW_API FixedStepAccumulator
    {
    public:
        /** @brief 1/60 초 · 프레임당 4 스텝으로 둡니다. */
        FixedStepAccumulator();

        /** @brief 스텝 크기와 프레임당 상한을 바꿉니다. 쌓인 시간은 그대로입니다. 0 이하 값은 무시합니다. */
        void configure( float32 fixedTimeStep, uint32 maxStepsPerFrame );
        /**
         * @brief @p deltaTime 을 쌓고 이번 프레임에 돌릴 스텝 수를 돌려줍니다. 상한을 넘는 시간은 버립니다(`getDroppedTime`).
         * @details 음수 · NaN 은 0 으로 봅니다.
         */
        uint32 advance( float32 deltaTime );
        /** @brief 쌓인 시간을 비웁니다(씬을 새로 시작할 때 · 순간이동 뒤 보간을 끊을 때). */
        void reset();

        /** @brief 남은 시간 ÷ 스텝 크기(0..1) — 마지막 스텝과 다음 스텝 사이 어디를 그릴지입니다. */
        float32 getAlpha() const;
        /** @brief 스텝 크기(초)입니다. */
        float32 getFixedTimeStep() const { return _fixedTimeStep; }
        /** @brief 프레임당 스텝 상한입니다. */
        uint32 getMaxStepsPerFrame() const { return _maxStepsPerFrame; }
        /** @brief 상한 때문에 지금까지 버린 시간(초)입니다(진단용). */
        float64 getDroppedTime() const { return _droppedTime; }

    private:
        float64 _accumulated;
        float64 _droppedTime;
        float32 _fixedTimeStep;
        uint32  _maxStepsPerFrame;
    };
} // namespace sw
