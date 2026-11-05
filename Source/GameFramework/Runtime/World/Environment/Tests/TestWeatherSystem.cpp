#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

using namespace sw;

namespace
{
    struct WeatherSystemTestUtil
    {
        static inline const hashed_string kSpring{ "Spring" };
        static inline const hashed_string kSummer{ "Summer" };
        static inline const hashed_string kWetness{ "wetness" };

        /** @brief 맑음은 어느 계절에나, 비는 여름에만 옵니다. 전환 10 초, 길이는 고정입니다. */
        static void loadTwoWeathers( WeatherCatalog& outCatalog )
        {
            SW_REQUIRE_TRUE( outCatalog.loadFromXMLText( R"(<WeatherCatalog transition="10">
                <Weather id="Clear" minDuration="100" maxDuration="100"><Values wetness="0"/></Weather>
                <Weather id="Rain" seasons="Summer:1" minDuration="50" maxDuration="50"><Values wetness="1"/></Weather>
            </WeatherCatalog>)",
                                                         "weather" ) );
        }

        /** @brief 셋이 고르게 나오는 표입니다. 순서가 시드에 따라 달라집니다. */
        static void loadThreeWeathers( WeatherCatalog& outCatalog )
        {
            SW_REQUIRE_TRUE( outCatalog.loadFromXMLText( R"(<WeatherCatalog transition="1">
                <Weather id="A" minDuration="10" maxDuration="20"/>
                <Weather id="B" minDuration="10" maxDuration="20"/>
                <Weather id="C" minDuration="10" maxDuration="20"/>
            </WeatherCatalog>)",
                                                         "weather" ) );
        }

        /** @brief 지금 날씨가 끝날 때까지 흘려 다음 날씨로 넘깁니다. */
        static bool finishCurrent( WeatherSystem& weather, const hashed_string& season )
        {
            return weather.update( weather.getRemaining() + 0.5f, season );
        }
    };
} // namespace

SW_TEST_CASE( WeatherSystemTest, SeasonWeightsDecideWhatIsPossible )
{
    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadTwoWeathers( catalog );
    const WeatherDef* pRain = catalog.find( hashed_string( "Rain" ) );
    SW_REQUIRE_NOT_NULL( pRain );
    SW_EXPECT_EQUAL( 0.0f, pRain->computeWeight( WeatherSystemTestUtil::kSpring ) );
    SW_EXPECT_EQUAL( 1.0f, pRain->computeWeight( WeatherSystemTestUtil::kSummer ) );
    const WeatherDef* pClear = catalog.find( hashed_string( "Clear" ) );
    SW_REQUIRE_NOT_NULL( pClear );
    SW_EXPECT_EQUAL( 1.0f, pClear->computeWeight( WeatherSystemTestUtil::kSpring ) );
}

SW_TEST_CASE( WeatherSystemTest, InvalidSeasonWeightSkipsTheWeather )
{
    SW_EXPECT_LOG( "WeatherSystem.WeatherInvalidSeasonWeight" );
    SW_EXPECT_LOG( "WeatherSystem.WeatherInvalidFieldsSkipped" );
    WeatherCatalog catalog;
    SW_EXPECT_TRUE( catalog.loadFromXMLText( R"(<WeatherCatalog>
        <Weather id="Good"/>
        <Weather id="Bad" seasons="Spring:-1"/>
    </WeatherCatalog>)",
                                             "weather" ) );
    SW_EXPECT_TRUE( catalog.find( hashed_string( "Good" ) ) != nullptr );
    SW_EXPECT_TRUE( catalog.find( hashed_string( "Bad" ) ) == nullptr );
}

// 시작 계절에 올 날씨가 없어도 올 수 있는 계절이 되면 날씨가 시작된다.
SW_TEST_CASE( WeatherSystemTest, StartsOnceASeasonAllowsAWeather )
{
    WeatherCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<WeatherCatalog><Weather id="Rain" seasons="Summer:1"/></WeatherCatalog>)", "weather" ) );
    WeatherSystem weather;
    weather.initialize( &catalog, 7, WeatherSystemTestUtil::kSpring );
    SW_EXPECT_TRUE( weather.getCurrent().empty() );
    SW_EXPECT_FALSE( weather.update( 1.0f, WeatherSystemTestUtil::kSpring ) );
    SW_EXPECT_TRUE( weather.update( 1.0f, WeatherSystemTestUtil::kSummer ) );
    SW_EXPECT_TRUE( weather.getCurrent() == hashed_string( "Rain" ) );
    SW_EXPECT_TRUE( weather.getRemaining() > 0.0f );
    SW_EXPECT_EQUAL( 1.0f, weather.getBlend() );
}

SW_TEST_CASE( WeatherSystemTest, ChangeBlendsOverTheTransition )
{
    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadTwoWeathers( catalog );
    WeatherSystem weather;
    weather.initialize( &catalog, 1, WeatherSystemTestUtil::kSpring );
    SW_EXPECT_TRUE( weather.getCurrent() == hashed_string( "Clear" ) );
    SW_EXPECT_EQUAL( 1.0f, weather.getBlend() );

    SW_EXPECT_TRUE( WeatherSystemTestUtil::finishCurrent( weather, WeatherSystemTestUtil::kSummer ) );
    SW_EXPECT_TRUE( weather.getCurrent() == hashed_string( "Rain" ) );
    SW_EXPECT_TRUE( weather.getPrevious() == hashed_string( "Clear" ) );
    SW_EXPECT_EQUAL( 0.0f, weather.computeValue( WeatherSystemTestUtil::kWetness ) );

    SW_EXPECT_FALSE( weather.update( 5.0f, WeatherSystemTestUtil::kSummer ) );
    SW_EXPECT_NEAR( 0.5f, weather.getBlend(), 1e-4f );
    SW_EXPECT_NEAR( 0.5f, weather.computeValue( WeatherSystemTestUtil::kWetness ), 1e-4f );

    SW_EXPECT_FALSE( weather.update( 50.0f - 5.0f - 1.0f, WeatherSystemTestUtil::kSummer ) );
    SW_EXPECT_EQUAL( 1.0f, weather.getBlend() );
    SW_EXPECT_NEAR( 1.0f, weather.computeValue( WeatherSystemTestUtil::kWetness ), 1e-4f );
}

// 다른 날씨가 없으면 그대로 이어가고 바뀌었다고 하지 않는다.
SW_TEST_CASE( WeatherSystemTest, StaysWhenThereIsNoOtherWeather )
{
    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadTwoWeathers( catalog );
    WeatherSystem weather;
    weather.initialize( &catalog, 1, WeatherSystemTestUtil::kSpring );
    SW_EXPECT_FALSE( WeatherSystemTestUtil::finishCurrent( weather, WeatherSystemTestUtil::kSpring ) );
    SW_EXPECT_TRUE( weather.getCurrent() == hashed_string( "Clear" ) );
    SW_EXPECT_NEAR( 100.0f, weather.getRemaining(), 1e-2f );
}

SW_TEST_CASE( WeatherSystemTest, NothingFlowsWithoutTimeOrCatalog )
{
    WeatherSystem empty;
    empty.initialize( nullptr, 1, WeatherSystemTestUtil::kSpring );
    SW_EXPECT_FALSE( empty.update( 10.0f, WeatherSystemTestUtil::kSpring ) );
    SW_EXPECT_TRUE( empty.getCurrent().empty() );
    SW_EXPECT_EQUAL( 0.0f, empty.computeValue( WeatherSystemTestUtil::kWetness ) );

    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadTwoWeathers( catalog );
    WeatherSystem weather;
    weather.initialize( &catalog, 1, WeatherSystemTestUtil::kSpring );
    const float32 remaining = weather.getRemaining();
    SW_EXPECT_FALSE( weather.update( 0.0f, WeatherSystemTestUtil::kSpring ) );
    SW_EXPECT_FALSE( weather.update( -5.0f, WeatherSystemTestUtil::kSpring ) );
    SW_EXPECT_EQUAL( remaining, weather.getRemaining() );
}

SW_TEST_CASE( WeatherSystemTest, ForceWeatherHonoursDurationAndImmediate )
{
    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadTwoWeathers( catalog );
    WeatherSystem weather;
    weather.initialize( &catalog, 1, WeatherSystemTestUtil::kSpring );

    weather.forceWeather( hashed_string( "NoSuchWeather" ), 5.0f );
    SW_EXPECT_TRUE( weather.getCurrent() == hashed_string( "Clear" ) );

    weather.forceWeather( hashed_string( "Rain" ), 7.0f );
    SW_EXPECT_TRUE( weather.getCurrent() == hashed_string( "Rain" ) );
    SW_EXPECT_EQUAL( 7.0f, weather.getRemaining() );
    SW_EXPECT_EQUAL( 0.0f, weather.getBlend() );

    weather.forceWeather( hashed_string( "Clear" ), 0.0f, true );
    SW_EXPECT_EQUAL( 1.0f, weather.getBlend() );
    SW_EXPECT_NEAR( 100.0f, weather.getRemaining(), 1e-2f );

    // 같은 날씨를 다시 정하면 전환을 처음부터 하지 않고 길이만 바꾼다.
    weather.forceWeather( hashed_string( "Clear" ), 3.0f );
    SW_EXPECT_EQUAL( 1.0f, weather.getBlend() );
    SW_EXPECT_EQUAL( 3.0f, weather.getRemaining() );
}

// 예보는 상태를 바꾸지 않고, 실제로 흘렀을 때와 같은 날씨를 말한다.
SW_TEST_CASE( WeatherSystemTest, ForecastMatchesWhatActuallyHappens )
{
    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadThreeWeathers( catalog );
    WeatherSystem weather;
    weather.initialize( &catalog, 12345, WeatherSystemTestUtil::kSpring );
    const hashed_string current   = weather.getCurrent();
    const float32       remaining = weather.getRemaining();

    vector<hashed_string> listForecast;
    weather.forecast( WeatherSystemTestUtil::kSpring, 8, listForecast );
    SW_REQUIRE_EQUAL( size_t{ 8 }, listForecast.size() );
    SW_EXPECT_TRUE( weather.getCurrent() == current );
    SW_EXPECT_EQUAL( remaining, weather.getRemaining() );

    for ( const hashed_string& expected : listForecast )
    {
        SW_EXPECT_TRUE( WeatherSystemTestUtil::finishCurrent( weather, WeatherSystemTestUtil::kSpring ) );
        SW_EXPECT_TRUE( weather.getCurrent() == expected );
    }
}

SW_TEST_CASE( WeatherSystemTest, SameSeedSameWeather )
{
    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadThreeWeathers( catalog );
    WeatherSystem first;
    WeatherSystem second;
    first.initialize( &catalog, 99, WeatherSystemTestUtil::kSpring );
    second.initialize( &catalog, 99, WeatherSystemTestUtil::kSpring );
    for ( uint32 step = 0; step < 16; ++step )
    {
        SW_EXPECT_TRUE( first.getCurrent() == second.getCurrent() );
        SW_EXPECT_EQUAL( first.getRemaining(), second.getRemaining() );
        (void)WeatherSystemTestUtil::finishCurrent( first, WeatherSystemTestUtil::kSpring );
        (void)WeatherSystemTestUtil::finishCurrent( second, WeatherSystemTestUtil::kSpring );
    }
}

// 저장한 상태로 돌아오면 앞으로의 날씨까지 같다.
SW_TEST_CASE( WeatherSystemTest, StateRoundTripKeepsTheFuture )
{
    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadThreeWeathers( catalog );
    WeatherSystem weather;
    weather.initialize( &catalog, 5, WeatherSystemTestUtil::kSpring );
    (void)WeatherSystemTestUtil::finishCurrent( weather, WeatherSystemTestUtil::kSpring );
    Archive written;
    weather.writeState( written );

    WeatherSystem loaded;
    loaded.initialize( &catalog, 1000, WeatherSystemTestUtil::kSpring );
    Archive reader( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( loaded.readState( reader ) );
    SW_EXPECT_TRUE( loaded.getCurrent() == weather.getCurrent() );
    SW_EXPECT_TRUE( loaded.getPrevious() == weather.getPrevious() );
    SW_EXPECT_EQUAL( weather.getRemaining(), loaded.getRemaining() );
    SW_EXPECT_EQUAL( weather.getBlend(), loaded.getBlend() );

    vector<hashed_string> listExpected;
    vector<hashed_string> listLoaded;
    weather.forecast( WeatherSystemTestUtil::kSpring, 6, listExpected );
    loaded.forecast( WeatherSystemTestUtil::kSpring, 6, listLoaded );
    SW_EXPECT_TRUE( listExpected == listLoaded );
}

// 지금 표에 없는 날씨의 상태나 깨진 바이트는 거절하고 상태를 그대로 둔다.
SW_TEST_CASE( WeatherSystemTest, RefusedStateLeavesTheSystemAlone )
{
    WeatherCatalog catalog;
    WeatherSystemTestUtil::loadTwoWeathers( catalog );
    WeatherSystem saved;
    saved.initialize( &catalog, 3, WeatherSystemTestUtil::kSpring );
    Archive written;
    saved.writeState( written );

    WeatherCatalog otherCatalog;
    SW_REQUIRE_TRUE( otherCatalog.loadFromXMLText( R"(<WeatherCatalog><Weather id="Snow"/></WeatherCatalog>)", "weather" ) );
    WeatherSystem other;
    other.initialize( &otherCatalog, 3, WeatherSystemTestUtil::kSpring );
    const float32 remaining = other.getRemaining();
    Archive       unknown( written.getData(), written.getSize() );
    SW_EXPECT_FALSE( other.readState( unknown ) );
    SW_EXPECT_TRUE( other.getCurrent() == hashed_string( "Snow" ) );
    SW_EXPECT_EQUAL( remaining, other.getRemaining() );

    Archive truncated( written.getData(), written.getSize() - 2 );
    SW_EXPECT_FALSE( saved.readState( truncated ) );
    SW_EXPECT_TRUE( saved.getCurrent() == hashed_string( "Clear" ) );
}
