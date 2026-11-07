/**
 * @file AbilityActions.h
 * @brief 어빌리티 · 체력 동작 어휘의 칸입니다 — `ability.applyEffect` · `ability.removeEffect` · `ability.removeStacks` · `ability.grant` · `ability.activate` · `health.kill`.
 * @details 대상은 실행기가 채운 `ActionTargetSlot` 이다(지역 효과는 대상 역할마다, 대화 · 행동 트리도 채우면 같은 이름이 든다). 건 이펙트는 `ActionSourceSlot` 의
 *          길(`volume:<이름>` · `surface:<종류>`)을 실어 Ability 디버거가 출처를 보이고, `ability.removeEffect` 가 이펙트 이름 없이 불리면 그 길의 것만 지운다.
 *          값(`value=damage`)은 실행기가 그 순간 계산한 수(`ActionValuesSlot`)를 SetByCaller 로 넣는다 — 저항 · 면역은 이펙트의 실행 · 수정자(대상 쪽)가 깎는다.
 *          언리얼 GAS 의 Apply/Remove GameplayEffect · Give/TryActivate Ability 노드의 자리다(기억).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class AbilityCatalog;
    class GameActionCall;

    /** @brief `ability.applyEffect` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AbilityEffectActionParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Effect id in the AbilityCatalog" )
        hashed_string _effect{};
        PROPERTY( Min = 1, Tooltip = "Effect level" )
        int32 _level{ 1 };
        PROPERTY( Tooltip = "SetByCaller name the value goes to (empty: none)" )
        hashed_string _setByCaller{};
        PROPERTY( Tooltip = "Name of the executor value put into the SetByCaller (a zone module's expression)" )
        hashed_string _value{};
    };

    /** @brief `ability.removeEffect` 의 칸입니다. 이펙트가 비면 이 길(출처)이 건 이펙트를 모두 지웁니다. */
    REFLECT()
    struct SW_GF_API AbilityEffectRemoveParams
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Effect id to remove (empty: every effect applied through this source)" )
        hashed_string _effect{};
    };

    /** @brief `ability.removeStacks` 의 칸입니다(점프 탈출이 가라앉음 스택을 줄인다). */
    REFLECT()
    struct SW_GF_API AbilityStackRemoveParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Stacking effect id" )
        hashed_string _effect{};
        PROPERTY( Min = 1, Tooltip = "Stacks to take away (the effect ends when none are left)" )
        int32 _stacks{ 1 };
    };

    /** @brief `ability.grant` · `ability.activate` 의 칸입니다. */
    REFLECT()
    struct SW_GF_API AbilityGrantParams
    {
        REFLECT_BODY();

        PROPERTY( Required, Tooltip = "Ability id in the AbilityCatalog" )
        hashed_string _ability{};
    };

    /** @brief `health.kill` 의 칸입니다. 기본은 무적을 존중합니다. */
    REFLECT()
    struct SW_GF_API HealthKillParams
    {
        REFLECT_BODY();

        HealthKillParams();

        PROPERTY( Tooltip = "Kill even an invulnerable target (pits, engulfing sand); off respects State.Invulnerable" )
        uint8 _bIgnoreInvulnerable : 1;
        uint8 _reserved            : 7;
    };

    /**
     * @struct AbilityActions
     * @brief 동작 칸이 가리키는 카탈로그 id 를 읽을 때 봅니다(지역 프로필 로드 — 모르는 이펙트 · 어빌리티면 로드 실패).
     */
    struct SW_GF_API AbilityActions
    {
        /**
         * @brief 호출 @p call 이 이 파일의 어휘이면 카탈로그에 있는 id 인지 봅니다. 다른 어휘는 true 입니다. 틀리면 까닭을 오류 로그로 남깁니다.
         * @param pCatalog 게임의 어빌리티 카탈로그. 없으면 이 어휘의 동작은 쓸 수 없으므로 false 입니다.
         */
        [[nodiscard]] static bool validateReferences( const GameActionCall& call, const AbilityCatalog* pCatalog, string_view sourceName );
    };
} // namespace sw
