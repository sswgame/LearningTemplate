/**
 * @file Test/TestFramework/TestBench.h
 * @brief 벤치 케이스(`XxxBenchTest`)가 함께 쓰는 시간 재기 · 표본 요약 · 한 줄 보고.
 */
#pragma once
#include "Core/Time/CpuClock.h"

#include "Engine/EngineMinimal.h"

#include <limits>

namespace test
{
    /** @brief 표본의 백분위 값 — 표본을 그 자리에서 정렬한다. 비어 있으면 0. */
    int64 getPercentile( sw::vector<int64>& listSample, uint32 percent );

    /**
     * @brief `[Bench] 이름  min · p50 · p90 · max (표본 수)` 한 줄을 찍습니다(마이크로초 표본).
     * @details Shipping 은 Info 로그가 컴파일에서 빠져 값만 계산하고 만다. 벤치마다 같은 모양으로 찍어야 두 벤치의 줄을 나란히 놓고 견줄 수 있다.
     */
    void logBenchSamples( const utf8* pLabel, sw::vector<int64>& listSample );

    /**
     * @brief @p body 를 @p roundCount 번 돌려 **가장 짧은 판**의 연산당 ns 를 10 배 정수로 돌려줍니다(소수 한 자리).
     * @details 최솟값을 쓰는 것은 스케줄러 · 캐시가 끼어든 판을 버리려는 것이다 — 한 판의 연산 수(@p opCount)가 충분히 커야 뜻이 있다.
     */
    template <typename TBody>
    int64 measureBestDeciNanosPerOp( uint64 opCount, uint32 roundCount, TBody&& body )
    {
        int64 bestNanos = std::numeric_limits<int64>::max();
        for ( uint32 round = 0; round < roundCount; ++round )
        {
            const sw::CpuStopwatch stopwatch;
            body();
            const int64 nanos = stopwatch.getElapsedNanoseconds();
            bestNanos         = nanos < bestNanos ? nanos : bestNanos;
        }
        return ( bestNanos * 10 ) / static_cast<int64>( opCount == 0 ? 1 : opCount );
    }

    /** @brief `[Bench] 이름  x.y ns/op` 한 줄(`measureBestDeciNanosPerOp` 의 값). */
    void logBenchDeciNanos( const utf8* pLabel, int64 deciNanos );
} // namespace test
