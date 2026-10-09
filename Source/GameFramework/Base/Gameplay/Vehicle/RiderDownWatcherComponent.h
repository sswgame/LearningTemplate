/**
 * @file RiderDownWatcherComponent.h
 * @brief 탑승자가 쓰러지면 강제로 내립니다 — 같은 오브젝트의 체력 신호(`HealthListenerComponent`)를 듣습니다. `MountUtil::mount` 가 탑승자에게 붙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Combat/HealthListenerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class RiderDownWatcherComponent
     * @brief 체력이 `Died` 로 바뀌면 앉아 있던 좌석에서 강제 하차합니다(`MountUtil::dismount( bForced = true )` — 하차 자리를 찾지 않고 지금 자리에서,
     *        물리 바디가 있으면 물리로 뗀다). 체력 원천이 틱 안에서 알릴 수 있어 내리기는 틱 뒤로 미룹니다.
     * @details 탑승 중에도 탑승자의 히트박스는 소켓 계층을 따라가 그대로 맞습니다 — 탈것 체력은 탈것의 것입니다(결정: 탑승 중 무적 없음).
     */
    REFLECT( Category = "Vehicle", DisplayName = "Rider Down Watcher", Tooltip = "Forces the rider off its seat when its health reports death" )
    class SW_GF_API RiderDownWatcherComponent : public HealthListenerComponent
    {
    public:
        REFLECT_BODY();

        RiderDownWatcherComponent();
        ~RiderDownWatcherComponent() override = default;

        void onHealthChanged( const HealthChangedEvent& event ) override;

    private:
        /** @brief 틱 뒤 — 앉아 있으면 강제로 내립니다. */
        void forceDismount();
    };
} // namespace sw
