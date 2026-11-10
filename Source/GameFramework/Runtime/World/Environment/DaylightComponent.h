/**
 * @file DaylightComponent.h
 * @brief 해 — 디렉터가 답하는 하루의 시각과 강수(`GameModeComponent::readDaylight`)로 같은 오브젝트의 방향광 높이 · 방향 · 세기를 맞춥니다.
 * @details HarvestValley 의 `DaylightComponent` 를 옮겼다 — 시각만 디렉터에게 묻고 곡선은 씬 데이터라 어느 게임이든 쓴다(언리얼 SkyAtmosphere 의 해 구동이 엔진 쪽인 것과 같은 자리).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class DaylightComponent
     * @brief 해 뜨는 시각에 동쪽 낮게, 한낮에 높게, 해 지는 시각이 넘으면 어둡습니다. 강수 중에는 세기가 줄어듭니다. 시각이 없는 디렉터(`readDaylight` 가 false)면 빛을 그대로 둡니다.
     * @details `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 `DirectionalLightComponent` 에만 씁니다.
     *          낮의 비율(해 뜸 0 → 해 짐 1)로 높이 곡선 sin(비율 × π)을 만들고, 세기 · 높이각은 지평선 값과 한낮 값 사이를 그 높이로, 방위각은 아침 값과
     *          저녁 값 사이를 비율로 잇는다. 곡선의 수치는 모두 PROPERTY(씬 데이터)다.
     */
    REFLECT( Category = "Environment", DisplayName = "Daylight", Tooltip = "Drives the directional light on this object from the game mode clock and weather" )
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
        PROPERTY( Category = "Sun", DisplayName = "Rain Dimming", Tooltip = "Intensity factor while it rains or snows", Min = 0.0, Max = 1.0 )
        float32 _rainDimming;
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
        PROPERTY( Category = "Sun Curve", DisplayName = "Horizon Pitch", Tooltip = "Light pitch with the sun on the horizon", Units = rad )
        float32 _horizonPitch;
        PROPERTY( Category = "Sun Curve", DisplayName = "Noon Pitch", Tooltip = "Light pitch with the sun at its highest", Units = rad )
        float32 _noonPitch;
        PROPERTY( Category = "Sun Curve", DisplayName = "Morning Yaw", Tooltip = "Light yaw at sunrise", Units = rad )
        float32 _morningYaw;
        PROPERTY( Category = "Sun Curve", DisplayName = "Evening Yaw", Tooltip = "Light yaw at sunset", Units = rad )
        float32 _eveningYaw;
    };
} // namespace sw
