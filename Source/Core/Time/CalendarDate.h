/**
 * @file CalendarDate.h
 * @brief 그레고리력 날짜와 유닉스 일 수(1970-01-01 이 0)의 변환입니다. 시간대 · 시계를 모르는 순수 계산이다.
 * @details Howard Hinnant 의 days_from_civil · civil_from_days(C++20 `std::chrono::year_month_day` 와 같은 식). 시간대 표 · 스토어 시각 · HTTP 날짜가 같은 식을 각자 적던 것을 모았다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct CalendarDate @brief 그레고리력 날짜입니다(월 · 일은 1 부터). */
    struct CalendarDate
    {
        int64 _year{ 1970 };
        int32 _month{ 1 };
        int32 _day{ 1 };

        /** @brief 내림 나눗셈입니다(C++ 의 `/` 는 0 쪽으로 자른다) — 1970 앞의 초 · 일을 가를 때 쓴다. */
        static constexpr int64 floorDivide( int64 value, int64 divisor )
        {
            const int64 quotient = value / divisor;
            return ( value % divisor != 0 && ( value < 0 ) != ( divisor < 0 ) ) ? quotient - 1 : quotient;
        }

        /** @brief 1970-01-01 부터 그 날짜까지의 일 수입니다. 그 달에 없는 날(02-31)은 다음 달로 넘어간 날짜의 일 수다(`isValid` 로 먼저 본다). */
        constexpr int64 toUnixDays() const
        {
            const int64 year         = _year - ( _month <= kMonthBeforeMarch ? 1 : 0 );
            const int64 era          = floorDivide( year, kYearPerEra );
            const int64 yearOfEra    = year - era * kYearPerEra;
            const int64 monthShifted = _month > kMonthBeforeMarch ? _month - kMarch : _month + kMonthPerYear - kMarch;
            const int64 dayOfYear    = ( kDaySlope * monthShifted + kDayBias ) / kDayDivisor + _day - 1;
            const int64 dayOfEra     = yearOfEra * kDayPerYear + yearOfEra / kFourYear - yearOfEra / kHundredYear + dayOfYear;
            return era * kDayPerEra + dayOfEra - kDayToUnixEpoch;
        }

        /** @brief 유닉스 일 수의 날짜입니다. */
        static constexpr CalendarDate fromUnixDays( int64 unixDays )
        {
            const int64  shifted      = unixDays + kDayToUnixEpoch;
            const int64  era          = floorDivide( shifted, kDayPerEra );
            const int64  dayOfEra     = shifted - era * kDayPerEra;
            const int64  yearOfEra    = ( dayOfEra - dayOfEra / kDayPerFourYear + dayOfEra / kDayPerHundredYear - dayOfEra / ( kDayPerEra - 1 ) ) / kDayPerYear;
            const int64  dayOfYear    = dayOfEra - ( kDayPerYear * yearOfEra + yearOfEra / kFourYear - yearOfEra / kHundredYear );
            const int64  monthShifted = ( kDayDivisor * dayOfYear + kDayBias ) / kDaySlope;
            CalendarDate date{};
            date._day   = static_cast<int32>( dayOfYear - ( kDaySlope * monthShifted + kDayBias ) / kDayDivisor + 1 );
            date._month = static_cast<int32>( monthShifted + kMarch <= kMonthPerYear ? monthShifted + kMarch : monthShifted + kMarch - kMonthPerYear );
            date._year  = yearOfEra + era * kYearPerEra + ( date._month <= kMonthBeforeMarch ? 1 : 0 );
            return date;
        }

        /** @brief 그 달에 있는 날짜인지입니다. */
        constexpr bool isValid() const
        {
            if ( _month < 1 || _month > kMonthPerYear || _day < 1 || _day > kMaxDay )
                return false;
            const CalendarDate echo = fromUnixDays( toUnixDays() );
            return echo._month == _month && echo._day == _day;
        }

    private:
        // 해를 3 월부터 세면 2 월(윤일)이 끝에 온다. 한 주기는 400 년이고, 3 월부터 센 달의 첫날은 (153 · 달 + 2) / 5 다.
        static constexpr int64 kYearPerEra        = 400;
        static constexpr int64 kHundredYear       = 100;
        static constexpr int64 kFourYear          = 4;
        static constexpr int64 kDayPerYear        = 365;
        static constexpr int64 kDayPerFourYear    = 1460;   ///< 윤일을 뺀 4 년의 일 수
        static constexpr int64 kDayPerHundredYear = 36524;  ///< 윤일을 하나 뺀 100 년의 일 수
        static constexpr int64 kDayPerEra         = 146097; ///< 400 년의 일 수
        static constexpr int64 kDayToUnixEpoch    = 719468; ///< 0000-03-01 부터 1970-01-01 까지
        static constexpr int64 kDaySlope          = 153;
        static constexpr int64 kDayBias           = 2;
        static constexpr int64 kDayDivisor        = 5;
        static constexpr int32 kMonthPerYear      = 12;
        static constexpr int32 kMonthBeforeMarch  = 2;
        static constexpr int32 kMarch             = 3;
        static constexpr int32 kMaxDay            = 31;
    };
} // namespace sw
