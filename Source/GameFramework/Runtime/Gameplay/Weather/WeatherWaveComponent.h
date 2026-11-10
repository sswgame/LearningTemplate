/**
 * @file WeatherWaveComponent.h
 * @brief 날씨가 같은 오브젝트의 물(`WaterBodyComponent`) 파도 진폭을 정합니다 — 폭풍이면 파도가 커지고 그 위의 배가 크게 피칭 · 롤링합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class WeatherWaveComponent
     * @brief 틱마다 씬의 첫 열린 판 상태(`SessionStateComponent`)의 날씨 값 `_weatherValue`(기본 `wind`, 0 = 잔잔 .. 1 = 폭풍)를 읽어
     *        물의 진폭 배율을 1 → `_stormScale` 사이로 정합니다(`WaterBodyComponent::setWaveAmplitudeScale`).
     * @details Engine 의 물은 GameFramework 의 날씨를 읽을 수 없어(층 방향) 이 어댑터가 밀어 넣는다(`SplineRiverPathComponent` 와 같은 자리).
     *          화면 파도와 부력이 같은 배율을 본다. 날씨가 없는 씬(판 상태가 날씨를 열지 않았다)은 배율 1 — 적은 파도 그대로다.
     *          날씨의 전환(`WeatherSystem::computeValue` 의 섞기)이 그대로 진폭의 전환이 된다. 언리얼 Water 의 파도 에셋을 날씨가 바꾸는 자리다.
     */
    REFLECT( Category = "Environment", DisplayName = "Weather Waves", Tooltip = "Weather value scales the wave amplitude of the water body on this object (storms grow the waves)" )
    class SW_GF_API WeatherWaveComponent : public Component
    {
    public:
        REFLECT_BODY();

        WeatherWaveComponent();
        ~WeatherWaveComponent() override = default;

        void onTick( float32 deltaTime ) override;

        /** @brief 날씨 값 @p weatherValue(음수는 0)에서 진폭 배율입니다 — 0 이면 1, 1 이면 @p stormScale, 그 사이와 너머는 직선입니다. */
        static float32 computeAmplitudeScale( float32 weatherValue, float32 stormScale );

    private:
        PROPERTY( Required, Category = "Weather", DisplayName = "Weather Value", Tooltip = "Weather value (WeatherCatalog <Values>) read as storm strength, 0 calm .. 1 storm" )
        hashed_string _weatherValue;
        PROPERTY( Category = "Weather", DisplayName = "Storm Scale", Min = 0.0, Tooltip = "Wave amplitude scale at weather value 1 (calm is 1)" )
        float32 _stormScale;
    };
} // namespace sw
