/**
 * @file DaylightComponent.h
 * @brief 해 — 디렉터가 답하는 하루의 시각(`GameModeComponent::readHourOfDay`)으로 같은 오브젝트의 방향광 높이 · 방향 · 세기를 맞춥니다.
 * @details 시각만 디렉터에게 묻고 곡선은 씬 데이터라 어느 게임이든 쓴다(언리얼 SkyAtmosphere 의 해 구동이 엔진 쪽인 것과 같은 자리).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Radians.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class DaylightComponent
     * @brief 해 뜨는 시각에 동쪽 낮게, 한낮에 높게, 해 지는 시각이 넘으면 어둡습니다. 날씨의 어둠은 쓰지 않습니다(카탈로그의 SunIntensity 바인딩이 배율로 건다). 시각이 없는 디렉터(`readHourOfDay` 가 false)면 빛을 그대로 둡니다.
     * @details `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 `DirectionalLightComponent` 에만 씁니다.
     *          낮의 비율(해 뜸 0 → 해 짐 1)로 높이 곡선 sin(비율 × π)을 만들고, 세기 · 높이각은 지평선 값과 한낮 값 사이를 그 높이로, 방위각은 아침 값과
     *          저녁 값 사이를 비율로 잇는다. 곡선의 수치는 모두 PROPERTY(씬 데이터)다.
     */
    REFLECT( Category = "Environment", DisplayName = "Daylight", Tooltip = "Drives the directional light on this object from the game mode clock" )
    class SW_GF_API DaylightComponent : public Component
    {
    public:
        REFLECT_BODY();

        DaylightComponent();
        virtual ~DaylightComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

    private:
        PROPERTY( Category = "Sun", DisplayName = "Game Mode", Tooltip = "Object with the game mode that answers the time of day" )
        GameObjectHandle _gameMode;
        PROPERTY( Category = "Sun", DisplayName = "Night Intensity", Tooltip = "Intensity after sunset and before the night ends", Min = 0.0 )
        float32 _nightIntensity;
        PROPERTY( Category = "Sun Curve", DisplayName = "Sunrise Hour", Tooltip = "Hour the sun is on the eastern horizon", Min = 0.0, Max = 24.0 )
        float32 _sunriseHour;
        PROPERTY( Category = "Sun Curve", DisplayName = "Sunset Hour", Tooltip = "Hour the sun is on the western horizon; later is night", Min = 0.0, Max = 24.0 )
        float32 _sunsetHour;
        PROPERTY( Category = "Sun Curve", DisplayName = "Night End Hour", Tooltip = "Hour after midnight until which it is still night", Min = 0.0, Max = 24.0 )
        float32 _nightEndHour;
        PROPERTY( Category = "Sun Curve", DisplayName = "Horizon Intensity", Tooltip = "Intensity with the sun on the horizon", Min = 0.0 )
        float32 _horizonIntensity;
        PROPERTY( Category = "Sun Curve", DisplayName = "Noon Intensity", Tooltip = "Intensity with the sun at its highest", Min = 0.0 )
        float32 _noonIntensity;
        PROPERTY( Category = "Sun Curve", DisplayName = "Horizon Pitch", Tooltip = "Light pitch with the sun on the horizon" )
        Radians _horizonPitch;
        PROPERTY( Category = "Sun Curve", DisplayName = "Noon Pitch", Tooltip = "Light pitch with the sun at its highest" )
        Radians _noonPitch;
        PROPERTY( Category = "Sun Curve", DisplayName = "Morning Yaw", Tooltip = "Light yaw at sunrise" )
        Radians _morningYaw;
        PROPERTY( Category = "Sun Curve", DisplayName = "Evening Yaw", Tooltip = "Light yaw at sunset" )
        Radians _eveningYaw;
    };
} // namespace sw
