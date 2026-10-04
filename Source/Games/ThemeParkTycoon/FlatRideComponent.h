/**
 * @file FlatRideComponent.h
 * @brief 평지 놀이기구(회전목마 · 찻잔)의 모습 — 탄 손님이 있을 때 돕니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class FlatRideComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터 시뮬레이션의 탑승자 수를 읽기만 하고 자기 오브젝트의 메시만 돌립니다.
     */
    REFLECT( Category = "ThemePark", DisplayName = "Flat Ride View", Tooltip = "Spins a flat ride while the park director's simulation has riders on it" )
    class FlatRideComponent : public Component
    {
    public:
        REFLECT_BODY();

        FlatRideComponent();
        virtual ~FlatRideComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 따라갈 디렉터 · 놀이기구 번호 · 도는 빠르기(rad/s)를 정합니다. */
        void assignRide( GameObjectHandle director, int32 rideIndex, float32 spin );

    private:
        PROPERTY( Category = "Ride", DisplayName = "Director", Tooltip = "Object with the ParkDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Ride", DisplayName = "Ride Index", Tooltip = "Ride slot in the simulation" )
        int32 _rideIndex;
        PROPERTY( Category = "Ride", DisplayName = "Spin", Tooltip = "Turn rate while riders are on", Meta = "Units=rad/s" )
        float32 _spin;

        float32 _angle;
    };
} // namespace sw
