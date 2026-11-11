#include "pch.h"

#include "GameFramework/Runtime/World/Environment/WorldClock.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        float32 smoothStep( float32 edge0, float32 edge1, float32 value )
        {
            if ( edge1 <= edge0 )
                return value >= edge1 ? 1.0f : 0.0f;
            return MathUtil::smoothstep( edge0, edge1, value );
        }
    } // namespace

    WorldClock::WorldClock()
        : _settings{}
        , _eventBuffer{}
        , _secondOfDay{ 0.0 }
        , _timeScale{ 1.0f }
        , _day{ 0 }
        , _bPaused{ SW_FALSE }
    {
    }

    bool WorldClock::initialize( const WorldClockSettings& settings )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "WorldClock.WorldClockRefusedSettings", "WorldClock: refused settings - %#", rangeError.c_str() );
            return false;
        }
        _settings = settings;
        if ( _settings._listSeason.empty() )
            _settings._listSeason.push_back( hashed_string( "Default" ) );
        _eventBuffer.clear();
        _timeScale = 1.0f;
        _bPaused   = SW_FALSE;
        if ( setTime( 0, _settings._startHour ) == false )
            return false;
        return true;
    }

    bool WorldClock::setTimeScale( float32 timeScale )
    {
        if ( timeScale < 0.0f )
        {
            SW_LOG_ERROR_ID( "WorldClock.WorldClockRefusedTimeScale", "WorldClock: refused time scale %# - it must not be negative", timeScale );
            return false;
        }
        _timeScale = timeScale;
        return true;
    }

    bool WorldClock::setTime( int32 day, float32 hour )
    {
        const bool bInvalid = day < 0 || hour < 0.0f || hour >= 24.0f;
        if ( bInvalid )
        {
            SW_LOG_ERROR_ID( "WorldClock.WorldClockRefusedTimeDay", "WorldClock: refused time day %# hour %# - day must not be negative and hour must be within [0, 24)", day, hour );
            return false;
        }
        _day         = day;
        _secondOfDay = static_cast<float64>( hour ) / 24.0 * _settings._secondsPerDay;
        return true;
    }

    float32 WorldClock::getHour() const
    {
        return static_cast<float32>( _secondOfDay / _settings._secondsPerDay * 24.0 );
    }

    int32 WorldClock::getMinute() const
    {
        const float32 hour = getHour();
        return static_cast<int32>( ( hour - static_cast<float32>( static_cast<int32>( hour ) ) ) * 60.0f );
    }

    int32 WorldClock::getDayOfSeason() const
    {
        return _day % _settings._daysPerSeason + 1;
    }

    int32 WorldClock::getSeasonIndex() const
    {
        return ( _day / _settings._daysPerSeason ) % static_cast<int32>( _settings._listSeason.size() );
    }

    hashed_string WorldClock::getSeasonName() const
    {
        return _settings._listSeason[static_cast<size_t>( getSeasonIndex() )];
    }

    int32 WorldClock::getYear() const
    {
        return _day / ( _settings._daysPerSeason * static_cast<int32>( _settings._listSeason.size() ) ) + 1;
    }

    DayPhase WorldClock::computePhaseAt( const WorldClockSettings& settings, float32 hour )
    {
        if ( hour >= settings._nightHour || hour < settings._dawnHour )
            return DayPhase::Night;
        if ( hour < settings._dayHour )
            return DayPhase::Dawn;
        if ( hour < settings._duskHour )
            return DayPhase::Day;
        return DayPhase::Dusk;
    }

    DayPhase WorldClock::getDayPhase() const
    {
        return computePhase( getHour() );
    }

    float32 WorldClock::computeDaylight() const
    {
        const float32 hour = getHour();
        return smoothStep( _settings._dawnHour, _settings._dayHour, hour ) *
               ( 1.0f - smoothStep( _settings._duskHour, _settings._nightHour, hour ) );
    }

    float32 WorldClock::computeSunAngle() const
    {
        return ( getHour() - 6.0f ) * 15.0f;
    }

    void WorldClock::update( float32 deltaTime )
    {
        if ( _bPaused || deltaTime <= 0.0f )
            return;
        advanceGameSeconds( deltaTime * _timeScale );
    }

    void WorldClock::advanceToHour( float32 hour )
    {
        const float32 target = MathUtil::clamp( hour, 0.0f, 23.9999f ) / 24.0f * _settings._secondsPerDay;
        float32       gap    = static_cast<float32>( target - _secondOfDay );
        if ( gap <= 0.0f )
            gap += _settings._secondsPerDay;
        advanceGameSeconds( gap );
    }

    void WorldClock::advanceMinutes( float32 gameMinutes )
    {
        constexpr float32 kGameMinutesPerDay = static_cast<float32>( constant::time::kSecondsPerDay / constant::time::kSecondsPerMinute );
        if ( gameMinutes > 0.0f )
            advanceGameSeconds( gameMinutes / kGameMinutesPerDay * _settings._secondsPerDay );
    }

    void WorldClock::advanceGameSeconds( float32 gameSeconds )
    {
        constexpr int32   kHoursPerDay   = 24;
        constexpr float64 kHourEpsilon   = 1.0e-9; // 경계에 올려놓은 시각을 나눈 몫이 N 바로 아래로 반올림되지 않게
        const float64     secondsPerHour = static_cast<float64>( _settings._secondsPerDay ) / kHoursPerDay;
        // 시 경계마다 끊어 가며 넘긴다(하루를 건너뛰어도 모든 경계를 알린다). 지금 시를 **정수로** 들고 경계를 넘을 때마다 하나 올린다 — 예전에는 매 걸음
        // `getHour()` 를 다시 구해, 경계에 올려놓은 시각이 N 바로 아래로 반올림되면 다음 경계까지가 0 이 되어 걸음이 1e-4 로 고정됐고 하루 길이에 따라
        // (3500 · 5000 · 7000 · 10000 …) 2048 초를 넘은 시각에서는 그 걸음이 float 에 더해지지 않아 시계가 영구히 멈췄다(감사 G-2). 매 반복은 남은 시간을
        // 다 쓰거나 시를 하나 올리므로 반드시 끝난다(언리얼 FTimespan 의 정수 틱 자리).
        int32   hour = MathUtil::clamp( static_cast<int32>( _secondOfDay / secondsPerHour + kHourEpsilon ), 0, kHoursPerDay - 1 );
        float64 left = gameSeconds;
        while ( left > 0.0 )
        {
            const DayPhase phaseBefore = computePhase( getHour() );
            const float64  boundary    = static_cast<float64>( hour + 1 ) * secondsPerHour;
            const float64  toNextHour  = boundary - _secondOfDay;
            if ( left < toNextHour - 1.0e-6 )
            {
                _secondOfDay += left; // 이 시 안에서 끝난다
                left = 0.0;
            }
            else
            {
                left         = MathUtil::max( 0.0, left - MathUtil::max( toNextHour, 0.0 ) );
                _secondOfDay = boundary;
                ++hour;
                if ( hour >= kHoursPerDay )
                {
                    hour                     = 0;
                    _secondOfDay             = 0.0;
                    const int32 seasonBefore = getSeasonIndex();
                    const int32 yearBefore   = getYear();
                    ++_day;
                    _eventBuffer.push( WorldClockEvent{ _day, WorldClockEvent::Kind::DayChanged } );
                    if ( getSeasonIndex() != seasonBefore )
                        _eventBuffer.push( WorldClockEvent{ getSeasonIndex(), WorldClockEvent::Kind::SeasonChanged } );
                    if ( getYear() != yearBefore )
                        _eventBuffer.push( WorldClockEvent{ getYear(), WorldClockEvent::Kind::YearChanged } );
                }
                _eventBuffer.push( WorldClockEvent{ hour, WorldClockEvent::Kind::HourChanged } );
            }
            const DayPhase phaseAfter = computePhase( getHour() );
            if ( phaseAfter != phaseBefore )
                _eventBuffer.push( WorldClockEvent{ static_cast<int32>( phaseAfter ), WorldClockEvent::Kind::PhaseChanged } );
        }
    }

    void WorldClock::pollEvents( vector<WorldClockEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void WorldClock::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion ); // 판 1(float32 시각 · 머리표 없음)은 거절한다
        io.value( _day );
        io.value( _secondOfDay );
        io.value( _timeScale );
        io.flag( _bPaused );
        // NaN 은 비교가 모두 거짓이라 여기서 걸린다.
        io.check( 0 <= _day && 0.0 <= _secondOfDay && _secondOfDay < _settings._secondsPerDay && 0.0f <= _timeScale );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
        }
    }

} // namespace sw
