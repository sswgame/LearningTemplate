#include "pch.h"

#include "Engine/Localization/TimeZoneInfo.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/Format/XMLSerializer.h"

namespace sw
{
    namespace
    {
        struct TimeZoneInfoInternal
        {
            static constexpr int32 kDaylightShiftMinutes = 60;

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
                const int64 standardSeconds = static_cast<int64>( zone._standardOffsetMinutes ) * constant::time::kSecondsPerMinute;
                int32       year{ 0 };
                int32       month{ 0 };
                int32       day{ 0 };
                computeCivilFromDays( floorDivide( unixSeconds + standardSeconds, constant::time::kSecondsPerDay ), year, month, day );
                int64 startSeconds{ 0 };
                int64 endSeconds{ 0 };
                if ( zone._daylightRule == DaylightRule::UnitedStates )
                {
                    const int64 daylightSeconds = standardSeconds + kDaylightShiftMinutes * constant::time::kSecondsPerMinute;
                    startSeconds                = findNthSunday( year, 3, 2 ) * constant::time::kSecondsPerDay + 2 * constant::time::kSecondsPerHour - standardSeconds;
                    endSeconds                  = findNthSunday( year, 11, 1 ) * constant::time::kSecondsPerDay + 2 * constant::time::kSecondsPerHour - daylightSeconds;
                }
                else
                {
                    startSeconds = findLastSunday( year, 3 ) * constant::time::kSecondsPerDay + constant::time::kSecondsPerHour;
                    endSeconds   = findLastSunday( year, 10 ) * constant::time::kSecondsPerDay + constant::time::kSecondsPerHour;
                }
                return startSeconds <= unixSeconds && unixSeconds < endSeconds;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool TimeZoneTable::loadFromAsset( const AssetID& assetID )
    {
        *this = TimeZoneTable{};
        if ( XMLSerializer::loadFile( assetID, this, *StaticType() ) == false )
        {
            SW_LOG_ERROR_ID( "TimeZoneTable.CannotRead", "[Localization] Time zone table could not be read or holds unknown keys: %#", assetID.view() );
            return false;
        }
        if ( validate() == false )
        {
            *this = TimeZoneTable{};
            return false;
        }
        return true;
    }

    bool TimeZoneTable::validate() const
    {
        bool bValid = true;
        for ( size_t index = 0; index < _listZone.size(); ++index )
        {
            const string& name = _listZone[index]._name;
            if ( name.empty() )
            {
                SW_LOG_ERROR_ID( "TimeZoneTable.EmptyName", "[Localization] Time zone #%# has an empty name", index );
                bValid = false;
            }
            else if ( findZone( name ) != &_listZone[index] )
            {
                SW_LOG_ERROR_ID( "TimeZoneTable.DuplicateName", "[Localization] Time zone '%#' is listed twice", name.c_str() );
                bValid = false;
            }
        }
        if ( findZone( "UTC" ) == nullptr )
        {
            SW_LOG_ERROR_ID( "TimeZoneTable.MissingUTC", "[Localization] Time zone table has no 'UTC' row" );
            bValid = false;
        }
        return bValid;
    }

    const TimeZoneInfo* TimeZoneTable::findZone( string_view zoneName ) const
    {
        return VectorUtil::findBy<&TimeZoneInfo::_name>( _listZone, zoneName );
    }

    int32 TimeZoneInfo::computeOffsetMinutes( int64 unixSeconds, const TimeZoneInfo& zone )
    {
        const int32 daylightMinutes = TimeZoneInfoInternal::isDaylight( unixSeconds, zone ) ? TimeZoneInfoInternal::kDaylightShiftMinutes : 0;
        return zone._standardOffsetMinutes + daylightMinutes;
    }

    TextDateTime TimeZoneInfo::convertUnixMilliseconds( int64 unixMilliseconds, const TimeZoneInfo& zone )
    {
        const int64 unixSeconds   = TimeZoneInfoInternal::floorDivide( unixMilliseconds, constant::time::kMillisecondsPerSecond );
        const int32 offsetMinutes = computeOffsetMinutes( unixSeconds, zone );
        const int64 localSeconds  = unixSeconds + static_cast<int64>( offsetMinutes ) * constant::time::kSecondsPerMinute;
        const int64 days          = TimeZoneInfoInternal::floorDivide( localSeconds, constant::time::kSecondsPerDay );
        const int64 secondOfDay   = localSeconds - days * constant::time::kSecondsPerDay;

        int32 year{ 0 };
        int32 month{ 0 };
        int32 day{ 0 };
        TimeZoneInfoInternal::computeCivilFromDays( days, year, month, day );

        TextDateTime value;
        value._year             = year;
        value._month            = static_cast<uint8>( month );
        value._day              = static_cast<uint8>( day );
        value._hour             = static_cast<uint8>( secondOfDay / constant::time::kSecondsPerHour );
        value._minute           = static_cast<uint8>( secondOfDay % constant::time::kSecondsPerHour / constant::time::kSecondsPerMinute );
        value._second           = static_cast<uint8>( secondOfDay % constant::time::kSecondsPerMinute );
        value._utcOffsetMinutes = static_cast<int16>( offsetMinutes );
        value._bZoned           = true;
        return value;
    }

    int64 TimeZoneInfo::computeUnixSeconds( const TextDateTime& value )
    {
        const int64 days = TimeZoneInfoInternal::computeDaysFromCivil( value._year, value._month, value._day );
        return days * constant::time::kSecondsPerDay + value._hour * constant::time::kSecondsPerHour +
               value._minute * constant::time::kSecondsPerMinute + value._second;
    }

    string TimeZoneInfo::formatOffset( int32 offsetMinutes, bool bPadded )
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
