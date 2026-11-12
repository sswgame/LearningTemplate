#include "pch.h"

#include "Core/Container/string.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Runtime/Gameplay/Ability/Ability.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySaveState.h"

#include <algorithm>

/**
 * @file AbilityComponentSave.cpp
 * @brief 어빌리티 컴포넌트의 런타임 상태를 상태 스냅샷(세이브 · 핫 리로드)에 싣고 되돌리는 일을 구현합니다.
 * @details 파일 나눔은 `AbilityComponent.cpp` 머리말을 본다.
 */

namespace sw
{
    namespace
    {
        /** @brief 어트리뷰트를 바꾸는 이펙트 · 어빌리티 세트를 모든 게임의 상태 스냅샷에 싣습니다(게임마다 올리지 않는다 — 빠뜨린 게임만 불러오기로 회복된다). */
        const StatefulComponentRegistrar<AbilityComponent> s_stateRegistration{ "AbilityComponent" };

        struct AbilityComponentSaveInternal
        {
            /** @brief 태그 하나와 그 개수입니다(이펙트 밖 몫을 셀 때). */
            struct TagCount
            {
                TagID _tag{};
                int32 _count{ 0 };
            };

            static TagCount* findTagCount( vector<TagCount>& listCount, TagID tag )
            {
                for ( TagCount& entry : listCount )
                {
                    if ( entry._tag == tag )
                        return &entry;
                }
                return nullptr;
            }

            static void subtractTags( vector<TagCount>& listCount, const TagContainer& tags )
            {
                for ( const TagID& tag : tags.getTags() )
                {
                    if ( TagCount* pEntry = findTagCount( listCount, tag ) )
                        --pEntry->_count;
                }
            }

            /** @brief 맵을 이름 차례의 목록으로 옮깁니다 — 해시 맵 순회 순서는 실행마다 달라 같은 상태가 다른 바이트가 된다. */
            static void appendSortedValues( const unordered_map<hashed_string, float32>& mapValue, vector<SavedAttributeValue>& outListValue )
            {
                for ( const auto& [name, value] : mapValue )
                {
                    outListValue.push_back( SavedAttributeValue{ name, value } );
                }
                std::sort( outListValue.begin(), outListValue.end(), []( const SavedAttributeValue& lhs, const SavedAttributeValue& rhs )
                { return lhs._attribute.view() < rhs._attribute.view(); } );
            }

            /** @brief 상태를 적용하기 전의 current 하나입니다 — 적용 뒤 바뀐 것만 알린다. */
            struct PreviousValue
            {
                AttributeSet* _pSet{ nullptr };
                hashed_string _attribute{};
                float32       _value{ 0.0f };
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    void AbilityComponent::serializeState( StateIO& io )
    {
        AbilitySaveState state;
        if ( io.isWriting() )
            captureSaveState( state );
        io.value( state );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        if ( applySaveState( state ) == false )
            io.fail();
    }

    void AbilityComponent::restoreState( vector<uint8>&& bytes )
    {
        _pendingStateBytes = std::move( bytes );
        if ( hasBegunPlay() )
            (void)applyPendingState(); // 실패는 안에서 알린다
    }

    bool AbilityComponent::applyPendingState()
    {
        if ( _pendingStateBytes.empty() )
            return false;
        Archive    archive( _pendingStateBytes.data(), static_cast<uint64>( _pendingStateBytes.size() ) );
        const bool bRead = StateIO::readInPlace( *this, archive ); // 본문을 지역 값에 다 읽고 세트 · 카탈로그와 맞춘 뒤에만 바꾼다
        _pendingStateBytes.clear();
        if ( bRead == false )
            SW_LOG_WARNING_ID( "AbilityComponent.SavedStateDidNotRead", "Ability state of '%#' did not read - starting fresh", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
        return bRead;
    }

    void AbilityComponent::captureSaveState( AbilitySaveState& outState ) const
    {
        outState._time = _time;
        for ( const unique_ptr<AttributeSet>& pAttributeSet : _listAttributeSet )
        {
            for ( const hashed_string& name : pAttributeSet->getAttributeNames() )
            {
                const AttributeData* pData = pAttributeSet->findAttribute( name );
                if ( pData != nullptr )
                    outState._listBaseValue.push_back( SavedAttributeValue{ name, pData->_baseValue } );
            }
        }

        vector<AbilityComponentSaveInternal::TagCount> listTagCount;
        for ( const TagID& tag : _ownedTagCounts.getTags().getTags() )
        {
            listTagCount.push_back( AbilityComponentSaveInternal::TagCount{ tag, _ownedTagCounts.getCount( tag ) } );
        }

        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _activeEffects )
        {
            const ActiveGameplayEffect& activeEffect = *pActiveEffect;
            if ( activeEffect._bPendingRemove == SW_TRUE )
                continue;
            if ( activeEffect._bInhibited == SW_FALSE )
                AbilityComponentSaveInternal::subtractTags( listTagCount, activeEffect._grantedTags ); // 다시 걸 때 붙는다
            const AbilityEffectSpec& spec = activeEffect._spec;
            if ( spec._pDef->_id.empty() )
            {
                // 카탈로그 밖(코드)에서 만든 정의는 다른 실행에서 다시 찾을 길이 없다 — 이펙트는 카탈로그 id 로만 싣는다.
                SW_LOG_ERROR_ID( "AbilityComponent.SavedEffectWithoutID", "Ability state of '%#': an active effect has no catalog id and is not saved", getOwner() != nullptr ? getOwner()->getName().c_str() : "?" );
                continue;
            }
            SavedAbilityEffect saved;
            saved._effectID      = spec._pDef->_id;
            saved._sourceName    = spec._context._sourceName;
            saved._level         = spec._level;
            saved._stackCount    = activeEffect._stackCount;
            saved._duration      = activeEffect._duration;
            saved._remainingTime = activeEffect._remainingTime;
            saved._periodTimer   = activeEffect._periodTimer;
            saved._startTime     = activeEffect._startTime;
            AbilityComponentSaveInternal::appendSortedValues( spec._mapSetByCaller, saved._listSetByCaller );
            AbilityComponentSaveInternal::appendSortedValues( spec._mapSourceAttribute, saved._listSourceAttribute );
            for ( const TagID& tag : spec._dynamicGrantedTags.getTags() )
            {
                saved._listDynamicGrantedTag.push_back( string( tag.getString() ) );
            }
            for ( const EvaluatedModifier& modifier : activeEffect._listEvaluatedModifier )
            {
                saved._listModifier.push_back( SavedAbilityModifier{ modifier._attribute, modifier._op, modifier._magnitude } );
            }
            outState._listEffect.push_back( std::move( saved ) );
        }

        // 실행 중 어빌리티가 붙인 태그는 빼고 싣는다 — 어빌리티는 이어 가지 않으므로 실으면 뗄 주인이 없는 태그로 남는다.
        for ( const unique_ptr<AbilitySpec>& pSpec : _abilitySpecs )
        {
            if ( pSpec->_bPendingRemove == SW_FALSE && pSpec->_pAbility->isActive() )
                AbilityComponentSaveInternal::subtractTags( listTagCount, pSpec->_pAbility->getConfig()._activationOwnedTags );
        }
        for ( const AbilityComponentSaveInternal::TagCount& entry : listTagCount )
        {
            if ( entry._count > 0 )
                outState._listLooseTag.push_back( SavedAbilityTag{ string( entry._tag.getString() ), entry._count } );
        }
    }

    bool AbilityComponent::applySaveState( const AbilitySaveState& state )
    {
        const hashed_string ownerName = getOwner() != nullptr ? getOwner()->getName() : hashed_string{ "?" };

        // 1) 맞추기 — 아무것도 바꾸기 전에 끝낸다. 세이브의 값을 받을 자리가 없으면 조용히 건너뛰지 않고 통째로 거절한다.
        for ( const SavedAttributeValue& saved : state._listBaseValue )
        {
            if ( findAttributeSetFor( saved._attribute ) == nullptr )
            {
                SW_LOG_ERROR_ID( "AbilityComponent.SavedAttributeMissing", "Ability state of '%#': the save has attribute '%#' but no attribute set here defines it - refused", ownerName.c_str(), saved._attribute.c_str() );
                return false;
            }
        }
        const AbilityCatalog* pCatalog = findCatalog();
        if ( state._listEffect.empty() == false && pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "AbilityComponent.SavedEffectsNoCatalog", "Ability state of '%#': the save has active effects but there is no AbilityCatalog - refused", ownerName.c_str() );
            return false;
        }
        vector<shared_ptr<const AbilityEffectDef>> listDef;
        listDef.reserve( state._listEffect.size() );
        for ( const SavedAbilityEffect& saved : state._listEffect )
        {
            shared_ptr<const AbilityEffectDef> pDef = pCatalog->findEffect( saved._effectID );
            if ( pDef == nullptr || pDef->_durationPolicy == EffectDurationPolicy::Instant )
            {
                SW_LOG_ERROR_ID( "AbilityComponent.SavedEffectUnknown", "Ability state of '%#': saved effect '%#' is not a duration or infinite effect in the catalog - refused", ownerName.c_str(), saved._effectID.c_str() );
                return false;
            }
            listDef.push_back( std::move( pDef ) );
        }

        // 2) 쓰기 — 실행 중 어빌리티를 끝내고(그 태그가 빠진다), 지금 이펙트를 뗀 뒤 태그 · base · 이펙트를 훅 없이 쓴다.
        cancelAllAbilities();
        vector<AbilityComponentSaveInternal::PreviousValue> listPrevious;
        for ( const unique_ptr<AttributeSet>& pAttributeSet : _listAttributeSet )
        {
            for ( const hashed_string& name : pAttributeSet->getAttributeNames() )
            {
                const AttributeData* pData = pAttributeSet->findAttribute( name );
                if ( pData != nullptr )
                    listPrevious.push_back( AbilityComponentSaveInternal::PreviousValue{ pAttributeSet.get(), name, pData->_currentValue } );
            }
        }

        _bRestoringState = SW_TRUE;
        {
            const ScopedListLock lock{ *this };
            const size_t         effectCount = _activeEffects.size();
            for ( size_t effectIndex = 0; effectIndex < effectCount; ++effectIndex )
            {
                // 해제 큐 · 알림을 낸다 — 버프 아이콘 · 걸린 동안의 연출이 지금 상태를 따라가야 한다.
                removeActiveEffectInternal( *_activeEffects[effectIndex], AbilityEffectLogKind::RemovedExplicitly );
            }

            // 이제 가진 태그는 이펙트 밖 몫뿐이다 — 세이브의 개수로 맞춘다(세트의 태그도 세이브에 들어 있어 두 번 붙지 않는다).
            vector<AbilityComponentSaveInternal::TagCount> listTarget;
            for ( const SavedAbilityTag& saved : state._listLooseTag )
            {
                listTarget.push_back( AbilityComponentSaveInternal::TagCount{ TagID::request( saved._tag ), saved._count } );
            }
            vector<TagID> listOwned = _ownedTagCounts.getTags().getTags(); // 고치는 동안 목록이 바뀌므로 사본으로 돈다
            for ( const TagID& tag : listOwned )
            {
                if ( AbilityComponentSaveInternal::findTagCount( listTarget, tag ) == nullptr )
                    _ownedTagCounts.updateCount( tag, -_ownedTagCounts.getCount( tag ) );
            }
            for ( const AbilityComponentSaveInternal::TagCount& target : listTarget )
            {
                const int32 delta = target._count - _ownedTagCounts.getCount( target._tag );
                if ( delta != 0 )
                    _ownedTagCounts.updateCount( target._tag, delta );
            }

            for ( const SavedAttributeValue& saved : state._listBaseValue )
            {
                findAttributeDataMutable( saved._attribute, nullptr )->_baseValue = saved._value; // 1) 에서 있음을 봤다
            }
            _time = state._time;

            for ( size_t savedIndex = 0; savedIndex < state._listEffect.size(); ++savedIndex )
            {
                const SavedAbilityEffect&        saved      = state._listEffect[savedIndex];
                const AbilityEffectDef&          def        = *listDef[savedIndex];
                unique_ptr<ActiveGameplayEffect> pNewEffect = make_unique<ActiveGameplayEffect>();
                pNewEffect->_handle                         = ActiveEffectHandle{ ++_nextHandleID };
                pNewEffect->_spec._pDef                     = listDef[savedIndex];
                pNewEffect->_spec._level                    = saved._level;
                pNewEffect->_spec._context._sourceName      = saved._sourceName;
                for ( const SavedAttributeValue& value : saved._listSetByCaller )
                {
                    pNewEffect->_spec._mapSetByCaller.insert_or_assign( value._attribute, value._value );
                }
                for ( const SavedAttributeValue& value : saved._listSourceAttribute )
                {
                    pNewEffect->_spec._mapSourceAttribute.insert_or_assign( value._attribute, value._value );
                }
                pNewEffect->_grantedTags = def._grantedTags;
                for ( const string& tagText : saved._listDynamicGrantedTag )
                {
                    const TagID tag = TagID::request( tagText );
                    pNewEffect->_spec._dynamicGrantedTags.addTag( tag );
                    pNewEffect->_grantedTags.addTag( tag );
                }
                for ( const SavedAbilityModifier& modifier : saved._listModifier )
                {
                    pNewEffect->_listEvaluatedModifier.push_back( EvaluatedModifier{ modifier._attribute, modifier._op, modifier._magnitude } );
                }
                pNewEffect->_duration      = saved._duration;
                pNewEffect->_remainingTime = saved._remainingTime;
                pNewEffect->_periodTimer   = saved._periodTimer;
                pNewEffect->_startTime     = saved._startTime;
                pNewEffect->_stackCount    = saved._stackCount;
                // 걸린 동안의 조건이 있는 이펙트는 멈춘 채 넣고 아래 `refreshOngoingTagRequirements` 가 모든 태그가 선 뒤에 붙인다.
                pNewEffect->_bInhibited = def._ongoingRequiredTags.getTagCount() > 0 ? SW_TRUE : SW_FALSE;

                ActiveGameplayEffect* pActiveEffect = pNewEffect.get();
                _activeEffects.add( std::move( pNewEffect ) );
                if ( pActiveEffect->_bInhibited == SW_FALSE )
                    engageActiveEffect( *pActiveEffect );
                dispatchCues( pActiveEffect->_spec, AbilityPresentationPhase::Added, static_cast<float32>( pActiveEffect->_stackCount ) );
                _effectAppliedMulticast.broadcast( *pActiveEffect );
            }
            refreshOngoingTagRequirements();
        }

        // 3) 한 번에 집계 · 클램프 — 먼저 모든 current 를 훅 없이 세운 뒤(체력 클램프가 읽는 최대 체력이 버프 · 레벨 업을 반영한다) base 와 current 를 훅으로 자른다.
        for ( const unique_ptr<AttributeSet>& pAttributeSet : _listAttributeSet )
        {
            for ( const hashed_string& name : pAttributeSet->getAttributeNames() )
            {
                AttributeData* pData = pAttributeSet->findAttributeMutable( name );
                pData->_currentValue = computeAggregatedValue( name, pData->_baseValue );
            }
        }
        for ( const unique_ptr<AttributeSet>& pAttributeSet : _listAttributeSet )
        {
            for ( const hashed_string& name : pAttributeSet->getAttributeNames() )
            {
                AttributeData* pData   = pAttributeSet->findAttributeMutable( name );
                float32        newBase = pData->_baseValue;
                pAttributeSet->preAttributeBaseChange( name, newBase );
                pData->_baseValue = newBase;
                float32 newValue  = computeAggregatedValue( name, newBase );
                pAttributeSet->preAttributeChange( name, newValue );
                pData->_currentValue = newValue;
            }
        }
        // 알림은 값이 다 선 뒤에 낸다. 훅이 다시 쓰는 값(최대 체력이 줄어 체력을 다시 씀)은 이미 맞아 집계를 미뤄도 잃는 것이 없다.
        for ( const AbilityComponentSaveInternal::PreviousValue& previous : listPrevious )
        {
            const AttributeData* pData = previous._pSet->findAttribute( previous._attribute );
            if ( pData->_currentValue != previous._value )
                notifyAttributeChanged( *previous._pSet, previous._attribute, previous._value, pData->_currentValue );
        }
        _bRestoringState = SW_FALSE;

        refreshOngoingTagRequirements();
        notifyHealthChanged( true );
        return true;
    }
} // namespace sw
