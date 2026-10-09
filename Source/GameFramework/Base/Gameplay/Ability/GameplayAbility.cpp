#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/GameplayAbility.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityTask.h"

namespace sw
{
    SW_LOG_CALLER( "GameplayAbility" );

    GameplayAbility::GameplayAbility()
        : _config{}
        , _triggerEventData{}
        , _listTask{}
        , _pAbilitySystem{ nullptr }
        , _specHandle{}
        , _level{ 1 }
        , _activationSerial{ 0 }
        , _taskTickDepth{ 0 }
        , _bActive{ SW_FALSE }
        , _bTriggeredByEvent{ SW_FALSE }
        , _bInputPressed{ SW_FALSE }
        , _bEnding{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    GameplayAbility::~GameplayAbility()
    {
        // 컴포넌트는 지우기 전에 끝내고(`retireAbilitySpec`) 작업을 비운다. 여기서는 남은 작업이 걸어 둔 구독만 확실히 뗀다.
        for ( unique_ptr<AbilityTask>& pTask : _listTask )
        {
            if ( pTask != nullptr )
                pTask->finish( true );
        }
        _listTask.clear();
    }

    bool GameplayAbility::canActivateAbility( const GameplayEventData* pTriggerEvent ) const
    {
        (void)pTriggerEvent;
        return true;
    }

    void GameplayAbility::onEndAbility( bool bWasCancelled )
    {
        (void)bWasCancelled;
    }

    void GameplayAbility::onTickAbility( float32 deltaTime )
    {
        (void)deltaTime;
    }

    void GameplayAbility::onGranted()
    {
    }

    void GameplayAbility::onRemoved()
    {
    }

    void GameplayAbility::onInputPressed()
    {
    }

    void GameplayAbility::onInputReleased()
    {
    }

    bool GameplayAbility::commitAbility()
    {
        if ( _pAbilitySystem == nullptr || isActive() == false )
            return false;
        if ( isOnCooldown() || canAffordCost() == false )
            return false;

        if ( _config._pCostEffect != nullptr )
            (void)_pAbilitySystem->applyGameplayEffectSpecToSelf( _pAbilitySystem->makeOutgoingSpec( _config._pCostEffect, _level ) ); // 막혀도 커밋은 선다
        if ( _config._pCooldownEffect != nullptr )
            // 막혀도 커밋은 선다 — 쿨다운 효과가 면역으로 막히면 쿨다운 없이 간다
            (void)_pAbilitySystem->applyGameplayEffectSpecToSelf( _pAbilitySystem->makeOutgoingSpec( _config._pCooldownEffect, _level ) );
        return true;
    }

    bool GameplayAbility::canAffordCost() const
    {
        if ( _pAbilitySystem == nullptr || _config._pCostEffect == nullptr )
            return true;
        return _pAbilitySystem->canApplyAttributeModifiers( _pAbilitySystem->makeOutgoingSpec( _config._pCostEffect, _level ) );
    }

    bool GameplayAbility::isOnCooldown() const
    {
        if ( _pAbilitySystem == nullptr || _config._pCooldownEffect == nullptr )
            return false;
        return _pAbilitySystem->hasAnyMatchingTags( _config._pCooldownEffect->_grantedTags );
    }

    float32 GameplayAbility::getCooldownRemaining() const
    {
        if ( _pAbilitySystem == nullptr || _config._pCooldownEffect == nullptr )
            return 0.0f;
        return _pAbilitySystem->findLongestRemainingTimeWithGrantedTags( _config._pCooldownEffect->_grantedTags );
    }

    void GameplayAbility::endAbility()
    {
        finishActivation( false );
    }

    void GameplayAbility::cancelAbility()
    {
        finishActivation( true );
    }

    float32 GameplayAbility::getParameter( const hashed_string& name, float32 fallback ) const
    {
        const auto mapIter = _config._mapParameter.find( name );
        return mapIter != _config._mapParameter.end() ? mapIter->second : fallback;
    }

    hashed_string GameplayAbility::getNameParameter( const hashed_string& name ) const
    {
        const auto mapIter = _config._mapNameParameter.find( name );
        return mapIter != _config._mapNameParameter.end() ? mapIter->second : hashed_string{};
    }

    GameObject* GameplayAbility::getAvatar() const
    {
        return _pAbilitySystem != nullptr ? _pAbilitySystem->getOwner() : nullptr;
    }

    GameplayEffectSpec GameplayAbility::makeOutgoingSpec( const hashed_string& effectId ) const
    {
        if ( _pAbilitySystem == nullptr )
            return GameplayEffectSpec{};
        return _pAbilitySystem->makeOutgoingSpecById( effectId, _level );
    }

    ActiveEffectHandle GameplayAbility::applyEffectSpecToOwner( const GameplayEffectSpec& spec )
    {
        if ( _pAbilitySystem == nullptr )
            return ActiveEffectHandle{};
        return _pAbilitySystem->applyGameplayEffectSpecToSelf( spec );
    }

    ActiveEffectHandle GameplayAbility::applyEffectSpecToTarget( const GameplayEffectSpec& spec, AbilitySystemComponent* pTarget )
    {
        if ( _pAbilitySystem == nullptr )
            return ActiveEffectHandle{};
        return _pAbilitySystem->applyGameplayEffectSpecToTarget( spec, pTarget );
    }

    AbilityTask* GameplayAbility::addTask( unique_ptr<AbilityTask> pTask )
    {
        if ( pTask == nullptr || isActive() == false || _bEnding == SW_TRUE )
            return nullptr;

        AbilityTask* pRaw = pTask.get();
        pRaw->_pAbility   = this;
        _listTask.push_back( std::move( pTask ) );

        // 시작 안에서 바로 끝나 콜백이 어빌리티를 끝낼 수 있다 — 그동안 목록에서 지우지 않는다(돌려줄 포인터가 살아 있어야 한다).
        ++_taskTickDepth;
        pRaw->onActivate();
        --_taskTickDepth;
        removeFinishedTasks();
        return pRaw;
    }

    AbilityTask* GameplayAbility::waitDelay( float32 seconds, const Delegate<void()>& onFinished )
    {
        return addTask( make_unique<AbilityTaskWaitDelay>( seconds, onFinished ) );
    }

    AbilityTask* GameplayAbility::waitGameplayEvent( TagID eventTag, const Delegate<void( const GameplayEventData& )>& onEvent, bool bOnlyTriggerOnce )
    {
        return addTask( make_unique<AbilityTaskWaitGameplayEvent>( eventTag, onEvent, bOnlyTriggerOnce ) );
    }

    uint32 GameplayAbility::getActiveTaskCount() const
    {
        uint32 activeCount = 0;
        for ( const unique_ptr<AbilityTask>& pTask : _listTask )
        {
            if ( pTask != nullptr && pTask->isFinished() == false )
                ++activeCount;
        }
        return activeCount;
    }

    void GameplayAbility::bindToAbilitySystem( AbilitySystemComponent* pAbilitySystem, AbilitySpecHandle specHandle, int32 level )
    {
        _pAbilitySystem = pAbilitySystem;
        _specHandle     = specHandle;
        _level          = level;
    }

    void GameplayAbility::beginActivation( const GameplayEventData* pTriggerEvent )
    {
        _bActive           = SW_TRUE;
        _bEnding           = SW_FALSE;
        _bTriggeredByEvent = pTriggerEvent != nullptr ? SW_TRUE : SW_FALSE;
        _triggerEventData  = pTriggerEvent != nullptr ? *pTriggerEvent : GameplayEventData{};
        ++_activationSerial;

        if ( _pAbilitySystem != nullptr )
            _pAbilitySystem->handleAbilityActivated( *this );
        activateAbility( pTriggerEvent );
    }

    void GameplayAbility::finishActivation( bool bWasCancelled )
    {
        if ( isActive() == false || _bEnding == SW_TRUE )
            return;
        _bEnding = SW_TRUE;

        endAllTasks();
        _bActive = SW_FALSE;
        if ( _pAbilitySystem != nullptr )
            _pAbilitySystem->handleAbilityEnded( *this, bWasCancelled );
        onEndAbility( bWasCancelled );
        _bEnding = SW_FALSE;
    }

    void GameplayAbility::tickAbility( float32 deltaTime )
    {
        if ( isActive() == false )
            return;

        // 작업 콜백이 어빌리티를 끝내거나 새 작업을 붙일 수 있다 — 인덱스로 돌고(붙인 작업은 다음 틱부터), 지우기는 다 돈 뒤에 한다.
        const uint32 serial    = _activationSerial;
        const size_t taskCount = _listTask.size();
        ++_taskTickDepth;
        for ( size_t taskIndex = 0; taskIndex < taskCount && taskIndex < _listTask.size(); ++taskIndex )
        {
            AbilityTask* pTask = _listTask[taskIndex].get();
            if ( pTask != nullptr && pTask->isFinished() == false )
                pTask->onTick( deltaTime );
            if ( isActive() == false || serial != _activationSerial )
                break;
        }
        --_taskTickDepth;
        removeFinishedTasks();

        // 작업이 끝냈거나 다시 시작했으면 이번 틱의 `onTickAbility` 는 건너뛴다 — 끝난 발동에 틱을 주지 않는다.
        if ( isActive() && serial == _activationSerial )
            onTickAbility( deltaTime );
    }

    void GameplayAbility::setInputPressed( bool bPressed )
    {
        const uint8 bNewPressed = bPressed ? SW_TRUE : SW_FALSE;
        if ( _bInputPressed == bNewPressed )
            return;
        _bInputPressed = bNewPressed;
        if ( isActive() == false )
            return;
        if ( bPressed )
            onInputPressed();
        else
            onInputReleased();
    }

    void GameplayAbility::removeFinishedTasks()
    {
        if ( _taskTickDepth > 0 )
            return;
        for ( size_t taskIndex = _listTask.size(); taskIndex > 0; --taskIndex )
        {
            const unique_ptr<AbilityTask>& pTask = _listTask[taskIndex - 1];
            if ( pTask == nullptr || pTask->isFinished() )
                _listTask.erase( _listTask.begin() + static_cast<ptrdiff_t>( taskIndex - 1 ) );
        }
    }

    void GameplayAbility::endAllTasks()
    {
        // 끝내는 순서는 붙인 순서다. `finish` 가 구독을 떼므로 그 뒤로는 어떤 콜백도 오지 않는다.
        const size_t taskCount = _listTask.size();
        for ( size_t taskIndex = 0; taskIndex < taskCount && taskIndex < _listTask.size(); ++taskIndex )
        {
            AbilityTask* pTask = _listTask[taskIndex].get();
            if ( pTask != nullptr )
                pTask->finish( true );
        }
        removeFinishedTasks();
    }

    void ApplyEffectsAbility::activateAbility( const GameplayEventData* pTriggerEvent )
    {
        if ( commitAbility() == false )
        {
            cancelAbility();
            return;
        }

        AbilitySystemComponent* pAbilitySystem = getAbilitySystem();
        const hashed_string     selfEffect     = getNameParameter( "selfEffect" );
        if ( pAbilitySystem != nullptr && selfEffect.empty() == false )
            (void)applyEffectSpecToOwner( makeOutgoingSpec( selfEffect ) ); // 대상 태그에 막혀도 어빌리티는 끝난다

        const hashed_string targetEffect = getNameParameter( "targetEffect" );
        if ( pAbilitySystem != nullptr && targetEffect.empty() == false && pTriggerEvent != nullptr )
        {
            // 상대는 이벤트의 "다른 쪽" 이다 — 받은 이벤트(`_target` 이 주인 자신, 피격)면 일으킨 쪽, 보낸 이벤트면 받은 쪽.
            GameObject*             pOwner        = pAbilitySystem->getOwner();
            GameObjectManager*      pManager      = pOwner != nullptr ? pOwner->getManager() : nullptr;
            const bool              bTargetIsSelf = pOwner != nullptr && ( pTriggerEvent->_target.isValid() == false || pTriggerEvent->_target == pOwner->getHandle() );
            const GameObjectHandle  targetHandle  = bTargetIsSelf ? pTriggerEvent->_instigator : pTriggerEvent->_target;
            GameObject*             pTargetObject = pManager != nullptr ? pManager->resolveGameObject( targetHandle ) : nullptr;
            AbilitySystemComponent* pTarget       = pTargetObject != nullptr ? pTargetObject->getComponent<AbilitySystemComponent>() : nullptr;
            if ( pTarget != nullptr )
                (void)applyEffectSpecToTarget( makeOutgoingSpec( targetEffect ), pTarget ); // 효과 핸들은 쓰지 않는다 — 막히면 걸리지 않을 뿐이다
            else
                SW_LOG_WARNING( "ApplyEffects '%#': the trigger event has no target with an AbilitySystemComponent", getConfig()._id.c_str() );
        }
        endAbility();
    }
} // namespace sw
