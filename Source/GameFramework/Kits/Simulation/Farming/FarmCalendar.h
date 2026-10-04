/**
 * @file FarmCalendar.h
 * @brief 농장 생활 게임의 달력 — 하루의 시각 · 날짜 · 계절 · 해입니다(하베스트 문 · 스타듀 밸리의 하루 흐름).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    /** @brief 계절입니다. 데이터(XML)는 이름 그대로 적습니다(대소문자 무시). */
    enum class FarmSeason : uint8
    {
        Spring = 0,
        Summer,
        Fall,
        Winter
    };

    /** @brief 계절 수입니다. */
    inline constexpr int32 kFarmSeasonCount = 4;

    /** @brief 계절 하나의 비트입니다(작물이 자라는 계절 마스크). */
    constexpr uint8 makeFarmSeasonBit( FarmSeason season )
    {
        return static_cast<uint8>( 1u << static_cast<uint32>( season ) );
    }

    /** @brief 계절 이름을 읽습니다. 모르는 이름이면 false 이고 @p outSeason 은 그대로입니다. */
    [[nodiscard]] SW_GF_API bool parseFarmSeason( string_view text, FarmSeason& outSeason );
    /** @brief 계절 이름입니다. */
    SW_GF_API const utf8* toString( FarmSeason season );

    /**
     * @class FarmCalendar
     * @brief 6:00 에 시작해 다음 날 2:00(26:00)에 끝나는 하루, 계절마다 28 일, 네 계절이 한 해입니다.
     * @details 시각은 분(0..1440+) 실수로 듭니다 — 게임은 실제 1 초에 게임 몇 분을 흘릴지 정해 `advanceMinutes` 를 부릅니다. 하루 끝(26:00)에 닿으면
     *          더 흐르지 않고 true 를 돌려줍니다(쓰러짐 — 게임이 잠재운다). 잠들면(`startNextDay`) 다음 날 6:00 입니다.
     */
    class SW_GF_API FarmCalendar
    {
    public:
        static constexpr int32   kDaysPerSeason  = 28;
        static constexpr float32 kDayStartMinute = 6.0f * 60.0f;
        static constexpr float32 kDayEndMinute   = 26.0f * 60.0f;

        FarmCalendar();

        /** @brief 1 년 봄 1 일 6:00 으로 되돌립니다. */
        void reset();
        /** @brief 날짜를 정하고 그날 6:00 으로 둡니다(세이브 불러오기). 범위 밖이면 잘라 넣습니다. */
        void setDate( int32 year, FarmSeason season, int32 day );
        /**
         * @brief 시각을 @p minutes 만큼 흘립니다. 하루 끝에 닿았으면(또는 이미 끝이었으면) true 입니다 — 시각은 끝에서 멈춥니다.
         */
        bool advanceMinutes( float32 minutes );
        /** @brief 다음 날 6:00 으로 넘깁니다. 계절 · 해가 넘어가면 같이 넘깁니다. 계절이 바뀌었으면 true 입니다. */
        bool startNextDay();

        int32      getYear() const { return _year; }
        FarmSeason getSeason() const { return _season; }
        int32      getDay() const { return _day; }
        float32    getMinuteOfDay() const { return _minuteOfDay; }
        /** @brief 시(0..26)입니다. 26 은 다음 날 2 시입니다. */
        int32 getHour() const { return static_cast<int32>( _minuteOfDay ) / 60; }
        /** @brief 분(0..59)입니다. */
        int32 getMinute() const { return static_cast<int32>( _minuteOfDay ) % 60; }
        /** @brief 하루 끝(26:00)에 닿았으면 true 입니다. */
        bool isDayOver() const { return _minuteOfDay >= kDayEndMinute; }
        /** @brief 1 년 봄 1 일부터 지난 날 수(0 부터)입니다. */
        int32 getElapsedDays() const;

        /** @brief 날짜 · 시각을 씁니다(핫 리로드 · 세이브). */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌거나 범위 밖이면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        float32    _minuteOfDay;
        int32      _year;
        int32      _day;
        FarmSeason _season;
    };
} // namespace sw
