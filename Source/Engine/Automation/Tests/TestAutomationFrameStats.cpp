#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Automation/Scenario/AutomationFrameStats.h"

using namespace sw;

SW_TEST_CASE( AutomationFrameStatsTest, SummaryUsesNearestRankPercentiles )
{
    vector<float64> listMilliseconds;
    for ( int32 index = 1; index <= 100; ++index )
    {
        listMilliseconds.push_back( static_cast<float64>( index ) ); // 1 … 100 ms, 섞이지 않은 순서로도 정렬해 본다
    }
    std::swap( listMilliseconds[0], listMilliseconds[99] );
    const FrameStatsSummary summary = AutomationFrameStats::summarize( listMilliseconds );
    SW_EXPECT_EQUAL( 100u, summary._frameCount );
    SW_EXPECT_NEAR( 50.5, summary._averageMilliseconds, 1e-9 );
    SW_EXPECT_NEAR( 1.0, summary._minMilliseconds, 1e-9 );
    SW_EXPECT_NEAR( 100.0, summary._maxMilliseconds, 1e-9 );
    SW_EXPECT_NEAR( 50.0, summary._p50Milliseconds, 1e-9 );
    SW_EXPECT_NEAR( 95.0, summary._p95Milliseconds, 1e-9 );
    SW_EXPECT_NEAR( 99.0, summary._p99Milliseconds, 1e-9 );
    SW_EXPECT_NEAR( 1000.0 / 50.5, summary._averageFramesPerSecond, 1e-9 );
}

SW_TEST_CASE( AutomationFrameStatsTest, EmptyRecordingIsAllZero )
{
    const FrameStatsSummary summary = AutomationFrameStats::summarize( {} );
    SW_EXPECT_EQUAL( 0u, summary._frameCount );
    SW_EXPECT_NEAR( 0.0, summary._p99Milliseconds, 0.0 );
}
