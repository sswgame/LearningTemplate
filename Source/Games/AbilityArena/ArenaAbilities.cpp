#include "pch.h"

#include "Games/AbilityArena/ArenaAbilities.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Ability/AbilityCatalog.h"
#include "GameFramework/Ability/AbilitySystemComponent.h"

#include "Games/AbilityArena/ArenaControllerComponent.h"
#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    void ArenaMeleeStrikeAbility::activateAbility( const GameplayEventData* pTriggerEvent )
    {
        (void)pTriggerEvent;
        if ( commitAbility() == false )
        {
            cancelAbility();
            return;
        }

        AbilitySystemComponent*       pAbilitySystem = getAbilitySystem();
        const ArenaDirectorComponent* pDirector      = pAbilitySystem != nullptr ? ArenaDirectorComponent::findForUnit( *pAbilitySystem ) : nullptr;
        if ( pDirector != nullptr )
        {
            AbilitySystemComponent* pTarget = pDirector->findNearestHostile( *pAbilitySystem, getParameter( "range", 2.0f ) );
            GameplayEffectSpec      spec    = makeOutgoingSpec( getNameParameter( "damageEffect" ) );
            if ( pTarget != nullptr && spec.isValid() )
            {
                spec.setSetByCallerMagnitude( "Damage", getParameter( "damage", 0.0f ) );
                (void)applyEffectSpecToTarget( spec, pTarget ); // 무적(BlockedTag)이면 걸리지 않는다
            }
        }
        endAbility();
    }

    void ArenaProjectileAbility::activateAbility( const GameplayEventData* pTriggerEvent )
    {
        (void)pTriggerEvent;
        if ( commitAbility() == false )
        {
            cancelAbility();
            return;
        }

        AbilitySystemComponent*         pAbilitySystem = getAbilitySystem();
        const ArenaDirectorComponent*   pDirector      = pAbilitySystem != nullptr ? ArenaDirectorComponent::findForUnit( *pAbilitySystem ) : nullptr;
        const GameObject*               pOwner         = pAbilitySystem != nullptr ? pAbilitySystem->getOwner() : nullptr;
        const ArenaControllerComponent* pController    = pOwner != nullptr ? pOwner->getComponent<ArenaControllerComponent>() : nullptr;
        if ( pDirector != nullptr && pController != nullptr )
        {
            GameplayEffectSpec spec = makeOutgoingSpec( getNameParameter( "damageEffect" ) );
            if ( spec.isValid() )
                spec.setSetByCallerMagnitude( "Damage", getParameter( "damage", 0.0f ) );
            const hashed_string      extraEffect = getNameParameter( "extraEffect" );
            const GameplayEffectSpec extraSpec   = extraEffect.empty() ? GameplayEffectSpec{} : makeOutgoingSpec( extraEffect );
            pDirector->launchProjectile( *pAbilitySystem, pController->getFacing(), spec, extraSpec, getParameter( "speed", 10.0f ), getParameter( "range", 12.0f ) );
        }
        endAbility();
    }

    void ArenaDashAbility::activateAbility( const GameplayEventData* pTriggerEvent )
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
        (void)waitDelay( getParameter( "duration", 0.25f ), SW_DELEGATE_METHOD( Delegate<void()>, &ArenaDashAbility::handleDashFinished, this ) );
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
