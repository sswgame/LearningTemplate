/**
 * @file HapticPlayback.h
 * @brief 장치 하나가 지금 재생하는 진동 효과(`HapticEffectDef`)의 시간 진행입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class IInputFeedbackDevice;
    struct HapticEffectDef;
} // namespace sw

namespace sw
{
    /**
     * @class HapticPlayback
     * @brief 진동 효과 하나를 처음부터 끝까지 출력(`IInputFeedbackDevice`)으로 내보냅니다. 장치가 들고 프레임마다 진행합니다.
     * @details 정해진 시간의 고정 진동(`GamepadDevice::playVibration`)도 고정 효과(`HapticEffectDef::makeConstant`)로 같은 길을 지난다 — 시간 진행이 한 곳이다.
     *          shortcut: 장치마다 효과 하나만 재생한다(새 재생이 앞 것을 바꾼다), 겹쳐 내야 하면 효과 목록과 채널마다 최대값 합성(언리얼 `ActiveForceFeedbackEffects`)으로 늘린다.
     */
    class SW_API HapticPlayback
    {
    public:
        HapticPlayback();

        /** @brief @p pEffect 를 @p scale 배로 처음부터 재생하고 0 초 값을 @p output 에 바로 냅니다. 효과가 없으면 오류를 남기고 false, 출력이 거절하면 false 입니다. */
        [[nodiscard]] bool start( shared_ptr<const HapticEffectDef> pEffect, float32 scale, IInputFeedbackDevice& output );
        /** @brief 시간을 @p deltaSeconds 만큼 진행하고 그 값을 냅니다. 고리가 아닌 효과가 끝나면 @p output 을 멈춥니다(`stopVibration`). */
        void advance( float32 deltaSeconds, IInputFeedbackDevice& output );
        /** @brief 재생을 잊습니다. 출력은 건드리지 않습니다(장치의 `stopVibration` 이 부른다). */
        void clear();
        bool isPlaying() const { return _pEffect != nullptr; }

    private:
        shared_ptr<const HapticEffectDef> _pEffect;
        float32                           _time;
        float32                           _scale;
        float2                            _sentValue; ///< 마지막으로 출력에 낸 값(같으면 다시 내지 않는다)
    };
} // namespace sw
