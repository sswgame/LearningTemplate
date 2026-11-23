#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/MinigameRewardComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEffect.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/ReputationComponent.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

namespace sw
{
    MinigameRewardComponent::MinigameRewardComponent()
        : _listReward{}
        , _playerState{}
        , _listCurrency{}
        , _listReputation{}
        , _listQuest{}
        , _finishedSubscription{}
        , _appliedCount{}
    {
    }

    MinigameRewardComponent::~MinigameRewardComponent() = default;

    void MinigameRewardComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        if ( pFlow == nullptr )
            return;
        _finishedSubscription = pFlow->onFinished().add( SW_DELEGATE_METHOD( Delegate<void( const MinigameResult& )>, &MinigameRewardComponent::onMinigameFinished, this ) );
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

    void MinigameRewardComponent::setPlayerRewardTable( GameObjectHandle playerState, vector<MinigameRewardCurrency> listCurrency, vector<MinigameRewardReputation> listReputation, vector<MinigameRewardQuest> listQuest )
    {
        _playerState    = playerState;
        _listCurrency   = std::move( listCurrency );
        _listReputation = std::move( listReputation );
        _listQuest      = std::move( listQuest );
    }

    template <typename T>
    T* MinigameRewardComponent::findOnPlayerState( const MinigameResult& result, const MinigameResultValue& value ) const
    {
        const GameObject* const pOwner       = getOwner();
        const GameObject* const pPlayerState = pOwner != nullptr && pOwner->getManager() != nullptr ? pOwner->getManager()->resolveGameObject( _playerState ) : nullptr;
        T* const                pFound       = pPlayerState != nullptr ? pPlayerState->getComponent<T>() : nullptr;
        if ( pFound != nullptr && pFound->isOpen() )
            return pFound;
        SW_LOG_ERROR_ID( "MinigameReward.NoReceiver", "Minigame '%#' result value '%#': the player state object '%#' has no open %# - the reward is dropped", result._minigame.c_str(),
                         value._name.c_str(), pPlayerState != nullptr ? pPlayerState->getName().c_str() : "(none)", T::StaticType()->_name.c_str() );
        return nullptr;
    }

    bool MinigameRewardComponent::applyEffect( const MinigameResult& result, const MinigameResultValue& value, const MinigameRewardEffect& reward )
    {
        static const hashed_string    s_amountName( kAmountName );
        GameObject* const             pOwner   = getOwner();
        AbilitySystemComponent* const pAbility = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        const AbilityCatalog*         pCatalog = pAbility != nullptr ? pAbility->findCatalog() : nullptr;
        if ( pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameReward.NoAbilitySystem", "Minigame '%#' result value '%#': '%#' has no AbilitySystemComponent with an AbilityCatalog - the reward is dropped",
                             result._minigame.c_str(), value._name.c_str(), pOwner != nullptr ? pOwner->getName().c_str() : "(no owner)" );
            return false;
        }
        const shared_ptr<const AbilityEffectDef> pDef = pCatalog->findEffect( reward._effect.key() );
        if ( pDef == nullptr )
        {
            SW_LOG_ERROR_ID( "MinigameReward.UnknownEffect", "Minigame '%#' result value '%#' points at effect '%#', which is not in the AbilityCatalog",
                             result._minigame.c_str(), value._name.c_str(), reward._effect.c_str() );
            return false;
        }
        AbilityEffectSpec spec = pAbility->makeOutgoingSpec( pDef );
        spec.setSetByCallerMagnitude( s_amountName, static_cast<float32>( value._amount ) );
        if ( pAbility->applyGameplayEffectSpecToSelf( spec ).isValid() == false )
        {
            SW_LOG_ERROR_ID( "MinigameReward.EffectRejected", "Minigame '%#' result value '%#': effect '%#' was not applied (blocked by the target's tags)", result._minigame.c_str(),
                             value._name.c_str(), reward._effect.c_str() );
            return false;
        }
        return true;
    }

    bool MinigameRewardComponent::applyResult( const MinigameResult& result )
    {
        bool       bAllApplied = true;
        const auto count       = [this, &bAllApplied]( bool bApplied )
        {
            if ( bApplied )
                _appliedCount.add();
            else
                bAllApplied = false;
        };
        for ( const MinigameResultValue& value : result._listValue )
        {
            bool bInTable = false;
            for ( const MinigameRewardEffect& reward : _listReward )
            {
                if ( reward._value != value._name )
                    continue;
                bInTable = true;
                count( applyEffect( result, value, reward ) );
            }
            for ( const MinigameRewardCurrency& reward : _listCurrency )
            {
                if ( reward._value != value._name )
                    continue;
                bInTable                       = true;
                WalletComponent* const pWallet = findOnPlayerState<WalletComponent>( result, value );
                if ( pWallet != nullptr && value._amount > 0 )
                    pWallet->getWallet().add( reward._currency, value._amount );
                else if ( pWallet != nullptr )
                    SW_LOG_ERROR_ID( "MinigameReward.AmountNotPositive", "Minigame '%#' result value '%#' is %# - a currency reward must be above 0", result._minigame.c_str(), value._name.c_str(),
                                     value._amount );
                count( pWallet != nullptr && value._amount > 0 );
            }
            const int32 amount = static_cast<int32>( MathUtil::clamp<int64>( value._amount, -MathUtil::kMaxInt32, MathUtil::kMaxInt32 ) );
            for ( const MinigameRewardReputation& reward : _listReputation )
            {
                if ( reward._value != value._name )
                    continue;
                bInTable                               = true;
                ReputationComponent* const pReputation = findOnPlayerState<ReputationComponent>( result, value );
                if ( pReputation != nullptr )
                    (void)pReputation->getReputation().changeValue( reward._faction, amount ); // 바뀐 양은 세력 범위가 정한다
                count( pReputation != nullptr );
            }
            for ( const MinigameRewardQuest& reward : _listQuest )
            {
                if ( reward._value != value._name )
                    continue;
                bInTable                           = true;
                QuestLogComponent* const pQuestLog = findOnPlayerState<QuestLogComponent>( result, value );
                if ( pQuestLog != nullptr )
                    (void)pQuestLog->getQuestLog().notify( reward._kind, reward._target, amount ); // 그 목표를 세는 퀘스트가 없으면 0 이다
                count( pQuestLog != nullptr );
            }
            if ( bInTable )
                continue;
            SW_LOG_ERROR_ID( "MinigameReward.UnknownValue", "Minigame '%#' result value '%#' has no row in the reward tables of '%#' - add a row for it", result._minigame.c_str(),
                             value._name.c_str(), getOwner() != nullptr ? getOwner()->getName().c_str() : "(no owner)" );
            bAllApplied = false;
        }
        return bAllApplied;
    }
} // namespace sw
