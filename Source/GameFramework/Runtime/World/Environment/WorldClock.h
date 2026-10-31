/**
 * @file WorldClock.h
 * @brief 게임 시계를 정의합니다.
 * @details 하루 길이, 시간 배율, 멈춤, 시와 분과 날과 계절과 해, 새벽과 낮과 해질녘과 밤, 햇빛 세기, 잠자기(그 시각까지 건너뛰기)를 다룬다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/FourCcUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
} // namespace sw

namespace sw
{
    /**
     * @brief 시계 설정입니다. 시각은 0..24 시입니다.
     * @details 범위는 필드의 `PROPERTY( Min, Max )` 이고, 벗어난 값은 `WorldClock::initialize` 가 오류로 거절한다(`ConfigRangeUtil`).
     *          예전에는 하루 길이와 계절 날 수를 조용히 1 로 올렸다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_GF_API WorldClockSettings
    {
        REFLECT_BODY();
        PROPERTY()
        vector<hashed_string> _listSeason{}; ///< 비면 계절 하나("Default")다. 빈 목록은 틀린 값이 아니라 "계절을 쓰지 않는 게임" 이라는 계약이고, 계절 번호와 이름 계산이 늘 항목 하나 이상을 보게 한다
        PROPERTY( Min = 1, Units = s )
        float32 _secondsPerDay{ 1440.0f }; ///< 하루의 실제 초. 기본은 24 분이다
        PROPERTY( Min = 0, Max = 23.99 )
        float32 _startHour{ 6.0f };
        PROPERTY( Min = 0, Max = 24 )
        float32 _dawnHour{ 5.0f };
        PROPERTY( Min = 0, Max = 24 )
        float32 _dayHour{ 7.0f };
        PROPERTY( Min = 0, Max = 24 )
        float32 _duskHour{ 18.0f };
        PROPERTY( Min = 0, Max = 24 )
        float32 _nightHour{ 20.0f };
        PROPERTY( Min = 1 )
        int32 _daysPerSeason{ 28 };
    };
} // namespace sw

namespace sw
{
    /** @brief 하루의 때(새벽, 낮, 해질녘, 밤)입니다. */
    ENUM()
    enum class DayPhase : uint8
    {
        Night = 0,
        Dawn,
        Day,
        Dusk
    };

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
} // namespace sw

namespace sw
{
    /**
     * @class WorldClock
     * @brief 오픈월드, 농장, 생활 장르의 시계입니다. 시간은 `update` 로만 흐르며, 한 번에 많이 흘러도 넘은 시와 날의 경계를 모두 알립니다. 잠과 빨리 감기가 그 경우입니다.
     */
    class SW_GF_API WorldClock
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CLOK" );
        static constexpr uint32 kStateVersion = 1;

        WorldClock();

        /** @brief 설정을 받고 시작 시각으로 옮깁니다. 설정이 선언된 범위 밖이면 오류를 남기고 false 를 반환하며, 시계는 그대로입니다. */
        [[nodiscard]] bool initialize( const WorldClockSettings& settings );
        void               update( float32 deltaTime );
        void               setPaused( bool bPaused ) { _bPaused = bPaused ? SW_TRUE : SW_FALSE; }
        /** @brief 배속이 음수이면 오류를 남기고 false 입니다(조용히 0 으로 맞추지 않습니다). */
        [[nodiscard]] bool setTimeScale( float32 timeScale );
        /** @brief 그 날 그 시각으로 옮깁니다. 알림은 없으며 로드에 씁니다. */
        /** @details 날이 음수이거나 시각이 [0, 24) 밖이면 오류를 남기고 false 입니다(맞추지 않습니다). */
        [[nodiscard]] bool setTime( int32 day, float32 hour );
        /** @brief 다음 @p hour 시까지 건너뜁니다. 잠에 씁니다. 지나간 경계는 알립니다. */
        void advanceToHour( float32 hour );
        /** @brief 게임 시간 @p gameMinutes 분을 바로 흘립니다(멈춤 · 배율과 무관). 빨리 감기와 자동화 단계 `AdvanceGameTime` 에 씁니다. 지나간 경계는 알립니다. */
        void advanceMinutes( float32 gameMinutes );

        float32       getHour() const;
        int32         getHourInt() const { return static_cast<int32>( getHour() ); }
        int32         getMinute() const;
        int32         getDay() const { return _day; }
        int32         getDayOfSeason() const;
        int32         getSeasonIndex() const;
        hashed_string getSeasonName() const;
        int32         getYear() const;
        DayPhase      getDayPhase() const;
        /** @brief 햇빛 세기를 반환합니다. 0 이 밤이고 1 이 한낮이며, 새벽과 해질녘에 부드럽게 바뀝니다. 조명과 하늘색에 씁니다. */
        float32 computeDaylight() const;
        /** @brief 해의 각도(도)를 반환합니다. 6 시가 0°, 12 시가 90°, 18 시가 180° 입니다. */
        float32                   computeSunAngle() const;
        float32                   getTimeScale() const { return _timeScale; }
        const WorldClockSettings& getSettings() const { return _settings; }
        bool                      isPaused() const { return _bPaused != SW_FALSE; }
        void                      drainEvents( vector<WorldClockEvent>& outListEvent );
        /** @brief 날, 하루 안의 초, 배율, 멈춤을 씁니다. 설정(하루 길이와 계절)은 `initialize` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 상태를 바꿉니다. 알림은 없습니다. 날이 음수이거나 시각이 지금 설정의 하루 밖이면 false 를 반환하고 상태는 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        /** @brief @p settings 의 시계에서 @p hour 시의 때를 반환합니다. 시계가 그 시각에 있을 때의 `getDayPhase` 와 같으며, 일정이 미래 시각을 판정할 때 씁니다. */
        static DayPhase computePhaseAt( const WorldClockSettings& settings, float32 hour );

    private:
        DayPhase computePhase( float32 hour ) const { return computePhaseAt( _settings, hour ); }
        void     advanceGameSeconds( float32 gameSeconds );

        WorldClockSettings           _settings;
        EventBuffer<WorldClockEvent> _eventBuffer;
        float32                      _secondOfDay; ///< 0.._secondsPerDay
        float32                      _timeScale;
        int32                        _day;
        uint8                        _bPaused;
    };
} // namespace sw
