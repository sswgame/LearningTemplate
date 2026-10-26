#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemEvents.h"
#include "GameFramework/Base/Gameplay/Ability/GameplayAbility.h"
#include "GameFramework/Base/UI/Marker/DamageNumberComponent.h"

namespace sw
{
    SW_LOG_CALLER( "AbilitySystemComponent" );
} // namespace sw

namespace sw
{
    /**
     * @class AbilitySystemModuleUnloadGuard
     * @brief 모듈(DLL, SO)을 언로드하기 전에, 그 모듈의 코드(게임 어빌리티, 어트리뷰트 세트, 실행 계산, 델리게이트)를 가리키는 참조를 컴포넌트에서 제거합니다.
     * @details 게임 모듈이 준 어빌리티 인스턴스는 vtable 이 그 모듈 안에 있다. 핫 리로드가 모듈을 언로드한 뒤에 컴포넌트가 그 인스턴스를 지우면,
     *          이미 언로드된 코드로 점프해서 크래시가 난다. 그래서 컴포넌트마다 리스너를 하나 두고(`IModuleUnloadListener`),
     *          언로드하기 전에 코드가 아직 남아 있을 때 해당 범위의 것을 지운다.
     */
    class AbilitySystemModuleUnloadGuard final : public IModuleUnloadListener
    {
    public:
        explicit AbilitySystemModuleUnloadGuard( AbilitySystemComponent& owner )
            : _owner{ owner }
        {
        }

        const utf8* getModuleUnloadListenerName() const override { return "ability system"; }

        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
        {
            (void)outKeepImageMapped; // 모두 지울 수 있다
            return _owner.onModuleUnloading( pBegin, pEnd );
        }

    private:
        AbilitySystemComponent& _owner;
    };
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 0) 수명
    // ------------------------------------------------------------------------------
    AbilitySystemComponent::ScopedListLock::ScopedListLock( AbilitySystemComponent& owner )
        : _owner{ owner }
    {
        ++_owner._listLockDepth;
    }

    AbilitySystemComponent::ScopedListLock::~ScopedListLock()
    {
        --_owner._listLockDepth;
        if ( _owner._listLockDepth == 0 && _owner._bHasPendingRemoval == SW_TRUE )
            _owner.compactPendingRemovals();
    }

    AbilitySystemComponent::AbilitySystemComponent()
        : _listAttributeSet{}
        , _listActiveEffect{}
        , _listAbilitySpec{}
        , _ownedTagCounts{}
        , _ownedTagSubscription{}
        , _blockedAbilityTagCounts{}
        , _mapAttributeChangedMulticast{}
        , _mapGameplayEventMulticast{}
        , _anyAttributeChangedMulticast{}
        , _effectAppliedMulticast{}
        , _effectRemovedMulticast{}
        , _abilityActivatedMulticast{}
        , _abilityEndedMulticast{}
        , _gameplayCueMulticast{}
        , _pModuleCodeGuard{ nullptr }
        , _pCatalog{ nullptr }
        , _time{ 0.0f }
        , _nextHandleID{ 0 }
        , _listLockDepth{ 0 }
        , _bHasPendingRemoval{ SW_FALSE }
        , _bRefreshingOngoingTags{ SW_FALSE }
        , _abilitySetID{}
        , _healthAttribute{ "Health" }
        , _maxHealthAttribute{ "MaxHealth" }
        , _damageNumberOffset{ 0.0f, 0.6f, 0.0f }
        , _bShowDamageNumbers{ false }
    {
        _pModuleCodeGuard = make_unique<AbilitySystemModuleUnloadGuard>( *this );
        // 결정: 태그가 바뀌는 길(이펙트 · 어빌리티 · 이펙트 없이 붙인 태그)은 여럿이지만 알림은 집합 하나에서 나온다 — 걸린 동안의 조건과
        // 태그로 발동하는 어빌리티는 그 알림 하나에 건다. 소멸자의 `unregisterAllChanged` 가 함께 푼다.
        _ownedTagSubscription = _ownedTagCounts.registerChanged( GameplayTagCountContainer::TagChangedDelegate::create<&AbilitySystemComponent::onOwnedTagChanged>( this ) );
    }

    AbilitySystemComponent::~AbilitySystemComponent()
    {
        // 순서는 다음과 같다. 먼저 순회가 더 일어나지 않게 하고, 바깥 구독을 해제한다(지우는 중에 게임 코드를 부르지 않기 위해서다). 그다음 어빌리티를 지우고(작업이 이 컴포넌트에서 구독을 해제한다), 마지막에 나머지를 지운다.
        _pModuleCodeGuard.reset();
        _anyAttributeChangedMulticast.removeAll();
        _ownedTagCounts.unregisterAllChanged();
        _effectAppliedMulticast.removeAll();
        _effectRemovedMulticast.removeAll();
        _abilityActivatedMulticast.removeAll();
        _abilityEndedMulticast.removeAll();
        _gameplayCueMulticast.removeAll();
        _mapAttributeChangedMulticast.clear();
        clearAllAbilities();
        _listActiveEffect.clear();
        _listAttributeSet.clear();
        _mapGameplayEventMulticast.clear();
    }

    void AbilitySystemComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _abilitySetID.empty() == false )
            (void)grantAbilitySet( _abilitySetID ); // 실패는 안에서 알린다
        notifyHealthChanged( true );
    }

    void AbilitySystemComponent::onEndPlay()
    {
        cancelAllAbilities();
        Component::onEndPlay();
    }

    void AbilitySystemComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        advanceTime( deltaTime );
    }

    void AbilitySystemComponent::compactPendingRemovals()
    {
        _bHasPendingRemoval = SW_FALSE;

        // 지울 것을 목록 밖으로 먼저 옮긴다. 소멸자(어빌리티의 작업 정리)가 이 컴포넌트를 불러도 목록은 이미 정리된 상태이다.
        vector<unique_ptr<ActiveGameplayEffect>> listRemovedEffect;
        for ( size_t effectIndex = _listActiveEffect.size(); effectIndex > 0; --effectIndex )
        {
            unique_ptr<ActiveGameplayEffect>& pActiveEffect = _listActiveEffect[effectIndex - 1];
            if ( pActiveEffect->_bPendingRemove == SW_TRUE )
            {
                listRemovedEffect.push_back( std::move( pActiveEffect ) );
                _listActiveEffect.erase( _listActiveEffect.begin() + static_cast<ptrdiff_t>( effectIndex - 1 ) );
            }
        }

        vector<unique_ptr<AbilitySpec>> listRemovedSpec;
        for ( size_t specIndex = _listAbilitySpec.size(); specIndex > 0; --specIndex )
        {
            unique_ptr<AbilitySpec>& pSpec = _listAbilitySpec[specIndex - 1];
            if ( pSpec->_bPendingRemove == SW_TRUE )
            {
                listRemovedSpec.push_back( std::move( pSpec ) );
                _listAbilitySpec.erase( _listAbilitySpec.begin() + static_cast<ptrdiff_t>( specIndex - 1 ) );
            }
        }

        // 소멸 중에 다시 지우기가 표시되면(드문 경우) 잠금 밖이므로 한 번 더 정리한다.
        ++_listLockDepth;
        listRemovedSpec.clear();
        listRemovedEffect.clear();
        --_listLockDepth;
        if ( _bHasPendingRemoval == SW_TRUE )
            compactPendingRemovals();
    }

    // ------------------------------------------------------------------------------
    // 1) 시간, 카탈로그, 세트
    // ------------------------------------------------------------------------------
    void AbilitySystemComponent::advanceTime( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const ScopedListLock lock{ *this };
        _time += deltaTime;
        updateActiveEffects( deltaTime );
        updateActiveAbilities( deltaTime );
    }

    const AbilityCatalog* AbilitySystemComponent::findCatalog() const
    {
        if ( _pCatalog != nullptr )
            return _pCatalog;
        return game::findSubsystem<AbilityCatalog>();
    }

    bool AbilitySystemComponent::grantAbilitySet( const hashed_string& setID )
    {
        const AbilityCatalog* pCatalog = findCatalog();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING( "grantAbilitySet '%#': no AbilityCatalog (setCatalog or bind it as a game service)", setID.c_str() );
            return false;
        }
        const AbilitySetDef* pSet = pCatalog->findAbilitySet( setID );
        if ( pSet == nullptr )
        {
            SW_LOG_WARNING( "grantAbilitySet: no ability set '%#' in the catalog", setID.c_str() );
            return false;
        }

        // 세트의 기본값은 붙이기 **전에** 정한다. 붙인 뒤에 정하면 "체력 ≤ 최대 체력" 같은 훅이 값을 정하는 순서에 따라 값을 잘라 버린다.
        for ( const AbilitySetAttributeSetEntry& setEntry : pSet->_listAttributeSet )
        {
            unique_ptr<AttributeSet> pAttributeSet = pCatalog->createAttributeSet( setEntry._className );
            if ( pAttributeSet == nullptr )
            {
                SW_LOG_WARNING( "grantAbilitySet '%#': attribute set class '%#' is not registered", setID.c_str(), setEntry._className.c_str() );
                continue;
            }
            for ( const AbilitySetAttributeEntry& attributeEntry : setEntry._listAttribute )
            {
                pAttributeSet->defineAttribute( attributeEntry._attribute, attributeEntry._baseValue );
                if ( attributeEntry._bHasRange == SW_TRUE )
                    pAttributeSet->setAttributeRange( attributeEntry._attribute, attributeEntry._minValue, attributeEntry._maxValue );
            }
            (void)addAttributeSet( std::move( pAttributeSet ) ); // 방금 만든 세트이므로 nullptr 이 아니다
        }

        for ( const TagID& tag : pSet->_looseTags.getTags() )
        {
            _ownedTagCounts.updateCount( tag, 1 );
        }
        for ( const AbilitySetAbilityEntry& abilityEntry : pSet->_listAbility )
        {
            (void)giveAbilityByID( abilityEntry._abilityID, abilityEntry._level, abilityEntry._inputID ); // 실패는 안에서 알린다
        }
        for ( const AbilitySetEffectEntry& effectEntry : pSet->_listEffect )
        {
            (void)applyGameplayEffectToSelf( effectEntry._effectID, effectEntry._level ); // 태그 조건에 막힌 시작 이펙트는 걸리지 않는다
        }
        return true;
    }

    // ------------------------------------------------------------------------------
    // 9) 내부: 태그
    // ------------------------------------------------------------------------------
    bool AbilitySystemComponent::isAbilityBlocked( const TagContainer& abilityTags ) const
    {
        for ( const TagID& blockedTag : _blockedAbilityTagCounts.getTags().getTags() )
        {
            for ( const TagID& abilityTag : abilityTags.getTags() )
            {
                if ( abilityTag.isSubtagOf( blockedTag ) )
                    return true;
            }
        }
        return false;
    }

    void AbilitySystemComponent::onOwnedTagChanged( TagID tag, int32 newCount )
    {
        refreshOngoingTagRequirements();
        if ( newCount <= 0 )
            return;

        // 태그가 생기면(0 → 1) 그 태그나 상위 태그를 `_ownedTagTriggerTags` 로 가진 어빌리티를 발동한다(언리얼 `OwnedTagAdded` 트리거).
        const ScopedListLock lock{ *this };
        GameplayEventData    eventData;
        eventData._eventTag    = tag;
        const size_t specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            const AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_TRUE )
                continue;
            bool bTriggers = false;
            for ( const TagID& triggerTag : spec._pAbility->getConfig()._ownedTagTriggerTags.getTags() )
            {
                if ( tag.isSubtagOf( triggerTag ) )
                {
                    bTriggers = true;
                    break;
                }
            }
            if ( bTriggers )
                (void)tryActivateAbility( spec._handle, &eventData ); // 조건에 막히면 발동하지 않는 것이 뜻이다
        }
    }

    void AbilitySystemComponent::refreshOngoingTagRequirements()
    {
        if ( _bRefreshingOngoingTags == SW_TRUE )
            return;
        _bRefreshingOngoingTags = SW_TRUE;
        {
            // 이펙트 하나를 떼고 붙이면 태그가 바뀌어 앞서 본 이펙트의 답이 달라질 수 있다 — 바뀐 것이 없을 때까지 다시 본다.
            // 서로의 태그를 조건으로 거는 이펙트가 번갈아 뒤집히면 끝나지 않으므로 이펙트 수 + 1 번에서 멈추고 알린다.
            const ScopedListLock lock{ *this };
            const size_t         maxPassCount = _listActiveEffect.size() + 1;
            bool                 bChanged     = true;
            size_t               passCount    = 0;
            while ( bChanged && passCount < maxPassCount )
            {
                bChanged = false;
                ++passCount;
                const size_t effectCount = _listActiveEffect.size();
                for ( size_t effectIndex = 0; effectIndex < effectCount && effectIndex < _listActiveEffect.size(); ++effectIndex )
                {
                    ActiveGameplayEffect& activeEffect = *_listActiveEffect[effectIndex];
                    if ( activeEffect._bPendingRemove == SW_TRUE )
                        continue;
                    const TagContainer& requiredTags = activeEffect._spec._pDef->_ongoingRequiredTags;
                    if ( requiredTags.getTagCount() == 0 )
                        continue;
                    const bool bShouldInhibit = _ownedTagCounts.hasAllMatchingTags( requiredTags ) == false;
                    const bool bInhibited     = activeEffect._bInhibited == SW_TRUE;
                    if ( bShouldInhibit == bInhibited )
                        continue;
                    bChanged = true;
                    if ( bShouldInhibit )
                    {
                        activeEffect._bInhibited = SW_TRUE;
                        disengageActiveEffect( activeEffect );
                    }
                    else
                    {
                        activeEffect._bInhibited = SW_FALSE;
                        engageActiveEffect( activeEffect );
                    }
                }
            }
            if ( bChanged )
                SW_LOG_ERROR( "refreshOngoingTagRequirements: effects keep inhibiting each other through their ongoing required tags - stopped after %# passes", passCount );
        }
        _bRefreshingOngoingTags = SW_FALSE;
    }

    // ------------------------------------------------------------------------------
    // 11) 내부: 스레드, 연결된 UI, 모듈 코드
    // ------------------------------------------------------------------------------
    bool AbilitySystemComponent::canMutateNow() const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || pManager->isStructuralMutationFrozen() == false )
            return true;
        return GameObjectManager::getTickingObject() == pOwner;
    }

    void AbilitySystemComponent::spawnDamageNumber( float32 amount )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;

        // 위치는 지금 읽는다. 자기 오브젝트이므로 이 워커가 읽어도 된다. 숫자 생성은 틱 중이면 틱 직후에 한다(`DamageNumberComponent::spawnNumber`).
        const SceneComponent* pRoot         = pOwner->getPrimarySceneComponent();
        const float3          position      = ( pRoot != nullptr ? pRoot->getWorldPosition() : float3{} ) + _damageNumberOffset;
        const int32           roundedDamage = static_cast<int32>( MathUtil::round( amount ) );
        if ( roundedDamage <= 0 )
            return;
        DamageNumberComponent::spawnNumber( *pManager, position, roundedDamage );
    }

    uint32 AbilitySystemComponent::onModuleUnloading( const void* pBegin, const void* pEnd )
    {
        uint32 releasedCount = 0;

        // 1) 델리게이트: 게임이 등록한 구독이며 스텁이 그 모듈 안에 있다. 지우기 전에 해제해서 정리 중에 호출되지 않게 한다.
        releasedCount += _anyAttributeChangedMulticast.removeCodeWithin( pBegin, pEnd );
        releasedCount += _ownedTagCounts.unregisterChangedWithin( pBegin, pEnd );
        releasedCount += _effectAppliedMulticast.removeCodeWithin( pBegin, pEnd );
        releasedCount += _effectRemovedMulticast.removeCodeWithin( pBegin, pEnd );
        releasedCount += _abilityActivatedMulticast.removeCodeWithin( pBegin, pEnd );
        releasedCount += _abilityEndedMulticast.removeCodeWithin( pBegin, pEnd );
        releasedCount += _gameplayCueMulticast.removeCodeWithin( pBegin, pEnd );
        for ( auto& [name, pMulticast] : _mapAttributeChangedMulticast )
        {
            (void)name;
            if ( pMulticast != nullptr )
                releasedCount += pMulticast->removeCodeWithin( pBegin, pEnd );
        }

        {
            const ScopedListLock lock{ *this };

            // 2) 어빌리티: 인스턴스의 vtable 이 그 모듈 안에 있으면 코드가 아직 남아 있는 지금 제거한다. 작업이 등록한 이벤트 구독도 함께 해제된다.
            const size_t specCount = _listAbilitySpec.size();
            for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
            {
                AbilitySpec& spec = *_listAbilitySpec[specIndex];
                if ( spec._bPendingRemove == SW_TRUE )
                    continue;
                if ( IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( spec._pAbility.get() ), pBegin, pEnd ) )
                {
                    retireAbilitySpec( spec );
                    ++releasedCount;
                }
            }

            // 3) 그 모듈의 실행 계산을 쓰는 이펙트: 다음 주기에 언로드된 코드를 부르지 않도록 해제한다.
            const size_t effectCount = _listActiveEffect.size();
            for ( size_t effectIndex = 0; effectIndex < effectCount && effectIndex < _listActiveEffect.size(); ++effectIndex )
            {
                ActiveGameplayEffect& activeEffect = *_listActiveEffect[effectIndex];
                if ( activeEffect._bPendingRemove == SW_TRUE )
                    continue;
                for ( const shared_ptr<const IGameplayEffectExecution>& pExecution : activeEffect._spec._pDef->_listExecution )
                {
                    if ( IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( pExecution.get() ), pBegin, pEnd ) )
                    {
                        removeActiveEffectInternal( activeEffect );
                        ++releasedCount;
                        break;
                    }
                }
            }
        }

        // 4) 게임 모듈이 정의한 어트리뷰트 세트: 값도 함께 사라진다. 모듈을 다시 로드하면 게임이 세트를 다시 준다.
        for ( size_t setIndex = _listAttributeSet.size(); setIndex > 0; --setIndex )
        {
            if ( IModuleUnloadListener::isAddressWithin( IModuleUnloadListener::findVtableAddress( _listAttributeSet[setIndex - 1].get() ), pBegin, pEnd ) )
            {
                _listAttributeSet.erase( _listAttributeSet.begin() + static_cast<ptrdiff_t>( setIndex - 1 ) );
                ++releasedCount;
            }
        }
        return releasedCount;
    }
} // namespace sw
