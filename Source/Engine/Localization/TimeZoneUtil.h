/**
 * @file TimeZoneUtil.h
 * @brief 시간대(IANA 이름)를 UTC 오프셋으로 바꾸고 UTC 유닉스 시각을 그 시간대의 날짜 · 시각으로 바꿉니다.
 * @details tz 데이터베이스를 들이지 않는다. 지원하는 시간대는 표(`TimeZoneUtil.cpp`)에 있는 이름뿐이고, 표준 오프셋과 일광 절약 규칙 두 가지(미국 · 유럽 연합)로 계산한다.
 *          표에 없는 이름은 `findZone` 이 nullptr 이다 — 호출자가 UTC 로 쓸지 오류로 볼지 정한다. 과거 규칙 변경(2007 년 이전 미국 등)은 반영하지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Localization/CultureInfo.h"

namespace sw
{
    /** @brief 일광 절약 시간 규칙입니다. */
    enum class DaylightRule : uint8
    {
        None = 0,
        UnitedStates, ///< 3 월 둘째 일요일 현지 02:00 ~ 11 월 첫째 일요일 현지 02:00
        Europe        ///< 3 월 마지막 일요일 01:00 UTC ~ 10 월 마지막 일요일 01:00 UTC
    };

    /** @brief 시간대 한 줄입니다. */
    struct TimeZoneInfo
    {
        const utf8*  _pName;                 ///< IANA 이름(`Asia/Seoul`)
        int16        _standardOffsetMinutes; ///< UTC 와의 차이(표준시)
        DaylightRule _daylightRule;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TimeZoneUtil
     * @brief 시간대 조회와 UTC ↔ 현지 날짜 변환입니다. 상태가 없습니다.
     */
    struct SW_API TimeZoneUtil
    {
        /** @brief 이름의 시간대입니다(대소문자 구분, 없으면 nullptr). `UTC` 는 항상 있습니다. */
        static const TimeZoneInfo* findZone( string_view zoneName );
        /** @brief UTC 유닉스 시각(초)에서 그 시간대의 UTC 오프셋(분)입니다. 일광 절약 시간 중이면 표준 오프셋에 60 분이 더해집니다. */
        static int32 computeOffsetMinutes( int64 unixSeconds, const TimeZoneInfo& zone );
        /** @brief UTC 유닉스 시각(밀리초)을 그 시간대의 날짜 · 시각으로 바꿉니다. `_bZoned` 와 `_utcOffsetMinutes` 가 채워집니다. */
        static TextDateTime convertUnixMilliseconds( int64 unixMilliseconds, const TimeZoneInfo& zone );
        /** @brief 날짜 · 시각(오프셋은 무시)을 UTC 로 보고 유닉스 시각(초)으로 바꿉니다. */
        static int64 computeUnixSeconds( const TextDateTime& value );
        /** @brief `UTC`, `UTC+9`, `UTC+5:30` 모양의 오프셋 글입니다. @p bPadded 면 `UTC+09:00` 입니다. */
        static string formatOffset( int32 offsetMinutes, bool bPadded );
    };
} // namespace sw
