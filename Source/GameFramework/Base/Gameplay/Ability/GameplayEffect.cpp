#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/GameplayEffect.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Gameplay/Ability/CombatAttributeSet.h"

namespace sw
{
    float32 GameplayEffectSpec::computeDuration() const
    {
        if ( _pDef == nullptr || _pDef->_durationPolicy != EffectDurationPolicy::HasDuration )
            return 0.0f;
        return MathUtil::max( 0.0f, _pDef->_duration.compute( _level ) );
    }

    std::optional<float32> GameplayEffectSpec::getSetByCallerMagnitude( const hashed_string& name ) const
    {
        const auto mapIter = _mapSetByCaller.find( name );
        if ( mapIter == _mapSetByCaller.end() )
            return std::nullopt;
        return mapIter->second;
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
        // SetByCaller 항은 부르는 쪽이 값을 넣었을 때만 더한다(공격력 · 기본 피해만으로 때리는 이펙트도 있다).
        const std::optional<float32> callerDamage = _damageName.empty() ? std::nullopt : spec.getSetByCallerMagnitude( _damageName );
        if ( callerDamage.has_value() )
            rawDamage += *callerDamage;
        if ( rawDamage <= 0.0f )
            return;

        // 방어 감쇠: 방어 100 이면 피해가 절반이 되고, 0 이면 그대로이다. 음수 방어(약화)로 피해가 커지지 않도록 0 에서 자른다. 곱이 1 을 넘지 않게 하기 위해서다.
        // 방어 어트리뷰트가 없는 대상은 방어가 없다(감쇠하지 않는다).
        const std::optional<float32> targetArmor     = params._pTarget->getAttributeValue( CombatAttributes::armor() );
        const float32                armor           = targetArmor.has_value() ? MathUtil::max( 0.0f, *targetArmor ) : 0.0f;
        const float32                mitigatedDamage = rawDamage * 100.0f / ( 100.0f + armor );

        EvaluatedModifier modifier;
        modifier._attribute = CombatAttributes::incomingDamage();
        modifier._op        = AttributeModOp::Add;
        modifier._magnitude = mitigatedDamage;
        outListModifier.push_back( modifier );
    }
} // namespace sw
