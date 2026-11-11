#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/World/Environment/WorldClock.h"

using namespace sw;

namespace
{
    struct WorldClockTestUtil
    {
        /** @brief 하루 24 초(1 초 = 1 시간), 계절 둘, 계절마다 2 일인 시계 설정입니다. */
        static WorldClockSettings makeSettings()
        {
            WorldClockSettings settings;
            settings._listSeason    = { hashed_string( "Spring" ), hashed_string( "Summer" ) };
            settings._secondsPerDay = 24.0f;
            settings._startHour     = 0.0f;
            settings._daysPerSeason = 2;
            return settings;
        }

        static uint32 countEvents( const vector<WorldClockEvent>& listEvent, WorldClockEvent::Kind kind )
        {
            uint32 count = 0;
            for ( const WorldClockEvent& event : listEvent )
            {
                count += event._kind == kind ? 1u : 0u;
            }
            return count;
        }
    };
} // namespace

// 틀린 설정은 고쳐 쓰지 않고 거절한다.
SW_TEST_CASE( WorldClockTest, RefusesSettingsOutOfRange )
{
    SW_EXPECT_LOG( "WorldClock.WorldClockRefusedSettings" );
    WorldClockSettings settings = WorldClockTestUtil::makeSettings();
    settings._secondsPerDay     = 0.0f;
    WorldClock clock;
    SW_EXPECT_FALSE( clock.initialize( settings ) );
}

SW_TEST_CASE( WorldClockTest, RefusesBadTimeAndNegativeScale )
{
    SW_EXPECT_LOG( "WorldClock.WorldClockRefusedTimeDay" );
    SW_EXPECT_LOG( "WorldClock.WorldClockRefusedTimeScale" );
    WorldClock clock;
    SW_REQUIRE_TRUE( clock.initialize( WorldClockTestUtil::makeSettings() ) );
    SW_EXPECT_FALSE( clock.setTime( 0, 24.0f ) );
    SW_EXPECT_FALSE( clock.setTimeScale( -1.0f ) );
}

SW_TEST_CASE( WorldClockTest, EmptySeasonListBecomesOneDefaultSeason )
{
    WorldClockSettings settings = WorldClockTestUtil::makeSettings();
    settings._listSeason.clear();
    WorldClock clock;
    SW_REQUIRE_TRUE( clock.initialize( settings ) );
    SW_EXPECT_TRUE( clock.getSeasonName() == hashed_string( "Default" ) );
}

// 계절 날 수를 넘기면 다음 계절로, 계절을 다 돌면 다음 해로 넘어간다.
SW_TEST_CASE( WorldClockTest, DaysRollIntoSeasonsAndYears )
{
    WorldClock clock;
    SW_REQUIRE_TRUE( clock.initialize( WorldClockTestUtil::makeSettings() ) );
    SW_EXPECT_EQUAL( 0, clock.getSeasonIndex() );
    SW_REQUIRE_TRUE( clock.setTime( 2, 1.0f ) );
    SW_EXPECT_EQUAL( 1, clock.getSeasonIndex() );
    SW_EXPECT_TRUE( clock.getSeasonName() == hashed_string( "Summer" ) );
    const int32 firstYear = clock.getYear();
    SW_REQUIRE_TRUE( clock.setTime( 4, 1.0f ) );
    SW_EXPECT_EQUAL( 0, clock.getSeasonIndex() );
    SW_EXPECT_EQUAL( firstYear + 1, clock.getYear() );
}

// 시간을 흘리면 넘은 경계마다 사건이 나온다.
SW_TEST_CASE( WorldClockTest, CrossingADayRaisesItsEvents )
{
    WorldClock clock;
    SW_REQUIRE_TRUE( clock.initialize( WorldClockTestUtil::makeSettings() ) );
    vector<WorldClockEvent> listEvent;
    clock.pollEvents( listEvent );
    listEvent.clear();
    clock.update( 25.0f ); // 하루 하고 한 시간
    clock.pollEvents( listEvent );
    SW_EXPECT_EQUAL( 1u, WorldClockTestUtil::countEvents( listEvent, WorldClockEvent::Kind::DayChanged ) );
    SW_EXPECT_TRUE( WorldClockTestUtil::countEvents( listEvent, WorldClockEvent::Kind::HourChanged ) >= 1u );
    SW_EXPECT_NEAR( 1.0f, clock.getHour(), 0.01f );
}

// 멈춘 배율에서는 흐르지 않고, 배율만큼 빨리 흐른다.
SW_TEST_CASE( WorldClockTest, TimeScaleControlsTheFlow )
{
    WorldClock clock;
    SW_REQUIRE_TRUE( clock.initialize( WorldClockTestUtil::makeSettings() ) );
    SW_REQUIRE_TRUE( clock.setTimeScale( 0.0f ) );
    clock.update( 5.0f );
    SW_EXPECT_NEAR( 0.0f, clock.getHour(), 0.001f );
    SW_REQUIRE_TRUE( clock.setTimeScale( 2.0f ) );
    clock.update( 1.0f );
    SW_EXPECT_NEAR( 2.0f, clock.getHour(), 0.001f );
}

// 지난 시각으로 건너뛰면 다음 날의 그 시각이다(되돌아가지 않는다).
SW_TEST_CASE( WorldClockTest, AdvanceToAPastHourGoesToTheNextDay )
{
    WorldClock clock;
    SW_REQUIRE_TRUE( clock.initialize( WorldClockTestUtil::makeSettings() ) );
    SW_REQUIRE_TRUE( clock.setTime( 0, 10.0f ) );
    clock.advanceToHour( 8.0f );
    SW_EXPECT_NEAR( 8.0f, clock.getHour(), 0.01f );
    SW_EXPECT_EQUAL( 1, clock.getDay() );
}

SW_TEST_CASE( WorldClockTest, PhasesFollowTheConfiguredHours )
{
    const WorldClockSettings settings;
    SW_EXPECT_TRUE( WorldClock::computePhaseAt( settings, 3.0f ) == DayPhase::Night );
    SW_EXPECT_TRUE( WorldClock::computePhaseAt( settings, 6.0f ) == DayPhase::Dawn );
    SW_EXPECT_TRUE( WorldClock::computePhaseAt( settings, 12.0f ) == DayPhase::Day );
    SW_EXPECT_TRUE( WorldClock::computePhaseAt( settings, 19.0f ) == DayPhase::Dusk );
    SW_EXPECT_TRUE( WorldClock::computePhaseAt( settings, 22.0f ) == DayPhase::Night );
}

/**
 * @brief [WorldClockTest] 어떤 하루 길이에서도 하루를 걸으면 시 경계 24 개를 모두 지나 다음 날이 된다(감사 G-2)
 * @details 하루 3500 · 5000 · 7000 · 10000 · 20000 초에서 시 경계에 올려놓은 시각이 N 바로 아래로 반올림되어 걸음이 1e-4 로 고정됐고,
 *          2048 초를 넘은 시각에서는 그 걸음이 float 에 더해지지 않아 시계가 영구히 멈췄다. 1 분씩 하루를 흘린다.
 */
SW_TEST_CASE( WorldClockTest, AdvancesAcrossEveryHourForAnyDayLength )
{
    for ( const float32 secondsPerDay : { 1440.0f, 3500.0f, 5000.0f, 7000.0f, 10000.0f, 20000.0f, 86400.0f } )
    {
        WorldClockSettings settings = WorldClockTestUtil::makeSettings();
        settings._secondsPerDay     = secondsPerDay;
        WorldClock clock;
        SW_REQUIRE_TRUE( clock.initialize( settings ) );
        for ( int32 minute = 0; minute < 24 * 60; ++minute )
            clock.advanceMinutes( 1.0f );
        vector<WorldClockEvent> listEvent;
        clock.pollEvents( listEvent );
        SW_EXPECT_EQUAL( 24u, WorldClockTestUtil::countEvents( listEvent, WorldClockEvent::Kind::HourChanged ) );
        SW_EXPECT_EQUAL( 1u, WorldClockTestUtil::countEvents( listEvent, WorldClockEvent::Kind::DayChanged ) );
    }
}
