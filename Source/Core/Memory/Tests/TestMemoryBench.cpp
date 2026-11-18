// MemoryBenchTest — 할당 · 해제 한 번의 비용을 스레드 수 · 크기 · 해제 모양마다 잰다(감사 C-6, 설계 AC-1 걸음 ①).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/MemoryBackend.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include <thread>

/**
 * @file TestMemoryBench.cpp
 * @brief 힙 백엔드(`MemoryBackend::allocate` · `free`) 한 번의 비용(ns)입니다. 시간 문턱은 두지 않고 값을 로그(`MemoryBenchTest.AllocateFreeCost`)로 남긴다.
 * @details 재는 법: Release `App --test --test-run-disabled --test-filter=MemoryBenchTest.*` 5 번, 줄마다 중앙값.
 *          판정(설계 "출하 할당기와 컨테이너 복잡도" ①): 시나리오의 프레임당 할당 수 × 이 ns 가 프레임의 3 % 이상이거나, 8 스레드 ns 가 1 스레드의 3 배 이상이면서
 *          워커 할당이 프레임당 1000 번 이상이면 ② 로 간다.
 *          `Memory` 입구가 아니라 백엔드를 바로 부른다 — 배포본이 아닌 구성의 할당 헤더 · 기록기 확인을 빼고 할당기 자체를 견준다(`SW_MEMORY_BACKEND` 선택의 근거).
 */
namespace sw
{
    namespace
    {
        /** @brief 해제 모양입니다. */
        enum class FreeShape : uint8
        {
            SameThread,  ///< 잡은 스레드가 곧 푼다
            OtherThread, ///< 다른 스레드가 푼다(작업 큐 · 메시지가 넘어가는 자리)
            LiveRandom,  ///< 1000 개를 살려 두고 무작위 하나를 풀고 다시 잡는다(조각난 힙)
        };

        struct MemoryBenchInternal
        {
            static constexpr uint32 kOpsPerThread = 100000;
            static constexpr uint32 kLiveCount    = 1000;

            static uint64 nextRandom( uint64& inoutState )
            {
                inoutState ^= inoutState << 13;
                inoutState ^= inoutState >> 7;
                inoutState ^= inoutState << 17;
                return inoutState;
            }

            /** @brief 스레드 하나의 몫을 돕니다. @p listBlock 은 다른 스레드에 넘길 블록(OtherThread)을 받는 칸입니다. */
            static void runShare( FreeShape shape, size_t size, uint64 seed, vector<void*>& listBlock )
            {
                if ( shape == FreeShape::SameThread )
                {
                    for ( uint32 op = 0; op < kOpsPerThread; ++op )
                    {
                        MemoryBackend::free( MemoryBackend::allocate( size ) );
                    }
                    return;
                }
                if ( shape == FreeShape::OtherThread )
                {
                    listBlock.resize( kOpsPerThread );
                    for ( void*& pBlock : listBlock )
                    {
                        pBlock = MemoryBackend::allocate( size );
                    }
                    return;
                }
                vector<void*> listLive( kLiveCount );
                for ( void*& pBlock : listLive )
                {
                    pBlock = MemoryBackend::allocate( size );
                }
                uint64 state = seed;
                for ( uint32 op = 0; op < kOpsPerThread; ++op )
                {
                    void*& pSlot = listLive[nextRandom( state ) % kLiveCount];
                    MemoryBackend::free( pSlot );
                    pSlot = MemoryBackend::allocate( size );
                }
                for ( void* pBlock : listLive )
                {
                    MemoryBackend::free( pBlock );
                }
            }

            /** @brief @p threadCount 스레드로 한 번 재고 할당 · 해제 한 쌍의 평균 ns 를 돌려줍니다. */
            static int64 measure( uint32 threadCount, size_t size, FreeShape shape )
            {
                vector<vector<void*>> listShare( threadCount );
                const Stopwatch       watch;
                {
                    vector<std::thread> listThread;
                    for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
                    {
                        listThread.emplace_back( [shape, size, threadIndex, &listShare]
                        { runShare( shape, size, 0x9E3779B97F4A7C15ull + threadIndex, listShare[threadIndex] ); } );
                    }
                    for ( std::thread& worker : listThread )
                    {
                        worker.join();
                    }
                }
                if ( shape == FreeShape::OtherThread )
                {
                    // 스레드 i 가 잡은 블록을 스레드 i + 1 이 푼다(스레드 하나면 새 스레드 하나가 푼다).
                    vector<std::thread> listThread;
                    for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
                    {
                        vector<void*>& listBlock = listShare[( threadIndex + 1 ) % threadCount];
                        listThread.emplace_back( [&listBlock]
                        {
                            for ( void* pBlock : listBlock )
                            {
                                MemoryBackend::free( pBlock );
                            } } );
                    }
                    for ( std::thread& worker : listThread )
                    {
                        worker.join();
                    }
                }
                // 스레드를 세우는 시간이 들어가지만 몫(10 만 쌍)에 비해 작다.
                return watch.getElapsedNanoseconds() / static_cast<int64>( kOpsPerThread );
            }
        };
    } // namespace
} // namespace sw

using namespace sw;

/**
 * @brief [MemoryBenchTest] 스레드 1 · 4 · 8 × 16 · 64 · 256 · 4096 B × 같은 스레드 풀기 · 다른 스레드 풀기 · 살아 있는 1000 개 무작위의 한 쌍 비용(ns)입니다.
 * @details 값은 스레드 하나의 몫(10 만 쌍)을 끝내는 벽시계 시간 ÷ 10 만 — 스레드가 다투면 1 스레드보다 커진다.
 */
SW_TEST_CASE( MemoryBenchTest, AllocateFreeCost, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 600 )
{
    static constexpr uint32      kArrThreadCount[] = { 1, 4, 8 };
    static constexpr size_t      kArrSize[]        = { 16, 64, 256, 4096 };
    static constexpr FreeShape   kArrShape[]       = { FreeShape::SameThread, FreeShape::OtherThread, FreeShape::LiveRandom };
    static constexpr const utf8* kArrShapeName[]   = { "same-thread", "other-thread", "live-random" };
    uint32                       measuredCount     = 0;
    for ( size_t shapeIndex = 0; shapeIndex < std::size( kArrShape ); ++shapeIndex )
    {
        for ( const size_t size : kArrSize )
        {
            for ( const uint32 threadCount : kArrThreadCount )
            {
                const int64 nanosPerPair = MemoryBenchInternal::measure( threadCount, size, kArrShape[shapeIndex] );
                SW_LOG_INFO_ID( "MemoryBenchTest.AllocateFreeCost", "allocate+free %# B, %# threads, %#: %# ns per pair", size, threadCount, kArrShapeName[shapeIndex],
                                nanosPerPair );
                ++measuredCount;
            }
        }
    }
    SW_EXPECT_EQUAL( measuredCount, static_cast<uint32>( std::size( kArrShape ) * std::size( kArrSize ) * std::size( kArrThreadCount ) ) );
}
