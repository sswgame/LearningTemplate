#include "pch.h"

#include "GameFramework/Base/Ability/GameplayEffect.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Ability/CombatAttributeSet.h"

namespace sw
{
    float32 GameplayEffectSpec::computeDuration() const
    {
        if ( _pDef == nullptr || _pDef->_durationPolicy != EffectDurationPolicy::HasDuration )
            return 0.0f;
        return MathUtil::max( 0.0f, _pDef->_duration.compute( _level ) );
    }

    float32 GameplayEffectSpec::getSetByCallerMagnitude( const hashed_string& name, float32 fallback ) const
    {
        const auto mapIter = _mapSetByCaller.find( name );
        return mapIter != _mapSetByCaller.end() ? mapIter->second : fallback;
    }

    bool GameplayEffectSpec::findSourceAttribute( const hashed_string& name, float32& outValue ) const
    {
        const auto mapIter = _mapSourceAttribute.find( name );
        if ( mapIter == _mapSourceAttribute.end() )
            return false;
        outValue = mapIter->second;
        return true;
    }

    DamageExecution::DamageExecution()
        : _damageName{ "Damage" }
        , _attackPowerCoefficient{ 0.0f }
        , _baseDamage{ 0.0f }
    {
    }

    void DamageExecution::setParameters( const hashed_string& damageName, float32 attackPowerCoefficient, float32 baseDamage )
    {
        _damageName             = damageName;
        _attackPowerCoefficient = attackPowerCoefficient;
        _baseDamage             = baseDamage;
    }

    void DamageExecution::execute( const GameplayEffectExecutionParams& params, vector<EvaluatedModifier>& outListModifier ) const
    {
        if ( params._pSpec == nullptr || params._pTarget == nullptr )
            return;

        const GameplayEffectSpec& spec        = *params._pSpec;
        float32                   attackPower = 0.0f;
        (void)spec.findSourceAttribute( CombatAttributes::attackPower(), attackPower ); // 쏜 쪽에 공격력이 없으면 0 이다

        float32 rawDamage = _baseDamage + attackPower * _attackPowerCoefficient;
        if ( _damageName.empty() == false )
            rawDamage += spec.getSetByCallerMagnitude( _damageName, 0.0f );
        if ( rawDamage <= 0.0f )
            return;

        // 방어 감쇠 — 방어 100 이면 절반, 0 이면 그대로. 음수 방어(약화)로 피해가 커지지는 않게 0 에서 자른다(곱이 1 을 넘지 않는다).
        const float32 armor           = MathUtil::max( 0.0f, params._pTarget->getAttributeValue( CombatAttributes::armor() ) );
        const float32 mitigatedDamage = rawDamage * 100.0f / ( 100.0f + armor );

        EvaluatedModifier modifier;
        modifier._attribute = CombatAttributes::incomingDamage();
        modifier._op        = AttributeModOp::Add;
        modifier._magnitude = mitigatedDamage;
        outListModifier.push_back( modifier );
    }
} // namespace sw
