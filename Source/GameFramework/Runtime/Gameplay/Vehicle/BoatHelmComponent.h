/**
 * @file BoatHelmComponent.h
 * @brief 배 폰의 조타입니다. 의도를 같은 오브젝트의 `BoatComponent` 조타 입력(추진 · 키)으로 바꿉니다. 조타수는 운전석(`VehicleSeatComponent`)에 앉은 쪽입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Environment/Water/BoatComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct ControlIntent;

    /**
     * @class BoatHelmComponent
     * @brief 배(동적 선체, `BuoyancyComponent`, `BoatComponent`, 폰, 조타륜 운전석)에 붙습니다. 의도는 물리 차 · 말과 같은 "가고 싶은 월드 방향" 이고,
     *        선수 방향으로 투영해서 앞 성분은 추진(노 · 엔진 · 돛을 편 정도), 옆 성분은 키로 씁니다(`ControlIntent::computeLocalMove`).
     * @details PrePhysics 틱에서 조타 입력을 넣어 두면 이번 물리 프레임에 반영된다(`PhysicsCarComponent` 와 같은 자리). 운전석이 비면 폰을 쥔 조종자가 없어
     *          의도가 0 이라 배는 키를 가운데로 두고 노를 멈춘다(돛은 접힌다).
     */
    REFLECT( Category = "Vehicle", DisplayName = "Boat Helm", Tooltip = "Control intent -> boat helm input: throttle (oars, engine, sail) and rudder" )
    class SW_GF_API BoatHelmComponent : public Component
    {
    public:
        REFLECT_BODY();

        BoatHelmComponent();
        ~BoatHelmComponent() override = default;

        void onTick( TickContext& context ) override;

        /** @brief 의도를 조타 입력으로 바꿉니다. @p boatForward 는 선체의 월드 앞 방향입니다(XZ 만 본다). */
        static BoatHelmInput toHelmInput( const ControlIntent& intent, const float3& boatForward );
    };
} // namespace sw
