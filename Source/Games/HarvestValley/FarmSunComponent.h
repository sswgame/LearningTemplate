/**
 * @file FarmSunComponent.h
 * @brief 농장의 해 — 디렉터 달력의 시각과 비로 같은 오브젝트의 방향광 높이 · 방향 · 세기를 맞춥니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class FarmSunComponent
     * @brief 6 시에 동쪽 낮게, 한낮에 높게, 20 시가 넘으면 어둡습니다. 비 오는 날은 세기가 반쯤입니다.
     * @details `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 `DirectionalLightComponent` 에만 씁니다.
     */
    REFLECT( Category = "Farming", DisplayName = "Farm Sun", Tooltip = "Drives the directional light on this object from the farm director clock and weather" )
    class FarmSunComponent : public Component
    {
    public:
        REFLECT_BODY();

        FarmSunComponent();
        virtual ~FarmSunComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        PROPERTY( Category = "Sun", DisplayName = "Director", Tooltip = "Object with the FarmDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Sun", DisplayName = "Rain Dimming", Tooltip = "Intensity factor on a rainy day", Min = 0.0, Max = 1.0 )
        float32 _rainDimming;
        PROPERTY( Category = "Sun", DisplayName = "Night Intensity", Tooltip = "Intensity after 20:00", Min = 0.0 )
        float32 _nightIntensity;
    };
} // namespace sw
