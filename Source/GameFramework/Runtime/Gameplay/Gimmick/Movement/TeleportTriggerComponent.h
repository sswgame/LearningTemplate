/**
 * @file TeleportTriggerComponent.h
 * @brief 들어온 것을 다른 자리로 순간이동시키는 볼륨입니다(텔레포터 · 포털 · 층 이동 문).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Trigger/TriggerComponent.h"

namespace sw
{
    /**
     * @class TeleportTriggerComponent
     * @brief 활성자가 들어오면(`TriggerComponent` — 겹침 또는 거리) `_destination` 오브젝트의 자리로 옮기고 운동을 지웁니다(`TeleportType::ResetPhysics`).
     * @details 판정은 서버다(옮기기는 게임 상태다) — 예측하는 클라이언트는 서버의 자리로 바로잡힌다(언리얼 텔레포터가 `HasAuthority` 뒤에서 `TeleportTo` 를 부르는 자리).
     *          `_bMatchFacing` 이면 몸 요와 조종 요를 도착 오브젝트의 앞(+Z)에 맞춘다. 도착 오브젝트가 다른 텔레포트 볼륨이면(짝 포털) 그 볼륨은 도착한 것이 나갈 때까지 그것의 진입을 세지 않는다 —
     *          나갔다 다시 들어와야 돌아간다. 도착 자리가 그 볼륨 밖이면(겹침이 생기지 않으면) 물리가 한 걸음 돈 뒤 기다림을 버린다. 안에 선 채 저장 · 복원해도 되돌아가지 않는다. 동작 어휘의 순간이동은 `object.teleport`(`ObjectActions`)이다.
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
        /** @brief 도착 오브젝트가 없으면 오류를 남깁니다. 되살아난 도착(안에 선 채 저장)을 다시 기다립니다. */
        void onBeginPlay() override;
        /** @brief 겹침 이탈도 구독합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 도착했는데 겹침이 생기지 않은 것과 거리로 재어 나간 것을 도착에서 뺍니다. */
        void onTick( TickContext& context ) override;
        /** @brief 꺼지면 안의 것을 내보낸 것이다(트리거 계약) — 도착을 잊습니다. */
        void onOwnerActiveInHierarchyChanged() override;
        void onPropertyChanged( hashed_string propertyName ) override;

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        /** @brief @p arrived 가 이 볼륨에 도착했다 — 나갈 때까지 그것의 진입을 세지 않는다. 꺼진 볼륨이면 아무것도 하지 않는다. */
        void expectArrival( GameObjectHandle arrived );
        /** @brief @p object 가 나갔다 — 다음 진입은 센다. */
        void removeArrival( GameObjectHandle object );
        void handleOverlapEnd( const OverlapInfo& overlap );

    private:
        /** @brief 겹침이 아직 생기지 않은 도착 하나입니다. */
        struct PendingArrival
        {
            GameObjectHandle _object{};
            optional<uint64> _step{}; ///< 도착 뒤 첫 틱의 물리 스텝 번호 — 이보다 돌았는데 진입이 없으면 도착 자리가 볼륨 밖이다
        };

        PROPERTY( Category = "Teleport", DisplayName = "Destination", Tooltip = "Object whose position (and +Z facing) the activator is moved to" )
        GameObjectHandle _destination;
        PROPERTY( Category = "Teleport", DisplayName = "Match Facing", Tooltip = "Turn the body and the control rotation to the destination's forward" )
        bool _bMatchFacing;
        PROPERTY( RuntimeState, Category = "Teleport", DisplayName = "Arrivals", Tooltip = "Objects that arrived here and have not left yet; their entry is ignored (runtime)" )
        vector<GameObjectHandle> _listArrival;

        vector<PendingArrival> _listPendingArrival; ///< `_listArrival` 가운데 겹침을 아직 기다리는 것
        Subscription           _overlapEndSubscription;
    };
} // namespace sw
