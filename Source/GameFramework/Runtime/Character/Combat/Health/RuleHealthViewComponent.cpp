#include "pch.h"

#include "GameFramework/Runtime/Character/Combat/Health/RuleHealthViewComponent.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    RuleHealthViewComponent::RuleHealthViewComponent()
        : _health{ 0.0f }
        , _maxHealth{ 0.0f }
        , _bHasSnapshot{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    RuleHealthViewComponent::~RuleHealthViewComponent() = default;

    HealthReading RuleHealthViewComponent::getHealthReading() const
    {
        HealthReading reading;
        reading._health     = _health;
        reading._maxHealth  = _maxHealth;
        reading._bHasHealth = _bHasSnapshot;
        reading._bDead      = _bHasSnapshot == SW_TRUE && _health <= 0.0f ? SW_TRUE : SW_FALSE;
        return reading;
    }

    void RuleHealthViewComponent::applySnapshot( float32 health, float32 maxHealth )
    {
        const bool bFirst   = _bHasSnapshot == SW_FALSE;
        const bool bChanged = MathUtil::nearEqual( _health, health ) == false || MathUtil::nearEqual( _maxHealth, maxHealth ) == false;
        if ( bFirst == false && bChanged == false )
            return;
        _health       = health;
        _maxHealth    = maxHealth;
        _bHasSnapshot = SW_TRUE;
        IHealthSource::notifyHealthChanged( _healthSourcePort, getOwner(), bFirst );
    }

    HealthReading RuleHealthViewComponent::HealthSourcePort::getHealthReading() const
    {
        return getPortOwner<const RuleHealthViewComponent>( *this, offsetof( RuleHealthViewComponent, _healthSourcePort ) ).getHealthReading();
    }
} // namespace sw
