/**
 * @file AbilityComponentAbility.cpp
 * @brief `AbilityComponent` 의 어빌리티 · 게임플레이 이벤트 부분입니다.
 * @details 어빌리티 부여 · 발동 · 취소 · 입력 · 활성 어빌리티 틱과 게임플레이 이벤트 · 큐 구독, 체력 읽기를 맡습니다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Module/ModuleLibraryUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Ability/Ability.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityEvents.h"
#include "GameFramework/Base/UI/Marker/DamageNumberComponent.h"

namespace sw
{
    SW_LOG_CALLER( "AbilityComponentAbility" );

    namespace
    {
        /** @brief @p container 의 태그 중 하나라도 @p query 의 태그이거나 그 하위 태그이면 true 입니다. */
        bool matchesAnyTag( const TagContainer& container, const TagContainer& query )
        {
            for ( const TagID& queryTag : query.getTags() )
            {
                if ( container.hasTag( queryTag, false ) )
                    return true;
            }
            return false;
        }
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 5) 어빌리티
    // ------------------------------------------------------------------------------
    AbilitySpecHandle AbilityComponent::giveAbility( unique_ptr<Ability> pAbility, int32 level, int32 inputID )
    {
        if ( pAbility == nullptr )
            return AbilitySpecHandle{};

        unique_ptr<AbilitySpec> pSpec = make_unique<AbilitySpec>();
        pSpec->_handle                = AbilitySpecHandle{ ++_nextHandleID };
        pSpec->_pAbility              = std::move( pAbility );
        pSpec->_level                 = level;
        pSpec->_inputID               = inputID;

        Ability*                pRaw   = pSpec->_pAbility.get();
        const AbilitySpecHandle handle = pSpec->_handle;
        pRaw->bindToAbilitySystem( this, handle, level );
        _listAbilitySpec.push_back( std::move( pSpec ) );

        const ScopedListLock lock{ *this };
        pRaw->onGranted();
        if ( pRaw->getConfig()._bActivateOnGranted == SW_TRUE )
            (void)tryActivateAbility( handle ); // 패시브가 조건에 막히면 다음 부여나 발동을 기다린다
        return handle;
    }

    AbilitySpecHandle AbilityComponent::giveAbilityByID( const hashed_string& abilityID, int32 level, int32 inputID )
    {
        const AbilityCatalog* pCatalog = findCatalog();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING( "giveAbilityByID '%#': no AbilityCatalog", abilityID.c_str() );
            return AbilitySpecHandle{};
        }
        return giveAbility( pCatalog->createAbility( abilityID ), level, inputID );
    }

    bool AbilityComponent::clearAbility( AbilitySpecHandle handle )
    {
        const ScopedListLock lock{ *this };
        AbilitySpec*         pSpec = findAbilitySpecMutable( handle );
        if ( pSpec == nullptr )
            return false;
        retireAbilitySpec( *pSpec );
        return true;
    }

    void AbilityComponent::clearAllAbilities()
    {
        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            retireAbilitySpec( *_listAbilitySpec[specIndex] );
        }
    }

    AbilityActivationResult AbilityComponent::tryActivateAbility( AbilitySpecHandle handle, const AbilityEventData* pTriggerEvent )
    {
        const ScopedListLock          lock{ *this };
        const AbilityActivationResult result = canActivateAbility( handle, pTriggerEvent );
        if ( result != AbilityActivationResult::Activated )
            return result;

        AbilitySpec* pSpec    = findAbilitySpecMutable( handle );
        Ability*     pAbility = pSpec->_pAbility.get(); // canActivateAbility 가 스펙과 인스턴스를 이미 확인했다
        if ( pAbility->isActive() )
            pAbility->cancelAbility(); // 다시 발동하기: 실행 중인 발동을 끝내고 새로 시작한다
        // 취소 알림이 스펙을 제거했을 수 있다.
        if ( pSpec->_bPendingRemove == SW_TRUE )
            return AbilityActivationResult::InvalidSpec;
        pAbility->beginActivation( pTriggerEvent );
        return AbilityActivationResult::Activated;
    }

    AbilityActivationResult AbilityComponent::canActivateAbility( AbilitySpecHandle handle, const AbilityEventData* pTriggerEvent ) const
    {
        const AbilitySpec* pSpec = findAbilitySpec( handle );
        if ( pSpec == nullptr || pSpec->_pAbility == nullptr )
            return AbilityActivationResult::InvalidSpec;

        const Ability&       ability = *pSpec->_pAbility;
        const AbilityConfig& config  = ability.getConfig();
        if ( ability.isActive() && config._bRetriggerInstancedAbility == SW_FALSE )
            return AbilityActivationResult::AlreadyActive;
        if ( isAbilityBlocked( config._abilityTags ) )
            return AbilityActivationResult::BlockedByTag;
        if ( _ownedTagCounts.hasAllMatchingTags( config._activationRequiredTags ) == false )
            return AbilityActivationResult::MissingRequiredTag;
        if ( _ownedTagCounts.hasAnyMatchingTags( config._activationBlockedTags ) )
            return AbilityActivationResult::BlockedByTag;
        if ( ability.isOnCooldown() )
            return AbilityActivationResult::OnCooldown;
        if ( ability.canAffordCost() == false )
            return AbilityActivationResult::CannotAffordCost;
        if ( ability.canActivateAbility( pTriggerEvent ) == false )
            return AbilityActivationResult::RejectedByAbility;
        return AbilityActivationResult::Activated;
    }

    bool AbilityComponent::tryActivateAbilitiesByTag( const TagContainer& tags )
    {
        const ScopedListLock      lock{ *this };
        vector<AbilitySpecHandle> listCandidate;
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            const bool bMatches = pSpec->_bPendingRemove == SW_FALSE &&
                                  matchesAnyTag( pSpec->_pAbility->getConfig()._abilityTags, tags );
            if ( bMatches )
                listCandidate.push_back( pSpec->_handle );
        }

        bool bAnyActivated = false;
        for ( const AbilitySpecHandle& handle : listCandidate )
        {
            if ( tryActivateAbility( handle ) == AbilityActivationResult::Activated )
                bAnyActivated = true;
        }
        return bAnyActivated;
    }

    AbilitySpecHandle AbilityComponent::findAbilitySpecHandle( const hashed_string& abilityID ) const
    {
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            if ( pSpec->_bPendingRemove == SW_FALSE && pSpec->_pAbility->getConfig()._id == abilityID )
                return pSpec->_handle;
        }
        return AbilitySpecHandle{};
    }

    Ability* AbilityComponent::findAbility( AbilitySpecHandle handle ) const
    {
        const AbilitySpec* pSpec = findAbilitySpec( handle );
        return pSpec != nullptr ? pSpec->_pAbility.get() : nullptr;
    }

    bool AbilityComponent::isAbilityActive( AbilitySpecHandle handle ) const
    {
        const Ability* pAbility = findAbility( handle );
        return pAbility != nullptr && pAbility->isActive();
    }

    void AbilityComponent::cancelAbility( AbilitySpecHandle handle )
    {
        const ScopedListLock lock{ *this };
        Ability*             pAbility = findAbility( handle );
        if ( pAbility != nullptr )
            pAbility->cancelAbility();
    }

    void AbilityComponent::cancelAbilitiesWithTags( const TagContainer& tags, const Ability* pIgnore )
    {
        if ( tags.getTagCount() == 0 )
            return;

        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec& spec     = *_listAbilitySpec[specIndex];
            Ability*     pAbility = spec._pAbility.get();
            const bool   bSkip    = spec._bPendingRemove == SW_TRUE || pAbility == pIgnore || pAbility->isActive() == false;
            if ( bSkip )
                continue;
            if ( matchesAnyTag( pAbility->getConfig()._abilityTags, tags ) )
                pAbility->cancelAbility();
        }
    }

    void AbilityComponent::cancelAllAbilities()
    {
        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_FALSE && spec._pAbility->isActive() )
                spec._pAbility->cancelAbility();
        }
    }

    uint32 AbilityComponent::getAbilityCount() const
    {
        uint32 abilityCount = 0;
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            if ( pSpec->_bPendingRemove == SW_FALSE )
                ++abilityCount;
        }
        return abilityCount;
    }

    void AbilityComponent::getAbilitySpecHandles( vector<AbilitySpecHandle>& outListHandle ) const
    {
        outListHandle.clear();
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            if ( pSpec->_bPendingRemove == SW_FALSE )
                outListHandle.push_back( pSpec->_handle );
        }
    }

    void AbilityComponent::abilityInputPressed( int32 inputID )
    {
        if ( inputID == kNoInputID )
            return;

        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_TRUE || spec._inputID != inputID )
                continue;
            Ability*   pAbility   = spec._pAbility.get();
            const bool bWasActive = pAbility->isActive();
            pAbility->setInputPressed( true );
            if ( bWasActive == false )
                (void)tryActivateAbility( spec._handle ); // 쿨다운이나 비용으로 막히면 그대로 둔다(UI 가 `canActivateAbility` 로 표시한다)
        }
    }

    void AbilityComponent::abilityInputReleased( int32 inputID )
    {
        if ( inputID == kNoInputID )
            return;

        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_FALSE && spec._inputID == inputID )
                spec._pAbility->setInputPressed( false );
        }
    }

    Subscription AbilityComponent::registerAbilityActivated( const AbilityActivatedDelegate& delegate )
    {
        return _abilityActivatedMulticast.add( delegate );
    }

    Subscription AbilityComponent::registerAbilityEnded( const AbilityEndedDelegate& delegate )
    {
        return _abilityEndedMulticast.add( delegate );
    }

    // ------------------------------------------------------------------------------
    // 6) 게임플레이 이벤트와 큐
    // ------------------------------------------------------------------------------
    int32 AbilityComponent::handleGameplayEvent( TagID eventTag, const AbilityEventData& payload )
    {
        if ( eventTag.isValid() == false )
            return 0;

        const ScopedListLock lock{ *this };
        AbilityEventData     eventData = payload;
        eventData._eventTag            = eventTag;

        // 1) 트리거: 이벤트 태그가 트리거 태그와 같거나 그 하위이면 발동한다("Event.Hit.Fire" 는 "Event.Hit" 트리거를 발동시킨다).
        int32        triggeredCount = 0;
        const size_t specCount      = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            const AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_TRUE )
                continue;
            bool bTriggers = false;
            for ( const TagID& triggerTag : spec._pAbility->getConfig()._triggerEventTags.getTags() )
            {
                if ( eventTag.isSubtagOf( triggerTag ) )
                {
                    bTriggers = true;
                    break;
                }
            }
            if ( bTriggers && tryActivateAbility( spec._handle, &eventData ) == AbilityActivationResult::Activated )
                ++triggeredCount;
        }

        // 2) 구독자: 같은 태그와 그 조상 태그마다 호출한다("Event.Hit.Fire", "Event.Hit", "Event" 순). 조상의 id 는 intern 하지 않고 해시만 구한다.
        const utf8* pTagText = eventTag.getString();
        if ( pTagText != nullptr )
        {
            const size_t tagLength = StringUtil::strlen( pTagText );
            for ( size_t charIndex = tagLength; charIndex > 0; --charIndex )
            {
                const bool bBoundary = charIndex == tagLength || pTagText[charIndex] == '.';
                if ( bBoundary == false )
                    continue;
                const uint64 prefixID = TagID::computeID( pTagText, charIndex );
                const auto   mapIter  = _mapGameplayEventMulticast.find( prefixID );
                if ( mapIter != _mapGameplayEventMulticast.end() && mapIter->second != nullptr )
                    mapIter->second->broadcast( eventData );
            }
        }
        else
        {
            const auto mapIter = _mapGameplayEventMulticast.find( eventTag._id );
            if ( mapIter != _mapGameplayEventMulticast.end() && mapIter->second != nullptr )
                mapIter->second->broadcast( eventData );
        }
        return triggeredCount;
    }

    void AbilityComponent::sendGameplayEventToTarget( AbilityComponent* pTarget, TagID eventTag, const AbilityEventData& payload )
    {
        if ( pTarget == nullptr || eventTag.isValid() == false )
            return;
        if ( pTarget->canMutateNow() )
        {
            (void)pTarget->handleGameplayEvent( eventTag, payload ); // 발동 수는 보내는 쪽이 쓰지 않는다
            return;
        }

        GameObject*                              pTargetOwner = pTarget->getOwner();
        GameObjectManager*                       pManager     = pTargetOwner != nullptr ? pTargetOwner->getManager() : nullptr;
        const TComponentHandle<AbilityComponent> targetHandle{ pTarget };
        if ( pManager == nullptr )
            return;
        pManager->deferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, targetHandle, eventTag, payload]()
        {
            AbilityComponent* pResolved = pManager->resolveComponent( targetHandle );
            if ( pResolved != nullptr )
                (void)pResolved->handleGameplayEvent( eventTag, payload );
        } ) );
    }

    Subscription AbilityComponent::registerGameplayEvent( TagID eventTag, const AbilityEventDelegate& delegate )
    {
        if ( eventTag.isValid() == false )
            return Subscription{}; // 빈 구독 — `isActive` 가 false 다
        // 멀티캐스트는 힙에 두어 맵이 다시 해시해도 구독이 가리키는 주소가 그대로다.
        unique_ptr<MulticastDelegate<void( const AbilityEventData& )>>& pMulticast = _mapGameplayEventMulticast[eventTag._id];
        if ( pMulticast == nullptr )
            pMulticast = make_unique<MulticastDelegate<void( const AbilityEventData& )>>();
        return pMulticast->add( delegate );
    }

    Subscription AbilityComponent::registerGameplayCue( const AbilityPresentationDelegate& delegate )
    {
        return _gameplayCueMulticast.add( delegate );
    }

    void AbilityComponent::setHealthAttributes( const hashed_string& health, const hashed_string& maxHealth )
    {
        _healthAttribute    = health;
        _maxHealthAttribute = maxHealth;
        notifyHealthChanged( true );
    }

    HealthReading AbilityComponent::getHealthReading() const
    {
        // 체력과 최대 체력이 둘 다 있어야 읽은 것이다 — 없는 최대 체력을 0 으로 보지 않는다.
        HealthReading                reading;
        const std::optional<float32> health    = getAttributeValue( _healthAttribute );
        const std::optional<float32> maxHealth = getAttributeValue( _maxHealthAttribute );
        if ( health.has_value() == false || maxHealth.has_value() == false )
        {
            reading._bHasHealth = SW_FALSE;
            return reading;
        }
        reading._health    = *health;
        reading._maxHealth = *maxHealth;
        reading._bDead     = reading._health <= 0.0f ? SW_TRUE : SW_FALSE;
        return reading;
    }

    // ------------------------------------------------------------------------------
    // 10) 내부: 어빌리티
    // ------------------------------------------------------------------------------
    AbilityComponent::AbilitySpec* AbilityComponent::findAbilitySpecMutable( AbilitySpecHandle handle )
    {
        return const_cast<AbilitySpec*>( findAbilitySpec( handle ) );
    }

    const AbilityComponent::AbilitySpec* AbilityComponent::findAbilitySpec( AbilitySpecHandle handle ) const
    {
        if ( handle.isValid() == false )
            return nullptr;
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            if ( pSpec->_handle == handle && pSpec->_bPendingRemove == SW_FALSE )
                return pSpec.get();
        }
        return nullptr;
    }

    void AbilityComponent::handleAbilityActivated( Ability& ability )
    {
        const AbilityConfig& config = ability.getConfig();
        _ownedTagCounts.updateCounts( config._activationOwnedTags, 1 );
        _blockedAbilityTagCounts.updateCounts( config._blockAbilitiesWithTags, 1 );
        cancelAbilitiesWithTags( config._cancelAbilitiesWithTags, &ability );
        _abilityActivatedMulticast.broadcast( ability );
    }

    void AbilityComponent::handleAbilityEnded( Ability& ability, bool bWasCancelled )
    {
        // 발동할 때 붙인 것을 그대로 뗀다. 실행 중에는 설정을 바꾸지 않는다는 `Ability::getConfig` 의 약속에 의존한다.
        const AbilityConfig& config = ability.getConfig();
        _ownedTagCounts.updateCounts( config._activationOwnedTags, -1 );
        _blockedAbilityTagCounts.updateCounts( config._blockAbilitiesWithTags, -1 );
        _abilityEndedMulticast.broadcast( ability, bWasCancelled );
    }

    void AbilityComponent::updateActiveAbilities( float32 deltaTime )
    {
        const size_t specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_FALSE && spec._pAbility->isActive() )
                spec._pAbility->updateAbility( deltaTime );
        }
    }

    void AbilityComponent::retireAbilitySpec( AbilitySpec& spec )
    {
        if ( spec._bPendingRemove == SW_TRUE )
            return;
        Ability* pAbility = spec._pAbility.get();
        if ( pAbility->isActive() )
            pAbility->cancelAbility();
        pAbility->onRemoved();
        spec._bPendingRemove = SW_TRUE;
        _bHasPendingRemoval  = SW_TRUE;
    }
} // namespace sw
