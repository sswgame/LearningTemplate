/**
 * @file AbilitySystemEvents.h
 * @brief 어빌리티 시스템의 이벤트 페이로드와 "game" 채널 이벤트입니다.
 *
 * @details 알림은 두 길로 나갑니다 — 언리얼 GAS 와 같습니다.
 *          - **게임플레이 이벤트**(`GameplayEventData`): 태그 하나와 페이로드. `AbilitySystemComponent::handleGameplayEvent` 로 한 오브젝트에
 *            보내고, 그 태그를 트리거로 둔 어빌리티가 발동하며 `AbilityTaskWaitGameplayEvent` 가 깹니다(언리얼 `SendGameplayEventToActor`).
 *          - **게임플레이 큐**(`GameplayCueEvent`): 이펙트가 낸 "보여 줄 것"(피격 섬광 · 오라 · 소리). 게임 상태를 바꾸지 않는 연출 전용입니다.
 *            컴포넌트 델리게이트(`registerGameplayCue`)와 "game" 채널 둘로 나갑니다(`GameEventUtil::send`).
 *          쓰러짐은 `AbilityOwnerDiedEvent` 입니다 — `CombatAttributeSet` 이 체력이 0 에 닿은 자리에서 한 번 냅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Event/EventType.h"
#include "Core/String/TagID.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Base/Ability/AbilitySystemTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 게임플레이 이벤트 페이로드(언리얼 `FGameplayEventData`)
    // ------------------------------------------------------------------------------
    /**
     * @brief 한 오브젝트에 보내는 게임플레이 이벤트입니다 — 어빌리티 트리거 · 대기 작업이 받습니다.
     * @details 대상 · 쏜 쪽은 핸들입니다(받는 쪽이 그 프레임 안에서 풀어 씁니다). 크기(`_magnitude`)는 이벤트마다 뜻이 다릅니다 — 피해량,
     *          콤보 단계, 맞힌 수처럼 보내는 쪽과 받는 쪽이 정합니다.
     */
    struct GameplayEventData
    {
        TagID            _eventTag{};        ///< 이벤트 종류("Event.Hit", "Event.Death")
        GameObjectHandle _instigator{};      ///< 일으킨 쪽(없으면 무효 핸들)
        GameObjectHandle _target{};          ///< 받은 쪽(보통 이벤트를 받은 오브젝트 자신)
        float32          _magnitude{ 0.0f }; ///< 이벤트마다 뜻이 다른 수
        TagContainer     _instigatorTags{};  ///< 일으킨 쪽의 태그(보내는 쪽이 채운다)
        TagContainer     _targetTags{};      ///< 받은 쪽의 태그(보내는 쪽이 채운다)
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) "game" 채널 이벤트
    // ------------------------------------------------------------------------------
    /**
     * @brief 이펙트가 낸 게임플레이 큐 하나입니다(언리얼 `UGameplayCueManager` 가 받는 자리).
     * @details 즉시 · 주기 실행이면 `Executed`, 지속 이펙트가 걸리고 풀리면 `Added` · `Removed` 입니다. 크기는 실행에서는 그 이펙트가 바꾼
     *          양(피해면 음수), 지속에서는 스택 수입니다. 연출 전용이라 이 이벤트로 게임 상태를 바꾸지 않습니다.
     */
    struct SW_GF_API GameplayCueEvent final : IEvent
    {
        TagID            _cueTag;     ///< 큐 태그("GameplayCue.Hit.Fire")
        GameObjectHandle _target;     ///< 큐가 난 오브젝트(이펙트를 받은 쪽)
        GameObjectHandle _instigator; ///< 이펙트를 건 쪽
        float32          _magnitude;  ///< 실행: 바뀐 양, 지속: 스택 수
        GameplayCuePhase _phase;      ///< 실행 · 걸림 · 풀림

        /** @brief 빈 큐 · 실행 단계로 둡니다. */
        GameplayCueEvent() noexcept
            : _cueTag{}
            , _target{}
            , _instigator{}
            , _magnitude{ 0.0f }
            , _phase{ GameplayCuePhase::Executed } {}

        SW_DECLARE_GAMEPLAY_EVENT( GameplayCueEvent );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 어빌리티 시스템을 가진 오브젝트가 쓰러졌음을 알립니다(체력이 0 에 닿은 순간 한 번).
     * @details `CombatAttributeSet` 이 체력이 0 에 닿은 자리에서 냅니다. 같은 자리에서 주인에게 `State.Dead` 태그가 붙고 `Event.Death`
     *          게임플레이 이벤트가 갑니다 — 컴포넌트 안의 반응은 그 이벤트로, 바깥(룸 · HUD · 점수)은 이 채널 이벤트로 받습니다.
     */
    struct SW_GF_API AbilityOwnerDiedEvent final : IEvent
    {
        GameObjectHandle _target;     ///< 쓰러진 오브젝트
        GameObjectHandle _instigator; ///< 마지막 피해를 준 쪽(모르면 무효 핸들)

        /** @brief 무효 핸들 둘로 둡니다. */
        AbilityOwnerDiedEvent() noexcept
            : _target{}
            , _instigator{} {}

        SW_DECLARE_GAMEPLAY_EVENT( AbilityOwnerDiedEvent );
    };
} // namespace sw
