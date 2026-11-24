/**
 * @file FlatRideComponent.h
 * @brief 평지 놀이기구(회전목마 · 찻잔)의 모습 — 탄 손님이 있을 때 돕니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/GameFrameworkComponent.h"

namespace sw
{
    /**
     * @class FlatRideComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터 시뮬레이션의 탑승자 수를 읽기만 하고 자기 오브젝트의 메시만 돌립니다.
     */
    REFLECT( Category = "ThemePark", DisplayName = "Flat Ride View", Tooltip = "Spins a flat ride while the park game mode's simulation has riders on it" )
    class SW_KIT_CLIENT_API FlatRideComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 디렉터(PrePhysics)가 이 프레임의 탑승을 정한 뒤에 읽는다.

        FlatRideComponent();
        virtual ~FlatRideComponent() override = default;

        void onTick( TickContext& context ) override;

        /** @brief 따라갈 디렉터 · 놀이기구 번호 · 도는 빠르기(rad/s) · 처음 각(공원 원점의 요, rad)을 정합니다. */
        void assignRide( GameObjectHandle gameMode, int32 rideIndex, float32 spin, float32 startAngle );

    private:
        PROPERTY( Category = "Ride", DisplayName = "Ride Index", Tooltip = "Ride slot in the simulation" )
        int32 _rideIndex;
        PROPERTY( Category = "Ride", DisplayName = "Spin", Tooltip = "Turn rate while riders are on", Units = "rad/s" )
        float32 _spin;

        float32 _angle;
    };
} // namespace sw
