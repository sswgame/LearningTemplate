/**
 * @file TeleportTriggerComponent.h
 * @brief 들어온 것을 다른 자리로 순간이동시키는 볼륨입니다(텔레포터 · 포털 · 층 이동 문).
 */
#pragma once
#include "Core/Container/vector.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/TriggerComponent.h"

namespace sw
{
    /**
     * @class TeleportTriggerComponent
     * @brief 활성자가 들어오면(`TriggerComponent` — 겹침 또는 거리) `_destination` 오브젝트의 자리로 옮기고 운동을 지웁니다(`TeleportType::ResetPhysics`).
     * @details 판정은 서버다(옮기기는 게임 상태다) — 예측하는 클라이언트는 서버의 자리로 바로잡힌다(언리얼 텔레포터가 `HasAuthority` 뒤에서 `TeleportTo` 를 부르는 자리).
     *          `_bMatchFacing` 이면 몸 요와 조종 요를 도착 오브젝트의 앞(+Z)에 맞춘다. 도착 오브젝트가 다른 텔레포트 볼륨이면(짝 포털) 그 볼륨은 도착해서 생긴 진입 한 번을 세지 않는다 —
     *          나갔다 다시 들어와야 돌아간다. 동작 어휘의 순간이동은 `object.teleport`(`ObjectActions`)이다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Teleport Trigger", Tooltip = "Moves what enters to the destination object (portal, floor door); paired portals do not bounce" )
    class SW_GF_API TeleportTriggerComponent : public TriggerComponent
    {
    public:
        REFLECT_BODY();

        TeleportTriggerComponent();
        ~TeleportTriggerComponent() override = default;

        /** @brief 도착 오브젝트를 정합니다. 코드로 만든 볼륨 · 테스트가 씁니다. 씬에 놓은 볼륨은 PROPERTY 가 채웁니다. */
        void setDestination( GameObjectHandle destination ) { _destination = destination; }
        /** @brief 도착 오브젝트가 없으면 오류를 남깁니다. */
        void onBeginPlay() override;

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        /** @brief @p arrived 가 이 볼륨에 도착했다 — 그 진입 한 번을 세지 않는다. */
        void expectArrival( GameObjectHandle arrived );

    private:
        PROPERTY( Category = "Teleport", DisplayName = "Destination", Tooltip = "Object whose position (and +Z facing) the activator is moved to" )
        GameObjectHandle _destination;
        PROPERTY( Category = "Teleport", DisplayName = "Match Facing", Tooltip = "Turn the body and the control rotation to the destination's forward" )
        bool _bMatchFacing;
        PROPERTY( RuntimeState, Category = "Teleport", DisplayName = "Arrivals", Tooltip = "Objects that arrived here and whose next entry is ignored (runtime)" )
        vector<GameObjectHandle> _listArrival;
    };
} // namespace sw
