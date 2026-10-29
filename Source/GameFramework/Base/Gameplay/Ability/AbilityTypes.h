/**
 * @file AbilityTypes.h
 * @brief 어빌리티 시스템이 함께 쓰는 작은 값 타입과 열거형입니다. 언리얼의 Gameplay Ability System 에 해당합니다.
 *
 * @details 어빌리티 시스템은 네 부분으로 나뉘며, 언리얼 GAS 와 이름과 역할이 1:1 로 대응한다.
 *          - **어트리뷰트**(`AttributeSet`): 숫자 상태(체력, 마나, 공격력). 기본값(base)과 지속 효과를 더한 현재값(current)을 따로 둔다.
 *          - **게임플레이 이펙트**(`AbilityEffectDef` 와 `AbilityEffectSpec`): 어트리뷰트와 태그를 바꾸는 데이터. 즉시, 지속, 무한 유형이 있고 주기와 스택을 지원한다.
 *          - **게임플레이 어빌리티**(`Ability`): 발동 조건(태그, 비용, 쿨다운)을 통과하면 실행되는 행동. 지연 작업은 `AbilityTask` 가 맡는다.
 *          - **어빌리티 시스템 컴포넌트**(`AbilityComponent`): 위 셋을 한 오브젝트에서 함께 돌리고, 시간과 태그와 이벤트를 처리한다.
 *
 *          이 파일은 네 부분이 모두 include 하므로 다른 어빌리티 헤더를 include 하지 않는다.
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 핸들. 활성 이펙트와 어빌리티 스펙을 가리키는 발급 id 이다(0 은 무효이고, 한 번 쓴 id 는 다시 쓰지 않는다)
    // ------------------------------------------------------------------------------
    /**
     * @brief 대상에 적용된 이펙트 하나를 가리키는 핸들입니다(언리얼의 `FActiveGameplayEffectHandle` 에 해당).
     * @details 즉시(Instant) 이펙트도 적용에 성공하면 유효한 핸들을 받는다. 다만 활성 목록에는 없으므로 `findActiveEffect` 는 nullptr 을 반환한다.
     *          "적용됐는가" 는 `isValid()` 로, "아직 걸려 있는가" 는 `AbilityComponent::isActiveEffect` 로 확인한다.
     */
    struct ActiveEffectHandle
    {
        uint64 _id{ 0 };

        /** @brief 발급된 핸들이면 true 입니다. */
        constexpr bool isValid() const { return _id != 0; }
        /** @brief 같은 이펙트를 가리키는지 비교합니다. */
        constexpr bool operator==( const ActiveEffectHandle& other ) const { return _id == other._id; }
        /** @brief 다른 이펙트를 가리키는지 비교합니다. */
        constexpr bool operator!=( const ActiveEffectHandle& other ) const { return _id != other._id; }
    };
} // namespace sw

namespace sw
{
    /** @brief 부여된 어빌리티 하나를 가리키는 핸들입니다(언리얼의 `FGameplayAbilitySpecHandle` 에 해당). */
    struct AbilitySpecHandle
    {
        uint64 _id{ 0 };

        /** @brief 발급된 핸들이면 true 입니다. */
        constexpr bool isValid() const { return _id != 0; }
        /** @brief 같은 스펙을 가리키는지 비교합니다. */
        constexpr bool operator==( const AbilitySpecHandle& other ) const { return _id == other._id; }
        /** @brief 다른 스펙을 가리키는지 비교합니다. */
        constexpr bool operator!=( const AbilitySpecHandle& other ) const { return _id != other._id; }
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 이펙트 열거형. 데이터(XML)에는 열거자 이름을 그대로 적는다. 대소문자는 구분하지 않으며, `TypeRegistry::enumFromString` 이 리플렉션 이름으로 읽는다
    // ------------------------------------------------------------------------------
    /**
     * @brief 모디파이어가 어트리뷰트를 바꾸는 방식입니다(언리얼의 `EGameplayModOp` 에 해당).
     * @details 지속 이펙트의 현재값은 언리얼의 집계 규칙을 그대로 따른다.
     *          `((base + ΣAdd) × (1 + Σ(Multiply - 1))) ÷ (1 + Σ(Divide - 1))`, `Override` 가 있으면 마지막 것이 우선한다.
     *          곱은 **보너스를 더한다**. ×1.2 와 ×1.3 이 함께 걸리면 ×1.5 이고, ×1.56 이 아니다. 즉시 이펙트는 base 에 바로 쓴다.
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

    // ------------------------------------------------------------------------------
    // 3) 어빌리티 열거형
    // ------------------------------------------------------------------------------
    /**
     * @brief 어빌리티 발동이 실패한 이유입니다. `AbilityComponent::tryActivateAbility` 가 반환합니다.
     * @details 언리얼은 실패 이유를 태그로 반환한다. 여기서는 UI 와 테스트가 바로 읽을 수 있도록 열거형으로 둔다(이름은 `TypeRegistry::enumToString` 으로 얻는다).
     */
    ENUM()
    enum class AbilityActivationResult : uint8
    {
        Activated = 0,      ///< 발동했다
        InvalidSpec,        ///< 그런 스펙이 없다
        AlreadyActive,      ///< 이미 실행 중이다(다시 발동하기 허용이 꺼져 있다)
        OnCooldown,         ///< 쿨다운 태그가 걸려 있다
        CannotAffordCost,   ///< 비용을 치를 수 없다
        MissingRequiredTag, ///< 주인에게 필요한 태그가 없다
        BlockedByTag,       ///< 주인의 태그나 다른 어빌리티가 막았다
        RejectedByAbility   ///< 어빌리티의 `canActivateAbility` 가 거절했다
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 5) 레벨에 따라 커지는 수. 언리얼 `FScalableFloat` 에서 곡선을 뺀 버전이다
    // ------------------------------------------------------------------------------
    /**
     * @brief 레벨 1 의 값과 레벨마다 더할 값입니다. `compute( level ) = base + perLevel × (level - 1)` 로 계산합니다.
     * @details 언리얼은 커브 테이블을 쓴다. 여기서는 데이터에 두 수를 적는 직선 하나로 충분하다. 커브가 필요해지면 이 타입에 테이블을 추가한다.
     */
    struct ScalableFloat
    {
        float32 _baseValue{ 0.0f }; ///< 레벨 1 의 값
        float32 _perLevel{ 0.0f };  ///< 레벨이 하나 오를 때 더할 값

        /** @brief @p level 에서의 값입니다. 1 보다 작은 레벨은 1 로 취급합니다. */
        constexpr float32 compute( int32 level ) const
        {
            const int32 clampedLevel = level < 1 ? 1 : level;
            return _baseValue + _perLevel * static_cast<float32>( clampedLevel - 1 );
        }
    };
} // namespace sw
