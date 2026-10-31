#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/AbilityComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionTypes.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"
#include "GameFramework/Base/Gameplay/Ability/Ability.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityEvents.h"
#include "GameFramework/Base/UI/Marker/DamageNumberComponent.h"

namespace sw
{
    SW_LOG_CALLER( "AbilityComponent" );
} // namespace sw

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
        , _pCatalog{ nullptr }
        , _time{ 0.0f }
        , _nextHandleID{ 0 }
        , _listLockDepth{ 0 }
        , _blockedHealthDecreaseCount{ 0 }
        , _bHasPendingRemoval{ SW_FALSE }
        , _bRefreshingOngoingTags{ SW_FALSE }
        , _abilitySetID{}
        , _healthAttribute{ "Health" }
        , _maxHealthAttribute{ "MaxHealth" }
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
        _listActiveEffect.clear();
        _listAttributeSet.clear();
        _mapGameplayEventMulticast.clear();
    }

    void AbilityComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _abilitySetID.empty() == false )
            (void)grantAbilitySet( _abilitySetID ); // 실패는 안에서 알린다
        notifyHealthChanged( true );
    }

    void AbilityComponent::onEndPlay()
    {
        cancelAllAbilities();
        Component::onEndPlay();
    }

    void AbilityComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        advanceTime( deltaTime );
    }

    void AbilityComponent::compactPendingRemovals()
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
        return game::findSubsystem<AbilityCatalog>();
    }

    bool AbilityComponent::grantAbilitySet( const hashed_string& setID )
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
        refreshOngoingTagRequirements();
        if ( newCount <= 0 )
            return;

        // 태그가 생기면(0 → 1) 그 태그나 상위 태그를 `_ownedTagTriggerTags` 로 가진 어빌리티를 발동한다(언리얼 `OwnedTagAdded` 트리거).
        const ScopedListLock lock{ *this };
        AbilityEventData     eventData;
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

    void AbilityComponent::refreshOngoingTagRequirements()
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

        // 위치는 지금 읽는다. 자기 오브젝트이므로 이 워커가 읽어도 된다. 숫자 생성은 틱 중이면 틱 직후에 한다(`DamageNumberComponent::spawnNumber`).
        const SceneComponent* pRoot         = pOwner->getPrimarySceneComponent();
        const float3          position      = ( pRoot != nullptr ? pRoot->getWorldPosition() : float3{} ) + _damageNumberOffset;
        const int32           roundedDamage = static_cast<int32>( MathUtil::round( amount ) );
        if ( roundedDamage <= 0 )
            return;
        DamageNumberComponent::spawnNumber( *pManager, position, roundedDamage );
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
            const size_t specCount = _listAbilitySpec.size();
            for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
            {
                AbilitySpec& spec = *_listAbilitySpec[specIndex];
                if ( spec._bPendingRemove == SW_FALSE && isOfModule( spec._pAbility->getTypeInfo() ) )
                    retireAbilitySpec( spec );
            }

            // 2) 그 모듈의 실행 계산을 쓰는 이펙트: 다음 주기에 언로드된 코드를 부르지 않도록 해제한다.
            const size_t effectCount = _listActiveEffect.size();
            for ( size_t effectIndex = 0; effectIndex < effectCount && effectIndex < _listActiveEffect.size(); ++effectIndex )
            {
                ActiveGameplayEffect& activeEffect = *_listActiveEffect[effectIndex];
                if ( activeEffect._bPendingRemove == SW_TRUE )
                    continue;
                for ( const shared_ptr<const IGameplayEffectExecution>& pExecution : activeEffect._spec._pDef->_listExecution )
                {
                    if ( isOfModule( pExecution->getTypeInfo() ) )
                    {
                        removeActiveEffectInternal( activeEffect );
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

        struct AbilityComponentInternal
        {
            /** @brief 시간 비교의 허용 오차입니다. 0.1 초씩 더한 시간이 정확히 1 이 되지 않는 부동소수점 오차를 흡수합니다. */
            static constexpr float32 kTimeEpsilon = 1.0e-4f;

            /** @brief 지속 이펙트의 집계 결과입니다. */
            struct Aggregation
            {
                float32 _additive{ 0.0f };
                float32 _multiplyBonus{ 0.0f };
                float32 _divideBonus{ 0.0f };
                float32 _overrideValue{ 0.0f };
                bool    _bHasOverride{ false };
            };

            /** @brief 변경 하나를 스택 @p stackScale 만큼 집계에 반영합니다. */
            static void accumulate( const EvaluatedModifier& modifier, float32 stackScale, Aggregation& inoutAggregation )
            {
                switch ( modifier._op )
                {
                    case AttributeModOp::Add:
                    {
                        inoutAggregation._additive += modifier._magnitude * stackScale;
                        break;
                    }
                    case AttributeModOp::Multiply:
                    {
                        inoutAggregation._multiplyBonus += ( modifier._magnitude - 1.0f ) * stackScale;
                        break;
                    }
                    case AttributeModOp::Divide:
                    {
                        if ( modifier._magnitude != 0.0f )
                            inoutAggregation._divideBonus += ( modifier._magnitude - 1.0f ) * stackScale;
                        break;
                    }
                    case AttributeModOp::Override:
                    {
                        inoutAggregation._overrideValue = modifier._magnitude;
                        inoutAggregation._bHasOverride  = true;
                        break;
                    }
                }
            }

            /** @brief base 에 집계를 적용한 값입니다(언리얼의 `FAggregatorModChannel::EvaluateWithBase` 에 해당). */
            static float32 evaluate( float32 baseValue, const Aggregation& aggregation )
            {
                if ( aggregation._bHasOverride )
                    return aggregation._overrideValue;
                float32       value       = ( baseValue + aggregation._additive ) * ( 1.0f + aggregation._multiplyBonus );
                const float32 denominator = 1.0f + aggregation._divideBonus;
                if ( MathUtil::abs( denominator ) > kTimeEpsilon )
                    value /= denominator;
                return value;
            }

            /** @brief @p container 의 태그 중 하나라도 @p query 의 태그이거나 그 하위 태그이면 true 입니다. */
            static bool matchesAnyTag( const TagContainer& container, const TagContainer& query )
            {
                for ( const TagID& queryTag : query.getTags() )
                {
                    if ( container.hasTag( queryTag, false ) )
                        return true;
                }
                return false;
            }

            /** @brief 즉시 실행 한 번이 base 에 쓰는 값입니다. */
            static float32 applyToBase( float32 baseValue, const EvaluatedModifier& modifier, float32 stackScale )
            {
                switch ( modifier._op )
                {
                    case AttributeModOp::Add:
                    {
                        return baseValue + modifier._magnitude * stackScale;
                    }
                    case AttributeModOp::Multiply:
                    {
                        return baseValue * ( 1.0f + ( modifier._magnitude - 1.0f ) * stackScale );
                    }
                    case AttributeModOp::Divide:
                    {
                        const float32 denominator = 1.0f + ( modifier._magnitude - 1.0f ) * stackScale;
                        return MathUtil::abs( denominator ) > kTimeEpsilon ? baseValue / denominator : baseValue;
                    }
                    case AttributeModOp::Override:
                    {
                        return modifier._magnitude;
                    }
                }
                return baseValue;
            }
        };
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

namespace sw
{
    // ------------------------------------------------------------------------------
    // 2) 어트리뷰트
    // ------------------------------------------------------------------------------
    AttributeSet* AbilityComponent::addAttributeSet( unique_ptr<AttributeSet> pAttributeSet )
    {
        if ( pAttributeSet == nullptr )
            return nullptr;

        for ( const hashed_string& name : pAttributeSet->getAttributeNames() )
        {
            if ( findAttributeSetFor( name ) != nullptr )
                SW_LOG_WARNING( "addAttributeSet: attribute '%#' already exists in another set - the earlier set wins", name.c_str() );
        }

        AttributeSet* pRaw = pAttributeSet.get();
        pRaw->setOwningAbilitySystem( this );
        _listAttributeSet.push_back( std::move( pAttributeSet ) );

        // 먼저 걸린 지속 이펙트가 이 세트의 어트리뷰트를 바꾸고 있었을 수 있으므로, current 를 지금 다시 집계한다.
        for ( const hashed_string& name : pRaw->getAttributeNames() )
        {
            recomputeAttribute( name );
        }
        notifyHealthChanged( true );
        return pRaw;
    }

    AttributeSet* AbilityComponent::findAttributeSetFor( const hashed_string& name ) const
    {
        for ( const unique_ptr<AttributeSet>& pAttributeSet : _listAttributeSet )
        {
            if ( pAttributeSet->hasAttribute( name ) )
                return pAttributeSet.get();
        }
        return nullptr;
    }

    bool AbilityComponent::hasAttribute( const hashed_string& name ) const
    {
        return findAttributeSetFor( name ) != nullptr;
    }

    std::optional<float32> AbilityComponent::getAttributeValue( const hashed_string& name ) const
    {
        const AttributeSet*  pAttributeSet = findAttributeSetFor( name );
        const AttributeData* pData         = pAttributeSet != nullptr ? pAttributeSet->findAttribute( name ) : nullptr;
        if ( pData == nullptr )
            return std::nullopt;
        return pData->_currentValue;
    }

    std::optional<float32> AbilityComponent::getAttributeBaseValue( const hashed_string& name ) const
    {
        const AttributeSet*  pAttributeSet = findAttributeSetFor( name );
        const AttributeData* pData         = pAttributeSet != nullptr ? pAttributeSet->findAttribute( name ) : nullptr;
        if ( pData == nullptr )
            return std::nullopt;
        return pData->_baseValue;
    }

    bool AbilityComponent::setAttributeBaseValue( const hashed_string& name, float32 value )
    {
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = findAttributeDataMutable( name, &pAttributeSet );
        if ( pData == nullptr )
            return false;

        float32 newBase = value;
        pAttributeSet->preAttributeBaseChange( name, newBase );
        guardHealthBaseChange( name, pData->_baseValue, newBase );
        pData->_baseValue = newBase;
        recomputeAttribute( name );
        return true;
    }

    TagID AbilityComponent::getInvulnerableTag()
    {
        return "State.Invulnerable"_tag;
    }

    void AbilityComponent::guardHealthBaseChange( const hashed_string& name, float32 oldBase, float32& inoutNewBase )
    {
        const bool bHealthGoesDown = name == _healthAttribute && inoutNewBase < oldBase;
        if ( bHealthGoesDown && isInvulnerable() )
        {
            inoutNewBase = oldBase;
            ++_blockedHealthDecreaseCount;
        }
    }

    Subscription AbilityComponent::registerAttributeChanged( const hashed_string& name, const AttributeChangedDelegate& delegate )
    {
        if ( name.empty() )
            return _anyAttributeChangedMulticast.add( delegate );

        // 멀티캐스트는 힙에 두어 맵이 다시 해시해도 구독이 가리키는 주소가 그대로다.
        unique_ptr<MulticastDelegate<void( const AttributeChangeData& )>>& pMulticast = _mapAttributeChangedMulticast[name];
        if ( pMulticast == nullptr )
            pMulticast = make_unique<MulticastDelegate<void( const AttributeChangeData& )>>();
        return pMulticast->add( delegate );
    }

    // ------------------------------------------------------------------------------
    // 8) 내부: 어트리뷰트
    // ------------------------------------------------------------------------------
    void AbilityComponent::recomputeAttribute( const hashed_string& name )
    {
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = findAttributeDataMutable( name, &pAttributeSet );
        if ( pData == nullptr )
            return;

        AbilityComponentInternal::Aggregation aggregation;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            const bool bContributes = pActiveEffect->_bPendingRemove == SW_FALSE && pActiveEffect->_bInhibited == SW_FALSE;
            if ( bContributes == false )
                continue;
            const float32 stackScale = static_cast<float32>( pActiveEffect->_stackCount );
            for ( const EvaluatedModifier& modifier : pActiveEffect->_listEvaluatedModifier )
            {
                if ( modifier._attribute == name )
                    AbilityComponentInternal::accumulate( modifier, stackScale, aggregation );
            }
        }

        float32 newValue = AbilityComponentInternal::evaluate( pData->_baseValue, aggregation );
        pAttributeSet->preAttributeChange( name, newValue );
        const float32 oldValue = pData->_currentValue;
        if ( oldValue == newValue )
            return;
        pData->_currentValue = newValue;
        notifyAttributeChanged( *pAttributeSet, name, oldValue, newValue );
    }

    AttributeData* AbilityComponent::findAttributeDataMutable( const hashed_string& name, AttributeSet** ppOutSet )
    {
        for ( const unique_ptr<AttributeSet>& pAttributeSet : _listAttributeSet )
        {
            AttributeData* pData = pAttributeSet->findAttributeMutable( name );
            if ( pData == nullptr )
                continue;
            if ( ppOutSet != nullptr )
                *ppOutSet = pAttributeSet.get();
            return pData;
        }
        return nullptr;
    }

    void AbilityComponent::notifyAttributeChanged( AttributeSet& attributeSet, const hashed_string& name, float32 oldValue, float32 newValue )
    {
        attributeSet.postAttributeChange( name, oldValue, newValue );

        AttributeChangeData changeData;
        changeData._pAbilitySystem = this;
        changeData._attribute      = name;
        changeData._oldValue       = oldValue;
        changeData._newValue       = newValue;

        const auto mapIter = _mapAttributeChangedMulticast.find( name );
        if ( mapIter != _mapAttributeChangedMulticast.end() && mapIter->second != nullptr )
            mapIter->second->broadcast( changeData );
        _anyAttributeChangedMulticast.broadcast( changeData );

        const bool bHealth = name == _healthAttribute;
        if ( bHealth || name == _maxHealthAttribute )
            notifyHealthChanged( false );
        const bool bDamageTaken = bHealth && newValue < oldValue && _bShowDamageNumbers;
        if ( bDamageTaken )
            spawnDamageNumber( oldValue - newValue );
    }

} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // 4) 게임플레이 이펙트
    // ------------------------------------------------------------------------------
    AbilityEffectContext AbilityComponent::makeEffectContext() const
    {
        AbilityEffectContext context;
        const GameObject*    pOwner = getOwner();
        if ( pOwner != nullptr )
        {
            context._instigator   = pOwner->getHandle();
            context._effectCauser = pOwner->getHandle();
        }
        return context;
    }

    AbilityEffectSpec AbilityComponent::makeOutgoingSpec( const shared_ptr<const AbilityEffectDef>& pDef, int32 level ) const
    {
        AbilityEffectSpec spec;
        if ( pDef == nullptr )
            return spec;

        spec._pDef    = pDef;
        spec._level   = level;
        spec._context = makeEffectContext();
        // 쏜 쪽 어트리뷰트를 지금 저장해 둔다. 투사체가 나는 동안 쏜 쪽이 사라져도 공식은 이 값을 쓴다.
        for ( const unique_ptr<AttributeSet>& pAttributeSet : _listAttributeSet )
        {
            for ( const hashed_string& name : pAttributeSet->getAttributeNames() )
            {
                const AttributeData* pData = pAttributeSet->findAttribute( name );
                if ( pData != nullptr && spec._mapSourceAttribute.find( name ) == spec._mapSourceAttribute.end() )
                    spec._mapSourceAttribute.insert_or_assign( name, pData->_currentValue );
            }
        }
        return spec;
    }

    AbilityEffectSpec AbilityComponent::makeOutgoingSpecByID( const hashed_string& effectID, int32 level ) const
    {
        const AbilityCatalog* pCatalog = findCatalog();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING( "makeOutgoingSpecByID '%#': no AbilityCatalog", effectID.c_str() );
            return AbilityEffectSpec{};
        }
        shared_ptr<const AbilityEffectDef> pDef = pCatalog->findEffect( effectID );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "makeOutgoingSpecByID: no effect '%#' in the catalog", effectID.c_str() );
            return AbilityEffectSpec{};
        }
        return makeOutgoingSpec( pDef, level );
    }

    ActiveEffectHandle AbilityComponent::applyGameplayEffectSpecToSelf( const AbilityEffectSpec& spec )
    {
        if ( spec.isValid() == false || canApplySpec( spec ) == false )
            return ActiveEffectHandle{};

        const ScopedListLock    lock{ *this };
        const AbilityEffectDef& def = *spec._pDef;
        if ( def._removeEffectsWithTags.getTagCount() > 0 )
            (void)removeActiveEffectsWithTags( def._removeEffectsWithTags ); // 지울 이펙트가 없어도 된다

        if ( def._durationPolicy == EffectDurationPolicy::Instant )
        {
            executeSpec( spec, 1 );
            return ActiveEffectHandle{ ++_nextHandleID };
        }
        return applyDurationSpec( spec );
    }

    ActiveEffectHandle AbilityComponent::applyGameplayEffectSpecToTarget( const AbilityEffectSpec& spec, AbilityComponent* pTarget )
    {
        if ( pTarget == nullptr || spec.isValid() == false )
            return ActiveEffectHandle{};
        if ( pTarget->canMutateNow() )
            return pTarget->applyGameplayEffectSpecToSelf( spec );

        // 다른 워커가 대상을 틱하고 있을 수 있으므로 틱 직후 게임 스레드에서 건다. 그 사이 대상이 사라질 수 있어서 핸들로 다시 찾는다.
        GameObject*                              pTargetOwner = pTarget->getOwner();
        GameObjectManager*                       pManager     = pTargetOwner != nullptr ? pTargetOwner->getManager() : nullptr;
        const TComponentHandle<AbilityComponent> targetHandle{ pTarget };
        if ( pManager == nullptr )
            return ActiveEffectHandle{};
        pManager->deferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, targetHandle, spec]()
        {
            AbilityComponent* pResolved = pManager->resolveComponent( targetHandle );
            if ( pResolved != nullptr )
                (void)pResolved->applyGameplayEffectSpecToSelf( spec ); // 미룬 적용은 핸들을 돌려줄 곳이 없다
        } ) );
        return ActiveEffectHandle{};
    }

    ActiveEffectHandle AbilityComponent::applyGameplayEffectToSelf( const hashed_string& effectID, int32 level )
    {
        const AbilityEffectSpec spec = makeOutgoingSpecByID( effectID, level );
        if ( spec.isValid() == false )
            return ActiveEffectHandle{};
        return applyGameplayEffectSpecToSelf( spec );
    }

    bool AbilityComponent::removeActiveEffect( ActiveEffectHandle handle, int32 stacksToRemove )
    {
        const ScopedListLock  lock{ *this };
        ActiveGameplayEffect* pActiveEffect = findActiveEffectMutable( handle );
        if ( pActiveEffect == nullptr )
            return false;

        if ( stacksToRemove > 0 && pActiveEffect->_stackCount > stacksToRemove )
        {
            pActiveEffect->_stackCount -= stacksToRemove;
            for ( const EvaluatedModifier& modifier : pActiveEffect->_listEvaluatedModifier )
            {
                recomputeAttribute( modifier._attribute );
            }
            return true;
        }
        removeActiveEffectInternal( *pActiveEffect );
        return true;
    }

    int32 AbilityComponent::removeActiveEffectsWithTags( const TagContainer& tags )
    {
        if ( tags.getTagCount() == 0 )
            return 0;

        const ScopedListLock lock{ *this };
        int32                removedCount = 0;
        const size_t         effectCount  = _listActiveEffect.size();
        for ( size_t effectIndex = 0; effectIndex < effectCount; ++effectIndex )
        {
            ActiveGameplayEffect* pActiveEffect = _listActiveEffect[effectIndex].get();
            if ( pActiveEffect->_bPendingRemove == SW_TRUE )
                continue;
            const bool bMatches = AbilityComponentInternal::matchesAnyTag( pActiveEffect->_spec._pDef->_assetTags, tags ) ||
                                  AbilityComponentInternal::matchesAnyTag( pActiveEffect->_grantedTags, tags );
            if ( bMatches == false )
                continue;
            removeActiveEffectInternal( *pActiveEffect );
            ++removedCount;
        }
        return removedCount;
    }

    const ActiveGameplayEffect* AbilityComponent::findActiveEffect( ActiveEffectHandle handle ) const
    {
        if ( handle.isValid() == false )
            return nullptr;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            if ( pActiveEffect->_handle == handle && pActiveEffect->_bPendingRemove == SW_FALSE )
                return pActiveEffect.get();
        }
        return nullptr;
    }

    uint32 AbilityComponent::getActiveEffectCount() const
    {
        uint32 activeCount = 0;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            if ( pActiveEffect->_bPendingRemove == SW_FALSE )
                ++activeCount;
        }
        return activeCount;
    }

    float32 AbilityComponent::getActiveEffectRemainingTime( ActiveEffectHandle handle ) const
    {
        const ActiveGameplayEffect* pActiveEffect = findActiveEffect( handle );
        if ( pActiveEffect == nullptr || pActiveEffect->_spec._pDef->_durationPolicy != EffectDurationPolicy::HasDuration )
            return 0.0f;
        return MathUtil::max( 0.0f, pActiveEffect->_remainingTime );
    }

    int32 AbilityComponent::getActiveEffectStackCount( ActiveEffectHandle handle ) const
    {
        const ActiveGameplayEffect* pActiveEffect = findActiveEffect( handle );
        return pActiveEffect != nullptr ? pActiveEffect->_stackCount : 0;
    }

    float32 AbilityComponent::findLongestRemainingTimeWithGrantedTags( const TagContainer& tags ) const
    {
        float32 longestTime = 0.0f;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            const bool bTimed = pActiveEffect->_bPendingRemove == SW_FALSE && pActiveEffect->_spec._pDef->_durationPolicy == EffectDurationPolicy::HasDuration;
            if ( bTimed && AbilityComponentInternal::matchesAnyTag( pActiveEffect->_grantedTags, tags ) )
                longestTime = MathUtil::max( longestTime, pActiveEffect->_remainingTime );
        }
        return longestTime;
    }

    bool AbilityComponent::canApplyAttributeModifiers( const AbilityEffectSpec& spec ) const
    {
        if ( spec.isValid() == false )
            return false;
        for ( const AbilityEffectModifier& modifier : spec._pDef->_listModifier )
        {
            if ( modifier._op != AttributeModOp::Add )
                continue;
            const std::optional<float32> current = getAttributeValue( modifier._attribute );
            if ( current.has_value() == false )
                return false;
            float32 magnitude = 0.0f;
            if ( computeModifierMagnitude( modifier, spec, magnitude ) == false )
                return false;
            if ( *current + magnitude < 0.0f )
                return false;
        }
        return true;
    }

    Subscription AbilityComponent::registerEffectApplied( const EffectDelegate& delegate )
    {
        return _effectAppliedMulticast.add( delegate );
    }

    Subscription AbilityComponent::registerEffectRemoved( const EffectDelegate& delegate )
    {
        return _effectRemovedMulticast.add( delegate );
    }

    // ------------------------------------------------------------------------------
    // 7) 내부: 이펙트
    // ------------------------------------------------------------------------------
    bool AbilityComponent::canApplySpec( const AbilityEffectSpec& spec ) const
    {
        const AbilityEffectDef& def = *spec._pDef;
        if ( _ownedTagCounts.hasAllMatchingTags( def._applicationRequiredTags ) == false )
            return false;
        return _ownedTagCounts.hasAnyMatchingTags( def._applicationBlockedTags ) == false;
    }

    bool AbilityComponent::computeModifierMagnitude( const AbilityEffectModifier& modifier, const AbilityEffectSpec& spec, float32& outMagnitude ) const
    {
        switch ( modifier._magnitudeSource )
        {
            case EffectMagnitudeSource::ScalableFloat:
            {
                outMagnitude = modifier._scalableMagnitude.compute( spec._level );
                return true;
            }
            case EffectMagnitudeSource::AttributeBased:
            {
                float32 attributeValue = 0.0f;
                if ( modifier._bFromSource == SW_TRUE )
                {
                    if ( spec.findSourceAttribute( modifier._backingAttribute, attributeValue ) == false )
                        return false;
                }
                else
                {
                    const std::optional<float32> backing = getAttributeValue( modifier._backingAttribute );
                    if ( backing.has_value() == false )
                        return false;
                    attributeValue = *backing;
                }
                outMagnitude = ( attributeValue + modifier._preMultiplyAdditive ) * modifier._coefficient + modifier._postMultiplyAdditive;
                return true;
            }
            case EffectMagnitudeSource::SetByCaller:
            {
                const auto mapIter = spec._mapSetByCaller.find( modifier._setByCallerName );
                if ( mapIter == spec._mapSetByCaller.end() )
                {
                    SW_LOG_WARNING( "Effect '%#': SetByCaller '%#' was not set - using 0", spec._pDef->_id.c_str(), modifier._setByCallerName.c_str() );
                    outMagnitude = 0.0f;
                    return true;
                }
                outMagnitude = mapIter->second;
                return true;
            }
        }
        return false;
    }

    void AbilityComponent::evaluateExecutionModifiers( const AbilityEffectSpec& spec, int32 stackCount, vector<EvaluatedModifier>& outListModifier ) const
    {
        const AbilityEffectDef& def = *spec._pDef;
        for ( const AbilityEffectModifier& modifier : def._listModifier )
        {
            EvaluatedModifier evaluated;
            evaluated._attribute = modifier._attribute;
            evaluated._op        = modifier._op;
            if ( computeModifierMagnitude( modifier, spec, evaluated._magnitude ) )
                outListModifier.push_back( evaluated );
        }

        AbilityEffectExecutionParams params;
        params._pSpec      = &spec;
        params._pTarget    = this;
        params._stackCount = stackCount;
        for ( const shared_ptr<const IGameplayEffectExecution>& pExecution : def._listExecution )
        {
            if ( pExecution != nullptr )
                pExecution->execute( params, outListModifier );
        }
    }

    void AbilityComponent::executeSpec( const AbilityEffectSpec& spec, int32 stackCount )
    {
        vector<EvaluatedModifier> listModifier;
        evaluateExecutionModifiers( spec, stackCount, listModifier );

        const float32 stackScale     = static_cast<float32>( MathUtil::max( 1, stackCount ) );
        float32       totalMagnitude = 0.0f;
        const uint32  blockedBefore  = _blockedHealthDecreaseCount;
        for ( const EvaluatedModifier& modifier : listModifier )
        {
            applyModifierToBase( modifier, stackScale, spec );
            if ( modifier._op == AttributeModOp::Add )
                totalMagnitude += modifier._magnitude * stackScale;
        }
        // 결정: 무적이 체력 내림을 막은 피격은 맞은 것이 아니므로 즉시 큐(피격 섬광 · 소리)를 내지 않는다 — 언리얼 Lyra 의 `Gameplay.DamageImmunity` 가 이펙트를 막아 큐도 안 나가는 것과 같은 결과다.
        if ( _blockedHealthDecreaseCount != blockedBefore )
            return;
        dispatchCues( spec, AbilityPresentationPhase::Executed, totalMagnitude );
    }

    void AbilityComponent::applyModifierToBase( const EvaluatedModifier& modifier, float32 stackScale, const AbilityEffectSpec& spec )
    {
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = findAttributeDataMutable( modifier._attribute, &pAttributeSet );
        if ( pData == nullptr )
        {
            SW_LOG_WARNING( "Effect '%#' modifies '%#' which this object does not have - skipped", spec._pDef->_id.c_str(), modifier._attribute.c_str() );
            return;
        }

        const float32 oldBase = pData->_baseValue;
        float32       newBase = AbilityComponentInternal::applyToBase( oldBase, modifier, stackScale );
        pAttributeSet->preAttributeBaseChange( modifier._attribute, newBase );
        guardHealthBaseChange( modifier._attribute, oldBase, newBase );
        pData->_baseValue = newBase;
        recomputeAttribute( modifier._attribute );

        // 훅이 다른 어트리뷰트(체력)를 바꿔도 세트 목록은 건드리지 않으므로, 세트 포인터를 그대로 써도 된다.
        AttributeModCallbackData callbackData;
        callbackData._pSpec     = &spec;
        callbackData._pTarget   = this;
        callbackData._attribute = modifier._attribute;
        callbackData._magnitude = modifier._op == AttributeModOp::Add ? modifier._magnitude * stackScale : modifier._magnitude;
        callbackData._deltaBase = newBase - oldBase;
        callbackData._op        = modifier._op;
        pAttributeSet->postGameplayEffectExecute( callbackData );
    }

    ActiveEffectHandle AbilityComponent::applyDurationSpec( const AbilityEffectSpec& spec )
    {
        const AbilityEffectDef& def = *spec._pDef;

        // 스택: 대상에 하나만 두고 스택 수를 올린다.
        if ( def._stackingPolicy == EffectStackingPolicy::AggregateByTarget )
        {
            ActiveGameplayEffect* pExisting = findStackableEffect( def );
            if ( pExisting != nullptr )
            {
                if ( pExisting->_stackCount < def._stackLimit )
                {
                    ++pExisting->_stackCount;
                    for ( const EvaluatedModifier& modifier : pExisting->_listEvaluatedModifier )
                    {
                        recomputeAttribute( modifier._attribute );
                    }
                }
                if ( def._bRefreshDurationOnStack == SW_TRUE )
                {
                    pExisting->_duration      = spec.computeDuration();
                    pExisting->_remainingTime = pExisting->_duration;
                }
                dispatchCues( pExisting->_spec, AbilityPresentationPhase::Added, static_cast<float32>( pExisting->_stackCount ) );
                _effectAppliedMulticast.broadcast( *pExisting );
                return pExisting->_handle;
            }
        }

        unique_ptr<ActiveGameplayEffect> pNewEffect = make_unique<ActiveGameplayEffect>();
        pNewEffect->_handle                         = ActiveEffectHandle{ ++_nextHandleID };
        pNewEffect->_spec                           = spec;
        pNewEffect->_duration                       = spec.computeDuration();
        pNewEffect->_remainingTime                  = pNewEffect->_duration;
        pNewEffect->_periodTimer                    = def._period;
        pNewEffect->_startTime                      = _time;
        pNewEffect->_stackCount                     = 1;
        pNewEffect->_grantedTags                    = def._grantedTags;
        for ( const TagID& tag : spec._dynamicGrantedTags.getTags() )
        {
            pNewEffect->_grantedTags.addTag( tag );
        }

        // 주기 이펙트의 모디파이어는 주기마다 base 에 쓴다(도트 대미지). 걸려 있는 동안 current 에는 반영하지 않는다(언리얼과 같음).
        if ( def.isPeriodic() == false )
        {
            for ( const AbilityEffectModifier& modifier : def._listModifier )
            {
                EvaluatedModifier evaluated;
                evaluated._attribute = modifier._attribute;
                evaluated._op        = modifier._op;
                if ( computeModifierMagnitude( modifier, spec, evaluated._magnitude ) )
                    pNewEffect->_listEvaluatedModifier.push_back( evaluated );
            }
        }

        // 걸린 동안의 태그 조건을 지금 못 맞추면 멈춘 채로 건다 — 태그가 생기면 `refreshOngoingTagRequirements` 가 붙인다.
        pNewEffect->_bInhibited = _ownedTagCounts.hasAllMatchingTags( def._ongoingRequiredTags ) ? SW_FALSE : SW_TRUE;

        ActiveGameplayEffect* pActiveEffect = pNewEffect.get();
        _listActiveEffect.push_back( std::move( pNewEffect ) );

        if ( pActiveEffect->_bInhibited == SW_FALSE )
            engageActiveEffect( *pActiveEffect );
        dispatchCues( pActiveEffect->_spec, AbilityPresentationPhase::Added, 1.0f );
        _effectAppliedMulticast.broadcast( *pActiveEffect );

        const bool bExecuteNow = def.isPeriodic() && def._bExecutePeriodicOnApplication == SW_TRUE && pActiveEffect->_bPendingRemove == SW_FALSE &&
                                 pActiveEffect->_bInhibited == SW_FALSE;
        if ( bExecuteNow )
            executeSpec( pActiveEffect->_spec, pActiveEffect->_stackCount );
        return pActiveEffect->_handle;
    }

    void AbilityComponent::removeActiveEffectInternal( ActiveGameplayEffect& activeEffect )
    {
        if ( activeEffect._bPendingRemove == SW_TRUE )
            return;
        activeEffect._bPendingRemove = SW_TRUE;
        _bHasPendingRemoval          = SW_TRUE;

        // 멈춘 이펙트는 이미 뗐다.
        if ( activeEffect._bInhibited == SW_FALSE )
            disengageActiveEffect( activeEffect );
        dispatchCues( activeEffect._spec, AbilityPresentationPhase::Removed, 0.0f );
        _effectRemovedMulticast.broadcast( activeEffect );
    }

    void AbilityComponent::engageActiveEffect( ActiveGameplayEffect& activeEffect )
    {
        _ownedTagCounts.updateCounts( activeEffect._grantedTags, 1 );
        for ( const hashed_string& abilityID : activeEffect._spec._pDef->_listGrantedAbility )
        {
            const AbilitySpecHandle handle = giveAbilityByID( abilityID, activeEffect._spec._level );
            if ( handle.isValid() )
                activeEffect._listGrantedAbilityHandle.push_back( handle );
        }
        for ( const EvaluatedModifier& modifier : activeEffect._listEvaluatedModifier )
        {
            recomputeAttribute( modifier._attribute );
        }
    }

    void AbilityComponent::disengageActiveEffect( ActiveGameplayEffect& activeEffect )
    {
        _ownedTagCounts.updateCounts( activeEffect._grantedTags, -1 );
        for ( const AbilitySpecHandle& handle : activeEffect._listGrantedAbilityHandle )
        {
            (void)clearAbility( handle ); // 게임이 먼저 거뒀으면 없다 — 거둔 것이 뜻이다
        }
        activeEffect._listGrantedAbilityHandle.clear();
        for ( const EvaluatedModifier& modifier : activeEffect._listEvaluatedModifier )
        {
            recomputeAttribute( modifier._attribute );
        }
    }

    ActiveGameplayEffect* AbilityComponent::findActiveEffectMutable( ActiveEffectHandle handle )
    {
        return const_cast<ActiveGameplayEffect*>( findActiveEffect( handle ) );
    }

    ActiveGameplayEffect* AbilityComponent::findStackableEffect( const AbilityEffectDef& def )
    {
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            if ( pActiveEffect->_bPendingRemove == SW_TRUE )
                continue;
            // 같은 정의 객체이거나 같은 id 이면 같은 이펙트로 본다. 카탈로그를 다시 읽어 정의 객체가 바뀌어도 같은 이펙트로 쌓인다.
            const AbilityEffectDef& activeDef = *pActiveEffect->_spec._pDef;
            const bool              bSameDef  = &activeDef == &def || ( def._id.empty() == false && activeDef._id == def._id );
            if ( bSameDef )
                return pActiveEffect.get();
        }
        return nullptr;
    }

    void AbilityComponent::updateActiveEffects( float32 deltaTime )
    {
        const size_t effectCount = _listActiveEffect.size();
        for ( size_t effectIndex = 0; effectIndex < effectCount && effectIndex < _listActiveEffect.size(); ++effectIndex )
        {
            // 원소가 `unique_ptr` 이고 잠금 중에는 지우지 않으므로, 콜백이 목록을 늘려도 이 포인터는 유효하다.
            ActiveGameplayEffect* pActiveEffect = _listActiveEffect[effectIndex].get();
            if ( pActiveEffect->_bPendingRemove == SW_TRUE )
                continue;

            const AbilityEffectDef& def       = *pActiveEffect->_spec._pDef;
            const bool              bPeriodic = def.isPeriodic();
            const bool              bTimed    = def._durationPolicy == EffectDurationPolicy::HasDuration;
            if ( bPeriodic == false && bTimed == false )
                continue;

            // 남은 시간을 다음 사건(주기 실행이나 만료)까지 잘라서 쓴다. 한 프레임이 주기 여러 개를 넘어도 순서대로 실행하고, 만료된 뒤의 주기는 실행하지 않는다.
            float32 timeLeft = deltaTime;
            while ( timeLeft > 0.0f && pActiveEffect->_bPendingRemove == SW_FALSE )
            {
                float32 step = timeLeft;
                if ( bPeriodic )
                    step = MathUtil::min( step, MathUtil::max( 0.0f, pActiveEffect->_periodTimer ) );
                if ( bTimed )
                    step = MathUtil::min( step, MathUtil::max( 0.0f, pActiveEffect->_remainingTime ) );
                timeLeft -= step;

                if ( bPeriodic )
                {
                    pActiveEffect->_periodTimer -= step;
                    if ( pActiveEffect->_periodTimer <= AbilityComponentInternal::kTimeEpsilon )
                    {
                        pActiveEffect->_periodTimer += def._period;
                        // 멈춘 이펙트는 주기를 세되 실행하지 않는다(언리얼 Inhibition 과 같다).
                        if ( pActiveEffect->_bInhibited == SW_FALSE )
                            executeSpec( pActiveEffect->_spec, pActiveEffect->_stackCount );
                    }
                }
                if ( bTimed && pActiveEffect->_bPendingRemove == SW_FALSE )
                {
                    pActiveEffect->_remainingTime -= step;
                    if ( pActiveEffect->_remainingTime > AbilityComponentInternal::kTimeEpsilon )
                        continue;

                    const bool bRemoveSingleStack = def._stackExpirationPolicy == EffectStackExpirationPolicy::RemoveSingleStackAndRefreshDuration &&
                                                    pActiveEffect->_stackCount > 1 && pActiveEffect->_duration > AbilityComponentInternal::kTimeEpsilon;
                    if ( bRemoveSingleStack )
                    {
                        --pActiveEffect->_stackCount;
                        pActiveEffect->_remainingTime = pActiveEffect->_duration;
                        for ( const EvaluatedModifier& modifier : pActiveEffect->_listEvaluatedModifier )
                        {
                            recomputeAttribute( modifier._attribute );
                        }
                    }
                    else
                    {
                        removeActiveEffectInternal( *pActiveEffect );
                    }
                }
            }
        }
    }

    void AbilityComponent::dispatchCues( const AbilityEffectSpec& spec, AbilityPresentationPhase phase, float32 magnitude )
    {
        const TagContainer& cueTags = spec._pDef->_cueTags;
        if ( cueTags.getTagCount() == 0 )
            return;

        const GameObject* pOwner = getOwner();
        for ( const TagID& cueTag : cueTags.getTags() )
        {
            AbilityPresentationEvent cueEvent;
            cueEvent._cueTag     = cueTag;
            cueEvent._target     = pOwner != nullptr ? pOwner->getHandle() : GameObjectHandle{};
            cueEvent._instigator = spec._context._instigator;
            cueEvent._magnitude  = magnitude;
            cueEvent._phase      = phase;
            _gameplayCueMulticast.broadcast( cueEvent );
            GameEventUtil::send( cueEvent );
            playCueDef( cueTag, phase );
        }
    }

    void AbilityComponent::playCueDef( TagID cueTag, AbilityPresentationPhase phase ) const
    {
#if defined( SW_WITH_CLIENT_CODE )
        // 결정: 큐 태그 → 연출은 데이터(카탈로그의 `<AbilityPresentation>`)가 정한다. 큐를 내는 길은 이 함수 하나라 즉시 · 주기 · 걸림 · 해제가 모두 같은 표를 본다.
        const AbilityCatalog*         pCatalog = findCatalog();
        const AbilityPresentationDef* pCueDef  = pCatalog != nullptr ? pCatalog->findCue( cueTag ) : nullptr;
        const bool                    bShows   = pCueDef != nullptr && ( phase != AbilityPresentationPhase::Removed || pCueDef->_bOnRemoved == SW_TRUE );
        if ( bShows == false )
            return;
        const GameObject*  pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 자리는 지금 읽는다(자기 오브젝트라 이 워커가 읽어도 된다). 스폰 · 소리는 게임 스레드의 틱 뒤에 한다.
        const SceneComponent*        pRoot    = pOwner->getPrimarySceneComponent();
        const float3                 position = ( pRoot != nullptr ? pRoot->getWorldPosition() : float3{} ) + pCueDef->_offset;
        const AbilityPresentationDef cueDef   = *pCueDef;
        pManager->executeOrDeferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, cueDef, position]()
        {
            if ( cueDef._prefab.empty() == false )
            {
                AssetManager*   pAssetManager = game::findSubsystem<AssetManager>();
                GameObject*     pSpawned      = pAssetManager != nullptr ? pAssetManager->getAssetCache<PrefabCache>().spawn( pManager, cueDef._prefab.c_str() ) : nullptr;
                SceneComponent* pScene        = pSpawned != nullptr ? pSpawned->getPrimarySceneComponent() : nullptr;
                if ( pScene != nullptr )
                    pScene->setWorldPosition( position );
                else
                    SW_LOG_WARNING( "Gameplay cue '%#': prefab '%#' could not be spawned", cueDef._cueTag.getString(), cueDef._prefab.c_str() );
            }
            // 재생 id 는 쓰지 않는다(원샷). 오디오 서비스가 없는 구성(헤드리스)에서는 0 이다.
            if ( cueDef._soundEvent.empty() == false && cueDef._bPositional == SW_TRUE )
                (void)GameSound::postEventAt( cueDef._soundEvent, position );
            else if ( cueDef._soundEvent.empty() == false )
                (void)GameSound::postEvent( cueDef._soundEvent );
        } ) );
#else
        (void)cueTag;
        (void)phase;
#endif
    }
} // namespace sw
