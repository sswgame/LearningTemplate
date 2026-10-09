#include "pch.h"

#include "GameFramework/Kits/Horror/CoopScavenger/ScavengerQuota.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/GameRandom.h"

namespace sw
{
    ScavengerQuota::ScavengerQuota()
        : _settings{}
        , _quota{ 0 }
        , _fulfilled{ 0 }
        , _daysLeft{ 0 }
        , _cycle{ 0 }
        , _bGameOver{ SW_FALSE }
    {
    }

    void ScavengerQuota::initialize( const ScavengerQuotaSettings& settings )
    {
        _settings  = settings;
        _quota     = settings._startQuota;
        _fulfilled = 0;
        _daysLeft  = settings._daysPerCycle;
        _cycle     = 0;
        _bGameOver = SW_FALSE;
    }

    void ScavengerQuota::addFulfilled( int32 value )
    {
        if ( value > 0 && _bGameOver == SW_FALSE )
            _fulfilled += value;
    }

    ScavengerDeadlineResult ScavengerQuota::endDay( GameRandom& random, int32& outOvertimeBonus )
    {
        outOvertimeBonus = 0;
        if ( _bGameOver == SW_TRUE )
            return ScavengerDeadlineResult::GameOver;
        if ( _daysLeft > 0 )
        {
            --_daysLeft;
            return ScavengerDeadlineResult::NotDue;
        }
        if ( isMet() == false )
        {
            _bGameOver = SW_TRUE;
            return ScavengerDeadlineResult::GameOver;
        }
        if ( _settings._overtimeDivisor > 0.0f )
            outOvertimeBonus = static_cast<int32>( static_cast<float32>( _fulfilled - _quota ) / _settings._overtimeDivisor );
        _quota += computeIncrease( _cycle, random.nextFloat() );
        ++_cycle;
        _fulfilled = 0;
        _daysLeft  = _settings._daysPerCycle;
        return ScavengerDeadlineResult::QuotaMet;
    }

    int32 ScavengerQuota::computeIncrease( int32 cycleIndex, float32 randomValue ) const
    {
        const float32 cycle    = static_cast<float32>( cycleIndex + 1 );
        const float32 curve    = 1.0f + cycle * cycle / _settings._steepness;
        const float32 noise    = 1.0f + _settings._randomness * ( randomValue - 0.5f );
        const float32 increase = _settings._increase * curve * MathUtil::max( 0.0f, noise );
        return static_cast<int32>( increase + 0.5f );
    }

    void ScavengerQuota::writeState( Archive& outArchive ) const
    {
        outArchive << _quota;
        outArchive << _fulfilled;
        outArchive << _daysLeft;
        outArchive << _cycle;
        outArchive << _bGameOver;
    }

    bool ScavengerQuota::readState( Archive& archive )
    {
        int32 quota     = 0;
        int32 fulfilled = 0;
        int32 daysLeft  = 0;
        int32 cycle     = 0;
        uint8 bGameOver = SW_FALSE;
        archive >> quota;
        archive >> fulfilled;
        archive >> daysLeft;
        archive >> cycle;
        archive >> bGameOver;
        const bool bValid = archive.isOk() && 0 <= fulfilled && 0 <= daysLeft && 0 <= cycle && bGameOver <= SW_TRUE;
        if ( bValid == false )
            return false;
        _quota     = quota;
        _fulfilled = fulfilled;
        _daysLeft  = daysLeft;
        _cycle     = cycle;
        _bGameOver = bGameOver;
        return true;
    }
} // namespace sw
