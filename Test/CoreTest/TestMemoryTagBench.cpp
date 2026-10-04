/**
 * @file TestMemoryTagBench.cpp
 * @brief 메모리 태그 추적의 비용 — 같은 할당 · 해제를 추적을 끄고 · 켜고 · 태그 스코프 안에서 잰다.
 * @details 숫자를 **찍기만** 한다(기계마다 다르다). Release 로 읽는다 — Debug 는 할당 헤더 검사 · 레이스 탐지가 숫자를 바꾼다.
 *          꺼진 길은 할당마다 `recordAllocation` 의 분기 하나다(배포본에는 헤더도 프로파일러도 없다).
 */
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryProfiler.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "MemoryTagBench" );

namespace
{
    struct MemoryTagBenchInternal
    {
        /** @brief 한 판의 할당 · 해제 짝 수입니다. */
        static constexpr uint64 kPairCount = 200000;
        /** @brief 판 수 — 가장 짧은 판을 쓴다. */
        static constexpr uint32 kBenchRoundCount = 5;

        /** @brief 64 바이트를 잡고 바로 놓는 일을 `kPairCount` 번 합니다. */
        static void allocateAndFree()
        {
            for ( uint64 index = 0; index < kPairCount; ++index )
            {
                void* pBlock = sw::Memory::allocate( 64 );
                sw::Memory::free( pBlock );
            }
        }

        /** @brief 같은 일을 태그 스코프 안에서 합니다(스코프 진입 · 이탈은 바깥 한 번). */
        static void allocateAndFreeInScope()
        {
            SW_MEMORY_SCOPE( Texture );
            allocateAndFree();
        }
    };
} // namespace

/**
 * @brief [MemoryTagBenchTest] 할당 · 해제 한 짝의 비용 — 추적 꺼짐 · 켜짐 · 켜짐 + 태그 스코프
 */
SW_TEST_CASE( MemoryTagBenchTest, AllocationOverheadWithTrackingOffAndOn )
{
    sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr )
        SW_TEST_SKIP( "no memory profiler in this configuration (Shipping)" );
    const bool bWasTracking = pProfiler->isTrackingEnabled();

    pProfiler->setTrackingEnabled( false );
    const int64 offDeciNanos = test::measureBestDeciNanosPerOp( MemoryTagBenchInternal::kPairCount, MemoryTagBenchInternal::kBenchRoundCount,
                                                                MemoryTagBenchInternal::allocateAndFree );
    pProfiler->setTrackingEnabled( true );
    const int64 onDeciNanos      = test::measureBestDeciNanosPerOp( MemoryTagBenchInternal::kPairCount, MemoryTagBenchInternal::kBenchRoundCount,
                                                                    MemoryTagBenchInternal::allocateAndFree );
    const int64 onScopeDeciNanos = test::measureBestDeciNanosPerOp( MemoryTagBenchInternal::kPairCount, MemoryTagBenchInternal::kBenchRoundCount,
                                                                    MemoryTagBenchInternal::allocateAndFreeInScope );
    pProfiler->setTrackingEnabled( bWasTracking );

    test::logBenchDeciNanos( "allocate+free 64 B, tracking off", offDeciNanos );
    test::logBenchDeciNanos( "allocate+free 64 B, tracking on", onDeciNanos );
    test::logBenchDeciNanos( "allocate+free 64 B, tracking on + tag scope", onScopeDeciNanos );
    SW_EXPECT_TRUE( offDeciNanos > 0 && onDeciNanos > 0 );
}
