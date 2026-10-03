/**
 * @file WorldClock.h
 * @brief 게임 시계 — 하루 길이 · 시간 배율 · 멈춤, 시 · 분 · 날 · 계절 · 해, 새벽 · 낮 · 해질녘 · 밤, 햇빛 세기, 잠자기(그 시각까지 건너뛰기)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 시계 설정입니다. 시각은 0..24 시입니다. */
    struct WorldClockSettings
    {
        vector<hashed_string> _listSeason{};             ///< 비면 계절 하나("Default")
        float32               _secondsPerDay{ 1440.0f }; ///< 실제 초 — 기본은 하루 24 분
        float32               _startHour{ 6.0f };
        float32               _dawnHour{ 5.0f };
        float32               _dayHour{ 7.0f };
        float32               _duskHour{ 18.0f };
        float32               _nightHour{ 20.0f };
        int32                 _daysPerSeason{ 28 };
    };

    /** @brief 하루의 때입니다. */
    enum class DayPhase : uint8
    {
        Night = 0,
        Dawn,
        Day,
        Dusk
    };

    SW_GF_API const utf8* toString( DayPhase phase );

    /** @brief 시계가 넘은 경계입니다. */
    struct WorldClockEvent
    {
        enum class Kind : uint8
        {
            HourChanged = 0, ///< _value = 새 시
            PhaseChanged,    ///< _value = DayPhase
            DayChanged,      ///< _value = 새 날(0 부터 센 전체 날)
            SeasonChanged,   ///< _value = 계절 번호
            YearChanged      ///< _value = 해(1 부터)
        };
        int32 _value{ 0 };
        Kind  _kind{ Kind::HourChanged };
    };

    /**
     * @class WorldClock
     * @brief 오픈월드 · 농장 · 생활 장르의 시계입니다. 시간은 `update` 로만 흐르고, 한 번에 많이 흘러도 넘은 시 · 날 경계를 모두 알립니다(잠 · 빨리 감기).
     */
    class SW_GF_API WorldClock
    {
    public:
        WorldClock();

        void initialize( const WorldClockSettings& settings );
        void update( float32 deltaTime );
        void setPaused( bool bPaused ) { _bPaused = bPaused ? SW_TRUE : SW_FALSE; }
        void setTimeScale( float32 timeScale );
        /** @brief 그 날 그 시각으로 옮깁니다(알림 없음 — 로드). */
        void setTime( int32 day, float32 hour );
        /** @brief 다음 @p hour 시까지 건너뜁니다(잠). 지나간 경계는 알립니다. */
        void advanceToHour( float32 hour );

        float32       getHour() const;
        int32         getHourInt() const { return static_cast<int32>( getHour() ); }
        int32         getMinute() const;
        int32         getDay() const { return _day; }
        int32         getDayOfSeason() const;
        int32         getSeasonIndex() const;
        hashed_string getSeasonName() const;
        int32         getYear() const;
        DayPhase      getDayPhase() const;
        /** @brief 햇빛 세기(0 밤 .. 1 한낮) — 새벽 · 해질녘에 부드럽게 바뀝니다(조명 · 하늘색). */
        float32 computeDaylight() const;
        /** @brief 해의 각도(도) — 6 시 0°, 12 시 90°, 18 시 180° 입니다. */
        float32 computeSunAngle() const;
        float32 getTimeScale() const { return _timeScale; }
        bool    isPaused() const { return _bPaused != SW_FALSE; }
        void    drainEvents( vector<WorldClockEvent>& outListEvent );

    private:
        DayPhase computePhase( float32 hour ) const;
        void     advanceGameSeconds( float32 gameSeconds );

        WorldClockSettings      _settings;
        vector<WorldClockEvent> _listEvent;
        float32                 _secondOfDay; ///< 0.._secondsPerDay
        float32                 _timeScale;
        int32                   _day;
        uint8                   _bPaused;
    };
} // namespace sw
