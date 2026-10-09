#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/World/World/LandRegistry.h"
#include "GameFramework/Base/World/World/WeatherSystem.h"
#include "GameFramework/Base/World/World/WorldClock.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 세계 — 공유 땅, 시계(시 · 때 · 날 · 계절 · 해 · 햇빛 · 잠자기 · 배율 · 멈춤)와 날씨(계절 가중치 · 지속 · 섞기 · 강제 · 예보).

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

/**
 * @brief [WorldTest] 공유 땅 — 얻기는 사각 전부이거나 아무것도(남의 칸이 하나라도 있으면 거절), 놓기는 내 칸만, 막힘은 남에게만,
 *        상태 바이트는 주인을 이름으로 실어 등록 순서가 다른 땅에도 같은 칸 주인으로 되살린다
 */
SW_TEST_CASE( WorldTest, LandClaimsAreAllOrNothingAndSurviveTheSnapshot )
{
    LandRegistry land;
    land.initialize( 8, 8, 1.0f, float3{} );
    const uint16 farm = land.registerOwner( "Farming" );
    const uint16 city = land.registerOwner( "CityBuilder" );
    SW_EXPECT_EQUAL( farm, land.registerOwner( "Farming" ) ); // 같은 이름은 같은 번호
    SW_EXPECT_TRUE( farm != city );

    SW_ASSERT_TRUE( land.claimRect( farm, 0, 0, 2, 2, false ) );
    SW_ASSERT_TRUE( land.claimRect( city, 4, 4, 5, 5, true ) );
    const uint32 revision = land.getRevision();
    SW_EXPECT_FALSE( land.claimRect( city, 2, 2, 4, 4, true ) ); // (2, 2) 가 밭의 것 — 반쯤 얻지 않는다
    SW_EXPECT_EQUAL( LandRegistry::kNoOwner, land.getOwner( 3, 3 ) );
    SW_EXPECT_EQUAL( revision, land.getRevision() );
    SW_EXPECT_FALSE( land.claimRect( farm, 7, 7, 8, 8, false ) ); // 밖
    SW_EXPECT_TRUE( land.claimRect( farm, 0, 0, 1, 1, false ) );  // 내 칸은 다시 얻어도 된다

    SW_EXPECT_TRUE( land.isBlockedFor( farm, 4, 4 ) );
    SW_EXPECT_FALSE( land.isBlockedFor( city, 4, 4 ) ); // 내 칸은 막힘이 아니다
    SW_EXPECT_FALSE( land.isBlockedFor( city, 0, 0 ) ); // 밭은 막힘 없이 얻었다
    SW_EXPECT_FALSE( land.isUsableBy( city, 0, 0 ) );
    SW_EXPECT_TRUE( land.isUsableBy( city, 6, 6 ) );

    land.releaseRect( city, 0, 0, 7, 7 ); // 내 칸만 놓는다
    SW_EXPECT_EQUAL( farm, land.getOwner( 1, 1 ) );
    SW_EXPECT_EQUAL( LandRegistry::kNoOwner, land.getOwner( 4, 4 ) );
    SW_ASSERT_TRUE( land.claimWorldRect( city, float3{ 5.0f, 0.0f, 5.0f }, float3{ 2.0f, 1.0f, 2.0f }, true ) ); // 4..5 × 4..5
    SW_EXPECT_EQUAL( city, land.getOwner( 4, 4 ) );
    SW_EXPECT_EQUAL( city, land.getOwner( 5, 5 ) );
    SW_EXPECT_EQUAL( LandRegistry::kNoOwner, land.getOwner( 6, 6 ) );
    SW_EXPECT_EQUAL( LandRegistry::kNoOwner, land.getOwner( 3, 3 ) );

    Archive archive;
    land.writeState( archive );
    vector<uint8> bytes;
    archive.writeData( bytes );

    // 다른 실행 — 등록 순서가 반대인 땅
    LandRegistry restored;
    restored.initialize( 8, 8, 1.0f, float3{} );
    const uint16 restoredCity = restored.registerOwner( "CityBuilder" );
    Archive      reader( bytes.data(), bytes.size() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( reader.getRemainingBytes() ) );
    SW_EXPECT_EQUAL( restoredCity, restored.getOwner( 4, 4 ) );
    SW_EXPECT_TRUE( restored.getOwnerName( 1, 1 ) == hashed_string( "Farming" ) );
    SW_EXPECT_EQUAL( restored.registerOwner( "Farming" ), restored.getOwner( 1, 1 ) );
    SW_EXPECT_TRUE( restored.isBlockedFor( restored.registerOwner( "Farming" ), 5, 5 ) );

    // 크기가 다른 땅에는 읽지 않는다
    LandRegistry smaller;
    smaller.initialize( 4, 4, 1.0f, float3{} );
    Archive smallReader( bytes.data(), bytes.size() );
    SW_EXPECT_FALSE( smaller.readState( smallReader ) );
}
