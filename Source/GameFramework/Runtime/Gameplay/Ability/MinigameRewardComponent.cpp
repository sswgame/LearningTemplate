#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/MinigameRewardComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEffect.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"

namespace sw
{
    MinigameRewardComponent::MinigameRewardComponent()
        : _listReward{}
        , _finishedSubscription{}
        , _appliedCount{ 0 }
    {
    }

    MinigameRewardComponent::~MinigameRewardComponent() = default;

    void MinigameRewardComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        if ( pFlow == nullptr )
            return;
        _finishedSubscription = pFlow->onFinished().add( MinigameFlow::FinishedDelegate::delegate_type::create<&MinigameRewardComponent::onMinigameFinished>( this ) );
    }

    void MinigameRewardComponent::onEndPlay()
    {
        _finishedSubscription.reset();
        Component::onEndPlay();
    }

    void MinigameRewardComponent::onMinigameFinished( const MinigameResult& result )
    {
        (void)applyResult( result );
    }

    const MinigameRewardEffect* MinigameRewardComponent::findReward( const hashed_string& value ) const
    {
        for ( const MinigameRewardEffect& reward : _listReward )
        {
            if ( reward._value == value )
                return &reward;
        }
        return nullptr;
    }

    bool MinigameRewardComponent::applyResult( const MinigameResult& result )
    {
        if ( result._listValue.empty() )
            return true;
        GameObject* const             pOwner   = getOwner();
        AbilitySystemComponent* const pAbility = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        const AbilityCatalog*         pCatalog = pAbility != nullptr ? pAbility->findCatalog() : nullptr;
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameReward.NoAbilitySystem", "Minigame '%#' result: '%#' has no AbilitySystemComponent with an AbilityCatalog - %# reward values are dropped",
                             result._minigame.c_str(), pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)", result._listValue.size() );
            return false;
        }
        static const hashed_string s_amountName( kAmountName );
        bool                       bAllApplied = true;
        for ( const MinigameResultValue& value : result._listValue )
        {
            const MinigameRewardEffect* const pReward = findReward( value._name );
            if ( pReward == nullptr )
            {
                SW_LOG_ERROR_ID( "MinigameReward.UnknownValue", "Minigame '%#' result value '%#' has no row in the reward table of '%#' - add a MinigameRewardEffect for it",
                                 result._minigame.c_str(), value._name.c_str(), pOwner->getName().c_str() );
                bAllApplied = false;
                continue;
            }
            const shared_ptr<const AbilityEffectDef> pDef = pCatalog->findEffect( pReward->_effect.key() );
            if ( pDef == nullptr )
            {
                SW_LOG_ERROR_ID( "MinigameReward.UnknownEffect", "Minigame '%#' result value '%#' points at effect '%#', which is not in the AbilityCatalog",
                                 result._minigame.c_str(), value._name.c_str(), pReward->_effect.c_str() );
                bAllApplied = false;
                continue;
            }
            AbilityEffectSpec spec = pAbility->makeOutgoingSpec( pDef );
            spec.setSetByCallerMagnitude( s_amountName, static_cast<float32>( value._amount ) );
            if ( pAbility->applyGameplayEffectSpecToSelf( spec ).isValid() == false )
            {
                SW_LOG_ERROR_ID( "MinigameReward.EffectRejected", "Minigame '%#' result value '%#': effect '%#' was not applied (blocked by the target's tags)", result._minigame.c_str(),
                                 value._name.c_str(), pReward->_effect.c_str() );
                bAllApplied = false;
                continue;
            }
            ++_appliedCount;
        }
        return bAllApplied;
    }
} // namespace sw
