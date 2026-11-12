#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Presentation/IDamageNumberSink.h"
#include "GameFramework/Runtime/Gameplay/Ability/Ability.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"

/**
 * @file AbilityComponentAttributes.cpp
 * @brief 어트리뷰트 세트 · 값 · 다시 계산 · 변경 알림을 구현합니다.
 * @details 파일 나눔은 `AbilityComponent.cpp` 머리말을 본다.
 */

namespace sw
{
    namespace
    {
        /** @brief 지속 이펙트의 집계 결과입니다(언리얼의 `FAggregatorModChannel` 에 해당). */
        struct AttributeAggregation
        {
            float32 _additive{ 0.0f };
            float32 _multiplyBonus{ 0.0f };
            float32 _divideBonus{ 0.0f };
            float32 _overrideValue{ 0.0f };
            bool    _bHasOverride{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다

            /** @brief 변경 하나를 스택 @p stackScale 만큼 집계에 반영합니다. */
            void accumulate( const EvaluatedModifier& modifier, float32 stackScale )
            {
                switch ( modifier._op )
                {
                    case AttributeModOp::Add:
                    {
                        _additive += modifier._magnitude * stackScale;
                        break;
                    }
                    case AttributeModOp::Multiply:
                    {
                        _multiplyBonus += ( modifier._magnitude - 1.0f ) * stackScale;
                        break;
                    }
                    case AttributeModOp::Divide:
                    {
                        if ( modifier._magnitude != 0.0f )
                            _divideBonus += ( modifier._magnitude - 1.0f ) * stackScale;
                        break;
                    }
                    case AttributeModOp::Override:
                    {
                        _overrideValue = modifier._magnitude;
                        _bHasOverride  = true;
                        break;
                    }
                }
            }

            /** @brief base 에 집계를 적용한 값입니다(언리얼의 `FAggregatorModChannel::EvaluateWithBase` 에 해당). */
            float32 evaluate( float32 baseValue ) const
            {
                if ( _bHasOverride )
                    return _overrideValue;
                float32       value       = ( baseValue + _additive ) * ( 1.0f + _multiplyBonus );
                const float32 denominator = 1.0f + _divideBonus;
                if ( MathUtil::abs( denominator ) > EvaluatedModifier::kMinDenominator )
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
    AttributeSet* AbilityComponent::addAttributeSet( unique_ptr<AttributeSet> pAttributeSet )
    {
        if ( pAttributeSet == nullptr )
            return nullptr;

        for ( const hashed_string& name : pAttributeSet->getAttributeNames() )
        {
            if ( findAttributeSetFor( name ) != nullptr )
                SW_LOG_WARNING_ID( "AbilityComponent.AddAttributeSetAttributeAlreadyExists", "addAttributeSet: attribute '%#' already exists in another set - the earlier set wins", name.c_str() );
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

    optional<float32> AbilityComponent::getAttributeValue( const hashed_string& name ) const
    {
        const AttributeSet*  pAttributeSet = findAttributeSetFor( name );
        const AttributeData* pData         = pAttributeSet != nullptr ? pAttributeSet->findAttribute( name ) : nullptr;
        if ( pData == nullptr )
            return nullopt;
        return pData->_currentValue;
    }

    optional<float32> AbilityComponent::getAttributeBaseValue( const hashed_string& name ) const
    {
        const AttributeSet*  pAttributeSet = findAttributeSetFor( name );
        const AttributeData* pData         = pAttributeSet != nullptr ? pAttributeSet->findAttribute( name ) : nullptr;
        if ( pData == nullptr )
            return nullopt;
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

    bool AbilityComponent::applyFatalDamage( bool bIgnoreInvulnerable )
    {
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = _healthAttribute.empty() ? nullptr : findAttributeDataMutable( _healthAttribute, &pAttributeSet );
        if ( pData == nullptr || pData->_baseValue <= 0.0f )
            return false;
        if ( isInvulnerable() && bIgnoreInvulnerable == false )
        {
            ++_blockedHealthDecreaseCount;
            return false;
        }
        // 무적을 넘는 즉사는 `guardHealthBaseChange` 를 지나지 않는다 — base 를 직접 쓰는 길은 이것 하나다.
        float32 newBase = 0.0f;
        pAttributeSet->preAttributeBaseChange( _healthAttribute, newBase );
        pData->_baseValue = newBase;
        recomputeAttribute( _healthAttribute );
        pAttributeSet->postFatalDamage();
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
        if ( _bRestoringState == SW_TRUE )
            return; // 상태 적용 끝에 모든 어트리뷰트를 한 번에 집계 · 클램프한다(`applySaveState`) — 하나씩 하면 체력이 옛 최대 체력으로 잘린다
        AttributeSet*  pAttributeSet = nullptr;
        AttributeData* pData         = findAttributeDataMutable( name, &pAttributeSet );
        if ( pData == nullptr )
            return;

        float32 newValue = computeAggregatedValue( name, pData->_baseValue );
        pAttributeSet->preAttributeChange( name, newValue );
        const float32 oldValue = pData->_currentValue;
        if ( oldValue == newValue )
            return;
        pData->_currentValue = newValue;
        notifyAttributeChanged( *pAttributeSet, name, oldValue, newValue );
    }

    float32 AbilityComponent::computeAggregatedValue( const hashed_string& name, float32 baseValue ) const
    {
        AttributeAggregation aggregation;
        for ( const unique_ptr<ActiveGameplayEffect>& pActiveEffect : _activeEffects )
        {
            const bool bContributes = pActiveEffect->_bPendingRemove == SW_FALSE && pActiveEffect->_bInhibited == SW_FALSE;
            if ( bContributes == false )
                continue;
            const float32 stackScale = static_cast<float32>( pActiveEffect->_stackCount );
            for ( const EvaluatedModifier& modifier : pActiveEffect->_listEvaluatedModifier )
            {
                if ( modifier._attribute == name )
                    aggregation.accumulate( modifier, stackScale );
            }
        }

        return aggregation.evaluate( baseValue );
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
        // 쓰러졌다 — 래그돌 · 절단에 치명 신호를 낸다(`deliverFatalHit`). 맞힌 자리는 즉시 이펙트의 컨텍스트가 실은 것이고, 없으면(주기 피해 · 코드가 깎음) 바디 없는 신호다.
        const bool bDied = bHealth && oldValue > 0.0f && newValue <= 0.0f;
        if ( bDied )
        {
            HitInfo hit;
            if ( _pExecutingContext != nullptr && _pExecutingContext->_bHasHit == SW_TRUE )
            {
                GameObject*        pOwner   = getOwner();
                GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
                hit                         = _pExecutingContext->_hit;
                hit._pInstigator            = pManager != nullptr ? pManager->resolveGameObject( _pExecutingContext->_instigator ) : nullptr;
            }
            deliverFatalHit( hit );
        }
        const bool bDamageTaken = bHealth && newValue < oldValue && _bShowDamageNumbers && _bRestoringState == SW_FALSE; // 불러온 체력은 맞은 것이 아니다
        if ( bDamageTaken )
            spawnDamageNumber( oldValue - newValue );
    }

} // namespace sw
