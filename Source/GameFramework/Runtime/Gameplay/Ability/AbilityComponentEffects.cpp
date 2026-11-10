#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Framework/Presentation/IAbilityCueSink.h"
#include "GameFramework/Runtime/Gameplay/Ability/Ability.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityEvents.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

/**
 * @file AbilityComponentEffects.cpp
 * @brief 게임플레이 이펙트(스펙 · 적용 · 지속 · 스택 · 제거 · 큐)를 구현합니다.
 * @details 파일 나눔은 `AbilityComponent.cpp` 머리말을 본다.
 */

namespace sw
{
    namespace
    {
        /** @brief 시간 비교의 허용 오차입니다. 0.1 초씩 더한 시간이 정확히 1 이 되지 않는 부동소수점 오차를 흡수합니다. */
        constexpr float32 kEffectTimeEpsilon = 1.0e-4f;
    } // namespace
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
            SW_LOG_WARNING_ID( "AbilityComponent.MakeOutgoingSpecByIDNoAbilityCatalog", "makeOutgoingSpecByID '%#': no AbilityCatalog", effectID.c_str() );
            return AbilityEffectSpec{};
        }
        shared_ptr<const AbilityEffectDef> pDef = pCatalog->findEffect( effectID );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING_ID( "AbilityComponent.MakeOutgoingSpecByIDNoEffectCatalog", "makeOutgoingSpecByID: no effect '%#' in the catalog", effectID.c_str() );
            return AbilityEffectSpec{};
        }
        return makeOutgoingSpec( pDef, level );
    }

    ActiveEffectHandle AbilityComponent::applyGameplayEffectSpecToSelf( const AbilityEffectSpec& spec )
    {
        if ( spec.isValid() == false )
            return ActiveEffectHandle{};
        const AbilityEffectLogKind rejection = findApplicationRejection( spec );
        if ( rejection != AbilityEffectLogKind::Applied )
        {
            recordEffectLog( spec, rejection );
            return ActiveEffectHandle{};
        }

        const ScopedListLock    lock{ *this };
        const AbilityEffectDef& def = *spec._pDef;
        if ( def._removeEffectsWithTags.getTagCount() > 0 )
            (void)removeActiveEffectsWithTags( def._removeEffectsWithTags ); // 지울 이펙트가 없어도 된다

        recordEffectLog( spec, AbilityEffectLogKind::Applied );
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

    ActiveEffectHandle AbilityComponent::findActiveEffectByID( const hashed_string& effectID ) const
    {
        for ( const unique_ptr<ActiveGameplayEffect>& pActive : _listActiveEffect )
        {
            const bool bMatch = pActive != nullptr && pActive->_bPendingRemove == SW_FALSE && pActive->_spec.isValid() && pActive->_spec._pDef->_id == effectID;
            if ( bMatch )
                return pActive->_handle;
        }
        return ActiveEffectHandle{};
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
        removeActiveEffectInternal( *pActiveEffect, AbilityEffectLogKind::RemovedExplicitly );
        return true;
    }

    int32 AbilityComponent::removeActiveEffectsFromSource( const hashed_string& sourceName )
    {
        if ( sourceName.size() == 0 )
            return 0;
        const ScopedListLock lock{ *this };
        int32                removedCount = 0;
        const size_t         effectCount  = _listActiveEffect.size();
        for ( size_t effectIndex = 0; effectIndex < effectCount; ++effectIndex )
        {
            ActiveGameplayEffect* pActiveEffect = _listActiveEffect[effectIndex].get();
            if ( pActiveEffect->_bPendingRemove == SW_TRUE || pActiveEffect->_spec._context._sourceName != sourceName )
                continue;
            removeActiveEffectInternal( *pActiveEffect, AbilityEffectLogKind::RemovedExplicitly );
            ++removedCount;
        }
        return removedCount;
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
            const bool bMatches = pActiveEffect->_spec._pDef->_assetTags.hasAnyTag( tags ) ||
                                  pActiveEffect->_grantedTags.hasAnyTag( tags );
            if ( bMatches == false )
                continue;
            removeActiveEffectInternal( *pActiveEffect, AbilityEffectLogKind::RemovedByTag );
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
            if ( bTimed && pActiveEffect->_grantedTags.hasAnyTag( tags ) )
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
            const optional<float32> current = getAttributeValue( modifier._attribute );
            if ( current.has_value() == false )
                return false;
            float32 magnitude = 0.0f;
            if ( computeModifierMagnitude( modifier, spec, magnitude ) == false )
                return false;
            // 같은 어트리뷰트의 변경은 합으로 본다. 하나씩 보면 각각 통과한 뒤 합쳐서 음수가 된다.
            float32 total = magnitude;
            for ( const AbilityEffectModifier& earlier : spec._pDef->_listModifier )
            {
                if ( &earlier == &modifier )
                    break;
                float32    earlierMagnitude = 0.0f;
                const bool bSameCost        = earlier._op == AttributeModOp::Add && earlier._attribute == modifier._attribute;
                if ( bSameCost && computeModifierMagnitude( earlier, spec, earlierMagnitude ) )
                    total += earlierMagnitude;
            }
            if ( *current + total < 0.0f )
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
    AbilityEffectLogKind AbilityComponent::findApplicationRejection( const AbilityEffectSpec& spec ) const
    {
        const AbilityEffectDef& def = *spec._pDef;
        if ( _ownedTagCounts.hasAllMatchingTags( def._applicationRequiredTags ) == false )
            return AbilityEffectLogKind::RejectedMissingTag;
        if ( _ownedTagCounts.hasAnyMatchingTags( def._applicationBlockedTags ) )
            return AbilityEffectLogKind::RejectedBlockedTag;
        return AbilityEffectLogKind::Applied;
    }

    void AbilityComponent::recordEffectLog( const AbilityEffectSpec& spec, AbilityEffectLogKind kind )
    {
        AbilityEffectLogEntry& entry = _arrEffectLog[_effectLogCount % kEffectLogCapacity];
        entry._effectID              = spec._pDef != nullptr ? spec._pDef->_id : hashed_string{};
        entry._sourceName            = spec._context._sourceName;
        entry._time                  = _time;
        entry._kind                  = kind;
        ++_effectLogCount;
    }

    void AbilityComponent::collectEffectLog( vector<AbilityEffectLogEntry>& outListEntry ) const
    {
        outListEntry.clear();
        const uint32 count = _effectLogCount < kEffectLogCapacity ? _effectLogCount : kEffectLogCapacity;
        for ( uint32 index = _effectLogCount - count; index < _effectLogCount; ++index )
        {
            outListEntry.push_back( _arrEffectLog[index % kEffectLogCapacity] );
        }
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
                    const optional<float32> backing = getAttributeValue( modifier._backingAttribute );
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
                    SW_LOG_WARNING_ID( "AbilityComponent.EffectSetByCallerNotSet", "Effect '%#': SetByCaller '%#' was not set - using 0", spec._pDef->_id.c_str(), modifier._setByCallerName.c_str() );
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
        // 이 이펙트로 체력이 0 에 닿으면 맞힌 자리로 치명 신호를 낸다(`notifyAttributeChanged`). 이펙트 안에서 다른 이펙트를 걸면 그 컨텍스트로 바뀌었다가 돌아온다.
        const AbilityEffectContext* pOuterContext = _pExecutingContext;
        _pExecutingContext                        = &spec._context;
        for ( const EvaluatedModifier& modifier : listModifier )
        {
            applyModifierToBase( modifier, stackScale, spec );
            if ( modifier._op == AttributeModOp::Add )
                totalMagnitude += modifier._magnitude * stackScale;
        }
        _pExecutingContext = pOuterContext;
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
            SW_LOG_WARNING_ID( "AbilityComponent.EffectModifiesWhichObject", "Effect '%#' modifies '%#' which this object does not have - skipped", spec._pDef->_id.c_str(), modifier._attribute.c_str() );
            return;
        }

        const float32 oldBase = pData->_baseValue;
        float32       newBase = modifier.applyToBase( oldBase, stackScale );
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

    void AbilityComponent::removeActiveEffectInternal( ActiveGameplayEffect& activeEffect, AbilityEffectLogKind cause )
    {
        if ( activeEffect._bPendingRemove == SW_TRUE )
            return;
        recordEffectLog( activeEffect._spec, cause );
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
        _ownedTagCounts.updateCounts( activeEffect._grantedTags, TagCountTable::kGrantDelta );
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
        _ownedTagCounts.updateCounts( activeEffect._grantedTags, TagCountTable::kRevokeDelta );
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
                    if ( pActiveEffect->_periodTimer <= kEffectTimeEpsilon )
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
                    if ( pActiveEffect->_remainingTime > kEffectTimeEpsilon )
                        continue;

                    const bool bRemoveSingleStack = def._stackExpirationPolicy == EffectStackExpirationPolicy::RemoveSingleStackAndRefreshDuration &&
                                                    pActiveEffect->_stackCount > 1 && pActiveEffect->_duration > kEffectTimeEpsilon;
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
                        removeActiveEffectInternal( *pActiveEffect, AbilityEffectLogKind::RemovedExpired );
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
        // 연출은 클라이언트 전용 창구가 낸다 — 전용 서버는 창구가 없어 큐를 내기만 하고 그리지 · 소리 내지 않는다.
        IAbilityCueSink* const pCueSink = game::findSubsystem<IAbilityCueSink>();
        if ( pCueSink == nullptr )
            return;
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
        // 자리는 지금 읽는다(자기 오브젝트라 이 워커가 읽어도 된다). 스폰 · 소리는 창구가 게임 스레드의 틱 뒤에 한다.
        const SceneComponent* pRoot    = pOwner->getPrimarySceneComponent();
        const float3          position = ( pRoot != nullptr ? pRoot->getWorldPosition() : float3{} ) + pCueDef->_offset;
        pCueSink->presentCue( *pManager, *pCueDef, position );
    }
} // namespace sw
