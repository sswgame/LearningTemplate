// ProgressionCatalogsTest — 통계 · 경험치 곡선 · 날씨 표의 칸 읽기(스키마: StatDef · ExperienceCurveFields · ExperienceLevelFields · WeatherDef).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Gameplay/Progression/LevelProgress.h"
#include "GameFramework/Runtime/Gameplay/Progression/PlayerStats.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

using namespace sw;

/** @brief [ProgressionCatalogsTest] 통계 — 종류는 열거자 이름, 이름을 적지 않으면 id, 상한은 `max` */
SW_TEST_CASE( ProgressionCatalogsTest, StatReadsKindNameAndMax )
{
    StatCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<StatCatalog><Stat id="kills" kind="Counter" max="9999"/><Stat id="best" name="Best Score" kind="Max"/></StatCatalog>)", "stats" ) );
    const StatDef* pKills = catalog.findStat( hashed_string( "kills" ) );
    SW_REQUIRE_NOT_NULL( pKills );
    SW_EXPECT_TRUE( pKills->_name == "kills" );
    SW_EXPECT_NEAR( 9999.0, pKills->_maxValue, 1e-9 );
    const StatDef* pBest = catalog.findStat( hashed_string( "best" ) );
    SW_REQUIRE_NOT_NULL( pBest );
    SW_EXPECT_TRUE( pBest->_kind == StatKind::Max );
    SW_EXPECT_TRUE( pBest->_name == "Best Score" );
}

/** @brief [ProgressionCatalogsTest] 경험치 곡선 — 공식의 빠진 칸은 생성자 값, 표의 xp 는 1 이상이어야 한다 */
SW_TEST_CASE( ProgressionCatalogsTest, ExperienceCurveFormulaAndTable )
{
    ExperienceCurve formula;
    SW_REQUIRE_TRUE( formula.loadFromXMLText( R"(<ExperienceCurve maxLevel="10" base="50"/>)", "curve" ) );
    SW_EXPECT_EQUAL( 10, formula.getMaxLevel() );

    ExperienceCurve table;
    SW_REQUIRE_TRUE( table.loadFromXMLText( R"(<ExperienceCurve><Level xp="100"/><Level xp="250"/></ExperienceCurve>)", "table" ) );
    SW_EXPECT_EQUAL( 3, table.getMaxLevel() );
    SW_EXPECT_EQUAL( int64( 250 ), table.getXPToNext( 2 ) );

    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    ExperienceCurve bad;
    SW_EXPECT_FALSE( bad.loadFromXMLText( R"(<ExperienceCurve><Level xp="0"/></ExperienceCurve>)", "bad" ) );
}

/** @brief [ProgressionCatalogsTest] 날씨 — 전환 시간 · 지속 시간을 읽고, 최대가 최소보다 작으면 그 날씨를 넣지 않는다 */
SW_TEST_CASE( ProgressionCatalogsTest, WeatherReadsDurationsAndRejectsOrder )
{
    WeatherCatalog catalog;
    SW_EXPECT_LOG( "WeatherSystem.WeatherDurationOrder" );
    SW_EXPECT_LOG( "WeatherSystem.WeatherInvalidFieldsSkipped" );
    (void)catalog.loadFromXMLText( R"(<WeatherCatalog transition="5">
        <Weather id="Rain" weight="2" minDuration="60" maxDuration="120" seasons="Spring:3,Summer"/>
        <Weather id="Bad" minDuration="300" maxDuration="100"/>
    </WeatherCatalog>)",
                                   "weather" );
    SW_EXPECT_NEAR( 5.0f, catalog.getTransitionTime(), 1e-6f );
    const WeatherDef* pRain = catalog.find( hashed_string( "Rain" ) );
    SW_REQUIRE_NOT_NULL( pRain );
    SW_EXPECT_NEAR( 60.0f, pRain->_minDuration, 1e-6f );
    SW_EXPECT_EQUAL( size_t( 2 ), pRain->_listSeasonWeight.size() );
    SW_EXPECT_TRUE( catalog.find( hashed_string( "Bad" ) ) == nullptr );
}

/** @brief [ProgressionCatalogsTest] 평판 — 시작값은 [min, max] 안(필드 경계 PROPERTY), 단계 · 연결을 읽는다 */
SW_TEST_CASE( ProgressionCatalogsTest, ReputationReadsFieldBoundsTiersAndLinks )
{
    ReputationCatalog catalog;
    SW_EXPECT_LOG( "XMLFields.ValueOutOfRange" );
    SW_EXPECT_LOG( "Serialization.ValueOutOfRange" );
    SW_EXPECT_LOG( "Reputation.FactionInvalidFieldsSkipped" );
    (void)catalog.loadFromXMLText( R"(<ReputationCatalog>
        <Faction id="Town" min="-100" max="100" start="10"><Tier name="Friendly" min="50"/><Link faction="Bandits" ratio="-0.5"/></Faction>
        <Faction id="Bad" min="0" max="10" start="20"/>
    </ReputationCatalog>)",
                                   "reputation" );
    const FactionDef* pTown = catalog.find( hashed_string( "Town" ) );
    SW_REQUIRE_NOT_NULL( pTown );
    SW_EXPECT_EQUAL( 10, pTown->_startValue );
    SW_REQUIRE_EQUAL( size_t( 1 ), pTown->_listTier.size() );
    SW_EXPECT_EQUAL( 50, pTown->_listTier[0]._minValue );
    SW_REQUIRE_EQUAL( size_t( 1 ), pTown->_listLink.size() );
    SW_EXPECT_NEAR( -0.5f, pTown->_listLink[0]._ratio, 1e-6f );
    SW_EXPECT_TRUE( catalog.find( hashed_string( "Bad" ) ) == nullptr );
}
