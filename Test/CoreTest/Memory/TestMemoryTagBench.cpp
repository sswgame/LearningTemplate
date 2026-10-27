/**
 * @file TestMemoryTagBench.cpp
 * @brief 메모리 태그 추적의 비용 — 같은 할당 · 해제를 추적을 끄고 · 켜고 · 태그 스코프 안에서 측정한다.
 * @details 숫자를 **찍기만** 한다(기계마다 다르다). Release 로 읽는다 — Debug 는 할당 헤더 검사 · 레이스 탐지가 숫자를 바꾼다.
 *          꺼진 길은 할당마다 `recordAllocation` 의 분기 하나다(배포본에는 헤더도 프로파일러도 없다).
 *          여러 스레드 케이스는 태그 통계 원자 값의 경합을 본다(구조 최적화 A ③, 표는 docs/decisions/5-3_CorePlatform.md).
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( MemoryTagBenchTest, "allocation overhead with memory tag tracking off and on" );
SW_TEST_TIME_BUDGET( MemoryTagBenchTest, 1000, "Release 케이스 최대 143 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

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

        /** @brief 여러 스레드 케이스에서 스레드 하나가 한 판에 하는 할당 · 해제 짝 수입니다. */
        static constexpr uint32 kConcurrentPairCount = 20000;
        /** @brief 여러 스레드 케이스의 판 수입니다(p50 · p99 를 본다). */
        static constexpr uint32 kConcurrentRoundCount = 11;

        /** @brief 스레드 하나의 몫 — @p tag 스코프 안에서 64 바이트를 `kConcurrentPairCount` 번 잡고 놓습니다. */
        static void allocateAndFreeWithTag( sw::MemoryTag tag )
        {
            const sw::ScopedMemoryTag scope{ tag };
            for ( uint32 index = 0; index < kConcurrentPairCount; ++index )
            {
                void* pBlock = sw::Memory::allocate( 64 );
                sw::Memory::free( pBlock );
            }
        }

        /**
         * @brief 스레드 @p threadCount 개가 동시에 할당 · 해제하는 판을 `kConcurrentRoundCount` 번 돌려 판마다의 벽시계(us)를 담습니다.
         * @param bTagPerThread true 면 스레드마다 다른 태그(이웃 태그 통계의 캐시 라인 공유를 본다), false 면 모두 같은 태그(한 통계에 경합).
         */
        static void measureConcurrentRounds( uint32 threadCount, bool bTagPerThread, sw::vector<int64>& outListRound )
        {
            outListRound.clear();
            for ( uint32 round = 0; round < kConcurrentRoundCount; ++round )
            {
                sw::atomic<uint32>      readyCount{ 0 };
                sw::atomic<bool>        bGo{ false };
                sw::vector<std::thread> listThread;
                listThread.reserve( threadCount );
                for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
                {
                    // 스레드마다 다른 태그면 `Unknown` 다음부터 차례로 고른다(태그 수를 넘으면 돌아 쓴다).
                    const uint32        tagIndex = bTagPerThread ? 1 + ( threadIndex % ( sw::kMemoryTagCount - 1 ) ) : static_cast<uint32>( sw::MemoryTag::Texture );
                    const sw::MemoryTag tag      = static_cast<sw::MemoryTag>( tagIndex );
                    listThread.emplace_back( [&readyCount, &bGo, tag]()
                    {
                        readyCount.fetch_add( 1, std::memory_order_acq_rel );
                        while ( bGo.load( std::memory_order_acquire ) == false )
                        {
                            sw::cpuPause();
                        }
                        allocateAndFreeWithTag( tag );
                    } );
                }
                while ( readyCount.load( std::memory_order_acquire ) < threadCount )
                {
                    std::this_thread::yield();
                }
                const sw::Stopwatch stopwatch;
                bGo.store( true, std::memory_order_release );
                for ( std::thread& thread : listThread )
                {
                    thread.join();
                }
                outListRound.push_back( stopwatch.getElapsedMicroseconds() );
            }
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

/**
 * @brief [MemoryTagBenchTest] 여러 스레드가 동시에 할당 · 해제할 때 — 같은 태그(한 통계에 경합) · 스레드마다 다른 태그(이웃 통계의 캐시 라인 공유), 추적 꺼짐 · 켜짐(구조 최적화 A ③)
 */
SW_TEST_CASE( MemoryTagBenchTest, ConcurrentAllocationContention )
{
    sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr )
        SW_TEST_SKIP( "no memory profiler in this configuration (Shipping)" );
    const bool bWasTracking = pProfiler->isTrackingEnabled();
    // 게임 · 렌더 스레드 몫을 뺀 워커 수와 같게 — 실제로 동시에 할당하는 스레드 수다.
    const uint32 processorCount = sw::thread::getUsableProcessorCount();
    const uint32 threadCount    = processorCount > 4 ? processorCount - 2 : 2;
    SW_LOG_INFO( "[Bench] concurrent allocation threads %#, %# pairs each per round", threadCount, MemoryTagBenchInternal::kConcurrentPairCount );

    sw::vector<int64> listRound;
    pProfiler->setTrackingEnabled( false );
    MemoryTagBenchInternal::measureConcurrentRounds( threadCount, false, listRound );
    test::logBenchSamples( "concurrent allocate+free 64 B, tracking off (us per round)", listRound );

    pProfiler->setTrackingEnabled( true );
    MemoryTagBenchInternal::measureConcurrentRounds( threadCount, false, listRound );
    test::logBenchSamples( "concurrent allocate+free 64 B, tracking on, same tag (us per round)", listRound );
    MemoryTagBenchInternal::measureConcurrentRounds( threadCount, true, listRound );
    test::logBenchSamples( "concurrent allocate+free 64 B, tracking on, tag per thread (us per round)", listRound );
    pProfiler->setTrackingEnabled( bWasTracking );

    SW_EXPECT_EQUAL( static_cast<size_t>( MemoryTagBenchInternal::kConcurrentRoundCount ), listRound.size() );
}
