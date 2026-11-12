#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionTypes.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IDamageNumberSink.h"
#include "GameFramework/Runtime/Gameplay/Ability/Ability.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEvents.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

/**
 * @file AbilityComponent.cpp
 * @brief 어빌리티 컴포넌트의 수명 · 시간 진행 · 태그 차단 · 모듈 언로드를 구현합니다.
 * @details 책임마다 파일이 나뉜다(언리얼 GAS 가 `FGameplayAbilitySpecContainer` · `FActiveGameplayEffectsContainer` · `UAttributeSet` 을 가르는 나눔).
 *          어빌리티 스펙 컨테이너는 `AbilityComponentAbilities.cpp`, 어트리뷰트는 `AbilityComponentAttributes.cpp`, 게임플레이 이펙트는 `AbilityComponentEffects.cpp`,
 *          상태 저장 · 복원은 `AbilityComponentSave.cpp` 다.
 */

namespace sw
{
    // ------------------------------------------------------------------------------
    // 0) 수명
    // ------------------------------------------------------------------------------
    AbilityComponent::ScopedListLock::ScopedListLock( AbilityComponent& owner )
        : _owner{ owner }
    {
        ++_owner._listLockDepth;
    }

    AbilityComponent::ScopedListLock::~ScopedListLock()
    {
        --_owner._listLockDepth;
        if ( _owner._listLockDepth == 0 && _owner._bHasPendingRemoval == SW_TRUE )
            _owner.compactPendingRemovals();
    }

    AbilityComponent::AbilityComponent()
        : _listAttributeSet{}
        , _activeEffects{}
        , _abilitySpecs{}
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
        , _pCatalog{ nullptr }
        , _pClassRegistry{ nullptr }
        , _time{ 0.0f }
        , _nextHandleID{ 0 }
        , _arrEffectLog{}
        , _effectLogCount{ 0 }
        , _listLockDepth{ 0 }
        , _blockedHealthDecreaseCount{ 0 }
        , _pExecutingContext{ nullptr }
        , _bHasPendingRemoval{ SW_FALSE }
        , _bRefreshingOngoingTags{ SW_FALSE }
        , _bRestoringState{ SW_FALSE }
        , _pendingStateBytes{}
        , _abilitySetID{}
        , _healthAttribute{ CombatAttributes::health() } // 프레임워크 공용 어트리뷰트 세트의 이름(엔진 상수) — 다른 세트를 쓰는 데이터가 덮는다
        , _maxHealthAttribute{ CombatAttributes::maxHealth() }
        , _damageNumberOffset{ 0.0f, 0.6f, 0.0f }
        , _bShowDamageNumbers{ false }
    {
        // 결정: 태그가 바뀌는 길(이펙트 · 어빌리티 · 이펙트 없이 붙인 태그)은 여럿이지만 알림은 집합 하나에서 나온다 — 걸린 동안의 조건과
        // 태그로 발동하는 어빌리티는 그 알림 하나에 건다. 소멸자의 `unregisterAllChanged` 가 함께 푼다.
        _ownedTagSubscription = _ownedTagCounts.registerChanged( TagCountTable::TagChangedDelegate::create<&AbilityComponent::onOwnedTagChanged>( this ) );
    }

    AbilityComponent::~AbilityComponent()
    {
        // 순서는 다음과 같다. 먼저 순회가 더 일어나지 않게 하고, 바깥 구독을 해제한다(지우는 중에 게임 코드를 부르지 않기 위해서다). 그다음 어빌리티를 지우고(작업이 이 컴포넌트에서 구독을 해제한다), 마지막에 나머지를 지운다.
        _anyAttributeChangedMulticast.removeAll();
        _ownedTagCounts.unregisterAllChanged();
        _effectAppliedMulticast.removeAll();
        _effectRemovedMulticast.removeAll();
        _abilityActivatedMulticast.removeAll();
        _abilityEndedMulticast.removeAll();
        _gameplayCueMulticast.removeAll();
        _mapAttributeChangedMulticast.clear();
        clearAllAbilities();
        _activeEffects.clear();
        _listAttributeSet.clear();
        _mapGameplayEventMulticast.clear();
    }

    void AbilityComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _abilitySetID.empty() == false )
            (void)grantAbilitySet( _abilitySetID ); // 실패는 안에서 알린다
        (void)applyPendingState();                  // 세트가 어트리뷰트 · 시작 이펙트를 만든 뒤에 덮는다. 없으면 새 판이다
        notifyHealthChanged( true );
    }

    void AbilityComponent::onEndPlay()
    {
        cancelAllAbilities();
        Component::onEndPlay();
    }

    void AbilityComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        advanceTime( context.getDeltaTime() );
    }

    void AbilityComponent::compactPendingRemovals()
    {
        _bHasPendingRemoval = SW_FALSE;

        // 지울 것을 목록 밖으로 먼저 옮긴다. 소멸자(어빌리티의 작업 정리)가 이 컴포넌트를 불러도 목록은 이미 정리된 상태이다.
        vector<unique_ptr<ActiveGameplayEffect>> listRemovedEffect;
        _activeEffects.takePendingRemovals( listRemovedEffect );
        vector<unique_ptr<AbilitySpec>> listRemovedSpec;
        _abilitySpecs.takePendingRemovals( listRemovedSpec );

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
    void AbilityComponent::advanceTime( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const ScopedListLock lock{ *this };
        _time += deltaTime;
        updateActiveEffects( deltaTime );
        updateActiveAbilities( deltaTime );
    }

    const AbilityCatalog* AbilityComponent::findCatalog() const
    {
        if ( _pCatalog != nullptr )
            return _pCatalog;
        return game::findSubsystem<const AbilityCatalog>();
    }

    const AbilityClassRegistry* AbilityComponent::findClassRegistry() const
    {
        if ( _pClassRegistry != nullptr )
            return _pClassRegistry;
        return game::findSubsystem<AbilityClassRegistry>();
    }

    bool AbilityComponent::grantAbilitySet( const hashed_string& setID )
    {
        const AbilityCatalog* pCatalog = findCatalog();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING_ID( "AbilityComponent.GrantAbilitySetNoAbilityCatalogSetCatalog", "grantAbilitySet '%#': no AbilityCatalog (setCatalog or bind it as a game service)", setID.c_str() );
            return false;
        }
        const AbilitySetDef* pSet = pCatalog->findAbilitySet( setID );
        if ( pSet == nullptr )
        {
            SW_LOG_WARNING_ID( "AbilityComponent.GrantAbilitySetNoAbilitySet", "grantAbilitySet: no ability set '%#' in the catalog", setID.c_str() );
            return false;
        }
        const AbilityClassRegistry* pClasses = findClassRegistry();
        if ( pClasses == nullptr )
        {
            SW_LOG_WARNING_ID( "AbilityComponent.GrantAbilitySetNoClassRegistry", "grantAbilitySet '%#': no AbilityClassRegistry (setClassRegistry or bind it as a game service)", setID.c_str() );
            return false;
        }

        // 세트의 기본값은 붙이기 **전에** 정한다. 붙인 뒤에 정하면 "체력 ≤ 최대 체력" 같은 훅이 값을 정하는 순서에 따라 값을 잘라 버린다.
        for ( const AbilitySetAttributeSetEntry& setEntry : pSet->_listAttributeSet )
        {
            unique_ptr<AttributeSet> pAttributeSet = pClasses->createAttributeSet( setEntry._className );
            if ( pAttributeSet == nullptr )
            {
                SW_LOG_WARNING_ID( "AbilityComponent.GrantAbilitySetAttributeSetClass", "grantAbilitySet '%#': attribute set class '%#' is not registered", setID.c_str(), setEntry._className.c_str() );
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
    bool AbilityComponent::isAbilityBlocked( const TagContainer& abilityTags ) const
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

    void AbilityComponent::onOwnedTagChanged( TagID tag, int32 newCount )
    {
        // 불러온 태그는 새로 생긴 일이 아니다 — 태그로 발동하는 어빌리티를 다시 쏘지 않고, 걸린 동안의 조건은 적용 끝에 한 번 본다(`applySaveState`).
        if ( _bRestoringState == SW_TRUE )
            return;
        refreshOngoingTagRequirements();
        if ( newCount <= 0 )
            return;

        // 태그가 생기면(0 → 1) 그 태그나 상위 태그를 `_ownedTagTriggerTags` 로 가진 어빌리티를 발동한다(언리얼 `OwnedTagAdded` 트리거).
        const ScopedListLock lock{ *this };
        AbilityEventData     eventData;
        eventData._eventTag    = tag;
        const size_t specCount = _abilitySpecs.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _abilitySpecs.size(); ++specIndex )
        {
            const AbilitySpec& spec = *_abilitySpecs[specIndex];
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

    void AbilityComponent::refreshOngoingTagRequirements()
    {
        if ( _bRefreshingOngoingTags == SW_TRUE )
            return;
        _bRefreshingOngoingTags = SW_TRUE;
        {
            // 이펙트 하나를 떼고 붙이면 태그가 바뀌어 앞서 본 이펙트의 답이 달라질 수 있다 — 바뀐 것이 없을 때까지 다시 본다.
            // 서로의 태그를 조건으로 거는 이펙트가 번갈아 뒤집히면 끝나지 않으므로 이펙트 수 + 1 번에서 멈추고 알린다.
            const ScopedListLock lock{ *this };
            const size_t         maxPassCount = _activeEffects.size() + 1;
            bool                 bChanged     = true;
            size_t               passCount    = 0;
            while ( bChanged && passCount < maxPassCount )
            {
                bChanged = false;
                ++passCount;
                const size_t effectCount = _activeEffects.size();
                for ( size_t effectIndex = 0; effectIndex < effectCount && effectIndex < _activeEffects.size(); ++effectIndex )
                {
                    ActiveGameplayEffect& activeEffect = *_activeEffects[effectIndex];
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
                SW_LOG_ERROR_ID( "AbilityComponent.RefreshOngoingTagRequirementsEffectsKeepInhibiting", "refreshOngoingTagRequirements: effects keep inhibiting each other through their ongoing required tags - stopped after %# passes", passCount );
        }
        _bRefreshingOngoingTags = SW_FALSE;
    }

    // ------------------------------------------------------------------------------
    // 11) 내부: 스레드, 연결된 UI, 모듈 코드
    // ------------------------------------------------------------------------------
    bool AbilityComponent::canMutateNow() const
    {
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr || pManager->isStructuralMutationFrozen() == false )
            return true;
        return GameObjectManager::getTickingObject() == pOwner;
    }

    void AbilityComponent::spawnDamageNumber( float32 amount )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;

        // 위치는 지금 읽는다. 자기 오브젝트이므로 이 워커가 읽어도 된다. 숫자는 창구(`IDamageNumberSink`)가 낸다 — 틱 중이면 틱 직후에 만들고, 전용 서버는 창구가 없다.
        const SceneComponent* pRoot         = pOwner->getPrimarySceneComponent();
        const float3          position      = ( pRoot != nullptr ? pRoot->getWorldPosition() : float3{} ) + _damageNumberOffset;
        const int32           roundedDamage = static_cast<int32>( MathUtil::round( amount ) );
        if ( roundedDamage <= 0 )
            return;
        if ( IDamageNumberSink* pDamageNumbers = game::findSubsystem<IDamageNumberSink>() )
            pDamageNumbers->spawnDamageNumber( *pManager, position, roundedDamage );
    }

    void AbilityComponent::onModuleUnloading( string_view moduleName )
    {
        const auto isOfModule = [moduleName]( const TypeInfo* pType )
        {
            return pType != nullptr && string_view( pType->_moduleName.c_str() ) == moduleName;
        };
        {
            const ScopedListLock lock{ *this };

            // 1) 어빌리티: 그 모듈 타입이면 코드가 아직 남아 있는 지금 제거한다. 작업이 등록한 이벤트 구독도 함께 해제된다.
            const size_t specCount = _abilitySpecs.size();
            for ( size_t specIndex = 0; specIndex < specCount && specIndex < _abilitySpecs.size(); ++specIndex )
            {
                AbilitySpec& spec = *_abilitySpecs[specIndex];
                if ( spec._bPendingRemove == SW_FALSE && isOfModule( spec._pAbility->getTypeInfo() ) )
                    retireAbilitySpec( spec );
            }

            // 2) 그 모듈의 실행 계산을 쓰는 이펙트: 다음 주기에 언로드된 코드를 부르지 않도록 해제한다.
            const size_t effectCount = _activeEffects.size();
            for ( size_t effectIndex = 0; effectIndex < effectCount && effectIndex < _activeEffects.size(); ++effectIndex )
            {
                ActiveGameplayEffect& activeEffect = *_activeEffects[effectIndex];
                if ( activeEffect._bPendingRemove == SW_TRUE )
                    continue;
                for ( const shared_ptr<const IGameplayEffectExecution>& pExecution : activeEffect._spec._pDef->_listExecution )
                {
                    if ( isOfModule( pExecution->getTypeInfo() ) )
                    {
                        removeActiveEffectInternal( activeEffect, AbilityEffectLogKind::RemovedUnloaded );
                        break;
                    }
                }
            }
        }

        // 3) 그 모듈이 정의한 어트리뷰트 세트: 값도 함께 사라진다. 모듈을 다시 로드하면 게임이 세트를 다시 준다.
        for ( size_t setIndex = _listAttributeSet.size(); setIndex > 0; --setIndex )
        {
            if ( isOfModule( _listAttributeSet[setIndex - 1]->getTypeInfo() ) )
                _listAttributeSet.erase( _listAttributeSet.begin() + static_cast<ptrdiff_t>( setIndex - 1 ) );
        }
    }

    void AbilityComponent::onRegister( GameObjectManager& manager )
    {
        HealthSourceComponent::onRegister( manager );
        manager.getComponentRegistry().add<AbilityComponent>( this );
    }

    void AbilityComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<AbilityComponent>( this );
        HealthSourceComponent::onUnregister( manager );
    }
} // namespace sw
