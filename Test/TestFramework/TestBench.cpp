#include "pch.h"

#include "TestFramework/TestBench.h"

#include <algorithm>

namespace test
{
    int64 getPercentile( sw::vector<int64>& listSample, uint32 percent )
    {
        if ( listSample.empty() )
            return 0;
        std::sort( listSample.begin(), listSample.end() );
        size_t rank = ( listSample.size() * percent ) / 100;
        if ( rank >= listSample.size() )
            rank = listSample.size() - 1;
        return listSample[rank];
    }

    void logBenchSamples( [[maybe_unused]] const utf8* pLabel, sw::vector<int64>& listSample )
    {
        [[maybe_unused]] const int64 minValue = getPercentile( listSample, 0 );
        [[maybe_unused]] const int64 p50      = getPercentile( listSample, 50 );
        [[maybe_unused]] const int64 p90      = getPercentile( listSample, 90 );
        [[maybe_unused]] const int64 maxValue = getPercentile( listSample, 100 );
        SW_LOG_INFO( "[Bench] %#  min %# us  p50 %# us  p90 %# us  max %# us  (%# samples)", pLabel, minValue, p50, p90, maxValue, listSample.size() );
    }

    void logBenchDeciNanos( [[maybe_unused]] const utf8* pLabel, [[maybe_unused]] int64 deciNanos )
    {
        SW_LOG_INFO( "[Bench] %#  %#.%# ns/op", pLabel, deciNanos / 10, deciNanos % 10 );
    }
} // namespace test
