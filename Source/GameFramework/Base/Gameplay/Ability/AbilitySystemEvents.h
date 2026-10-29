/**
 * @file AbilitySystemEvents.h
 * @brief 어빌리티 시스템의 이벤트 페이로드와 "game" 채널 이벤트입니다.
 *
 * @details 알림은 두 경로로 나가며, 언리얼 GAS 와 같다.
 *          - **게임플레이 이벤트**(`GameplayEventData`): 태그 하나와 페이로드로 이루어진다. `AbilitySystemComponent::handleGameplayEvent` 로 한 오브젝트에
 *            보내면, 그 태그를 트리거로 둔 어빌리티가 발동하고 `AbilityTaskWaitGameplayEvent` 가 깨어난다(언리얼의 `SendGameplayEventToActor` 에 해당).
 *          - **게임플레이 큐**(`AbilityPresentationEvent`): 이펙트가 내는 "보여 줄 것"(피격 섬광, 오라, 소리)이다. 게임 상태를 바꾸지 않는 연출 전용이다.
 *            컴포넌트 델리게이트(`registerGameplayCue`)와 "game" 채널, 두 경로로 나간다(`GameEventUtil::send`).
 *          쓰러짐은 `AbilityOwnerDiedEvent` 로 알린다. `CombatAttributeSet` 이 체력이 0 에 닿는 순간 한 번 낸다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Event/EventType.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/TagID.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/Base/Gameplay/Ability/AbilitySystemTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 게임플레이 이벤트 페이로드(언리얼의 `FGameplayEventData` 에 해당)
    // ------------------------------------------------------------------------------
    /**
     * @brief 한 오브젝트에 보내는 게임플레이 이벤트입니다. 어빌리티 트리거와 대기 작업이 이 이벤트를 받습니다.
     * @details 대상과 쏜 쪽은 핸들이다. 받는 쪽은 같은 프레임 안에서 핸들로 오브젝트를 찾아 쓴다. 크기(`_magnitude`)의 뜻은 이벤트마다 다르다.
     *          피해량, 콤보 단계, 맞힌 수처럼 보내는 쪽과 받는 쪽이 약속해서 정한다.
     */
    struct GameplayEventData
    {
        TagID            _eventTag{};        ///< 이벤트 종류("Event.Hit", "Event.Death")
        GameObjectHandle _instigator{};      ///< 일으킨 쪽(없으면 무효 핸들)
        GameObjectHandle _target{};          ///< 받은 쪽(보통 이벤트를 받은 오브젝트 자신)
        float32          _magnitude{ 0.0f }; ///< 이벤트마다 뜻이 다른 수
        TagContainer     _instigatorTags{};  ///< 일으킨 쪽의 태그(보내는 쪽이 채움)
        TagContainer     _targetTags{};      ///< 받은 쪽의 태그(보내는 쪽이 채움)
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) "game" 채널 이벤트
    // ------------------------------------------------------------------------------
    /**
     * @brief 이펙트가 낸 게임플레이 큐 하나입니다. 언리얼의 `UGameplayCueManager` 가 받는 정보에 해당합니다.
     * @details 즉시 실행이나 주기 실행이면 `Executed` 이고, 지속 이펙트가 걸리면 `Added`, 해제되면 `Removed` 이다. 크기는 실행에서는 그 이펙트가 바꾼
     *          양(피해이면 음수)이고, 지속에서는 스택 수이다. 연출 전용이므로 이 이벤트로 게임 상태를 바꾸지 않는다.
     */
    struct SW_GF_API AbilityPresentationEvent final : IEvent
    {
        TagID                    _cueTag;     ///< 큐 태그("AbilityPresentation.Hit.Fire")
        GameObjectHandle         _target;     ///< 큐가 발생한 오브젝트(이펙트를 받은 쪽)
        GameObjectHandle         _instigator; ///< 이펙트를 건 쪽
        float32                  _magnitude;  ///< 실행이면 바뀐 양, 지속이면 스택 수
        AbilityPresentationPhase _phase;      ///< 실행, 걸림, 해제 중 하나

        /** @brief 빈 큐와 실행 단계로 초기화합니다. */
        AbilityPresentationEvent() noexcept
            : _cueTag{}
            , _target{}
            , _instigator{}
            , _magnitude{ 0.0f }
            , _phase{ AbilityPresentationPhase::Executed } {}

        SW_DECLARE_GAMEPLAY_EVENT( AbilityPresentationEvent );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 어빌리티 시스템을 가진 오브젝트가 쓰러졌음을 알립니다. 체력이 0 에 닿은 순간 한 번 냅니다.
     * @details `CombatAttributeSet` 이 체력이 0 에 닿는 순간 낸다. 같은 순간에 주인에게 `State.Dead` 태그가 붙고 `Event.Death` 게임플레이 이벤트가 전달된다.
     *          컴포넌트 안의 반응은 그 이벤트로 받고, 바깥(룸, HUD, 점수)은 이 채널 이벤트로 받는다.
     */
    struct SW_GF_API AbilityOwnerDiedEvent final : IEvent
    {
        GameObjectHandle _target;     ///< 쓰러진 오브젝트
        GameObjectHandle _instigator; ///< 마지막으로 피해를 준 쪽(알 수 없으면 무효 핸들)

        /** @brief 두 핸들을 모두 무효로 초기화합니다. */
        AbilityOwnerDiedEvent() noexcept
            : _target{}
            , _instigator{} {}

        SW_DECLARE_GAMEPLAY_EVENT( AbilityOwnerDiedEvent );
    };
} // namespace sw
