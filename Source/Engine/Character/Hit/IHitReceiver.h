/**
 * @file IHitReceiver.h
 * @brief 맞음의 피해를 받는 쪽 리플렉션 인터페이스입니다 — 체력을 가진 컴포넌트(어빌리티 시스템, 키트의 유닛 스탯, 게임의 적)가 포트 멤버로 내보냅니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"

#include "Engine/Object/Component/Component.h" // HitInfo
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class IHitReceiver
     * @brief 맞음 피해의 받는 쪽 계약입니다. `CharacterRayHit::deliverHit` 가 대상 오브젝트에서 이것 하나를 찾아(`findInterface<IHitReceiver>()`) 먼저 부르고,
     *        그 다음에 반응(래그돌 · 절단 · 몸 흔들림)에 맞음 사건(`GameObjectEvents::_hitReceived`)을 알립니다.
     * @details 언리얼 `AActor::TakeDamage`(피해는 받는 쪽 하나가 처리) + `OnTakeAnyDamage`(반응은 델리게이트) 자리다. 피해를 사건으로만 보내면 체력 원천과 반응의 순서가
     *          구독 순서에 달리고, 보낸 쪽은 받은 쪽이 있었는지 모른다(docs/agents/Gameplay.md "시스템 사이 알림" 필수 일대일 효과). 체력 읽기는 GameFramework 의
     *          `IHealthSource` 다 — 같은 컴포넌트가 두 포트를 함께 내보낸다.
     */
    REFLECT( Interface, Category = "Combat", DisplayName = "Hit Receiver", Tooltip = "Contract of the component that takes a hit's damage on its object (unit stats, ability system)" )
    struct SW_API IHitReceiver
    {
        REFLECT_BODY();

        virtual ~IHitReceiver() = default;

        /** @brief 맞음 하나의 피해를 받습니다. 쓰러짐을 정하면 치명 사본을 반응에 다시 낸다(GameFramework `IHealthSource::deliverFatalHit`). */
        virtual void receiveHit( const HitInfo& hit ) = 0;
    };
} // namespace sw
