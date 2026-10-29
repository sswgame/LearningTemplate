#include "pch.h"

#include "Games/AbilityArena/ArenaAbilities.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityComponent.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"
#include "Games/AbilityArena/ArenaUnitComponent.h"

namespace sw
{
    SW_LOG_CALLER( "ArenaAbilities" );

    void ArenaMeleeStrikeAbility::activateAbility( const AbilityEventData* pTriggerEvent )
    {
        (void)pTriggerEvent;
        if ( commitAbility() == false )
        {
            cancelAbility();
            return;
        }

        AbilityComponent*             pAbilitySystem = getAbilitySystem();
        const ArenaDirectorComponent* pDirector      = pAbilitySystem != nullptr ? ArenaDirectorComponent::findForUnit( *pAbilitySystem ) : nullptr;
        // 사거리 · 피해는 어빌리티 데이터(`<Param>`)가 반드시 적는다.
        const std::optional<float32> range  = getParameter( "range" );
        const std::optional<float32> damage = getParameter( "damage" );
        if ( range.has_value() == false || damage.has_value() == false )
        {
            SW_LOG_ERROR( "ability '%#' needs <Param name=\"range\"> and <Param name=\"damage\">", getConfig()._id.c_str() );
            cancelAbility();
            return;
        }
        if ( pDirector != nullptr )
        {
            AbilityComponent* pTarget = pDirector->findNearestHostile( *pAbilitySystem, *range );
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
        const ArenaDirectorComponent* pDirector      = pAbilitySystem != nullptr ? ArenaDirectorComponent::findForUnit( *pAbilitySystem ) : nullptr;
        const GameObject*             pOwner         = pAbilitySystem != nullptr ? pAbilitySystem->getOwner() : nullptr;
        const ArenaUnitComponent*     pUnit          = pOwner != nullptr ? pOwner->getComponent<ArenaUnitComponent>() : nullptr;
        // 피해 · 속도 · 사거리는 어빌리티 데이터(`<Param>`)가 반드시 적는다.
        const std::optional<float32> damage    = getParameter( "damage" );
        const std::optional<float32> speed     = getParameter( "speed" );
        const std::optional<float32> range     = getParameter( "range" );
        const bool                   bComplete = damage.has_value() && speed.has_value() && range.has_value();
        if ( bComplete == false )
        {
            SW_LOG_ERROR( "ability '%#' needs <Param name=\"damage\">, \"speed\" and \"range\"", getConfig()._id.c_str() );
            cancelAbility();
            return;
        }
        if ( pDirector != nullptr && pUnit != nullptr )
        {
            AbilityEffectSpec spec = makeOutgoingSpec( getNameParameter( "damageEffect" ) );
            if ( spec.isValid() )
                spec.setSetByCallerMagnitude( "Damage", *damage );
            const hashed_string     extraEffect = getNameParameter( "extraEffect" );
            const AbilityEffectSpec extraSpec   = extraEffect.empty() ? AbilityEffectSpec{} : makeOutgoingSpec( extraEffect );
            pDirector->launchProjectile( *pAbilitySystem, pUnit->getFacing(), spec, extraSpec, *speed, *range );
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
            SW_LOG_ERROR( "ability '%#' needs <Param name=\"duration\">", getConfig()._id.c_str() );
            endAbility();
            return;
        }
        (void)waitDelay( *duration, SW_DELEGATE_METHOD( Delegate<void()>, &ArenaDashAbility::handleDashFinished, this ) );
    }

    void ArenaDashAbility::handleDashFinished()
    {
        endAbility();
    }

    void ArenaAbilities::registerClasses( AbilityCatalog& catalog )
    {
        catalog.registerAbilityClass<ArenaMeleeStrikeAbility>( "MeleeStrike" );
        catalog.registerAbilityClass<ArenaProjectileAbility>( "Projectile" );
        catalog.registerAbilityClass<ArenaDashAbility>( "Dash" );
    }
} // namespace sw
