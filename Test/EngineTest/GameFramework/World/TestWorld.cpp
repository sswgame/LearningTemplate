#include "pch.h"

#include "GameFramework/Base/World/WeatherSystem.h"
#include "GameFramework/Base/World/WorldClock.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 세계 — 시계(시 · 때 · 날 · 계절 · 해 · 햇빛 · 잠자기 · 배율 · 멈춤)와 날씨(계절 가중치 · 지속 · 섞기 · 강제 · 예보).

using namespace sw;

namespace
{
    constexpr const utf8* kWeatherTestXml = R"(
<WeatherCatalog transition="10">
  <Weather id="clear" weight="3" minDuration="100" maxDuration="200"><Values wetness="0" wind="0.1"/></Weather>
  <Weather id="rain" seasons="Spring:2,Summer:1" minDuration="50" maxDuration="80"><Values wetness="1" wind="0.5"/></Weather>
  <Weather id="snow" seasons="Winter:5" minDuration="100" maxDuration="100"><Values wetness="0.3" cold="1"/></Weather>
</WeatherCatalog>
)";
} // namespace

SW_TEST_CASE( WorldTest, ClockCountsHoursDaysSeasonsAndYears )
{
    WorldClockSettings settings;
    settings._secondsPerDay = 240.0f; // 1 시 = 10 초
    settings._daysPerSeason = 2;
    settings._startHour     = 4.0f;
    settings._listSeason    = { hashed_string( "Spring" ), hashed_string( "Winter" ) };
    WorldClock clock;
    clock.initialize( settings );
    SW_EXPECT_TRUE( clock.getDayPhase() == DayPhase::Night );
    SW_EXPECT_NEAR_EQUAL( 0.0f, clock.computeDaylight(), 1.0e-4f );

    clock.update( 15.0f ); // 5:30
    SW_EXPECT_EQUAL( 5, clock.getHourInt() );
    SW_EXPECT_EQUAL( 30, clock.getMinute() );
    SW_EXPECT_TRUE( clock.getDayPhase() == DayPhase::Dawn );
    vector<WorldClockEvent> listEvent;
    clock.drainEvents( listEvent );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( listEvent.size() ) ); // 5 시 · 새벽

    clock.update( 70.0f ); // 12:30
    SW_EXPECT_TRUE( clock.getDayPhase() == DayPhase::Day );
    SW_EXPECT_NEAR_EQUAL( 1.0f, clock.computeDaylight(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 97.5f, clock.computeSunAngle(), 1.0e-2f );

    // 잠 — 다음 날 6 시까지. 날 · 시 경계를 모두 알린다.
    clock.drainEvents( listEvent );
    listEvent.clear();
    clock.advanceToHour( 6.0f );
    SW_EXPECT_EQUAL( 1, clock.getDay() );
    SW_EXPECT_NEAR_EQUAL( 6.0f, clock.getHour(), 1.0e-2f );
    clock.drainEvents( listEvent );
    int32 hourCount = 0;
    int32 dayCount  = 0;
    for ( const WorldClockEvent& event : listEvent )
    {
        hourCount += event._kind == WorldClockEvent::Kind::HourChanged ? 1 : 0;
        dayCount += event._kind == WorldClockEvent::Kind::DayChanged ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 18, hourCount ); // 13 시 .. 다음 날 6 시
    SW_EXPECT_EQUAL( 1, dayCount );

    // 계절 · 해 — 하루 두 번이면 계절, 네 번이면 해.
    clock.setTimeScale( 2.0f );
    clock.update( 120.0f ); // 게임 240 초 = 하루
    SW_EXPECT_EQUAL( 2, clock.getDay() );
    SW_EXPECT_TRUE( clock.getSeasonName() == hashed_string( "Winter" ) );
    SW_EXPECT_EQUAL( 1, clock.getDayOfSeason() );
    clock.setPaused( true );
    clock.update( 1000.0f );
    SW_EXPECT_EQUAL( 2, clock.getDay() );
    clock.setPaused( false );
    clock.update( 240.0f );
    SW_EXPECT_EQUAL( 2, clock.getYear() );
    SW_EXPECT_TRUE( clock.getSeasonName() == hashed_string( "Spring" ) );
}

SW_TEST_CASE( WorldTest, WeatherFollowsSeasonWeightsBlendsAndForecasts )
{
    WeatherCatalog catalog;
    SW_ASSERT_TRUE( catalog.loadFromXmlText( kWeatherTestXml, "WorldTest" ) );
    SW_EXPECT_NEAR_EQUAL( 0.0f, catalog.findWeather( hashed_string( "snow" ) )->computeWeight( hashed_string( "Spring" ) ), 1.0e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, catalog.findWeather( hashed_string( "clear" ) )->computeWeight( hashed_string( "Winter" ) ), 1.0e-6f );

    // 봄에는 눈이 오지 않는다.
    const hashed_string spring( "Spring" );
    WeatherSystem       weather;
    weather.initialize( &catalog, 12345u, spring );
    int32 changeCount = 0;
    for ( int32 step = 0; step < 2000; ++step )
    {
        changeCount += weather.update( 10.0f, spring ) ? 1 : 0;
        SW_EXPECT_TRUE( weather.getCurrent() != hashed_string( "snow" ) );
    }
    SW_EXPECT_TRUE( changeCount > 50 );

    // 강제 · 섞기 — 맑음에서 비로 넘어가는 10 초.
    weather.forceWeather( hashed_string( "clear" ), 500.0f, true );
    weather.forceWeather( hashed_string( "rain" ), 500.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, weather.computeValue( hashed_string( "wetness" ) ), 1.0e-4f );
    (void)weather.update( 5.0f, spring );
    SW_EXPECT_NEAR_EQUAL( 0.5f, weather.getBlend(), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, weather.computeValue( hashed_string( "wetness" ) ), 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, weather.computeValue( hashed_string( "wind" ) ), 1.0e-4f );
    (void)weather.update( 5.0f, spring );
    SW_EXPECT_NEAR_EQUAL( 1.0f, weather.computeValue( hashed_string( "wetness" ) ), 1.0e-4f );

    // 예보는 실제로 올 날씨와 같다.
    vector<hashed_string> listForecast;
    weather.forecast( spring, 3, listForecast );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listForecast.size() ) );
    for ( const hashed_string& expected : listForecast )
    {
        (void)weather.update( weather.getRemaining() + 0.01f, spring );
        SW_EXPECT_TRUE( weather.getCurrent() == expected );
    }

    // 겨울에는 눈 · 맑음만.
    const hashed_string winter( "Winter" );
    for ( int32 step = 0; step < 500; ++step )
    {
        (void)weather.update( 10.0f, winter );
        if ( step > 10 )
            SW_EXPECT_TRUE( weather.getCurrent() != hashed_string( "rain" ) );
    }
}
