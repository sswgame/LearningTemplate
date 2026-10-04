#include "pch.h"

#include "GameFramework/Ability/AbilityTask.h"

#include "GameFramework/Ability/AbilitySystemComponent.h"
#include "GameFramework/Ability/GameplayAbility.h"

namespace sw
{
    AbilityTask::AbilityTask()
        : _pAbility{ nullptr }
        , _bFinished{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void AbilityTask::onActivate()
    {
    }

    void AbilityTask::onTick( float32 deltaTime )
    {
        (void)deltaTime;
    }

    void AbilityTask::onDestroy( bool bAbilityEnded )
    {
        (void)bAbilityEnded;
    }

    void AbilityTask::endTask()
    {
        finish( false );
    }

    void AbilityTask::finish( bool bAbilityEnded )
    {
        if ( _bFinished == SW_TRUE )
            return;
        _bFinished = SW_TRUE;
        onDestroy( bAbilityEnded );
    }

    AbilityTaskWaitDelay::AbilityTaskWaitDelay( float32 seconds, const Delegate<void()>& onFinished )
        : _onFinished{ onFinished }
        , _delay{ seconds }
    {
    }

    void AbilityTaskWaitDelay::onActivate()
    {
        // 0 이하의 지연은 다음 틱이 아니라 지금 끝난다 — "바로" 를 뜻한 호출이 한 프레임 늦지 않게.
        if ( _delay.isActive() == false )
            onTick( 0.0f );
    }

    void AbilityTaskWaitDelay::onTick( float32 deltaTime )
    {
        if ( isFinished() )
            return;
        _delay.tick( deltaTime );
        if ( _delay.isActive() )
            return;

        // 끝을 먼저 적고 부른다 — 콜백이 어빌리티를 끝내도(작업 정리) 이 작업이 다시 불리지 않는다.
        const Delegate<void()> callback = _onFinished;
        endTask();
        if ( callback.isBound() )
            callback();
    }

    AbilityTaskWaitGameplayEvent::AbilityTaskWaitGameplayEvent( TagID eventTag, const Delegate<void( const GameplayEventData& )>& onEvent, bool bOnlyTriggerOnce )
        : _onEvent{ onEvent }
        , _eventTag{ eventTag }
        , _subscription{}
        , _receivedCount{ 0 }
        , _bOnlyTriggerOnce{ static_cast<uint8>( bOnlyTriggerOnce ? SW_TRUE : SW_FALSE ) }
        , _reserved{ 0 }
    {
    }

    void AbilityTaskWaitGameplayEvent::onActivate()
    {
        GameplayAbility*        pAbility       = getAbility();
        AbilitySystemComponent* pAbilitySystem = pAbility != nullptr ? pAbility->getAbilitySystem() : nullptr;
        if ( pAbilitySystem == nullptr )
        {
            endTask();
            return;
        }
        _subscription = pAbilitySystem->registerGameplayEvent(
            _eventTag, SW_DELEGATE_METHOD( AbilitySystemComponent::GameplayEventDelegate, &AbilityTaskWaitGameplayEvent::handleEvent, this ) );
    }

    void AbilityTaskWaitGameplayEvent::onDestroy( bool bAbilityEnded )
    {
        (void)bAbilityEnded;
        GameplayAbility*        pAbility       = getAbility();
        AbilitySystemComponent* pAbilitySystem = pAbility != nullptr ? pAbility->getAbilitySystem() : nullptr;
        if ( pAbilitySystem != nullptr && _subscription.isValid() )
            pAbilitySystem->unregisterGameplayEvent( _eventTag, _subscription );
        _subscription = DelegateHandle{};
    }

    void AbilityTaskWaitGameplayEvent::handleEvent( const GameplayEventData& payload )
    {
        if ( isFinished() )
            return;
        ++_receivedCount;

        const Delegate<void( const GameplayEventData& )> callback = _onEvent;
        if ( _bOnlyTriggerOnce == SW_TRUE )
            endTask();
        if ( callback.isBound() )
            callback( payload );
    }
} // namespace sw
