/**
 * @file NetSimPlayModel.h
 * @brief 한 프로세스 재생의 화면 없는 규칙입니다. 프레임 시간을 고정 스텝 수로 바꾸는 진행기와 월드 뷰포트 칸의 크기입니다(ImGui 없음).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

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

namespace sw::editor
{
    /**
     * @struct NetSimWorldLayout
     * @brief 월드 뷰포트를 가로로 나란히 놓을 때의 칸 크기입니다.
     */
    struct NetSimWorldLayout
    {
        /** @brief 한 칸의 크기입니다. 머리글 두 줄 몫을 뺀 높이이고, 좁아도 폭 1 · 높이 @p minHeight 아래로 내려가지 않습니다. 월드가 없으면 한 칸으로 봅니다. */
        static float2 computeViewSize( float2 available, int32 worldCount, float32 itemSpacingX, float32 lineHeight, float32 minHeight );
    };
} // namespace sw::editor
