/**
 * @file SampleStatistics.h
 * @brief 표본 통계의 최근접 순위(nearest-rank) 백분위입니다. 프로파일러 · 자동화 프레임 통계 · 부하 봇 · 텔레메트리가 같은 규칙으로 고른다.
 * @details 순위는 1 부터 세고 올림한다(`ceil( percent * count / 100 )`, 정수 산술). 100 프레임의 p99 는 99 번째, p50 은 50 번째다. 층마다 따로 두면
 *          빈 표본 · 0 백분위 · 부동소수 올림이 곳마다 달라 같은 표본의 지표가 갈린다. 언리얼 · 유니티는 이 계산을 도구마다 따로 두지만 여기서는 Core 한 곳이다.
 *          컨테이너 층(Container)보다 아래라 정렬된 범위(`size()` 와 `operator[]` 가 있는 것)를 형식 인자로 받는다.
 */
#pragma once
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 표본 통계입니다. */
    struct SampleStatistics
    {
        /** @brief 백분위 최댓값입니다. */
        static constexpr uint32 kPercentMax = 100;
        /** @brief 중앙값(p50) 백분위입니다. */
        static constexpr uint32 kPercentMedian = 50;
        /** @brief 높은 꼬리(p95) 백분위입니다. */
        static constexpr uint32 kPercentHigh = 95;
        /** @brief 끝 꼬리(p99) 백분위입니다. */
        static constexpr uint32 kPercentTail = 99;

        /**
         * @brief 표본 @p sampleCount 개 중 @p percent 백분위의 최근접 순위(1 부터)입니다. 표본이 없으면 0 이고, 있으면 [1, @p sampleCount] 로 자릅니다.
         * @details @p percent 가 100 을 넘으면 100 으로 본다. 0 백분위는 첫 표본(순위 1)이다.
         */
        [[nodiscard]] static constexpr uint64 getNearestRank( uint64 sampleCount, uint32 percent )
        {
            if ( sampleCount == 0 )
                return 0;
            const uint64 clampedPercent = percent < kPercentMax ? percent : kPercentMax;
            const uint64 rank           = ( sampleCount * clampedPercent + ( kPercentMax - 1 ) ) / kPercentMax;
            return rank == 0 ? 1 : rank;
        }

        /** @brief 오름차순으로 정렬된 @p listSorted 의 @p percent 백분위 값(최근접 순위)입니다. 비어 있으면 빈 값입니다. */
        template <typename TSortedRange>
        [[nodiscard]] static auto findNearestRankPercentile( const TSortedRange& listSorted, uint32 percent ) -> optional<std::decay_t<decltype( listSorted[0] )>>
        {
            const uint64 rank = getNearestRank( static_cast<uint64>( listSorted.size() ), percent );
            if ( rank == 0 )
                return nullopt;
            return listSorted[static_cast<size_t>( rank - 1 )];
        }
    };
} // namespace sw
