#include "pch.h"

#include "GameFramework/Base/Combat/ResourceGauge.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Utility/StateArchiveUtil.h"

namespace sw
{
    ResourceGauge::ResourceGauge()
        : _settings{}
        , _value{ 0.0f }
        , _maxBonus{ 0.0f }
        , _regenScale{ 1.0f }
        , _sinceUse{ 0.0f }
        , _overheatPenalty{}
        , _bLocked{ SW_FALSE }
    {
        initialize( _settings );
    }

    ResourceGauge::ResourceGauge( const ResourceGaugeSettings& settings )
        : ResourceGauge()
    {
        initialize( settings );
    }

    void ResourceGauge::initialize( const ResourceGaugeSettings& settings )
    {
        _settings                       = settings;
        _settings._max                  = MathUtil::max( 1.0e-3f, _settings._max );
        _settings._regenRate            = MathUtil::max( 0.0f, _settings._regenRate );
        _settings._regenDelay           = MathUtil::max( 0.0f, _settings._regenDelay );
        _settings._exhaustThreshold     = MathUtil::clamp( _settings._exhaustThreshold, 0.0f, _settings._max );
        _settings._overheatCooldown     = MathUtil::max( 0.0f, _settings._overheatCooldown );
        _settings._overheatRecoverLevel = MathUtil::clamp( _settings._overheatRecoverLevel, 0.0f, _settings._max );
        _maxBonus                       = 0.0f;
        _regenScale                     = 1.0f;
        refill();
    }

    bool ResourceGauge::trySpend( float32 amount )
    {
        if ( _bLocked == SW_TRUE || amount < 0.0f )
            return false;
        if ( _settings._bOverheatMode == SW_TRUE )
        {
            if ( _value + amount > getMax() )
                return false;
            _value += amount;
            markUsed();
            if ( _value >= getMax() )
            {
                _bLocked = SW_TRUE;
                _overheatPenalty.start( _settings._overheatCooldown );
            }
            return true;
        }
        if ( _value < amount )
            return false;
        _value -= amount;
        markUsed();
        if ( _value <= 0.0f && _settings._exhaustThreshold > 0.0f )
        {
            _value   = 0.0f;
            _bLocked = SW_TRUE;
        }
        return true;
    }

    bool ResourceGauge::drain( float32 rate, float32 deltaTime )
    {
        if ( _bLocked == SW_TRUE )
            return false;
        const float32 amount = MathUtil::max( 0.0f, rate ) * MathUtil::max( 0.0f, deltaTime );
        if ( _settings._bOverheatMode == SW_TRUE )
        {
            _value = MathUtil::min( getMax(), _value + amount );
            markUsed();
            if ( _value >= getMax() )
            {
                _bLocked = SW_TRUE;
                _overheatPenalty.start( _settings._overheatCooldown );
                return false;
            }
            return true;
        }
        if ( _value <= 0.0f )
        {
            // 바닥에 닿은 채 계속 쓰려 한다 — 쓰는 중이니 회복 지연을 다시 센다(0 에 붙잡혀 있어도 다시 차지 않게).
            if ( amount > 0.0f )
                markUsed();
            return false;
        }
        _value -= amount;
        markUsed();
        if ( _value > 0.0f )
            return true;
        _value = 0.0f;
        if ( _settings._exhaustThreshold > 0.0f )
            _bLocked = SW_TRUE;
        return false;
    }

    float32 ResourceGauge::reduce( float32 amount )
    {
        if ( amount <= 0.0f )
            return 0.0f;
        markUsed();
        if ( _settings._bOverheatMode == SW_TRUE )
        {
            const float32 before = _value;
            _value               = MathUtil::min( getMax(), _value + amount );
            if ( _value >= getMax() && _bLocked == SW_FALSE )
            {
                _bLocked = SW_TRUE;
                _overheatPenalty.start( _settings._overheatCooldown );
            }
            return _value - before;
        }
        const float32 reduced = MathUtil::min( _value, amount );
        _value -= reduced;
        if ( _value <= 0.0f && _settings._exhaustThreshold > 0.0f )
        {
            _value   = 0.0f;
            _bLocked = SW_TRUE;
        }
        return reduced;
    }

    void ResourceGauge::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _overheatPenalty.isActive() )
        {
            // 과열 벌칙 — 이 시간 동안은 식지도 않는다. 벌칙이 끝난 걸음의 남은 몫은 버린다.
            _overheatPenalty.tick( deltaTime );
            return;
        }
        _sinceUse += deltaTime;
        // 이번 걸음 중 지연이 끝난 뒤의 몫만 찬다(걸음 크기가 달라도 같은 결과).
        const float32 regenTime = MathUtil::clamp( _sinceUse - _settings._regenDelay, 0.0f, deltaTime );
        if ( regenTime <= 0.0f || _settings._regenRate <= 0.0f )
            return;
        const float32 amount = _settings._regenRate * _regenScale * regenTime;
        if ( _settings._bOverheatMode == SW_TRUE )
            _value = MathUtil::max( 0.0f, _value - amount );
        else
            _value = MathUtil::min( getMax(), _value + amount );
        releaseLockIfRecovered();
    }

    void ResourceGauge::restore( float32 amount )
    {
        if ( amount <= 0.0f )
            return;
        if ( _settings._bOverheatMode == SW_TRUE )
            _value = MathUtil::max( 0.0f, _value - amount );
        else
            _value = MathUtil::min( getMax(), _value + amount );
        releaseLockIfRecovered();
    }

    void ResourceGauge::refill()
    {
        _value    = _settings._bOverheatMode == SW_TRUE ? 0.0f : getMax();
        _sinceUse = _settings._regenDelay;
        _overheatPenalty.clear();
        _bLocked = SW_FALSE;
    }

    void ResourceGauge::setMaxBonus( float32 bonus )
    {
        const float32 oldMax = getMax();
        _maxBonus            = MathUtil::max( -_settings._max + 1.0e-3f, bonus );
        const float32 delta  = getMax() - oldMax;
        if ( _settings._bOverheatMode == SW_FALSE && delta > 0.0f )
            _value += delta;
        _value = MathUtil::min( _value, getMax() );
    }

    float32 ResourceGauge::getRatio() const { return MathUtil::saturate( _value / getMax() ); }

    bool ResourceGauge::canUse() const
    {
        if ( _bLocked == SW_TRUE )
            return false;
        return _settings._bOverheatMode == SW_TRUE ? _value < getMax() : _value > 0.0f;
    }

    void ResourceGauge::markUsed() { _sinceUse = 0.0f; }

    void ResourceGauge::releaseLockIfRecovered()
    {
        if ( _bLocked == SW_FALSE || _overheatPenalty.isActive() )
            return;
        const bool bRecovered = _settings._bOverheatMode == SW_TRUE ? _value <= _settings._overheatRecoverLevel : _value >= _settings._exhaustThreshold;
        if ( bRecovered )
            _bLocked = SW_FALSE;
    }

    void ResourceGauge::writeState( Archive& outArchive ) const
    {
        outArchive << _value;
        outArchive << _maxBonus;
        outArchive << _regenScale;
        outArchive << _sinceUse;
        StateArchiveUtil::writeCountdown( outArchive, _overheatPenalty );
        outArchive << _bLocked;
    }

    bool ResourceGauge::readState( Archive& archive )
    {
        ResourceGauge restored = *this;
        archive >> restored._value;
        archive >> restored._maxBonus;
        archive >> restored._regenScale;
        archive >> restored._sinceUse;
        const bool bTimerRead = StateArchiveUtil::readCountdown( archive, restored._overheatPenalty );
        archive >> restored._bLocked;
        const bool bValid = bTimerRead && archive.isOk() && 0.0f <= restored._value && restored._bLocked <= SW_TRUE;
        if ( bValid == false )
            return false;
        *this = restored;
        return true;
    }
} // namespace sw
