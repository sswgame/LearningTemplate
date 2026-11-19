/**
 * @file NetSimStepPacer.h
 * @brief 한 프로세스 재생이 프레임 시간을 고정 스텝 수로 바꾸는 진행기입니다(ImGui 없음).
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @class NetSimStepPacer
     * @brief 프레임 시간을 모아 돌릴 고정 스텝 수를 정합니다. 한 프레임의 상한을 넘겨 밀린 시간은 버립니다(중단점 뒤 몰아 돌지 않게).
     */
    class NetSimStepPacer
    {
    public:
        NetSimStepPacer();

        /**
         * @brief @p frameSeconds 를 모으고 이번 프레임에 돌릴 스텝 수를 돌려줍니다. 상한 안에서 남는 몫은 다음 프레임으로 넘깁니다.
         * @param tickInterval 양수입니다(`NetSimHarness::initialize` 가 거른다).
         */
        uint32 consume( float64 frameSeconds, float64 tickInterval, uint32 maxSteps );
        void   reset() { _accumulatedSeconds = 0.0; }

        float64 getAccumulatedSeconds() const { return _accumulatedSeconds; }

    private:
        float64 _accumulatedSeconds;
    };
} // namespace sw::editor
