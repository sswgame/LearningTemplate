// 리로드 결과 집계 — 경로마다 마지막 상태, 낡음(실패 뒤 이전 값), 고정 용량, 표 글.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/AssetReload/ReloadTracker.h"

using namespace sw;

/** @brief [ReloadTrackerTest] 맡은 곳이 없는 결과는 적지 않고, 실패하면 낡음 · 성공하면 낡음이 풀리며 적용 차례가 그 성공이다 */
SW_TEST_CASE( ReloadTrackerTest, TracksStaleAndApplied )
{
    ReloadTracker tracker;
    tracker.record( ReloadSource::Config, "C:/p/Config/a.json", ReloadResult::NotTracked );
    SW_EXPECT_TRUE( tracker.copyEntries().empty() );

    tracker.record( ReloadSource::Config, "C:/p/Config/a.json", ReloadResult::Reloaded );
    tracker.record( ReloadSource::Config, "C:/p/Config/a.json", ReloadResult::Failed, "bad key\nsecond line" );
    vector<ReloadTrackerEntry> listEntry = tracker.copyEntries();
    SW_REQUIRE_EQUAL( size_t( 1 ), listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0].isStale() );
    SW_EXPECT_EQUAL( uint32( 2 ), listEntry[0]._attemptCount );
    SW_EXPECT_EQUAL( uint32( 1 ), listEntry[0]._failureCount );
    SW_EXPECT_EQUAL( string( "bad key" ), listEntry[0]._reason );  // 첫 줄만
    SW_EXPECT_EQUAL( uint64( 1 ), listEntry[0]._appliedSequence ); // 앞선 성공의 차례 — 지금 이 값으로 돈다
    SW_EXPECT_EQUAL( uint32( 1 ), tracker.countStale() );

    tracker.record( ReloadSource::Config, "C:/p/Config/a.json", ReloadResult::Reloaded );
    listEntry = tracker.copyEntries();
    SW_EXPECT_FALSE( listEntry[0].isStale() );
    SW_EXPECT_TRUE( listEntry[0]._reason.empty() );
    SW_EXPECT_EQUAL( uint64( 3 ), listEntry[0]._appliedSequence );
    SW_EXPECT_EQUAL( uint32( 0 ), tracker.countStale() );
}

/** @brief [ReloadTrackerTest] 같은 경로라도 길(설정 · 에셋 · 셰이더)이 다르면 다른 줄이고, 표는 늦은 차례부터 · stale 은 낡은 줄만이다 */
SW_TEST_CASE( ReloadTrackerTest, ReportOrdersAndFilters )
{
    ReloadTracker tracker;
    tracker.record( ReloadSource::Asset, "game/a.material", ReloadResult::Reloaded );
    tracker.record( ReloadSource::Shader, "engine/shaders/a.hlsl", ReloadResult::Failed, "error X3000" );
    const vector<ReloadTrackerEntry> listEntry = tracker.copyEntries();
    SW_REQUIRE_EQUAL( size_t( 2 ), listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._source == ReloadSource::Shader );

    const string all = tracker.formatReport( false );
    SW_EXPECT_TRUE( all.find( "2 path(s), 1 stale" ) != string::npos );
    SW_EXPECT_TRUE( all.find( "Asset OK    game/a.material" ) != string::npos );
    SW_EXPECT_TRUE( all.find( "Shader STALE engine/shaders/a.hlsl" ) != string::npos );
    SW_EXPECT_TRUE( all.find( "applied=never - error X3000" ) != string::npos );
    const string staleOnly = tracker.formatReport( true );
    SW_EXPECT_TRUE( staleOnly.find( "game/a.material" ) == string::npos );
}

/** @brief [ReloadTrackerTest] 고정 용량: 상한을 넘으면 가장 오래 소식이 없던 경로가 빠진다 */
SW_TEST_CASE( ReloadTrackerTest, CapacityEvictsOldest )
{
    ReloadTracker tracker;
    for ( uint32 index = 0; index <= ReloadTracker::kMaxPathCount; ++index )
    {
        tracker.record( ReloadSource::Asset, "game/" + to_string( index ), ReloadResult::Reloaded );
    }
    const vector<ReloadTrackerEntry> listEntry = tracker.copyEntries();
    SW_EXPECT_EQUAL( size_t( ReloadTracker::kMaxPathCount ), listEntry.size() );
    for ( const ReloadTrackerEntry& entry : listEntry )
    {
        SW_EXPECT_TRUE( entry._path != "game/0" );
    }
}
