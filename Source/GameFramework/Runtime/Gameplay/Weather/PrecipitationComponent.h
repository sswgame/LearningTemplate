/**
 * @file PrecipitationComponent.h
 * @brief 비 · 눈 — 날씨 값만큼 같은 오브젝트의 효과(`VFXComponent`)를 틀고, 주 시점 위를 따라다닙니다.
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
     * @class PrecipitationComponent
     * @brief 틱마다 씬의 첫 열린 판 상태(`GameStateComponent`)의 날씨 값 `_weatherValue` 를 읽어, 0 보다 크면 같은 오브젝트의 효과를 주 시점 `_height` 위에서 틀고 0 이면 멈춥니다.
     * @details 세기는 효과의 사용자 파라미터 `_intensityParameter` 의 알파로 건다(1, 1, 1, 값). 입자는 월드 공간에 남고(`VFXSimulationSpace::World`) 물리 바디에 부딪혀 사라지므로
     *          (`VFXCollisionMode::Kill`) 지붕 아래에는 비가 들지 않는다 — 실내 차폐를 따로 굽지 않는다(언리얼 Niagara 의 충돌 강수, 유니티 VFX Graph 의 깊이 충돌 자리).
     *          물리 광선이라 화면 밖 지붕도 막는다.
     *          지붕이 `_height` 보다 높은 실내는 효과가 태어날 때 하늘을 보아 막는다(`VFXCollisionDef::_openSkyDistance`).
     *          방울이 많은 강수는 비 가림 맵을 읽는 GPU 효과(`VFXCollisionMethod::RainOcclusion` — engine/vfx/precipitation_rain_heavy)를 쓴다.
     */
    REFLECT( Category = "Environment", DisplayName = "Precipitation", Tooltip = "Plays the effect on this object above the main view while a weather value is above 0 (rain, snow)" )
    class SW_GF_API PrecipitationComponent : public Component
    {
    public:
        REFLECT_BODY();

        PrecipitationComponent();
        ~PrecipitationComponent() override = default;

        void onTick( TickContext& context ) override;

    private:
        PROPERTY( Required, Category = "Weather", DisplayName = "Weather Value", Tooltip = "Weather value (WeatherCatalog <Values>) read as intensity, 0 none .. 1 full" )
        hashed_string _weatherValue;
        PROPERTY( Required, Category = "Weather", DisplayName = "Intensity Parameter", Meta = "NotLocalizable", Tooltip = "User parameter of the effect that receives (1, 1, 1, intensity)" )
        hashed_string _intensityParameter;
        PROPERTY( Category = "Weather", DisplayName = "Height", Min = 0.0, Units = m, Tooltip = "Height above the main view where the effect sits" )
        float32 _height;

        float32 _writtenIntensity; ///< 지난번에 건 세기 — 바뀔 때만 건다
    };
} // namespace sw
