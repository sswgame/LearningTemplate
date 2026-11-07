#include "pch.h"

#include "Games/AbilityArena/ArenaAbilities.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

#include "Games/AbilityArena/ArenaGameModeComponent.h"
#include "Games/AbilityArena/ArenaUnitComponent.h"

namespace sw
{
    void ArenaMeleeStrikeAbility::activateAbility( const AbilityEventData* pTriggerEvent )
    {
        (void)pTriggerEvent;
        if ( commitAbility() == false )
        {
            cancelAbility();
            return;
        }

        AbilityComponent*             pAbilitySystem = getAbilitySystem();
        const ArenaGameModeComponent* pGameMode      = pAbilitySystem != nullptr ? ArenaGameModeComponent::findForUnit( *pAbilitySystem ) : nullptr;
        // 사거리 · 피해는 어빌리티 데이터(`<Param>`)가 반드시 적는다.
        const std::optional<float32> range  = getParameter( "range" );
        const std::optional<float32> damage = getParameter( "damage" );
        if ( range.has_value() == false || damage.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ArenaAbilities.AbilityNeedsParamName", "ability '%#' needs <Param name=\"range\"> and <Param name=\"damage\">", getConfig()._id.c_str() );
            cancelAbility();
            return;
        }
        if ( pGameMode != nullptr )
        {
            AbilityComponent* pTarget = pGameMode->findNearestHostile( *pAbilitySystem, *range );
            AbilityEffectSpec spec    = makeOutgoingSpec( getNameParameter( "damageEffect" ) );
            if ( pTarget != nullptr && spec.isValid() )
            {
                spec.setSetByCallerMagnitude( "Damage", *damage );
                (void)applyEffectSpecToTarget( spec, pTarget ); // 무적(BlockedTag)이면 걸리지 않는다
            }
        }
        endAbility();
    }

    void ArenaProjectileAbility::activateAbility( const AbilityEventData* pTriggerEvent )
    {
        (void)pTriggerEvent;
        if ( commitAbility() == false )
        {
            cancelAbility();
            return;
        }

        AbilityComponent*             pAbilitySystem = getAbilitySystem();
        const ArenaGameModeComponent* pGameMode      = pAbilitySystem != nullptr ? ArenaGameModeComponent::findForUnit( *pAbilitySystem ) : nullptr;
        const GameObject*             pOwner         = pAbilitySystem != nullptr ? pAbilitySystem->getOwner() : nullptr;
        const ArenaUnitComponent*     pUnit          = pOwner != nullptr ? pOwner->getComponent<ArenaUnitComponent>() : nullptr;
        // 어느 탄인지는 어빌리티 데이터(`<Param name="projectile">`)가 반드시 적고, 피해 · 속도 · 사거리 · 반지름은 그 탄 정의(게임 서비스 `ProjectileCatalog`)다.
        // 탄의 피해는 숫자라 피해 이펙트의 SetByCaller 로 넘긴다 — 방어 · 저항은 이펙트 계산이 한다.
        const hashed_string      projectileID = getNameParameter( "projectile" );
        const ProjectileCatalog* pProjectiles = game::findSubsystem<const ProjectileCatalog>();
        const ProjectileDef*     pProjectile  = pProjectiles != nullptr && projectileID.empty() == false ? pProjectiles->find( projectileID ) : nullptr;
        if ( pProjectile == nullptr )
        {
            SW_LOG_ERROR_ID( "ArenaAbilities.AbilityNeedsProjectile", "ability '%#' needs <Param name=\"projectile\" text=\"...\"> naming a projectile in the projectile catalog (got '%#')",
                             getConfig()._id.c_str(), projectileID.c_str() );
            cancelAbility();
            return;
        }
        if ( pGameMode != nullptr && pUnit != nullptr )
        {
            AbilityEffectSpec spec = makeOutgoingSpec( getNameParameter( "damageEffect" ) );
            if ( spec.isValid() )
                spec.setSetByCallerMagnitude( "Damage", pProjectile->_damage );
            const hashed_string     extraEffect = getNameParameter( "extraEffect" );
            const AbilityEffectSpec extraSpec   = extraEffect.empty() ? AbilityEffectSpec{} : makeOutgoingSpec( extraEffect );
            pGameMode->launchProjectile( *pAbilitySystem, pUnit->getFacing(), projectileID, spec, extraSpec );
        }
        endAbility();
    }

    void ArenaDashAbility::activateAbility( const AbilityEventData* pTriggerEvent )
    {
        (void)pTriggerEvent;
        if ( commitAbility() == false )
        {
            cancelAbility();
            return;
        }

        const hashed_string dashEffect = getNameParameter( "dashEffect" );
        if ( dashEffect.empty() == false )
            (void)applyEffectSpecToOwner( makeOutgoingSpec( dashEffect ) ); // 이펙트가 없어도 대시는 시간만큼 돈다
        const std::optional<float32> duration = getParameter( "duration" );
        if ( duration.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ArenaAbilities.AbilityNeedsParamName3", "ability '%#' needs <Param name=\"duration\">", getConfig()._id.c_str() );
            endAbility();
            return;
        }
        (void)waitDelay( *duration, SW_DELEGATE_METHOD( Delegate<void()>, &ArenaDashAbility::handleDashFinished, this ) );
    }

    void ArenaDashAbility::handleDashFinished()
    {
        endAbility();
    }

    void ArenaAbilities::registerClasses( AbilityClassRegistry& classes )
    {
        classes.registerAbilityClass<ArenaMeleeStrikeAbility>( "MeleeStrike" );
        classes.registerAbilityClass<ArenaProjectileAbility>( "Projectile" );
        classes.registerAbilityClass<ArenaDashAbility>( "Dash" );
    }
} // namespace sw
