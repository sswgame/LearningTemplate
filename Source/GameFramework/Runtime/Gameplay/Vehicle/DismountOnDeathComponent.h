/**
 * @file DismountOnDeathComponent.h
 * @brief 탑승자가 쓰러지면 강제로 내리게 합니다. 같은 오브젝트의 체력 신호(`HealthListenerComponent`)를 듣습니다. `VehicleSeatComponent::mount` 가 탑승자에게 이 컴포넌트를 붙입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Character/Combat/Health/HealthListenerComponent.h"

namespace sw
{
    /**
     * @class DismountOnDeathComponent
     * @brief 체력이 `Died` 로 바뀌면 앉아 있던 좌석에서 강제로 하차합니다(`VehicleSeatComponent::dismount( bForced = true )`). 하차 위치를 찾지 않고 지금 위치에서 분리하며,
     *        물리 바디가 있으면 물리로 분리합니다. 체력 원천이 틱 안에서 알릴 수 있으므로 내리기는 틱 뒤로 미룹니다.
     * @details 탑승 중에도 탑승자의 히트박스는 소켓 계층을 따라가므로 그대로 맞는다. 탈것의 체력은 탈것의 것이다. 탑승 중이라고 무적이 되지는 않는다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Rider Down Watcher", Tooltip = "Forces the rider off its seat when its health reports death" )
    class SW_GF_API DismountOnDeathComponent : public HealthListenerComponent
    {
    public:
        REFLECT_BODY();

        DismountOnDeathComponent();
        ~DismountOnDeathComponent() override = default;

        void onHealthChanged( const HealthChangedEvent& event ) override;
        /** @brief 탑승자가 사라지면 좌석을 비웁니다. 끊긴 핸들을 든 좌석은 다시 탈 수 없습니다. */
        void onUnregister( GameObjectManager& manager ) override;

    private:
        /** @brief 틱 뒤에 실행합니다. 앉아 있으면 강제로 내립니다. */
        void forceDismount();
    };
} // namespace sw
