/**
 * @file AbilitySystemTypes.h
 * @brief 어빌리티 시스템(언리얼 Gameplay Ability System 과 같은 자리)이 함께 쓰는 작은 값 타입 · 열거입니다.
 *
 * @details 어빌리티 시스템은 넷으로 나뉩니다 — 언리얼 GAS 와 이름 · 역할이 1:1 입니다.
 *          - **어트리뷰트**(`AttributeSet`): 숫자 상태(체력 · 마나 · 공격력). 기본값(base)과 지속 효과를 더한 현재값(current)을 따로 둡니다.
 *          - **게임플레이 이펙트**(`GameplayEffectDef` · `GameplayEffectSpec`): 어트리뷰트 · 태그를 바꾸는 데이터. 즉시 · 지속 · 무한, 주기, 스택.
 *          - **게임플레이 어빌리티**(`GameplayAbility`): 발동 조건(태그 · 비용 · 쿨다운)을 지나 실행되는 행동. 지연 작업은 `AbilityTask`.
 *          - **어빌리티 시스템 컴포넌트**(`AbilitySystemComponent`): 위 셋을 한 오브젝트에 모아 시간 · 태그 · 이벤트를 돌립니다.
 *
 *          이 파일은 넷이 모두 include 하므로 다른 어빌리티 헤더를 include 하지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 핸들 — 활성 이펙트 · 어빌리티 스펙을 가리키는 발급 id(0 은 무효, 다시 쓰지 않는다)
    // ------------------------------------------------------------------------------
    /**
     * @brief 대상에 적용된 이펙트 하나를 가리킵니다(언리얼 `FActiveGameplayEffectHandle`).
     * @details 즉시(Instant) 이펙트도 적용에 성공하면 유효한 핸들을 받습니다 — 다만 활성 목록에는 없어 `findActiveEffect` 는 nullptr 입니다.
     *          그래서 "적용됐는가" 는 `isValid()` 로, "아직 걸려 있는가" 는 `AbilitySystemComponent::isActiveEffect` 로 묻습니다.
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
    /** @brief 부여된 어빌리티 하나를 가리킵니다(언리얼 `FGameplayAbilitySpecHandle`). */
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
    // 2) 이펙트 열거 — 데이터(XML)는 열거자 이름을 그대로 적는다(대소문자 무시)
    // ------------------------------------------------------------------------------
    /**
     * @brief 모디파이어가 어트리뷰트를 바꾸는 방식입니다(언리얼 `EGameplayModOp`).
     * @details 지속 이펙트의 현재값은 언리얼 집계 규칙 그대로입니다:
     *          `((base + ΣAdd) × (1 + Σ(Multiply − 1))) ÷ (1 + Σ(Divide − 1))`, `Override` 가 있으면 마지막 것이 이깁니다.
     *          곱은 **보너스를 더합니다** — ×1.2 와 ×1.3 이 함께 걸리면 ×1.5 입니다(×1.56 이 아닙니다). 즉시 이펙트는 base 에 그 자리에서 씁니다.
     */
    enum class AttributeModOp : uint8
    {
        Add = 0,  ///< 더하기(음수면 빼기)
        Multiply, ///< 곱하기(1 이 그대로)
        Divide,   ///< 나누기(1 이 그대로, 0 은 무시)
        Override  ///< 덮어쓰기
    };

    /** @brief 이펙트가 얼마나 걸려 있는지입니다(언리얼 `EGameplayEffectDurationType`). */
    enum class EffectDurationPolicy : uint8
    {
        Instant = 0, ///< 그 자리에서 base 를 바꾸고 끝난다(피해 · 회복 · 비용)
        HasDuration, ///< 정한 시간 동안 걸려 있다(버프 · 쿨다운 · 도트)
        Infinite     ///< 지울 때까지 걸려 있다(장비 · 패시브)
    };

    /** @brief 같은 이펙트가 다시 걸릴 때 어떻게 쌓을지입니다(언리얼 `EGameplayEffectStackingType`). */
    enum class EffectStackingPolicy : uint8
    {
        None = 0,         ///< 걸 때마다 새 인스턴스(서로 독립)
        AggregateByTarget ///< 대상에 하나만 두고 스택 수를 올린다(상한 `_stackLimit`)
    };

    /** @brief 쌓인 이펙트의 시간이 다 됐을 때 무엇을 지울지입니다(언리얼 `EGameplayEffectStackingExpirationPolicy`). */
    enum class EffectStackExpirationPolicy : uint8
    {
        ClearEntireStack = 0,               ///< 스택을 통째로 지운다
        RemoveSingleStackAndRefreshDuration ///< 하나만 내리고 시간을 다시 채운다
    };

    /**
     * @brief 모디파이어 크기를 어디서 얻는지입니다(언리얼 `EGameplayEffectMagnitudeCalculation`).
     * @details `AttributeBased` 는 적용하는 순간의 값을 씁니다(스냅샷) — 걸린 뒤 그 어트리뷰트가 바뀌어도 이미 걸린 이펙트는 다시 계산하지 않습니다.
     */
    enum class EffectMagnitudeSource : uint8
    {
        ScalableFloat = 0, ///< 데이터에 적은 수(레벨마다 늘 수 있다)
        AttributeBased,    ///< 쏜 쪽 · 맞는 쪽 어트리뷰트 × 계수 + 더하기
        SetByCaller        ///< 스펙을 만든 코드가 이름으로 넣는 수(`GameplayEffectSpec::setSetByCallerMagnitude`)
    };

    /** @brief 게임플레이 큐가 어떤 순간에 났는지입니다(언리얼 `EGameplayCueEvent`). */
    enum class GameplayCuePhase : uint8
    {
        Executed = 0, ///< 즉시 · 주기 실행(피격 섬광 · 피해 숫자)
        Added,        ///< 지속 이펙트가 걸렸다(불타는 오라 시작)
        Removed       ///< 지속 이펙트가 풀렸다(오라 끝)
    };

    // ------------------------------------------------------------------------------
    // 3) 어빌리티 열거
    // ------------------------------------------------------------------------------
    /**
     * @brief 어빌리티 발동이 왜 실패했는지입니다. `AbilitySystemComponent::tryActivateAbility` 가 돌려줍니다.
     * @details 언리얼은 실패 이유를 태그로 돌려줍니다. 여기서는 UI · 시험이 바로 읽게 열거로 둡니다.
     */
    enum class AbilityActivationResult : uint8
    {
        Activated = 0,      ///< 발동했다
        InvalidSpec,        ///< 그런 스펙이 없다
        AlreadyActive,      ///< 이미 돌고 있다(다시 걸기 허용이 꺼져 있다)
        OnCooldown,         ///< 쿨다운 태그가 걸려 있다
        CannotAffordCost,   ///< 비용을 치를 수 없다
        MissingRequiredTag, ///< 주인에게 필요한 태그가 없다
        BlockedByTag,       ///< 주인의 태그 · 다른 어빌리티가 막았다
        RejectedByAbility   ///< 어빌리티의 `canActivateAbility` 가 거절했다
    };

    /** @brief 발동 결과를 로그 · UI 에 쓸 이름으로 바꿉니다. */
    SW_GF_API const utf8* toString( AbilityActivationResult result );

    // ------------------------------------------------------------------------------
    // 4) 이름 ↔ 열거 — 데이터(XML)를 읽는 자리 하나(대소문자 무시)
    // ------------------------------------------------------------------------------
    /**
     * @struct AbilitySystemEnumUtil
     * @brief 열거자 이름을 열거로 읽습니다. 모르는 이름이면 false 이고 @p outValue 는 그대로입니다.
     * @details 리플렉션 `ENUM()` 을 쓰지 않는 이유: 이 열거들은 컴포넌트 PROPERTY 가 아니라 카탈로그 XML 에만 나옵니다. 읽는 자리가
     *          카탈로그 하나라 이름표를 여기 같이 둡니다 — 열거자를 더하면 이 표에도 한 줄 더합니다(시험이 왕복을 확인한다).
     */
    struct SW_GF_API AbilitySystemEnumUtil
    {
        [[nodiscard]] static bool parseModOp( string_view text, AttributeModOp& outValue );
        [[nodiscard]] static bool parseDurationPolicy( string_view text, EffectDurationPolicy& outValue );
        [[nodiscard]] static bool parseStackingPolicy( string_view text, EffectStackingPolicy& outValue );
        [[nodiscard]] static bool parseStackExpirationPolicy( string_view text, EffectStackExpirationPolicy& outValue );
        [[nodiscard]] static bool parseMagnitudeSource( string_view text, EffectMagnitudeSource& outValue );

        static const utf8* toString( AttributeModOp value );
        static const utf8* toString( EffectDurationPolicy value );
        static const utf8* toString( EffectStackingPolicy value );
        static const utf8* toString( EffectStackExpirationPolicy value );
        static const utf8* toString( EffectMagnitudeSource value );
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 5) 레벨에 따라 커지는 수(언리얼 `FScalableFloat` 의 곡선 없는 판)
    // ------------------------------------------------------------------------------
    /**
     * @brief 레벨 1 의 값과 레벨마다 더할 값입니다 — `compute( level ) = base + perLevel × (level − 1)`.
     * @details 언리얼은 커브 테이블을 씁니다. 여기서는 데이터에 두 수를 적는 직선 하나로 둡니다 — 커브가 필요해지면 이 타입에 표를 더합니다.
     */
    struct ScalableFloat
    {
        float32 _baseValue{ 0.0f }; ///< 레벨 1 의 값
        float32 _perLevel{ 0.0f };  ///< 레벨이 하나 오를 때 더할 값

        /** @brief @p level 에서의 값입니다. 1 보다 작은 레벨은 1 로 봅니다. */
        constexpr float32 compute( int32 level ) const
        {
            const int32 clampedLevel = level < 1 ? 1 : level;
            return _baseValue + _perLevel * static_cast<float32>( clampedLevel - 1 );
        }
    };
} // namespace sw
