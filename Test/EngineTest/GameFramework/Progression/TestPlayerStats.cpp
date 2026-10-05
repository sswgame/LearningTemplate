#include "pch.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Progression/PlayerStats.h"

#include "TestFramework/TestFramework.h"

// 로컬 통계 — 정의는 데이터, 카운터 · 최대 · 최소 · 시간, 종류가 맞지 않는 호출 · 모르는 id 무시, 바뀔 때만 알림, 상한, 프로필 파일 저장 · 읽기.

using namespace sw;

namespace
{
    constexpr const utf8* kPlayerStatsTestXml = R"(
<Stats>
  <Stat id="enemies_killed" kind="Counter" name="Enemies"/>
  <Stat id="coins" kind="Counter" max="999"/>
  <Stat id="best_score" kind="Max"/>
  <Stat id="fastest_lap" kind="Min"/>
  <Stat id="play_time" kind="Time"/>
  <Stat id="mystery" kind="Average"/>
</Stats>
)";
} // namespace

/**
 * @brief [PlayerStatsTest] 정의를 데이터에서 읽는다 — 모르는 종류는 오류로 빼고, 빠진 종류는 카운터, 이름이 없으면 id
 */
SW_TEST_CASE( PlayerStatsTest, CatalogReadsKindsAndRejectsUnknownOnes )
{
    StatCatalog catalog;
    {
        test::ScopedDefensiveTestLog expected( "a stat with an unknown kind" );
        SW_ASSERT_TRUE( catalog.loadFromXmlText( kPlayerStatsTestXml, "PlayerStatsTest" ) );
    }
    SW_EXPECT_EQUAL( size_t( 5 ), catalog.getStats().size() );
    SW_EXPECT_TRUE( catalog.findStat( "mystery" ) == nullptr );
    SW_ASSERT_NOT_NULL( catalog.findStat( "fastest_lap" ) );
    SW_EXPECT_TRUE( catalog.findStat( "fastest_lap" )->_kind == StatKind::Min );
    SW_EXPECT_EQUAL( string( "Enemies" ), catalog.findStat( "enemies_killed" )->_name );
    SW_EXPECT_EQUAL( string( "coins" ), catalog.findStat( "coins" )->_name );
}

/**
 * @brief [PlayerStatsTest] 카운터는 더하고(상한에서 멈춘다), 최대 · 최소는 기록이 좋아질 때만, 시간은 초를 더한다 — 바뀐 때만 알리고, 종류가 다르거나 모르는 id 는 무시한다
 */
SW_TEST_CASE( PlayerStatsTest, ValuesChangeByKindAndNotifyOnlyRealChanges )
{
    StatCatalog catalog;
    {
        test::ScopedDefensiveTestLog expected( "a stat with an unknown kind" );
        SW_ASSERT_TRUE( catalog.loadFromXmlText( kPlayerStatsTestXml, "PlayerStatsTest" ) );
    }
    PlayerStats stats;
    stats.initialize( &catalog );
    vector<StatChange> listChange;
    const uint32       handle = stats.registerChangeListener( SW_DELEGATE_LAMBDA( PlayerStats::ChangeDelegate, [&listChange]( const StatChange& change )
          { listChange.push_back( change ); } ) );

    SW_EXPECT_TRUE( stats.increment( "enemies_killed" ) );
    SW_EXPECT_TRUE( stats.increment( "enemies_killed", 4 ) );
    SW_EXPECT_FALSE( stats.increment( "enemies_killed", 0 ) );
    SW_EXPECT_EQUAL( int64( 5 ), stats.getCount( "enemies_killed" ) );
    SW_EXPECT_TRUE( stats.increment( "coins", 2000 ) );
    SW_EXPECT_EQUAL( int64( 999 ), stats.getCount( "coins" ) ); // 상한
    SW_EXPECT_FALSE( stats.increment( "coins", 5 ) );           // 이미 상한 — 바뀌지 않으면 알리지 않는다

    SW_EXPECT_TRUE( stats.submit( "best_score", 120.0 ) );
    SW_EXPECT_FALSE( stats.submit( "best_score", 80.0 ) );
    SW_EXPECT_TRUE( stats.submit( "best_score", 150.0 ) );
    SW_EXPECT_FALSE( stats.hasValue( "fastest_lap" ) );
    SW_EXPECT_TRUE( stats.submit( "fastest_lap", 95.5 ) ); // 첫 값은 늘 들어간다
    SW_EXPECT_FALSE( stats.submit( "fastest_lap", 99.0 ) );
    SW_EXPECT_TRUE( stats.submit( "fastest_lap", 91.25 ) );
    SW_EXPECT_NEAR_EQUAL( 91.25, stats.getValue( "fastest_lap" ), 1.0e-9 );

    SW_EXPECT_TRUE( stats.addTime( "play_time", 1.5 ) );
    SW_EXPECT_TRUE( stats.addTime( "play_time", 2.25 ) );
    SW_EXPECT_NEAR_EQUAL( 3.75, stats.getValue( "play_time" ), 1.0e-9 );

    {
        test::ScopedDefensiveTestLog expected( "wrong kind calls and an unknown stat" );
        SW_EXPECT_FALSE( stats.increment( "best_score" ) ); // 최대 통계에 카운터 호출
        SW_EXPECT_FALSE( stats.submit( "enemies_killed", 1.0 ) );
        SW_EXPECT_FALSE( stats.addTime( "coins", 1.0 ) );
        SW_EXPECT_FALSE( stats.increment( "no_such_stat" ) );
    }

    SW_ASSERT_EQUAL( size_t( 9 ), listChange.size() );
    SW_EXPECT_TRUE( listChange[1]._id == hashed_string( "enemies_killed" ) );
    SW_EXPECT_NEAR_EQUAL( 1.0, listChange[1]._oldValue, 1.0e-9 );
    SW_EXPECT_NEAR_EQUAL( 5.0, listChange[1]._newValue, 1.0e-9 );
    SW_EXPECT_TRUE( listChange[4]._kind == StatKind::Max );

    stats.unregisterChangeListener( handle );
    SW_EXPECT_TRUE( stats.increment( "enemies_killed" ) );
    SW_EXPECT_EQUAL( size_t( 9 ), listChange.size() );
}

/**
 * @brief [PlayerStatsTest] 프로필 파일로 쓰고 읽으면 같은 값이 돌아오고(알림 없음), 정의가 없어진 값은 버리고, 깨진 파일은 거절하고 그대로 둔다
 */
SW_TEST_CASE( PlayerStatsTest, ProfileFileRoundTripsAndDropsRemovedStats )
{
    StatCatalog catalog;
    {
        test::ScopedDefensiveTestLog expected( "a stat with an unknown kind" );
        SW_ASSERT_TRUE( catalog.loadFromXmlText( kPlayerStatsTestXml, "PlayerStatsTest" ) );
    }
    PlayerStats stats;
    stats.initialize( &catalog );
    SW_EXPECT_TRUE( stats.increment( "enemies_killed", 42 ) );
    SW_EXPECT_TRUE( stats.submit( "fastest_lap", 88.0 ) );
    SW_EXPECT_TRUE( stats.addTime( "play_time", 600.0 ) );
    const string path = test::makeTempPath( "profile_stats.bin" );
    SW_ASSERT_TRUE( stats.saveToFile( path ) );

    PlayerStats loaded;
    loaded.initialize( &catalog );
    int32 changeCount = 0;
    (void)loaded.registerChangeListener( SW_DELEGATE_LAMBDA( PlayerStats::ChangeDelegate, [&changeCount]( const StatChange& )
    { ++changeCount; } ) );
    SW_ASSERT_TRUE( loaded.loadFromFile( path ) );
    SW_EXPECT_EQUAL( 0, changeCount );
    SW_EXPECT_EQUAL( int64( 42 ), loaded.getCount( "enemies_killed" ) );
    SW_EXPECT_NEAR_EQUAL( 88.0, loaded.getValue( "fastest_lap" ), 1.0e-9 );
    SW_EXPECT_FALSE( loaded.hasValue( "best_score" ) );

    // 정의가 없어진 값은 버린다.
    StatCatalog smaller;
    StatDef     kept{};
    kept._id   = hashed_string( "play_time" );
    kept._kind = StatKind::Time;
    smaller.addStat( kept );
    PlayerStats trimmed;
    trimmed.initialize( &smaller );
    {
        test::ScopedDefensiveTestLog expected( "saved stats without a definition" );
        SW_ASSERT_TRUE( trimmed.loadFromFile( path ) );
    }
    SW_EXPECT_NEAR_EQUAL( 600.0, trimmed.getValue( "play_time" ), 1.0e-9 );
    SW_EXPECT_FALSE( trimmed.hasValue( "enemies_killed" ) );

    // 깨진 바이트는 거절하고 그대로 둔다.
    Archive written;
    stats.writeState( written );
    Archive cut( written.getData(), written.getSize() - 3 );
    SW_EXPECT_FALSE( loaded.readState( cut ) );
    SW_EXPECT_EQUAL( int64( 42 ), loaded.getCount( "enemies_killed" ) );
}
