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

    namespace
    {
        struct AbilitySystemComponentInternal
        {
            /** @brief 시간 비교의 허용 오차입니다 — 0.1 초씩 더한 시간이 정확히 1 이 되지 않는 것을 흡수합니다. */
            static constexpr float32 kTimeEpsilon = 1.0e-4f;

            /** @brief @p container 의 태그 중 하나라도 @p query 의 태그(또는 그 하위)이면 true 입니다. */
            static bool matchesAnyTag( const TagContainer& container, const TagContainer& query )
            {
                for ( const TagID& queryTag : query.getTags() )
                {
                    if ( container.hasTag( queryTag, false ) )
                        return true;
                }
                return false;
            }

            /** @brief 지속 이펙트의 집계 결과입니다. */
            struct Aggregation
            {
                float32 _additive{ 0.0f };
                float32 _multiplyBonus{ 0.0f };
                float32 _divideBonus{ 0.0f };
                float32 _overrideValue{ 0.0f };
                bool    _bHasOverride{ false };
            };

            /** @brief 변경 하나를 스택 @p stackScale 만큼 집계에 더합니다. */
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

            /** @brief base 에 집계를 얹은 값입니다(언리얼 `FAggregatorModChannel::EvaluateWithBase`). */
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
    /**
     * @class AbilitySystemModuleUnloadGuard
     * @brief 모듈(DLL · SO)을 내리기 전에 그 모듈의 코드(게임 어빌리티 · 어트리뷰트 묶음 · 실행 계산 · 델리게이트)를 컴포넌트에서 뗍니다.
     * @details 게임 모듈이 준 어빌리티 인스턴스는 vtable 이 그 모듈 안에 있습니다. 핫 리로드가 모듈을 내린 뒤 컴포넌트가 그것을 지우면 내려간
     *          코드로 뜁니다. 그래서 컴포넌트마다 보유자 하나를 두고(`IModuleUnloadListener`), 내리기 전에 그 범위의 것을 지금(코드가 아직 있을 때) 지웁니다.
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
        , _mapTagCount{}
        , _mapBlockedAbilityTag{}
        , _ownedTags{}
        , _mapAttributeChangedMulticast{}
        , _mapGameplayEventMulticast{}
        , _anyAttributeChangedMulticast{}
        , _tagChangedMulticast{}
        , _effectAppliedMulticast{}
        , _effectRemovedMulticast{}
        , _abilityActivatedMulticast{}
        , _abilityEndedMulticast{}
        , _gameplayCueMulticast{}
        , _pModuleCodeGuard{ nullptr }
        , _pCatalog{ nullptr }
        , _time{ 0.0f }
        , _nextHandleId{ 0 }
        , _listLockDepth{ 0 }
        , _bHasPendingRemoval{ SW_FALSE }
        , _abilitySetId{}
        , _healthAttribute{ "Health" }
        , _maxHealthAttribute{ "MaxHealth" }
        , _damageNumberOffset{ 0.0f, 0.6f, 0.0f }
        , _bShowDamageNumbers{ false }
    {
        _pModuleCodeGuard = make_unique<AbilitySystemModuleUnloadGuard>( *this );
    }

    AbilitySystemComponent::~AbilitySystemComponent()
    {
        // 순서: 더는 훑이지 않게 → 바깥 구독을 떼고(지우는 중에 게임 코드를 부르지 않는다) → 어빌리티(작업이 이 컴포넌트에서 구독을 뗀다) → 나머지.
        _pModuleCodeGuard.reset();
        _anyAttributeChangedMulticast.removeAll();
        _tagChangedMulticast.removeAll();
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
        if ( _abilitySetId.empty() == false )
            (void)grantAbilitySet( _abilitySetId ); // 실패는 안에서 알린다
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

    // ------------------------------------------------------------------------------
    // 1) 시간 · 카탈로그 · 세트
    // ------------------------------------------------------------------------------
    void AbilitySystemComponent::advanceTime( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;

        const ScopedListLock lock{ *this };
        _time += deltaTime;
        updateActiveEffects( deltaTime );
        tickActiveAbilities( deltaTime );
    }

    const AbilityCatalog* AbilitySystemComponent::findCatalog() const
    {
        if ( _pCatalog != nullptr )
            return _pCatalog;
        return game::getService<AbilityCatalog>();
    }

    bool AbilitySystemComponent::grantAbilitySet( const hashed_string& setId )
    {
        const AbilityCatalog* pCatalog = findCatalog();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING( "grantAbilitySet '%#': no AbilityCatalog (setCatalog or bind it as a game service)", setId.c_str() );
            return false;
        }
        const AbilitySetDef* pSet = pCatalog->findAbilitySet( setId );
        if ( pSet == nullptr )
        {
            SW_LOG_WARNING( "grantAbilitySet: no ability set '%#' in the catalog", setId.c_str() );
            return false;
        }

        // 묶음의 기본값은 붙이기 **전에** 정한다 — 붙인 뒤에 정하면 "체력 ≤ 최대 체력" 같은 훅이 정하는 순서에 따라 값을 자른다.
        for ( const AbilitySetAttributeSetEntry& setEntry : pSet->_listAttributeSet )
        {
            unique_ptr<AttributeSet> pAttributeSet = pCatalog->createAttributeSet( setEntry._className );
            if ( pAttributeSet == nullptr )
            {
                SW_LOG_WARNING( "grantAbilitySet '%#': attribute set class '%#' is not registered", setId.c_str(), setEntry._className.c_str() );
                continue;
            }
            for ( const AbilitySetAttributeEntry& attributeEntry : setEntry._listAttribute )
            {
                pAttributeSet->defineAttribute( attributeEntry._attribute, attributeEntry._baseValue );
                if ( attributeEntry._bHasRange == SW_TRUE )
                    pAttributeSet->setAttributeRange( attributeEntry._attribute, attributeEntry._minValue, attributeEntry._maxValue );
            }
            (void)addAttributeSet( std::move( pAttributeSet ) ); // 방금 만든 묶음이라 nullptr 이 아니다
        }

        for ( const TagID& tag : pSet->_looseTags.getTags() )
        {
            addLooseTag( tag );
        }
        for ( const AbilitySetAbilityEntry& abilityEntry : pSet->_listAbility )
        {
            (void)giveAbilityById( abilityEntry._abilityId, abilityEntry._level, abilityEntry._inputId ); // 실패는 안에서 알린다
        }
        for ( const AbilitySetEffectEntry& effectEntry : pSet->_listEffect )
        {
            (void)applyGameplayEffectToSelf( effectEntry._effectId, effectEntry._level ); // 태그 조건에 막힌 시작 이펙트는 걸리지 않는다
        }
        return true;
    }

    // ------------------------------------------------------------------------------
    // 2) 어트리뷰트
    // ------------------------------------------------------------------------------
    AttributeSet* AbilitySystemComponent::addAttributeSet( unique_ptr<AttributeSet> pAttributeSet )
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

        // 먼저 걸린 지속 이펙트가 이 묶음의 어트리뷰트를 바꾸고 있었을 수 있다 — current 를 지금 다시 집계한다.
        for ( const hashed_string& name : pRaw->getAttributeNames() )
        {
            recomputeAttribute( name );
        }
        notifyHealthChanged( true );
        return pRaw;
    }

    AttributeSet* AbilitySystemComponent::findAttributeSetFor( const hashed_string& name ) const
    {
        for ( const unique_ptr<AttributeSet>& pAttributeSet : _listAttributeSet )
        {
            if ( pAttributeSet->hasAttribute( name ) )
                return pAttributeSet.get();
        }
        return nullptr;
    }

    bool AbilitySystemComponent::hasAttribute( const hashed_string& name ) const
    {
        return findAttributeSetFor( name ) != nullptr;
    }

    float32 AbilitySystemComponent::getAttributeValue( const hashed_string& name, float32 fallback ) const
    {
        const AttributeSet*  pAttributeSet = findAttributeSetFor( name );
        const AttributeData* pData         = pAttributeSet != nullptr ? pAttributeSet->findAttribute( name ) : nullptr;
        return pData != nullptr ? pData->_currentValue : fallback;
    }

    float32 AbilitySystemComponent::getAttributeBaseValue( const hashed_string& name, float32 fallback ) const
    {
        const AttributeSet*  pAttributeSet = findAttributeSetFor( name );
        const AttributeData* pData         = pAttributeSet != nullptr ? pAttributeSet->findAttribute( name ) : nullptr;
        return pData != nullptr ? pData->_baseValue : fallback;
    }

    bool AbilitySystemComponent::setAttributeBaseValue( const hashed_string& name, float32 value )
    {
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = findAttributeDataMutable( name, &pAttributeSet );
        if ( pData == nullptr )
            return false;

        float32 newBase = value;
        pAttributeSet->preAttributeBaseChange( name, newBase );
        pData->_baseValue = newBase;
        recomputeAttribute( name );
        return true;
    }

    DelegateHandle AbilitySystemComponent::registerAttributeChanged( const hashed_string& name, const AttributeChangedDelegate& delegate )
    {
        if ( name.empty() )
            return _anyAttributeChangedMulticast.add( delegate );

        unique_ptr<MulticastDelegate<void( const AttributeChangeData& )>>& pMulticast = _mapAttributeChangedMulticast[name];
        if ( pMulticast == nullptr )
            pMulticast = make_unique<MulticastDelegate<void( const AttributeChangeData& )>>();
        return pMulticast->add( delegate );
    }

    void AbilitySystemComponent::unregisterAttributeChanged( const hashed_string& name, DelegateHandle handle )
    {
        if ( name.empty() )
        {
            _anyAttributeChangedMulticast.remove( handle );
            return;
        }
        const auto mapIter = _mapAttributeChangedMulticast.find( name );
        if ( mapIter != _mapAttributeChangedMulticast.end() && mapIter->second != nullptr )
            mapIter->second->remove( handle );
    }

    // ------------------------------------------------------------------------------
    // 3) 태그
    // ------------------------------------------------------------------------------
    bool AbilitySystemComponent::hasMatchingTag( TagID tag ) const
    {
        return tag.isValid() && _ownedTags.hasTag( tag, false );
    }

    bool AbilitySystemComponent::hasAllMatchingTags( const TagContainer& tags ) const
    {
        for ( const TagID& tag : tags.getTags() )
        {
            if ( hasMatchingTag( tag ) == false )
                return false;
        }
        return true;
    }

    bool AbilitySystemComponent::hasAnyMatchingTags( const TagContainer& tags ) const
    {
        return AbilitySystemComponentInternal::matchesAnyTag( _ownedTags, tags );
    }

    int32 AbilitySystemComponent::getTagCount( TagID tag ) const
    {
        const auto mapIter = _mapTagCount.find( tag._id );
        return mapIter != _mapTagCount.end() ? mapIter->second._count : 0;
    }

    void AbilitySystemComponent::addLooseTag( TagID tag, int32 count )
    {
        if ( count > 0 )
            updateTagCount( tag, count );
    }

    void AbilitySystemComponent::removeLooseTag( TagID tag, int32 count )
    {
        if ( count > 0 )
            updateTagCount( tag, -count );
    }

    DelegateHandle AbilitySystemComponent::registerTagChanged( const TagChangedDelegate& delegate )
    {
        return _tagChangedMulticast.add( delegate );
    }

    void AbilitySystemComponent::unregisterTagChanged( DelegateHandle handle )
    {
        _tagChangedMulticast.remove( handle );
    }

    // ------------------------------------------------------------------------------
    // 4) 게임플레이 이펙트
    // ------------------------------------------------------------------------------
    GameplayEffectContext AbilitySystemComponent::makeEffectContext() const
    {
        GameplayEffectContext context;
        const GameObject*     pOwner = getOwner();
        if ( pOwner != nullptr )
        {
            context._instigator   = pOwner->getHandle();
            context._effectCauser = pOwner->getHandle();
        }
        return context;
    }

    GameplayEffectSpec AbilitySystemComponent::makeOutgoingSpec( const shared_ptr<const GameplayEffectDef>& pDef, int32 level ) const
    {
        GameplayEffectSpec spec;
        if ( pDef == nullptr )
            return spec;

        spec._pDef    = pDef;
        spec._level   = level;
        spec._context = makeEffectContext();
        // 쏜 쪽 어트리뷰트를 지금 찍어 둔다 — 투사체가 나는 동안 쏜 쪽이 사라져도 공식은 이 값을 쓴다.
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

    GameplayEffectSpec AbilitySystemComponent::makeOutgoingSpecById( const hashed_string& effectId, int32 level ) const
    {
        const AbilityCatalog* pCatalog = findCatalog();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING( "makeOutgoingSpecById '%#': no AbilityCatalog", effectId.c_str() );
            return GameplayEffectSpec{};
        }
        shared_ptr<const GameplayEffectDef> pDef = pCatalog->findEffect( effectId );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "makeOutgoingSpecById: no effect '%#' in the catalog", effectId.c_str() );
            return GameplayEffectSpec{};
        }
        return makeOutgoingSpec( pDef, level );
    }

    ActiveEffectHandle AbilitySystemComponent::applyGameplayEffectSpecToSelf( const GameplayEffectSpec& spec )
    {
        if ( spec.isValid() == false || canApplySpec( spec ) == false )
            return ActiveEffectHandle{};

        const ScopedListLock     lock{ *this };
        const GameplayEffectDef& def = *spec._pDef;
        if ( def._removeEffectsWithTags.getTagCount() > 0 )
            (void)removeActiveEffectsWithTags( def._removeEffectsWithTags ); // 지울 것이 없어도 된다

        if ( def._durationPolicy == EffectDurationPolicy::Instant )
        {
            executeSpec( spec, 1 );
            return ActiveEffectHandle{ ++_nextHandleId };
        }
        return applyDurationSpec( spec );
    }

    ActiveEffectHandle AbilitySystemComponent::applyGameplayEffectSpecToTarget( const GameplayEffectSpec& spec, AbilitySystemComponent* pTarget )
    {
        if ( pTarget == nullptr || spec.isValid() == false )
            return ActiveEffectHandle{};
        if ( pTarget->canMutateNow() )
            return pTarget->applyGameplayEffectSpecToSelf( spec );

        // 다른 워커가 대상을 틱하고 있을 수 있다 — 틱 직후 게임 스레드에서 건다. 대상은 핸들로 다시 찾는다(그 사이 사라질 수 있다).
        GameObject*           pTargetOwner = pTarget->getOwner();
        GameObjectManager*    pManager     = pTargetOwner != nullptr ? pTargetOwner->getManager() : nullptr;
        const ComponentHandle targetHandle = pTarget->getHandle();
        if ( pManager == nullptr )
            return ActiveEffectHandle{};
        pManager->deferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, targetHandle, spec]()
        {
            Component* pComponent = pManager->resolveComponent( targetHandle );
            if ( pComponent != nullptr )
                (void)static_cast<AbilitySystemComponent*>( pComponent )->applyGameplayEffectSpecToSelf( spec ); // 미룬 적용은 핸들을 돌려줄 곳이 없다
        } ) );
        return ActiveEffectHandle{};
    }

    ActiveEffectHandle AbilitySystemComponent::applyGameplayEffectToSelf( const hashed_string& effectId, int32 level )
    {
        const GameplayEffectSpec spec = makeOutgoingSpecById( effectId, level );
        if ( spec.isValid() == false )
            return ActiveEffectHandle{};
        return applyGameplayEffectSpecToSelf( spec );
    }

    bool AbilitySystemComponent::removeActiveEffect( ActiveEffectHandle handle, int32 stacksToRemove )
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

    int32 AbilitySystemComponent::removeActiveEffectsWithTags( const TagContainer& tags )
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
            const bool bMatches = AbilitySystemComponentInternal::matchesAnyTag( pActiveEffect->_spec._pDef->_assetTags, tags ) ||
                                  AbilitySystemComponentInternal::matchesAnyTag( pActiveEffect->_grantedTags, tags );
            if ( bMatches == false )
                continue;
            removeActiveEffectInternal( *pActiveEffect );
            ++removedCount;
        }
        return removedCount;
    }

    const ActiveGameplayEffect* AbilitySystemComponent::findActiveEffect( ActiveEffectHandle handle ) const
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

    uint32 AbilitySystemComponent::getActiveEffectCount() const
    {
        uint32 activeCount = 0;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            if ( pActiveEffect->_bPendingRemove == SW_FALSE )
                ++activeCount;
        }
        return activeCount;
    }

    float32 AbilitySystemComponent::getActiveEffectRemainingTime( ActiveEffectHandle handle ) const
    {
        const ActiveGameplayEffect* pActiveEffect = findActiveEffect( handle );
        if ( pActiveEffect == nullptr || pActiveEffect->_spec._pDef->_durationPolicy != EffectDurationPolicy::HasDuration )
            return 0.0f;
        return MathUtil::max( 0.0f, pActiveEffect->_remainingTime );
    }

    int32 AbilitySystemComponent::getActiveEffectStackCount( ActiveEffectHandle handle ) const
    {
        const ActiveGameplayEffect* pActiveEffect = findActiveEffect( handle );
        return pActiveEffect != nullptr ? pActiveEffect->_stackCount : 0;
    }

    float32 AbilitySystemComponent::findLongestRemainingTimeWithGrantedTags( const TagContainer& tags ) const
    {
        float32 longestTime = 0.0f;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            const bool bTimed = pActiveEffect->_bPendingRemove == SW_FALSE && pActiveEffect->_spec._pDef->_durationPolicy == EffectDurationPolicy::HasDuration;
            if ( bTimed && AbilitySystemComponentInternal::matchesAnyTag( pActiveEffect->_grantedTags, tags ) )
                longestTime = MathUtil::max( longestTime, pActiveEffect->_remainingTime );
        }
        return longestTime;
    }

    bool AbilitySystemComponent::canApplyAttributeModifiers( const GameplayEffectSpec& spec ) const
    {
        if ( spec.isValid() == false )
            return false;
        for ( const GameplayEffectModifier& modifier : spec._pDef->_listModifier )
        {
            if ( modifier._op != AttributeModOp::Add )
                continue;
            if ( hasAttribute( modifier._attribute ) == false )
                return false;
            float32 magnitude = 0.0f;
            if ( computeModifierMagnitude( modifier, spec, magnitude ) == false )
                return false;
            if ( getAttributeValue( modifier._attribute ) + magnitude < 0.0f )
                return false;
        }
        return true;
    }

    DelegateHandle AbilitySystemComponent::registerEffectApplied( const EffectDelegate& delegate )
    {
        return _effectAppliedMulticast.add( delegate );
    }

    void AbilitySystemComponent::unregisterEffectApplied( DelegateHandle handle )
    {
        _effectAppliedMulticast.remove( handle );
    }

    DelegateHandle AbilitySystemComponent::registerEffectRemoved( const EffectDelegate& delegate )
    {
        return _effectRemovedMulticast.add( delegate );
    }

    void AbilitySystemComponent::unregisterEffectRemoved( DelegateHandle handle )
    {
        _effectRemovedMulticast.remove( handle );
    }

    // ------------------------------------------------------------------------------
    // 5) 어빌리티
    // ------------------------------------------------------------------------------
    AbilitySpecHandle AbilitySystemComponent::giveAbility( unique_ptr<GameplayAbility> pAbility, int32 level, int32 inputId )
    {
        if ( pAbility == nullptr )
            return AbilitySpecHandle{};

        unique_ptr<AbilitySpec> pSpec = make_unique<AbilitySpec>();
        pSpec->_handle                = AbilitySpecHandle{ ++_nextHandleId };
        pSpec->_pAbility              = std::move( pAbility );
        pSpec->_level                 = level;
        pSpec->_inputId               = inputId;

        GameplayAbility*        pRaw   = pSpec->_pAbility.get();
        const AbilitySpecHandle handle = pSpec->_handle;
        pRaw->bindToAbilitySystem( this, handle, level );
        _listAbilitySpec.push_back( std::move( pSpec ) );

        const ScopedListLock lock{ *this };
        pRaw->onGranted();
        if ( pRaw->getConfig()._bActivateOnGranted == SW_TRUE )
            (void)tryActivateAbility( handle ); // 패시브가 조건에 막히면 다음 부여 · 발동을 기다린다
        return handle;
    }

    AbilitySpecHandle AbilitySystemComponent::giveAbilityById( const hashed_string& abilityId, int32 level, int32 inputId )
    {
        const AbilityCatalog* pCatalog = findCatalog();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING( "giveAbilityById '%#': no AbilityCatalog", abilityId.c_str() );
            return AbilitySpecHandle{};
        }
        return giveAbility( pCatalog->createAbility( abilityId ), level, inputId );
    }

    bool AbilitySystemComponent::clearAbility( AbilitySpecHandle handle )
    {
        const ScopedListLock lock{ *this };
        AbilitySpec*         pSpec = findAbilitySpecMutable( handle );
        if ( pSpec == nullptr )
            return false;
        retireAbilitySpec( *pSpec );
        return true;
    }

    void AbilitySystemComponent::clearAllAbilities()
    {
        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            retireAbilitySpec( *_listAbilitySpec[specIndex] );
        }
    }

    AbilityActivationResult AbilitySystemComponent::tryActivateAbility( AbilitySpecHandle handle, const GameplayEventData* pTriggerEvent )
    {
        const ScopedListLock          lock{ *this };
        const AbilityActivationResult result = canActivateAbility( handle, pTriggerEvent );
        if ( result != AbilityActivationResult::Activated )
            return result;

        AbilitySpec*     pSpec    = findAbilitySpecMutable( handle );
        GameplayAbility* pAbility = pSpec->_pAbility.get(); // canActivateAbility 가 스펙 · 인스턴스를 확인했다
        if ( pAbility->isActive() )
            pAbility->cancelAbility(); // 다시 걸기 — 도는 발동을 끝내고 새로 시작한다
        // 취소 알림이 스펙을 거뒀을 수 있다.
        if ( pSpec->_bPendingRemove == SW_TRUE )
            return AbilityActivationResult::InvalidSpec;
        pAbility->beginActivation( pTriggerEvent );
        return AbilityActivationResult::Activated;
    }

    AbilityActivationResult AbilitySystemComponent::canActivateAbility( AbilitySpecHandle handle, const GameplayEventData* pTriggerEvent ) const
    {
        const AbilitySpec* pSpec = findAbilitySpec( handle );
        if ( pSpec == nullptr || pSpec->_pAbility == nullptr )
            return AbilityActivationResult::InvalidSpec;

        const GameplayAbility&       ability = *pSpec->_pAbility;
        const GameplayAbilityConfig& config  = ability.getConfig();
        if ( ability.isActive() && config._bRetriggerInstancedAbility == SW_FALSE )
            return AbilityActivationResult::AlreadyActive;
        if ( isAbilityBlocked( config._abilityTags ) )
            return AbilityActivationResult::BlockedByTag;
        if ( hasAllMatchingTags( config._activationRequiredTags ) == false )
            return AbilityActivationResult::MissingRequiredTag;
        if ( hasAnyMatchingTags( config._activationBlockedTags ) )
            return AbilityActivationResult::BlockedByTag;
        if ( ability.isOnCooldown() )
            return AbilityActivationResult::OnCooldown;
        if ( ability.canAffordCost() == false )
            return AbilityActivationResult::CannotAffordCost;
        if ( ability.canActivateAbility( pTriggerEvent ) == false )
            return AbilityActivationResult::RejectedByAbility;
        return AbilityActivationResult::Activated;
    }

    bool AbilitySystemComponent::tryActivateAbilitiesByTag( const TagContainer& tags )
    {
        const ScopedListLock      lock{ *this };
        vector<AbilitySpecHandle> listCandidate;
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            const bool bMatches = pSpec->_bPendingRemove == SW_FALSE &&
                                  AbilitySystemComponentInternal::matchesAnyTag( pSpec->_pAbility->getConfig()._abilityTags, tags );
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

    AbilitySpecHandle AbilitySystemComponent::findAbilitySpecHandle( const hashed_string& abilityId ) const
    {
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            if ( pSpec->_bPendingRemove == SW_FALSE && pSpec->_pAbility->getConfig()._id == abilityId )
                return pSpec->_handle;
        }
        return AbilitySpecHandle{};
    }

    GameplayAbility* AbilitySystemComponent::findAbility( AbilitySpecHandle handle ) const
    {
        const AbilitySpec* pSpec = findAbilitySpec( handle );
        return pSpec != nullptr ? pSpec->_pAbility.get() : nullptr;
    }

    bool AbilitySystemComponent::isAbilityActive( AbilitySpecHandle handle ) const
    {
        const GameplayAbility* pAbility = findAbility( handle );
        return pAbility != nullptr && pAbility->isActive();
    }

    void AbilitySystemComponent::cancelAbility( AbilitySpecHandle handle )
    {
        const ScopedListLock lock{ *this };
        GameplayAbility*     pAbility = findAbility( handle );
        if ( pAbility != nullptr )
            pAbility->cancelAbility();
    }

    void AbilitySystemComponent::cancelAbilitiesWithTags( const TagContainer& tags, const GameplayAbility* pIgnore )
    {
        if ( tags.getTagCount() == 0 )
            return;

        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec&     spec     = *_listAbilitySpec[specIndex];
            GameplayAbility* pAbility = spec._pAbility.get();
            const bool       bSkip    = spec._bPendingRemove == SW_TRUE || pAbility == pIgnore || pAbility->isActive() == false;
            if ( bSkip )
                continue;
            if ( AbilitySystemComponentInternal::matchesAnyTag( pAbility->getConfig()._abilityTags, tags ) )
                pAbility->cancelAbility();
        }
    }

    void AbilitySystemComponent::cancelAllAbilities()
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

    uint32 AbilitySystemComponent::getAbilityCount() const
    {
        uint32 abilityCount = 0;
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            if ( pSpec->_bPendingRemove == SW_FALSE )
                ++abilityCount;
        }
        return abilityCount;
    }

    void AbilitySystemComponent::getAbilitySpecHandles( vector<AbilitySpecHandle>& outListHandle ) const
    {
        outListHandle.clear();
        for ( const unique_ptr<AbilitySpec>& pSpec : _listAbilitySpec )
        {
            if ( pSpec->_bPendingRemove == SW_FALSE )
                outListHandle.push_back( pSpec->_handle );
        }
    }

    void AbilitySystemComponent::abilityInputPressed( int32 inputId )
    {
        if ( inputId == kNoInputId )
            return;

        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_TRUE || spec._inputId != inputId )
                continue;
            GameplayAbility* pAbility   = spec._pAbility.get();
            const bool       bWasActive = pAbility->isActive();
            pAbility->setInputPressed( true );
            if ( bWasActive == false )
                (void)tryActivateAbility( spec._handle ); // 쿨다운 · 비용으로 막히면 그대로 둔다(UI 가 `canActivateAbility` 로 보여 준다)
        }
    }

    void AbilitySystemComponent::abilityInputReleased( int32 inputId )
    {
        if ( inputId == kNoInputId )
            return;

        const ScopedListLock lock{ *this };
        const size_t         specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_FALSE && spec._inputId == inputId )
                spec._pAbility->setInputPressed( false );
        }
    }

    DelegateHandle AbilitySystemComponent::registerAbilityActivated( const AbilityActivatedDelegate& delegate )
    {
        return _abilityActivatedMulticast.add( delegate );
    }

    void AbilitySystemComponent::unregisterAbilityActivated( DelegateHandle handle )
    {
        _abilityActivatedMulticast.remove( handle );
    }

    DelegateHandle AbilitySystemComponent::registerAbilityEnded( const AbilityEndedDelegate& delegate )
    {
        return _abilityEndedMulticast.add( delegate );
    }

    void AbilitySystemComponent::unregisterAbilityEnded( DelegateHandle handle )
    {
        _abilityEndedMulticast.remove( handle );
    }

    // ------------------------------------------------------------------------------
    // 6) 게임플레이 이벤트 · 큐
    // ------------------------------------------------------------------------------
    int32 AbilitySystemComponent::handleGameplayEvent( TagID eventTag, const GameplayEventData& payload )
    {
        if ( eventTag.isValid() == false )
            return 0;

        const ScopedListLock lock{ *this };
        GameplayEventData    eventData = payload;
        eventData._eventTag            = eventTag;

        // 1) 트리거 — 이벤트 태그가 트리거 태그와 같거나 그 하위면 발동한다("Event.Hit.Fire" 는 "Event.Hit" 트리거를 깨운다).
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

        // 2) 구독자 — 같은 태그와 그 조상 태그마다("Event.Hit.Fire" → "Event.Hit" → "Event"). 조상의 id 는 intern 하지 않고 해시만 구한다.
        const utf8* pTagText = eventTag.getString();
        if ( pTagText != nullptr )
        {
            const size_t tagLength = StringUtil::strlen( pTagText );
            for ( size_t charIndex = tagLength; charIndex > 0; --charIndex )
            {
                const bool bBoundary = charIndex == tagLength || pTagText[charIndex] == '.';
                if ( bBoundary == false )
                    continue;
                const uint64 prefixId = TagID::computeId( pTagText, charIndex );
                const auto   mapIter  = _mapGameplayEventMulticast.find( prefixId );
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

    void AbilitySystemComponent::sendGameplayEventToTarget( AbilitySystemComponent* pTarget, TagID eventTag, const GameplayEventData& payload )
    {
        if ( pTarget == nullptr || eventTag.isValid() == false )
            return;
        if ( pTarget->canMutateNow() )
        {
            (void)pTarget->handleGameplayEvent( eventTag, payload ); // 발동 수는 보내는 쪽이 쓰지 않는다
            return;
        }

        GameObject*           pTargetOwner = pTarget->getOwner();
        GameObjectManager*    pManager     = pTargetOwner != nullptr ? pTargetOwner->getManager() : nullptr;
        const ComponentHandle targetHandle = pTarget->getHandle();
        if ( pManager == nullptr )
            return;
        pManager->deferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, targetHandle, eventTag, payload]()
        {
            Component* pComponent = pManager->resolveComponent( targetHandle );
            if ( pComponent != nullptr )
                (void)static_cast<AbilitySystemComponent*>( pComponent )->handleGameplayEvent( eventTag, payload );
        } ) );
    }

    DelegateHandle AbilitySystemComponent::registerGameplayEvent( TagID eventTag, const GameplayEventDelegate& delegate )
    {
        if ( eventTag.isValid() == false )
            return DelegateHandle{};
        unique_ptr<MulticastDelegate<void( const GameplayEventData& )>>& pMulticast = _mapGameplayEventMulticast[eventTag._id];
        if ( pMulticast == nullptr )
            pMulticast = make_unique<MulticastDelegate<void( const GameplayEventData& )>>();
        return pMulticast->add( delegate );
    }

    void AbilitySystemComponent::unregisterGameplayEvent( TagID eventTag, DelegateHandle handle )
    {
        const auto mapIter = _mapGameplayEventMulticast.find( eventTag._id );
        if ( mapIter != _mapGameplayEventMulticast.end() && mapIter->second != nullptr )
            mapIter->second->remove( handle );
    }

    DelegateHandle AbilitySystemComponent::registerGameplayCue( const GameplayCueDelegate& delegate )
    {
        return _gameplayCueMulticast.add( delegate );
    }

    void AbilitySystemComponent::unregisterGameplayCue( DelegateHandle handle )
    {
        _gameplayCueMulticast.remove( handle );
    }

    void AbilitySystemComponent::setHealthAttributes( const hashed_string& health, const hashed_string& maxHealth )
    {
        _healthAttribute    = health;
        _maxHealthAttribute = maxHealth;
        notifyHealthChanged( true );
    }

    HealthReading AbilitySystemComponent::getHealthReading() const
    {
        HealthReading reading;
        if ( hasAttribute( _healthAttribute ) == false )
        {
            reading._bHasHealth = SW_FALSE;
            return reading;
        }
        reading._health    = getAttributeValue( _healthAttribute );
        reading._maxHealth = getAttributeValue( _maxHealthAttribute );
        reading._bDead     = reading._health <= 0.0f ? SW_TRUE : SW_FALSE;
        return reading;
    }

    // ------------------------------------------------------------------------------
    // 7) 내부 — 이펙트
    // ------------------------------------------------------------------------------
    bool AbilitySystemComponent::canApplySpec( const GameplayEffectSpec& spec ) const
    {
        const GameplayEffectDef& def = *spec._pDef;
        if ( hasAllMatchingTags( def._applicationRequiredTags ) == false )
            return false;
        return hasAnyMatchingTags( def._applicationBlockedTags ) == false;
    }

    bool AbilitySystemComponent::computeModifierMagnitude( const GameplayEffectModifier& modifier, const GameplayEffectSpec& spec, float32& outMagnitude ) const
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
                    if ( hasAttribute( modifier._backingAttribute ) == false )
                        return false;
                    attributeValue = getAttributeValue( modifier._backingAttribute );
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

    void AbilitySystemComponent::evaluateExecutionModifiers( const GameplayEffectSpec& spec, int32 stackCount, vector<EvaluatedModifier>& outListModifier ) const
    {
        const GameplayEffectDef& def = *spec._pDef;
        for ( const GameplayEffectModifier& modifier : def._listModifier )
        {
            EvaluatedModifier evaluated;
            evaluated._attribute = modifier._attribute;
            evaluated._op        = modifier._op;
            if ( computeModifierMagnitude( modifier, spec, evaluated._magnitude ) )
                outListModifier.push_back( evaluated );
        }

        GameplayEffectExecutionParams params;
        params._pSpec      = &spec;
        params._pTarget    = this;
        params._stackCount = stackCount;
        for ( const shared_ptr<const IGameplayEffectExecution>& pExecution : def._listExecution )
        {
            if ( pExecution != nullptr )
                pExecution->execute( params, outListModifier );
        }
    }

    void AbilitySystemComponent::executeSpec( const GameplayEffectSpec& spec, int32 stackCount )
    {
        vector<EvaluatedModifier> listModifier;
        evaluateExecutionModifiers( spec, stackCount, listModifier );

        const float32 stackScale     = static_cast<float32>( MathUtil::max( 1, stackCount ) );
        float32       totalMagnitude = 0.0f;
        for ( const EvaluatedModifier& modifier : listModifier )
        {
            applyModifierToBase( modifier, stackScale, spec );
            if ( modifier._op == AttributeModOp::Add )
                totalMagnitude += modifier._magnitude * stackScale;
        }
        dispatchCues( spec, GameplayCuePhase::Executed, totalMagnitude );
    }

    void AbilitySystemComponent::applyModifierToBase( const EvaluatedModifier& modifier, float32 stackScale, const GameplayEffectSpec& spec )
    {
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = findAttributeDataMutable( modifier._attribute, &pAttributeSet );
        if ( pData == nullptr )
        {
            SW_LOG_WARNING( "Effect '%#' modifies '%#' which this object does not have - skipped", spec._pDef->_id.c_str(), modifier._attribute.c_str() );
            return;
        }

        const float32 oldBase = pData->_baseValue;
        float32       newBase = AbilitySystemComponentInternal::applyToBase( oldBase, modifier, stackScale );
        pAttributeSet->preAttributeBaseChange( modifier._attribute, newBase );
        pData->_baseValue = newBase;
        recomputeAttribute( modifier._attribute );

        // 훅이 다른 어트리뷰트(체력)를 바꾸며 묶음 목록을 건드리지 않으므로 묶음 포인터는 그대로 쓸 수 있다.
        AttributeModCallbackData callbackData;
        callbackData._pSpec     = &spec;
        callbackData._pTarget   = this;
        callbackData._attribute = modifier._attribute;
        callbackData._magnitude = modifier._op == AttributeModOp::Add ? modifier._magnitude * stackScale : modifier._magnitude;
        callbackData._deltaBase = newBase - oldBase;
        callbackData._op        = modifier._op;
        pAttributeSet->postGameplayEffectExecute( callbackData );
    }

    ActiveEffectHandle AbilitySystemComponent::applyDurationSpec( const GameplayEffectSpec& spec )
    {
        const GameplayEffectDef& def = *spec._pDef;

        // 스택 — 대상에 하나만 두고 개수를 올린다.
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
                dispatchCues( pExisting->_spec, GameplayCuePhase::Added, static_cast<float32>( pExisting->_stackCount ) );
                _effectAppliedMulticast.broadcast( *pExisting );
                return pExisting->_handle;
            }
        }

        unique_ptr<ActiveGameplayEffect> pNewEffect = make_unique<ActiveGameplayEffect>();
        pNewEffect->_handle                         = ActiveEffectHandle{ ++_nextHandleId };
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

        // 주기 이펙트의 모디파이어는 주기마다 base 에 쓴다(도트) — 걸려 있는 동안 current 에 얹지 않는다(언리얼과 같다).
        if ( def.isPeriodic() == false )
        {
            for ( const GameplayEffectModifier& modifier : def._listModifier )
            {
                EvaluatedModifier evaluated;
                evaluated._attribute = modifier._attribute;
                evaluated._op        = modifier._op;
                if ( computeModifierMagnitude( modifier, spec, evaluated._magnitude ) )
                    pNewEffect->_listEvaluatedModifier.push_back( evaluated );
            }
        }

        ActiveGameplayEffect* pActiveEffect = pNewEffect.get();
        _listActiveEffect.push_back( std::move( pNewEffect ) );

        updateTagCounts( pActiveEffect->_grantedTags, 1 );
        for ( const EvaluatedModifier& modifier : pActiveEffect->_listEvaluatedModifier )
        {
            recomputeAttribute( modifier._attribute );
        }
        dispatchCues( pActiveEffect->_spec, GameplayCuePhase::Added, 1.0f );
        _effectAppliedMulticast.broadcast( *pActiveEffect );

        const bool bExecuteNow = def.isPeriodic() && def._bExecutePeriodicOnApplication == SW_TRUE && pActiveEffect->_bPendingRemove == SW_FALSE;
        if ( bExecuteNow )
            executeSpec( pActiveEffect->_spec, pActiveEffect->_stackCount );
        return pActiveEffect->_handle;
    }

    void AbilitySystemComponent::removeActiveEffectInternal( ActiveGameplayEffect& activeEffect )
    {
        if ( activeEffect._bPendingRemove == SW_TRUE )
            return;
        activeEffect._bPendingRemove = SW_TRUE;
        _bHasPendingRemoval          = SW_TRUE;

        updateTagCounts( activeEffect._grantedTags, -1 );
        for ( const EvaluatedModifier& modifier : activeEffect._listEvaluatedModifier )
        {
            recomputeAttribute( modifier._attribute );
        }
        dispatchCues( activeEffect._spec, GameplayCuePhase::Removed, 0.0f );
        _effectRemovedMulticast.broadcast( activeEffect );
    }

    ActiveGameplayEffect* AbilitySystemComponent::findActiveEffectMutable( ActiveEffectHandle handle )
    {
        return const_cast<ActiveGameplayEffect*>( findActiveEffect( handle ) );
    }

    ActiveGameplayEffect* AbilitySystemComponent::findStackableEffect( const GameplayEffectDef& def )
    {
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            if ( pActiveEffect->_bPendingRemove == SW_TRUE )
                continue;
            // 같은 정의(같은 객체) 또는 같은 id — 카탈로그를 다시 읽어 정의 객체가 바뀌어도 같은 이펙트로 쌓인다.
            const GameplayEffectDef& activeDef = *pActiveEffect->_spec._pDef;
            const bool               bSameDef  = &activeDef == &def || ( def._id.empty() == false && activeDef._id == def._id );
            if ( bSameDef )
                return pActiveEffect.get();
        }
        return nullptr;
    }

    void AbilitySystemComponent::updateActiveEffects( float32 deltaTime )
    {
        const size_t effectCount = _listActiveEffect.size();
        for ( size_t effectIndex = 0; effectIndex < effectCount && effectIndex < _listActiveEffect.size(); ++effectIndex )
        {
            // 원소는 `unique_ptr` 이고 잠금 중에는 지우지 않으므로, 콜백이 목록을 늘려도 이 포인터는 산다.
            ActiveGameplayEffect* pActiveEffect = _listActiveEffect[effectIndex].get();
            if ( pActiveEffect->_bPendingRemove == SW_TRUE )
                continue;

            const GameplayEffectDef& def       = *pActiveEffect->_spec._pDef;
            const bool               bPeriodic = def.isPeriodic();
            const bool               bTimed    = def._durationPolicy == EffectDurationPolicy::HasDuration;
            if ( bPeriodic == false && bTimed == false )
                continue;

            // 남은 시간을 다음 사건(주기 실행 · 만료)까지 잘라 가며 쓴다 — 한 프레임이 주기 여럿을 넘어도 순서대로 실행하고, 만료 뒤의 주기는 실행하지 않는다.
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
                    if ( pActiveEffect->_periodTimer <= AbilitySystemComponentInternal::kTimeEpsilon )
                    {
                        pActiveEffect->_periodTimer += def._period;
                        executeSpec( pActiveEffect->_spec, pActiveEffect->_stackCount );
                    }
                }
                if ( bTimed && pActiveEffect->_bPendingRemove == SW_FALSE )
                {
                    pActiveEffect->_remainingTime -= step;
                    if ( pActiveEffect->_remainingTime > AbilitySystemComponentInternal::kTimeEpsilon )
                        continue;

                    const bool bRemoveSingleStack = def._stackExpirationPolicy == EffectStackExpirationPolicy::RemoveSingleStackAndRefreshDuration &&
                                                    pActiveEffect->_stackCount > 1 && pActiveEffect->_duration > AbilitySystemComponentInternal::kTimeEpsilon;
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

    void AbilitySystemComponent::dispatchCues( const GameplayEffectSpec& spec, GameplayCuePhase phase, float32 magnitude )
    {
        const TagContainer& cueTags = spec._pDef->_cueTags;
        if ( cueTags.getTagCount() == 0 )
            return;

        const GameObject* pOwner = getOwner();
        for ( const TagID& cueTag : cueTags.getTags() )
        {
            GameplayCueEvent cueEvent;
            cueEvent._cueTag     = cueTag;
            cueEvent._target     = pOwner != nullptr ? pOwner->getHandle() : GameObjectHandle{};
            cueEvent._instigator = spec._context._instigator;
            cueEvent._magnitude  = magnitude;
            cueEvent._phase      = phase;
            _gameplayCueMulticast.broadcast( cueEvent );
            GameEventUtil::send( cueEvent );
        }
    }

    void AbilitySystemComponent::compactPendingRemovals()
    {
        _bHasPendingRemoval = SW_FALSE;

        // 지우는 것을 목록 밖으로 먼저 옮긴다 — 소멸자(어빌리티의 작업 정리)가 이 컴포넌트를 불러도 목록은 이미 정돈돼 있다.
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

        // 소멸이 다시 지우기를 표시하면(드물다) 잠금 밖이니 한 번 더 정리한다.
        ++_listLockDepth;
        listRemovedSpec.clear();
        listRemovedEffect.clear();
        --_listLockDepth;
        if ( _bHasPendingRemoval == SW_TRUE )
            compactPendingRemovals();
    }

    // ------------------------------------------------------------------------------
    // 8) 내부 — 어트리뷰트
    // ------------------------------------------------------------------------------
    void AbilitySystemComponent::recomputeAttribute( const hashed_string& name )
    {
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = findAttributeDataMutable( name, &pAttributeSet );
        if ( pData == nullptr )
            return;

        AbilitySystemComponentInternal::Aggregation aggregation;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            if ( pActiveEffect->_bPendingRemove == SW_TRUE )
                continue;
            const float32 stackScale = static_cast<float32>( pActiveEffect->_stackCount );
            for ( const EvaluatedModifier& modifier : pActiveEffect->_listEvaluatedModifier )
            {
                if ( modifier._attribute == name )
                    AbilitySystemComponentInternal::accumulate( modifier, stackScale, aggregation );
            }
        }

        float32 newValue = AbilitySystemComponentInternal::evaluate( pData->_baseValue, aggregation );
        pAttributeSet->preAttributeChange( name, newValue );
        const float32 oldValue = pData->_currentValue;
        if ( oldValue == newValue )
            return;
        pData->_currentValue = newValue;
        notifyAttributeChanged( *pAttributeSet, name, oldValue, newValue );
    }

    AttributeData* AbilitySystemComponent::findAttributeDataMutable( const hashed_string& name, AttributeSet** ppOutSet )
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

    void AbilitySystemComponent::notifyAttributeChanged( AttributeSet& attributeSet, const hashed_string& name, float32 oldValue, float32 newValue )
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
        if ( bHealth && newValue < oldValue && _bShowDamageNumbers )
            spawnDamageNumber( oldValue - newValue );
    }

    // ------------------------------------------------------------------------------
    // 9) 내부 — 태그
    // ------------------------------------------------------------------------------
    void AbilitySystemComponent::updateTagCount( TagID tag, int32 delta )
    {
        if ( tag.isValid() == false || delta == 0 )
            return;

        const auto  mapIter  = _mapTagCount.find( tag._id );
        const int32 oldCount = mapIter != _mapTagCount.end() ? mapIter->second._count : 0;
        const int32 newCount = MathUtil::max( 0, oldCount + delta );
        if ( newCount == oldCount )
            return;

        if ( newCount == 0 )
            _mapTagCount.erase( tag._id );
        else
            _mapTagCount.insert_or_assign( tag._id, TagCountEntry{ tag, newCount } );

        // 알림은 생기고 사라질 때만 — 같은 태그를 주는 이펙트가 하나 더 걸려도 "태그가 생겼다" 는 아니다(언리얼 NewOrRemoved).
        if ( oldCount == 0 )
        {
            _ownedTags.addTag( tag );
            _tagChangedMulticast.broadcast( tag, newCount );
        }
        else if ( newCount == 0 )
        {
            _ownedTags.removeTag( tag );
            _tagChangedMulticast.broadcast( tag, 0 );
        }
    }

    void AbilitySystemComponent::updateTagCounts( const TagContainer& tags, int32 delta )
    {
        for ( const TagID& tag : tags.getTags() )
        {
            updateTagCount( tag, delta );
        }
    }

    void AbilitySystemComponent::updateBlockedAbilityTags( const TagContainer& tags, int32 delta )
    {
        for ( const TagID& tag : tags.getTags() )
        {
            const auto  mapIter  = _mapBlockedAbilityTag.find( tag._id );
            const int32 oldCount = mapIter != _mapBlockedAbilityTag.end() ? mapIter->second._count : 0;
            const int32 newCount = MathUtil::max( 0, oldCount + delta );
            if ( newCount == 0 )
                _mapBlockedAbilityTag.erase( tag._id );
            else
                _mapBlockedAbilityTag.insert_or_assign( tag._id, TagCountEntry{ tag, newCount } );
        }
    }

    bool AbilitySystemComponent::isAbilityBlocked( const TagContainer& abilityTags ) const
    {
        for ( const auto& [blockedId, blockedEntry] : _mapBlockedAbilityTag )
        {
            (void)blockedId;
            for ( const TagID& abilityTag : abilityTags.getTags() )
            {
                if ( abilityTag.isSubtagOf( blockedEntry._tag ) )
                    return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------------------
    // 10) 내부 — 어빌리티
    // ------------------------------------------------------------------------------
    AbilitySystemComponent::AbilitySpec* AbilitySystemComponent::findAbilitySpecMutable( AbilitySpecHandle handle )
    {
        return const_cast<AbilitySpec*>( findAbilitySpec( handle ) );
    }

    const AbilitySystemComponent::AbilitySpec* AbilitySystemComponent::findAbilitySpec( AbilitySpecHandle handle ) const
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

    void AbilitySystemComponent::handleAbilityActivated( GameplayAbility& ability )
    {
        const GameplayAbilityConfig& config = ability.getConfig();
        updateTagCounts( config._activationOwnedTags, 1 );
        updateBlockedAbilityTags( config._blockAbilitiesWithTags, 1 );
        cancelAbilitiesWithTags( config._cancelAbilitiesWithTags, &ability );
        _abilityActivatedMulticast.broadcast( ability );
    }

    void AbilitySystemComponent::handleAbilityEnded( GameplayAbility& ability, bool bWasCancelled )
    {
        // 발동 때 붙인 것을 그대로 뗀다 — 설정은 도는 동안 바꾸지 않는다(`GameplayAbility::getConfig` 의 약속).
        const GameplayAbilityConfig& config = ability.getConfig();
        updateTagCounts( config._activationOwnedTags, -1 );
        updateBlockedAbilityTags( config._blockAbilitiesWithTags, -1 );
        _abilityEndedMulticast.broadcast( ability, bWasCancelled );
    }

    void AbilitySystemComponent::tickActiveAbilities( float32 deltaTime )
    {
        const size_t specCount = _listAbilitySpec.size();
        for ( size_t specIndex = 0; specIndex < specCount && specIndex < _listAbilitySpec.size(); ++specIndex )
        {
            AbilitySpec& spec = *_listAbilitySpec[specIndex];
            if ( spec._bPendingRemove == SW_FALSE && spec._pAbility->isActive() )
                spec._pAbility->tickAbility( deltaTime );
        }
    }

    void AbilitySystemComponent::retireAbilitySpec( AbilitySpec& spec )
    {
        if ( spec._bPendingRemove == SW_TRUE )
            return;
        GameplayAbility* pAbility = spec._pAbility.get();
        if ( pAbility->isActive() )
            pAbility->cancelAbility();
        pAbility->onRemoved();
        spec._bPendingRemove = SW_TRUE;
        _bHasPendingRemoval  = SW_TRUE;
    }

    // ------------------------------------------------------------------------------
    // 11) 내부 — 스레드 · 연결된 UI · 모듈 코드
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

        // 자리는 지금 읽는다(자기 오브젝트라 이 워커가 읽어도 된다). 만들기는 틱 중이면 틱 직후다(`DamageNumberComponent::spawnNumber`).
        const SceneComponent* pRoot    = pOwner->getPrimarySceneComponent();
        const float3          position = ( pRoot != nullptr ? pRoot->getWorldPosition() : float3{} ) + _damageNumberOffset;
        const int32           value    = static_cast<int32>( MathUtil::round( amount ) );
        if ( value <= 0 )
            return;
        DamageNumberComponent::spawnNumber( *pManager, position, value );
    }

    uint32 AbilitySystemComponent::onModuleUnloading( const void* pBegin, const void* pEnd )
    {
        uint32 releasedCount = 0;

        // 1) 델리게이트 — 게임이 건 구독(스텁이 그 모듈 안). 지우기 전에 떼어 정리 중에 불리지 않게 한다.
        releasedCount += _anyAttributeChangedMulticast.removeCodeWithin( pBegin, pEnd );
        releasedCount += _tagChangedMulticast.removeCodeWithin( pBegin, pEnd );
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

            // 2) 어빌리티 — 인스턴스의 vtable 이 그 모듈 안이면 지금(코드가 아직 있을 때) 거둔다. 작업이 건 이벤트 구독도 함께 떨어진다.
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

            // 3) 그 모듈의 실행 계산을 쓰는 걸린 이펙트 — 다음 주기에 내려간 코드를 부르지 않게 푼다.
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

        // 4) 게임 모듈이 정의한 어트리뷰트 묶음 — 값은 함께 사라진다(다시 올라온 게임이 세트를 다시 준다).
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
