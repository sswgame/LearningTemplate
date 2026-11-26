/**
 * @file AbilityEffectPolicy.h
 * @brief 게임플레이 이펙트의 정책 열거형(모디파이어 연산 · 지속 · 스택 · 스택 만료 · 크기 출처 · 큐 순간)입니다. 데이터(XML)에는 열거자 이름을 그대로 적는다.
 *
 * @details 어빌리티 시스템은 네 부분으로 나뉘며, 언리얼 GAS 와 이름과 역할이 1:1 로 대응한다.
 *          - 어트리뷰트(`AttributeSet`): 숫자 상태(체력, 마나, 공격력). 기본값(base)과 지속 효과를 더한 현재값(current)을 따로 둔다.
 *          - 게임플레이 이펙트(`AbilityEffectDef` 와 `AbilityEffectSpec`): 어트리뷰트와 태그를 바꾸는 데이터. 즉시, 지속, 무한 유형이 있고 주기와 스택을 지원한다.
 *          - 게임플레이 어빌리티(`Ability`): 발동 조건(태그, 비용, 쿨다운)을 통과하면 실행되는 행동. 지연 작업은 `AbilityTask` 가 맡는다.
 *          - 어빌리티 시스템 컴포넌트(`AbilitySystemComponent`): 위 셋을 한 오브젝트에서 함께 돌리고, 시간과 태그와 이벤트를 처리한다.
 *
 *          핸들은 AbilityHandles.h, 발동 실패 이유는 AbilityActivationResult.h, 레벨 수치는 ScalableFloat.h 다. 이 파일들은 다른 어빌리티 헤더를 include 하지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 모디파이어가 어트리뷰트를 바꾸는 방식입니다(언리얼의 `EGameplayModOp` 에 해당).
     * @details 지속 이펙트의 현재값은 언리얼의 집계 규칙을 그대로 따른다.
     *          `((base + ΣAdd) × (1 + Σ(Multiply - 1))) ÷ (1 + Σ(Divide - 1))`, `Override` 가 있으면 마지막 것이 우선한다.
     *          곱은 보너스를 더한다. ×1.2 와 ×1.3 이 함께 걸리면 ×1.5 이고, ×1.56 이 아니다. 즉시 이펙트는 base 에 바로 쓴다.
     */
    ENUM()
    enum class AttributeModOp : uint8
    {
        Add = 0,  ///< 더하기(음수이면 빼기)
        Multiply, ///< 곱하기(1 이면 그대로)
        Divide,   ///< 나누기(1 이면 그대로, 0 은 무시)
        Override  ///< 덮어쓰기
    };

    /** @brief 이펙트가 얼마나 걸려 있는지 나타냅니다(언리얼의 `EGameplayEffectDurationType` 에 해당). */
    ENUM()
    enum class EffectDurationPolicy : uint8
    {
        Instant = 0, ///< base 를 바로 바꾸고 끝난다(피해, 회복, 비용)
        HasDuration, ///< 정한 시간 동안 걸려 있다(버프, 쿨다운, 도트 대미지)
        Infinite     ///< 지울 때까지 걸려 있다(장비, 패시브)
    };

    /** @brief 같은 이펙트가 다시 걸릴 때 어떻게 쌓을지 나타냅니다(언리얼의 `EGameplayEffectStackingType` 에 해당). */
    ENUM()
    enum class EffectStackingPolicy : uint8
    {
        None = 0,         ///< 적용할 때마다 새 인스턴스를 만든다(서로 독립)
        AggregateByTarget ///< 대상에 하나만 두고 스택 수를 올린다(상한은 `_stackLimit`)
    };

    /** @brief 쌓인 이펙트의 시간이 다 됐을 때 무엇을 지울지 나타냅니다(언리얼의 `EGameplayEffectStackingExpirationPolicy` 에 해당). */
    ENUM()
    enum class EffectStackExpirationPolicy : uint8
    {
        ClearEntireStack = 0,               ///< 스택을 통째로 지운다
        RemoveSingleStackAndRefreshDuration ///< 스택 하나만 줄이고 시간을 다시 채운다
    };

    /**
     * @brief 모디파이어 크기를 어디서 얻는지 나타냅니다(언리얼의 `EGameplayEffectMagnitudeCalculation` 에 해당).
     * @details `AttributeBased` 는 적용하는 순간의 값을 쓴다(스냅샷). 이펙트가 적용된 뒤에 그 어트리뷰트가 바뀌어도 이미 적용된 이펙트는 다시 계산하지 않는다.
     */
    ENUM()
    enum class EffectMagnitudeSource : uint8
    {
        ScalableFloat = 0, ///< 데이터에 적은 수(레벨에 따라 늘 수 있다)
        AttributeBased,    ///< 쏜 쪽이나 맞는 쪽 어트리뷰트에 계수를 곱하고 값을 더한다
        SetByCaller        ///< 스펙을 만든 코드가 이름으로 넣는 수(`AbilityEffectSpec::setSetByCallerMagnitude`)
    };

    /** @brief 게임플레이 큐가 어떤 순간에 발생했는지 나타냅니다(언리얼의 `EGameplayCueEvent` 에 해당). */
    enum class AbilityPresentationPhase : uint8
    {
        Executed = 0, ///< 즉시 실행이나 주기 실행(피격 섬광, 피해 숫자)
        Added,        ///< 지속 이펙트가 걸렸다(불타는 오라 시작)
        Removed       ///< 지속 이펙트가 해제됐다(오라 끝)
    };
} // namespace sw
