/**
 * @file AbilityActivationResult.h
 * @brief 어빌리티 발동이 실패한 이유(`AbilitySystemComponent::tryActivateAbility` 의 반환)입니다.
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
     * @brief 어빌리티 발동이 실패한 이유입니다. `AbilitySystemComponent::tryActivateAbility` 가 반환합니다.
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
