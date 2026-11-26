/**
 * @file AbilityHandles.h
 * @brief 활성 이펙트와 어빌리티 스펙을 가리키는 발급 핸들입니다(0 은 무효, 한 번 쓴 id 는 다시 쓰지 않는다). 언리얼 `FActiveGameplayEffectHandle` · `FGameplayAbilitySpecHandle` 자리.
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
     * @brief 대상에 적용된 이펙트 하나를 가리키는 핸들입니다(언리얼의 `FActiveGameplayEffectHandle` 에 해당).
     * @details 즉시(Instant) 이펙트도 적용에 성공하면 유효한 핸들을 받는다. 다만 활성 목록에는 없으므로 `findActiveEffect` 는 nullptr 을 반환한다.
     *          "적용됐는가" 는 `isValid()` 로, "아직 걸려 있는가" 는 `AbilitySystemComponent::isActiveEffect` 로 확인한다.
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
