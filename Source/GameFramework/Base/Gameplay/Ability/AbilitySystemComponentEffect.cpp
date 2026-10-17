/**
 * @file AbilitySystemComponentEffect.cpp
 * @brief `AbilitySystemComponent` 의 게임플레이 이펙트 부분입니다.
 * @details 스펙 만들기 · 적용(즉시 · 지속 · 스택) · 제거 · 주기 실행 · 남은 시간(쿨다운) 조회 · 큐 발송을 맡습니다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Gameplay/Ability/AbilitySystemEvents.h"
#include "GameFramework/Base/Gameplay/Ability/GameplayAbility.h"
#include "GameFramework/Base/UI/Marker/DamageNumberComponent.h"

namespace sw
{
    SW_LOG_CALLER( "AbilitySystemComponentEffect" );

    namespace
    {
        struct AbilitySystemComponentEffectInternal
        {
            /** @brief 시간 비교의 허용 오차입니다. 0.1 초씩 더한 시간이 정확히 1 이 되지 않는 부동소수점 오차를 흡수합니다. */
            static constexpr float32 kTimeEpsilon = 1.0e-4f;

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

    GameplayEffectSpec AbilitySystemComponent::makeOutgoingSpecByID( const hashed_string& effectID, int32 level ) const
    {
        const AbilityCatalog* pCatalog = findCatalog();
        if ( pCatalog == nullptr )
        {
            SW_LOG_WARNING( "makeOutgoingSpecByID '%#': no AbilityCatalog", effectID.c_str() );
            return GameplayEffectSpec{};
        }
        shared_ptr<const GameplayEffectDef> pDef = pCatalog->findEffect( effectID );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "makeOutgoingSpecByID: no effect '%#' in the catalog", effectID.c_str() );
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
            (void)removeActiveEffectsWithTags( def._removeEffectsWithTags ); // 지울 이펙트가 없어도 된다

        if ( def._durationPolicy == EffectDurationPolicy::Instant )
        {
            executeSpec( spec, 1 );
            return ActiveEffectHandle{ ++_nextHandleID };
        }
        return applyDurationSpec( spec );
    }

    ActiveEffectHandle AbilitySystemComponent::applyGameplayEffectSpecToTarget( const GameplayEffectSpec& spec, AbilitySystemComponent* pTarget )
    {
        if ( pTarget == nullptr || spec.isValid() == false )
            return ActiveEffectHandle{};
        if ( pTarget->canMutateNow() )
            return pTarget->applyGameplayEffectSpecToSelf( spec );

        // 다른 워커가 대상을 틱하고 있을 수 있으므로 틱 직후 게임 스레드에서 건다. 그 사이 대상이 사라질 수 있어서 핸들로 다시 찾는다.
        GameObject*                                    pTargetOwner = pTarget->getOwner();
        GameObjectManager*                             pManager     = pTargetOwner != nullptr ? pTargetOwner->getManager() : nullptr;
        const TComponentHandle<AbilitySystemComponent> targetHandle{ pTarget };
        if ( pManager == nullptr )
            return ActiveEffectHandle{};
        pManager->deferPostTick( SW_DELEGATE_LAMBDA( GameObjectManager::PostTickDelegate, [pManager, targetHandle, spec]()
        {
            AbilitySystemComponent* pResolved = pManager->resolveComponent( targetHandle );
            if ( pResolved != nullptr )
                (void)pResolved->applyGameplayEffectSpecToSelf( spec ); // 미룬 적용은 핸들을 돌려줄 곳이 없다
        } ) );
        return ActiveEffectHandle{};
    }

    ActiveEffectHandle AbilitySystemComponent::applyGameplayEffectToSelf( const hashed_string& effectID, int32 level )
    {
        const GameplayEffectSpec spec = makeOutgoingSpecByID( effectID, level );
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
            const bool bMatches = AbilitySystemComponentEffectInternal::matchesAnyTag( pActiveEffect->_spec._pDef->_assetTags, tags ) ||
                                  AbilitySystemComponentEffectInternal::matchesAnyTag( pActiveEffect->_grantedTags, tags );
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
            if ( bTimed && AbilitySystemComponentEffectInternal::matchesAnyTag( pActiveEffect->_grantedTags, tags ) )
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
    // 7) 내부: 이펙트
    // ------------------------------------------------------------------------------
    bool AbilitySystemComponent::canApplySpec( const GameplayEffectSpec& spec ) const
    {
        const GameplayEffectDef& def = *spec._pDef;
        if ( _ownedTagCounts.hasAllMatchingTags( def._applicationRequiredTags ) == false )
            return false;
        return _ownedTagCounts.hasAnyMatchingTags( def._applicationBlockedTags ) == false;
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
        float32       newBase = AbilitySystemComponentEffectInternal::applyToBase( oldBase, modifier, stackScale );
        pAttributeSet->preAttributeBaseChange( modifier._attribute, newBase );
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

    ActiveEffectHandle AbilitySystemComponent::applyDurationSpec( const GameplayEffectSpec& spec )
    {
        const GameplayEffectDef& def = *spec._pDef;

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
                dispatchCues( pExisting->_spec, GameplayCuePhase::Added, static_cast<float32>( pExisting->_stackCount ) );
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

        _ownedTagCounts.updateCounts( pActiveEffect->_grantedTags, 1 );
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

        _ownedTagCounts.updateCounts( activeEffect._grantedTags, -1 );
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
            // 같은 정의 객체이거나 같은 id 이면 같은 이펙트로 본다. 카탈로그를 다시 읽어 정의 객체가 바뀌어도 같은 이펙트로 쌓인다.
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
            // 원소가 `unique_ptr` 이고 잠금 중에는 지우지 않으므로, 콜백이 목록을 늘려도 이 포인터는 유효하다.
            ActiveGameplayEffect* pActiveEffect = _listActiveEffect[effectIndex].get();
            if ( pActiveEffect->_bPendingRemove == SW_TRUE )
                continue;

            const GameplayEffectDef& def       = *pActiveEffect->_spec._pDef;
            const bool               bPeriodic = def.isPeriodic();
            const bool               bTimed    = def._durationPolicy == EffectDurationPolicy::HasDuration;
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
                    if ( pActiveEffect->_periodTimer <= AbilitySystemComponentEffectInternal::kTimeEpsilon )
                    {
                        pActiveEffect->_periodTimer += def._period;
                        executeSpec( pActiveEffect->_spec, pActiveEffect->_stackCount );
                    }
                }
                if ( bTimed && pActiveEffect->_bPendingRemove == SW_FALSE )
                {
                    pActiveEffect->_remainingTime -= step;
                    if ( pActiveEffect->_remainingTime > AbilitySystemComponentEffectInternal::kTimeEpsilon )
                        continue;

                    const bool bRemoveSingleStack = def._stackExpirationPolicy == EffectStackExpirationPolicy::RemoveSingleStackAndRefreshDuration &&
                                                    pActiveEffect->_stackCount > 1 && pActiveEffect->_duration > AbilitySystemComponentEffectInternal::kTimeEpsilon;
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
} // namespace sw
