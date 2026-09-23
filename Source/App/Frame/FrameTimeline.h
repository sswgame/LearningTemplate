/**
 * @file FrameTimeline.h
 * @brief 프레임 시간 정책입니다. 가변 델타의 상한과 고정 스텝 분할을 한 곳에서 정합니다.
 *
 * @details 루프 본문에 흩어져 있던 시간 값(최대 델타 · 고정 스텝 길이 · 누산기)을 한 타입으로 모읍니다. 상용 엔진이 이 값들을
 *          설정으로 노출하는 이유는 두 가지입니다. 프로젝트마다 시뮬레이션 주기가 다르고, 고정 스텝에는 **프레임당 상한**이
 *          반드시 필요하기 때문입니다. 상한이 없으면 느린 프레임이 더 많은 스텝을 부르고, 그래서 더 느려지는 악순환(고정 스텝
 *          스파이럴)에 빠져 영원히 따라잡지 못합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Time/CpuTimer.h"

namespace sw
{
    /** @brief 한 프레임의 시간 분해 결과입니다. */
    struct FrameTime
    {
        /** @brief 최대 델타로 자른 이번 프레임의 가변 델타(초)입니다. */
        float32 _deltaTime;
        /** @brief 고정 스텝 하나의 길이(초)입니다. */
        float32 _fixedDeltaTime;
        /** @brief 이번 프레임에 돌려야 하는 고정 스텝 수입니다. 상한에서 잘립니다. */
        uint32 _fixedStepCount;

        FrameTime()
            : _deltaTime{ 0.0f }
            , _fixedDeltaTime{ 0.0f }
            , _fixedStepCount{ 0 }
        {
        }
    };

    /**
     * @class FrameTimeline
     * @brief 실시간 경과를 가변 델타와 고정 스텝 수로 나눕니다.
     */
    class FrameTimeline
    {
    public:
        /** @brief 설정이 비었을 때 쓰는 시뮬레이션 주기(60Hz)입니다. */
        static constexpr float32 kDefaultFixedDeltaTime = 1.0f / 60.0f;
        /** @brief 설정이 비었을 때 쓰는 최대 가변 델타(초)입니다. */
        static constexpr float32 kDefaultMaxFrameDeltaTime = 0.1f;
        /** @brief 설정이 비었을 때 쓰는 프레임당 고정 스텝 상한입니다. 두 기본값의 몫과 같습니다. */
        static constexpr uint32 kDefaultMaxFixedStepPerFrame = 6;

        FrameTimeline();

        /** @brief 시간 정책을 설정합니다. 0 이하의 값은 기본값으로 되돌립니다. */
        void configure( float32 maxFrameDeltaTime, float32 fixedDeltaTime, uint32 maxFixedStepPerFrame );
        /** @brief 타이머와 누산기를 0 에서 다시 시작합니다. 루프 진입 직전에 한 번 부릅니다. */
        void start();

        /** @brief 한 프레임을 진행하고 그 시간 분해 결과를 반환합니다. */
        FrameTime advance();

    private:
        CpuTimer _timer;
        /** @brief 아직 고정 스텝으로 쓰지 못한 남은 시간(초)입니다. */
        float32 _accumulator;
        float32 _maxFrameDeltaTime;
        float32 _fixedDeltaTime;
        uint32  _maxFixedStepPerFrame;
    };
} // namespace sw
