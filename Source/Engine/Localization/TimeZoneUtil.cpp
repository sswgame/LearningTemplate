#include "pch.h"

#include "Engine/Localization/TimeZoneUtil.h"

namespace sw
{
    namespace
    {
        struct TimeZoneUtilInternal
        {
            static constexpr int32 kDaylightShiftMinutes = 60;

            static constexpr TimeZoneInfo kArrZone[] = {
                {                "UTC",    0,         DaylightRule::None},
                {            "Etc/UTC",    0,         DaylightRule::None},
                {         "Asia/Seoul",  540,         DaylightRule::None},
                {         "Asia/Tokyo",  540,         DaylightRule::None},
                {      "Asia/Shanghai",  480,         DaylightRule::None},
                {        "Asia/Taipei",  480,         DaylightRule::None},
                {       "Asia/Kolkata",  330,         DaylightRule::None},
                {        "Asia/Riyadh",  180,         DaylightRule::None},
                {      "Europe/London",    0,       DaylightRule::Europe},
                {      "Europe/Berlin",   60,       DaylightRule::Europe},
                {       "Europe/Paris",   60,       DaylightRule::Europe},
                {      "Europe/Madrid",   60,       DaylightRule::Europe},
                {      "Europe/Warsaw",   60,       DaylightRule::Europe},
                {      "Europe/Moscow",  180,         DaylightRule::None},
                {   "America/New_York", -300, DaylightRule::UnitedStates},
                {    "America/Chicago", -360, DaylightRule::UnitedStates},
                {     "America/Denver", -420, DaylightRule::UnitedStates},
                {"America/Los_Angeles", -480, DaylightRule::UnitedStates},
            };

            /** @brief 내림 나눗셈입니다(C++ 의 `/` 는 0 쪽으로 자른다). */
            static int64 floorDivide( int64 value, int64 divisor )
            {
                int64 quotient = value / divisor;
                if ( ( value % divisor != 0 ) && ( ( value < 0 ) != ( divisor < 0 ) ) )
                    --quotient;
                return quotient;
            }

            /** @brief 1970-01-01 부터 그 날짜까지의 일 수입니다(Howard Hinnant 의 days_from_civil). */
            static int64 computeDaysFromCivil( int64 year, int32 month, int32 day )
            {
                year -= month <= 2 ? 1 : 0;
                const int64 era          = floorDivide( year, 400 );
                const int64 yearOfEra    = year - era * 400;
                const int64 monthShifted = month + ( month > 2 ? -3 : 9 );
                const int64 dayOfYear    = ( 153 * monthShifted + 2 ) / 5 + day - 1;
                const int64 dayOfEra     = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
                return era * 146097 + dayOfEra - 719468;
            }

            /** @brief 일 수에서 날짜입니다(Howard Hinnant 의 civil_from_days). */
            static void computeCivilFromDays( int64 days, int32& outYear, int32& outMonth, int32& outDay )
            {
                const int64 shifted      = days + 719468;
                const int64 era          = floorDivide( shifted, 146097 );
                const int64 dayOfEra     = shifted - era * 146097;
                const int64 yearOfEra    = ( dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096 ) / 365;
                const int64 dayOfYear    = dayOfEra - ( 365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100 );
                const int64 monthShifted = ( 5 * dayOfYear + 2 ) / 153;
                outDay                   = static_cast<int32>( dayOfYear - ( 153 * monthShifted + 2 ) / 5 + 1 );
                outMonth                 = static_cast<int32>( monthShifted < 10 ? monthShifted + 3 : monthShifted - 9 );
                outYear                  = static_cast<int32>( yearOfEra + era * 400 + ( outMonth <= 2 ? 1 : 0 ) );
            }

            /** @brief 일 수의 요일입니다(0 이 일요일). 1970-01-01 은 목요일입니다. */
            static int32 computeWeekday( int64 days )
            {
                const int64 weekday = ( days + 4 ) % 7;
                return static_cast<int32>( weekday < 0 ? weekday + 7 : weekday );
            }

            /** @brief 그 달의 @p ordinal 번째(1 부터) 일요일의 일 수입니다. */
            static int64 findNthSunday( int32 year, int32 month, int32 ordinal )
            {
                const int64 firstOfMonth = computeDaysFromCivil( year, month, 1 );
                const int32 untilSunday  = ( 7 - computeWeekday( firstOfMonth ) ) % 7;
                return firstOfMonth + untilSunday + static_cast<int64>( ordinal - 1 ) * 7;
            }

            /** @brief 그 달 마지막 일요일의 일 수입니다(3 월과 10 월은 31 일까지 있다). */
            static int64 findLastSunday( int32 year, int32 month )
            {
                const int64 lastOfMonth = computeDaysFromCivil( year, month, 31 );
                return lastOfMonth - computeWeekday( lastOfMonth );
            }

            /** @brief 그 UTC 시각이 일광 절약 시간 안인지입니다. */
            static bool isDaylight( int64 unixSeconds, const TimeZoneInfo& zone )
            {
                if ( zone._daylightRule == DaylightRule::None )
                    return false;
                const int64 standardSeconds = static_cast<int64>( zone._standardOffsetMinutes ) * constant::kSecondsPerMinute;
                int32       year{ 0 };
                int32       month{ 0 };
                int32       day{ 0 };
                computeCivilFromDays( floorDivide( unixSeconds + standardSeconds, constant::kSecondsPerDay ), year, month, day );
                int64 startSeconds{ 0 };
                int64 endSeconds{ 0 };
                if ( zone._daylightRule == DaylightRule::UnitedStates )
                {
                    const int64 daylightSeconds = standardSeconds + kDaylightShiftMinutes * constant::kSecondsPerMinute;
                    startSeconds                = findNthSunday( year, 3, 2 ) * constant::kSecondsPerDay + 2 * constant::kSecondsPerHour - standardSeconds;
                    endSeconds                  = findNthSunday( year, 11, 1 ) * constant::kSecondsPerDay + 2 * constant::kSecondsPerHour - daylightSeconds;
                }
                else
                {
                    startSeconds = findLastSunday( year, 3 ) * constant::kSecondsPerDay + constant::kSecondsPerHour;
                    endSeconds   = findLastSunday( year, 10 ) * constant::kSecondsPerDay + constant::kSecondsPerHour;
                }
                return startSeconds <= unixSeconds && unixSeconds < endSeconds;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const TimeZoneInfo* TimeZoneUtil::findZone( string_view zoneName )
    {
        for ( const TimeZoneInfo& zone : TimeZoneUtilInternal::kArrZone )
        {
            if ( zoneName == zone._pName )
                return &zone;
        }
        return nullptr;
    }

    int32 TimeZoneUtil::computeOffsetMinutes( int64 unixSeconds, const TimeZoneInfo& zone )
    {
        const int32 daylightMinutes = TimeZoneUtilInternal::isDaylight( unixSeconds, zone ) ? TimeZoneUtilInternal::kDaylightShiftMinutes : 0;
        return zone._standardOffsetMinutes + daylightMinutes;
    }

    TextDateTime TimeZoneUtil::convertUnixMilliseconds( int64 unixMilliseconds, const TimeZoneInfo& zone )
    {
        const int64 unixSeconds   = TimeZoneUtilInternal::floorDivide( unixMilliseconds, constant::kMillisecondsPerSecond );
        const int32 offsetMinutes = computeOffsetMinutes( unixSeconds, zone );
        const int64 localSeconds  = unixSeconds + static_cast<int64>( offsetMinutes ) * constant::kSecondsPerMinute;
        const int64 days          = TimeZoneUtilInternal::floorDivide( localSeconds, constant::kSecondsPerDay );
        const int64 secondOfDay   = localSeconds - days * constant::kSecondsPerDay;

        int32 year{ 0 };
        int32 month{ 0 };
        int32 day{ 0 };
        TimeZoneUtilInternal::computeCivilFromDays( days, year, month, day );

        TextDateTime value;
        value._year             = year;
        value._month            = static_cast<uint8>( month );
        value._day              = static_cast<uint8>( day );
        value._hour             = static_cast<uint8>( secondOfDay / constant::kSecondsPerHour );
        value._minute           = static_cast<uint8>( secondOfDay % constant::kSecondsPerHour / constant::kSecondsPerMinute );
        value._second           = static_cast<uint8>( secondOfDay % constant::kSecondsPerMinute );
        value._utcOffsetMinutes = static_cast<int16>( offsetMinutes );
        value._bZoned           = true;
        return value;
    }

    int64 TimeZoneUtil::computeUnixSeconds( const TextDateTime& value )
    {
        const int64 days = TimeZoneUtilInternal::computeDaysFromCivil( value._year, value._month, value._day );
        return days * constant::kSecondsPerDay + value._hour * constant::kSecondsPerHour +
               value._minute * constant::kSecondsPerMinute + value._second;
    }

    string TimeZoneUtil::formatOffset( int32 offsetMinutes, bool bPadded )
    {
        if ( offsetMinutes == 0 )
            return "UTC";
        const int32 magnitude = offsetMinutes < 0 ? -offsetMinutes : offsetMinutes;
        const int32 hours     = magnitude / 60;
        const int32 minutes   = magnitude % 60;
        string      text      = offsetMinutes < 0 ? "UTC-" : "UTC+";
        if ( bPadded && hours < 10 )
            text.push_back( '0' );
        text.append( to_string( static_cast<int64>( hours ) ) );
        if ( bPadded || minutes != 0 )
        {
            text.push_back( ':' );
            if ( minutes < 10 )
                text.push_back( '0' );
            text.append( to_string( static_cast<int64>( minutes ) ) );
        }
        return text;
    }
} // namespace sw
