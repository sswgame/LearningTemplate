#include "pch.h"

#include "GameFramework/Kits/Farming/FarmCalendar.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    namespace
    {
        struct FarmCalendarInternal
        {
            static constexpr const utf8* kArrSeasonName[kFarmSeasonCount] = { "Spring", "Summer", "Fall", "Winter" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool parseFarmSeason( string_view text, FarmSeason& outSeason )
    {
        for ( int32 seasonIndex = 0; seasonIndex < kFarmSeasonCount; ++seasonIndex )
        {
            if ( StringUtil::equals( text, string_view( FarmCalendarInternal::kArrSeasonName[seasonIndex] ), true ) )
            {
                outSeason = static_cast<FarmSeason>( seasonIndex );
                return true;
            }
        }
        return false;
    }

    const utf8* toString( FarmSeason season )
    {
        const int32 seasonIndex = static_cast<int32>( season );
        return 0 <= seasonIndex && seasonIndex < kFarmSeasonCount ? FarmCalendarInternal::kArrSeasonName[seasonIndex] : "Unknown";
    }

    FarmCalendar::FarmCalendar()
        : _minuteOfDay{ kDayStartMinute }
        , _year{ 1 }
        , _day{ 1 }
        , _season{ FarmSeason::Spring }
    {
    }

    void FarmCalendar::reset()
    {
        setDate( 1, FarmSeason::Spring, 1 );
    }

    void FarmCalendar::setDate( int32 year, FarmSeason season, int32 day )
    {
        _year        = MathUtil::max( 1, year );
        _season      = season;
        _day         = MathUtil::clamp( day, 1, kDaysPerSeason );
        _minuteOfDay = kDayStartMinute;
    }

    bool FarmCalendar::advanceMinutes( float32 minutes )
    {
        if ( minutes > 0.0f )
            _minuteOfDay = MathUtil::min( kDayEndMinute, _minuteOfDay + minutes );
        return isDayOver();
    }

    bool FarmCalendar::startNextDay()
    {
        _minuteOfDay = kDayStartMinute;
        ++_day;
        if ( _day <= kDaysPerSeason )
            return false;

        _day                   = 1;
        const int32 nextSeason = static_cast<int32>( _season ) + 1;
        if ( nextSeason >= kFarmSeasonCount )
        {
            _season = FarmSeason::Spring;
            ++_year;
        }
        else
        {
            _season = static_cast<FarmSeason>( nextSeason );
        }
        return true;
    }

    int32 FarmCalendar::getElapsedDays() const
    {
        return ( ( _year - 1 ) * kFarmSeasonCount + static_cast<int32>( _season ) ) * kDaysPerSeason + ( _day - 1 );
    }
} // namespace sw
