/**
 * @file HapticPlayback.h
 * @brief 장치 하나가 지금 재생하는 진동 효과(`HapticEffectDef`)들의 시간 진행과 합성입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"

namespace sw
{
    class IInputFeedbackDevice;
    struct HapticEffectDef;
} // namespace sw

namespace sw
{
    /**
     * @class HapticPlayback
     * @brief 진동 효과들을 처음부터 끝까지 출력(`IInputFeedbackDevice`)으로 내보냅니다. 장치가 들고 프레임마다 진행합니다.
     * @details 정해진 시간의 고정 진동(`GamepadDevice::playVibration`)도 고정 효과(`HapticEffectDef::makeConstant`)로 같은 길을 지난다 — 시간 진행이 한 곳이다.
     *          여러 효과는 겹쳐 재생하고 모터(채널)마다 최대값으로 합성한다(언리얼 `ActiveForceFeedbackEffects` 와 같은 규칙) — 피격 진동이 엔진 진동을 끊지 않는다.
     *          동시 재생은 `kMaxActiveEffects` 개까지이고 넘으면 가장 오래된 것을 내린다(고리 효과를 되풀이해 걸어도 목록이 자라지 않는다).
     */
    class SW_API HapticPlayback
    {
    public:
        HapticPlayback();

        /** @brief 동시에 재생하는 효과 수의 상한입니다. */
        static constexpr uint32 kMaxActiveEffects = 8;

        /**
         * @brief @p pEffect 를 @p scale 배로 처음부터 재생에 더하고 합성한 0 초 값을 @p output 에 바로 냅니다.
         * @return 효과가 없으면 오류를 남기고 false, 출력이 거절하면 더하지 않고 false 입니다(재생 중인 다른 효과는 그대로다).
         */
        [[nodiscard]] bool start( shared_ptr<const HapticEffectDef> pEffect, float32 scale, IInputFeedbackDevice& output );
        /** @brief 시간을 @p deltaSeconds 만큼 진행하고 합성한 값을 냅니다. 고리가 아닌 효과는 끝나면 내리고, 남은 효과가 없으면 @p output 을 멈춥니다(`stopVibration`). */
        void advance( float32 deltaSeconds, IInputFeedbackDevice& output );
        /** @brief 모든 재생을 잊습니다. 출력은 건드리지 않습니다(장치의 `stopVibration` 이 부른다). */
        void   clear();
        bool   isPlaying() const { return _listActive.empty() == false; }
        uint32 getActiveCount() const { return static_cast<uint32>( _listActive.size() ); }

    private:
        /** @brief 재생 중인 효과 하나입니다. */
        struct ActiveEffect
        {
            SharedAssetRef<HapticEffectDef> _pEffect;
            float32                         _time{ 0.0f };
            float32                         _scale{ 1.0f };
        };

        /** @brief 재생 중인 효과들의 지금 값을 모터마다 최대값으로 합칩니다. */
        float2 combine() const;

    private:
        vector<ActiveEffect> _listActive;
        float2               _sentValue; ///< 마지막으로 출력에 낸 값(같으면 다시 내지 않는다)
    };
} // namespace sw
