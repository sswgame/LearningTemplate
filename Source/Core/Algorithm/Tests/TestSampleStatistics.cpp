// SampleStatistics — 최근접 순위 백분위: 1 부터 세고 올림하며, 빈 표본 · 0 · 100 넘는 백분위를 한 규칙으로 다룬다.
#include "pch.h"

#include "Core/Algorithm/SampleStatistics.h"
#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

using namespace sw;

static_assert( SampleStatistics::getNearestRank( 0, 50 ) == 0, "no samples have no rank" );
static_assert( SampleStatistics::getNearestRank( 100, 99 ) == 99, "p99 of 100 frames is the 99th" );
static_assert( SampleStatistics::getNearestRank( 100, 50 ) == 50, "p50 of 100 frames is the 50th" );
static_assert( SampleStatistics::getNearestRank( 3, 50 ) == 2, "the rank rounds up" );
static_assert( SampleStatistics::getNearestRank( 7, 0 ) == 1, "p0 is the first sample" );
static_assert( SampleStatistics::getNearestRank( 7, 250 ) == 7, "a percent above 100 is the last sample" );

/** @brief [SampleStatisticsTest] 정렬된 표본에서 순위의 값을 고르고, 빈 표본은 빈 값이다 */
SW_TEST_CASE( SampleStatisticsTest, NearestRankPicksSortedSample )
{
    const vector<int64> listSorted{ 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
    SW_EXPECT_EQUAL( int64{ 50 }, *SampleStatistics::findNearestRankPercentile( listSorted, 50 ) );
    SW_EXPECT_EQUAL( int64{ 100 }, *SampleStatistics::findNearestRankPercentile( listSorted, 95 ) );
    SW_EXPECT_EQUAL( int64{ 10 }, *SampleStatistics::findNearestRankPercentile( listSorted, 1 ) );
    SW_EXPECT_EQUAL( int64{ 100 }, *SampleStatistics::findNearestRankPercentile( listSorted, 100 ) );

    const vector<float32> listEmpty;
    SW_EXPECT_FALSE( SampleStatistics::findNearestRankPercentile( listEmpty, 50 ).has_value() );
}

/** @brief [SampleStatisticsTest] 정수 산술이라 부동소수 올림 오차가 없다 — 0.07 * 100 은 부동소수로 7.000…01 이 되어 순위 8 로 넘어갔다 */
SW_TEST_CASE( SampleStatisticsTest, RankHasNoFloatingRoundOff )
{
    SW_EXPECT_EQUAL( uint64{ 7 }, SampleStatistics::getNearestRank( 100, 7 ) );
    SW_EXPECT_EQUAL( uint64{ 57 }, SampleStatistics::getNearestRank( 100, 57 ) );
}
