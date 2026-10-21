/**
 * @file AbilitySystemComponentAttribute.cpp
 * @brief `AbilitySystemComponent` 의 어트리뷰트 부분입니다.
 * @details 세트 등록 · 값 조회 · base 쓰기 · 지속 이펙트 집계로 현재값 다시 계산 · 변경 알림을 맡습니다.
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
    SW_LOG_CALLER( "AbilitySystemComponentAttribute" );

    namespace
    {
        struct AbilitySystemComponentAttributeInternal
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
        };
    } // namespace
} // namespace sw

namespace sw
{
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

        // 먼저 걸린 지속 이펙트가 이 세트의 어트리뷰트를 바꾸고 있었을 수 있으므로, current 를 지금 다시 집계한다.
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

    std::optional<float32> AbilitySystemComponent::getAttributeValue( const hashed_string& name ) const
    {
        const AttributeSet*  pAttributeSet = findAttributeSetFor( name );
        const AttributeData* pData         = pAttributeSet != nullptr ? pAttributeSet->findAttribute( name ) : nullptr;
        if ( pData == nullptr )
            return std::nullopt;
        return pData->_currentValue;
    }

    std::optional<float32> AbilitySystemComponent::getAttributeBaseValue( const hashed_string& name ) const
    {
        const AttributeSet*  pAttributeSet = findAttributeSetFor( name );
        const AttributeData* pData         = pAttributeSet != nullptr ? pAttributeSet->findAttribute( name ) : nullptr;
        if ( pData == nullptr )
            return std::nullopt;
        return pData->_baseValue;
    }

    bool AbilitySystemComponent::setAttributeBaseValue( const hashed_string& name, float32 value )
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

    TagID AbilitySystemComponent::getInvulnerableTag()
    {
        return "State.Invulnerable"_tag;
    }

    void AbilitySystemComponent::guardHealthBaseChange( const hashed_string& name, float32 oldBase, float32& inoutNewBase ) const
    {
        const bool bHealthGoesDown = name == _healthAttribute && inoutNewBase < oldBase;
        if ( bHealthGoesDown && isInvulnerable() )
            inoutNewBase = oldBase;
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
    // 8) 내부: 어트리뷰트
    // ------------------------------------------------------------------------------
    void AbilitySystemComponent::recomputeAttribute( const hashed_string& name )
    {
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = findAttributeDataMutable( name, &pAttributeSet );
        if ( pData == nullptr )
            return;

        AbilitySystemComponentAttributeInternal::Aggregation aggregation;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _listActiveEffect )
        {
            const bool bContributes = pActiveEffect->_bPendingRemove == SW_FALSE && pActiveEffect->_bInhibited == SW_FALSE;
            if ( bContributes == false )
                continue;
            const float32 stackScale = static_cast<float32>( pActiveEffect->_stackCount );
            for ( const EvaluatedModifier& modifier : pActiveEffect->_listEvaluatedModifier )
            {
                if ( modifier._attribute == name )
                    AbilitySystemComponentAttributeInternal::accumulate( modifier, stackScale, aggregation );
            }
        }

        float32 newValue = AbilitySystemComponentAttributeInternal::evaluate( pData->_baseValue, aggregation );
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
        const bool bDamageTaken = bHealth && newValue < oldValue && _bShowDamageNumbers;
        if ( bDamageTaken )
            spawnDamageNumber( oldValue - newValue );
    }
} // namespace sw
