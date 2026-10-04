#include "pch.h"

#include "GameFramework/World/WorldClock.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct WorldClockInternal
        {
            static float32 smoothStep( float32 edge0, float32 edge1, float32 value )
            {
                if ( edge1 <= edge0 )
                    return value >= edge1 ? 1.0f : 0.0f;
                const float32 ratio = MathUtil::saturate( ( value - edge0 ) / ( edge1 - edge0 ) );
                return ratio * ratio * ( 3.0f - 2.0f * ratio );
            }
        };
    } // namespace

    const utf8* toString( DayPhase phase )
    {
        switch ( phase )
        {
            case DayPhase::Night:
                return "Night";
            case DayPhase::Dawn:
                return "Dawn";
            case DayPhase::Day:
                return "Day";
            case DayPhase::Dusk:
                return "Dusk";
        }
        return "Unknown";
    }

    WorldClock::WorldClock()
        : _settings{}
        , _listEvent{}
        , _secondOfDay{ 0.0f }
        , _timeScale{ 1.0f }
        , _day{ 0 }
        , _bPaused{ SW_FALSE }
    {
    }

    void WorldClock::initialize( const WorldClockSettings& settings )
    {
        _settings                = settings;
        _settings._secondsPerDay = MathUtil::max( 1.0f, _settings._secondsPerDay );
        _settings._daysPerSeason = MathUtil::max( 1, _settings._daysPerSeason );
        if ( _settings._listSeason.empty() )
            _settings._listSeason.push_back( hashed_string( "Default" ) );
        _listEvent.clear();
        _timeScale = 1.0f;
        _bPaused   = SW_FALSE;
        setTime( 0, _settings._startHour );
    }

    void WorldClock::setTimeScale( float32 timeScale ) { _timeScale = MathUtil::max( 0.0f, timeScale ); }

    void WorldClock::setTime( int32 day, float32 hour )
    {
        _day         = MathUtil::max( 0, day );
        _secondOfDay = MathUtil::clamp( hour, 0.0f, 23.9999f ) / 24.0f * _settings._secondsPerDay;
    }

    float32 WorldClock::getHour() const { return _secondOfDay / _settings._secondsPerDay * 24.0f; }

    int32 WorldClock::getMinute() const
    {
        const float32 hour = getHour();
        return static_cast<int32>( ( hour - static_cast<float32>( static_cast<int32>( hour ) ) ) * 60.0f );
    }

    int32 WorldClock::getDayOfSeason() const { return _day % _settings._daysPerSeason + 1; }

    int32 WorldClock::getSeasonIndex() const
    {
        return ( _day / _settings._daysPerSeason ) % static_cast<int32>( _settings._listSeason.size() );
    }

    hashed_string WorldClock::getSeasonName() const { return _settings._listSeason[static_cast<size_t>( getSeasonIndex() )]; }

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

    DayPhase WorldClock::getDayPhase() const { return computePhase( getHour() ); }

    float32 WorldClock::computeDaylight() const
    {
        const float32 hour = getHour();
        return WorldClockInternal::smoothStep( _settings._dawnHour, _settings._dayHour, hour ) *
               ( 1.0f - WorldClockInternal::smoothStep( _settings._duskHour, _settings._nightHour, hour ) );
    }

    float32 WorldClock::computeSunAngle() const { return ( getHour() - 6.0f ) * 15.0f; }

    void WorldClock::update( float32 deltaTime )
    {
        if ( _bPaused || deltaTime <= 0.0f )
            return;
        advanceGameSeconds( deltaTime * _timeScale );
    }

    void WorldClock::advanceToHour( float32 hour )
    {
        const float32 target = MathUtil::clamp( hour, 0.0f, 23.9999f ) / 24.0f * _settings._secondsPerDay;
        float32       gap    = target - _secondOfDay;
        if ( gap <= 0.0f )
            gap += _settings._secondsPerDay;
        advanceGameSeconds( gap );
    }

    void WorldClock::advanceGameSeconds( float32 gameSeconds )
    {
        const float32 secondsPerHour = _settings._secondsPerDay / 24.0f;
        float32       left           = gameSeconds;
        // 시 경계마다 끊어 가며 넘긴다(하루를 건너뛰어도 모든 경계를 알린다).
        while ( left > 0.0f )
        {
            const float32  hourBefore    = getHour();
            const int32    hourIntBefore = static_cast<int32>( hourBefore );
            const DayPhase phaseBefore   = computePhase( hourBefore );
            const float32  toNextHour    = static_cast<float32>( hourIntBefore + 1 ) * secondsPerHour - _secondOfDay;
            float32        step          = MathUtil::min( left, MathUtil::max( toNextHour, 1.0e-4f ) );
            if ( MathUtil::abs( left - toNextHour ) < 1.0e-3f )
                step = toNextHour; // 반올림 오차로 경계 바로 앞에 멈추지 않게
            _secondOfDay += step;
            left = MathUtil::abs( left - step ) < 1.0e-3f ? 0.0f : left - step;
            if ( _secondOfDay >= _settings._secondsPerDay - 1.0e-3f )
            {
                _secondOfDay             = 0.0f;
                const int32 seasonBefore = getSeasonIndex();
                const int32 yearBefore   = getYear();
                ++_day;
                _listEvent.push_back( WorldClockEvent{ _day, WorldClockEvent::Kind::DayChanged } );
                if ( getSeasonIndex() != seasonBefore )
                    _listEvent.push_back( WorldClockEvent{ getSeasonIndex(), WorldClockEvent::Kind::SeasonChanged } );
                if ( getYear() != yearBefore )
                    _listEvent.push_back( WorldClockEvent{ getYear(), WorldClockEvent::Kind::YearChanged } );
            }
            const float32 hourAfter = getHour();
            if ( static_cast<int32>( hourAfter ) != hourIntBefore )
                _listEvent.push_back( WorldClockEvent{ static_cast<int32>( hourAfter ), WorldClockEvent::Kind::HourChanged } );
            const DayPhase phaseAfter = computePhase( hourAfter );
            if ( phaseAfter != phaseBefore )
                _listEvent.push_back( WorldClockEvent{ static_cast<int32>( phaseAfter ), WorldClockEvent::Kind::PhaseChanged } );
        }
    }

    void WorldClock::drainEvents( vector<WorldClockEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }
} // namespace sw
