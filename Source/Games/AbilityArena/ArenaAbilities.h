/**
 * @file ArenaAbilities.h
 * @brief AbilityArena 가 코드로 둔 어빌리티 클래스 셋입니다 — 근접 · 투사체 · 대시.
 *
 * @details 셋 다 **숫자를 갖지 않습니다.** 사거리 · 피해 · 속도 · 걸 이펙트는 데이터(abilities.xml 의 `<Param>`)가 주고, 비용 · 쿨다운 ·
 *          막는 태그도 데이터에 있습니다. 그래서 같은 클래스가 플레이어 근접(`GA_Melee`)과 적 근접(`GA_EnemyMelee`)으로 두 번 쓰입니다.
 *          회복 · 가시처럼 이펙트만 거는 어빌리티는 클래스 없이 프레임워크의 "ApplyEffects" 로 돕니다.
 */
#pragma once
#include "GameFramework/Base/Ability/GameplayAbility.h"

namespace sw
{
    class AbilityCatalog;

    /**
     * @brief 사거리(`range`) 안의 가장 가까운 적 하나에 피해 이펙트(`damageEffect`, SetByCaller "Damage" = `damage`)를 겁니다.
     * @details 대상이 없어도 휘두릅니다(쿨다운이 걸린다) — 헛손질도 행동이다.
     */
    class ArenaMeleeStrikeAbility final : public GameplayAbility
    {
    public:
        void activateAbility( const GameplayEventData* pTriggerEvent ) override;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 바라보는 쪽으로 투사체를 쏩니다(`speed` · `range`). 맞은 적에게 `damageEffect`(SetByCaller "Damage" = `damage`)와 `extraEffect` 를 겁니다.
     * @details 스펙은 쏘는 순간 만듭니다 — 날아가는 동안 쏜 쪽이 쓰러져도 공격력 스냅샷과 레벨이 남습니다.
     */
    class ArenaProjectileAbility final : public GameplayAbility
    {
    public:
        void activateAbility( const GameplayEventData* pTriggerEvent ) override;
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 대시 이펙트(`dashEffect` — 이동 속도 · 무적)를 자기에게 걸고 `duration` 초 기다렸다가 끝납니다.
     * @details 시간이 걸리는 어빌리티의 예입니다(`waitDelay`). 도는 동안 설정의 `BlockTag` 가 공격을 막고, 이동은 플레이어 컨트롤러가
     *          `State.Dashing` 을 보고 바라보는 쪽으로 밀어 줍니다.
     */
    class ArenaDashAbility final : public GameplayAbility
    {
    public:
        void activateAbility( const GameplayEventData* pTriggerEvent ) override;

    private:
        void handleDashFinished();
    };
} // namespace sw

namespace sw
{
    /** @struct ArenaAbilities @brief 카탈로그에 이 게임의 클래스 이름을 등록합니다("MeleeStrike" · "Projectile" · "Dash"). */
    struct ArenaAbilities
    {
        static void registerClasses( AbilityCatalog& catalog );
    };
} // namespace sw
