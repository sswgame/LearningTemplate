// LockKindBenchTest — 잠금 종류 감사(DesignBacklog "잠금 종류 감사(2026-11-20)")의 측정 후보를 스레드 2 · 4 · 8 경쟁에서 전후로 잰다.
#include "pch.h"

#include "Core/Concurrency/Queue/ConcurrentQueue.h"
#include "Core/Concurrency/Queue/LockedSwapQueue.h"
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include <thread>
#include <type_traits>

/**
 * @file TestLockKindBench.cpp
 * @brief 잠금 종류 후보의 전후 비교 벤치입니다. 시간 문턱은 두지 않고 값을 로그(`LockKindBenchTest.*`)로 남긴다 — 판정은 통합이 DesignBacklog 같은 절의 문턱으로 한다.
 * @details 재는 법: Release `App --test --test-run-disabled --test-filter=LockKindBenchTest.*` 5 번, 줄마다 중앙값. 코어 수보다 많은 스레드(8)는 Android 처럼
 *          코어가 적은 기기의 과다 구독도 본다. 값은 한 연산의 평균 ns(스레드 하나의 몫을 끝낸 벽시계 시간 ÷ 몫) — 다투면 커진다.
 *          실제 자리의 잠금을 바꾸지 않고, 같은 모양(구간 길이 · 자료 구조 · 읽기/쓰기 비율)을 따로 만들어 잠금만 갈아 끼운다.
 */
namespace sw
{
    namespace
    {
        struct LockKindBenchInternal
        {
            static constexpr uint32 kArrThreadCount[] = { 2, 4, 8 };

            static uint64 nextRandom( uint64& inoutState )
            {
                inoutState ^= inoutState << 13;
                inoutState ^= inoutState >> 7;
                inoutState ^= inoutState << 17;
                return inoutState;
            }

            /** @brief @p threadCount 스레드가 함께 출발해 @p body( 스레드 번호 ) 를 돌고, 끝난 벽시계 ns 를 돌려줍니다. */
            template <typename FBody>
            static int64 runThreads( uint32 threadCount, FBody&& body )
            {
                atomic<uint32>      readyCount{ 0 };
                atomic<uint8>       bGo{ SW_FALSE };
                vector<std::thread> listThread;
                listThread.reserve( threadCount );
                for ( uint32 threadIndex = 0; threadIndex < threadCount; ++threadIndex )
                {
                    listThread.emplace_back( [&, threadIndex]()
                    {
                        readyCount.fetch_add( 1, std::memory_order_acq_rel );
                        while ( bGo.load( std::memory_order_acquire ) == SW_FALSE )
                        {
                            std::this_thread::yield();
                        }
                        body( threadIndex );
                    } );
                }
                while ( readyCount.load( std::memory_order_acquire ) < threadCount )
                {
                    std::this_thread::yield();
                }
                const Stopwatch watch;
                bGo.store( SW_TRUE, std::memory_order_release );
                for ( std::thread& worker : listThread )
                {
                    worker.join();
                }
                return watch.getElapsedNanoseconds();
            }

            // ---- 1) 읽기가 대부분인 맵 조회(RenderPSOCache · CompressionCodecRegistry · ShaderBindingLayoutCache 의 모양) ----

            /** @brief 맵 하나와 그 잠금입니다. 읽기는 조회 하나, 쓰기는 값 하나 고치기(새 PSO 완성 · 코덱 등록 자리)입니다. */
            template <typename TLock>
            struct LockedMap
            {
                TLock                         _lock;
                unordered_map<uint64, uint64> _map;
            };

            template <typename TLock>
            static uint64 readOnce( LockedMap<TLock>& lockedMap, uint64 key ) SW_NO_THREAD_SAFETY_ANALYSIS
            {
                if constexpr ( std::is_same_v<TLock, shared_mutex> )
                {
                    ScopedSharedLock<shared_mutex> lock{ lockedMap._lock };
                    const auto                     it = lockedMap._map.find( key );
                    return it != lockedMap._map.end() ? it->second : 0;
                }
                else
                {
                    ScopedLock<TLock> lock{ lockedMap._lock };
                    const auto        it = lockedMap._map.find( key );
                    return it != lockedMap._map.end() ? it->second : 0;
                }
            }

            template <typename TLock>
            static void writeOnce( LockedMap<TLock>& lockedMap, uint64 key, uint64 value ) SW_NO_THREAD_SAFETY_ANALYSIS
            {
                ScopedLock<TLock> lock{ lockedMap._lock };
                lockedMap._map.insert_or_assign( key, value );
            }

            /** @brief 맵 크기 @p entryCount, 스레드 @p threadCount, 쓰기 한 번당 읽기 @p readsPerWrite(0 이면 쓰기 없음)로 한 연산의 평균 ns 를 잽니다. */
            template <typename TLock>
            static int64 measureMap( uint32 threadCount, uint32 entryCount, uint32 readsPerWrite )
            {
                static constexpr uint32 kOpsPerThread = 200000;
                LockedMap<TLock>        lockedMap;
                for ( uint32 index = 0; index < entryCount; ++index )
                {
                    lockedMap._map.insert_or_assign( static_cast<uint64>( index ) * 0x9E3779B97F4A7C15ull, index );
                }
                atomic<uint64> checksum{ 0 };
                const int64    elapsed = runThreads( threadCount, [&]( uint32 threadIndex )
                   {
                    uint64 state = 0x2545F4914F6CDD1Dull + threadIndex;
                    uint64 local = 0;
                    for ( uint32 op = 0; op < kOpsPerThread; ++op )
                    {
                        const uint64 key = ( nextRandom( state ) % entryCount ) * 0x9E3779B97F4A7C15ull;
                        if ( readsPerWrite != 0 && ( op % readsPerWrite ) == 0 )
                            writeOnce( lockedMap, key, op );
                        else
                            local += readOnce( lockedMap, key );
                    }
                    checksum.fetch_add( local, std::memory_order_relaxed );
                } );
                SW_EXPECT_TRUE( checksum.load() != ~uint64{ 0 } ); // 조회가 지워지지 않게 쓴다
                return elapsed / static_cast<int64>( kOpsPerThread );
            }

            /** @brief 한 자리 모양을 스레드 2 · 4 · 8 × 쓰기 비율마다 `mutex` · `shared_mutex` · `SpinLock` 으로 재서 로그에 남깁니다. */
            static void reportMap( const utf8* pSiteName, uint32 entryCount, const vector<uint32>& listReadsPerWrite )
            {
                for ( const uint32 threadCount : kArrThreadCount )
                {
                    for ( const uint32 readsPerWrite : listReadsPerWrite )
                    {
                        const int64 mutexNanos  = measureMap<mutex>( threadCount, entryCount, readsPerWrite );
                        const int64 sharedNanos = measureMap<shared_mutex>( threadCount, entryCount, readsPerWrite );
                        const int64 spinNanos   = measureMap<SpinLock>( threadCount, entryCount, readsPerWrite );
                        SW_LOG_INFO_ID( "LockKindBenchTest.ReadMostlyMap", "%# (%# entries) threads=%# readsPerWrite=%#: mutex %# ns, shared_mutex %# ns, SpinLock %# ns per op",
                                        pSiteName, entryCount, threadCount, readsPerWrite, mutexNanos, sharedNanos, spinNanos );
                    }
                }
            }

            // ---- 2) 여러 스레드가 쌓고 하나가 가져가는 큐(LockedSwapQueue) ----

            /** @brief 명령 큐 원소 크기(64 B)입니다 — 작은 람다 명령 · 완료 기록 자리. */
            struct Command64Bytes
            {
                uint64 _arrValue[8]{};
            };

            /** @brief 생산자 @p producerCount 가 쌓고 소비자 하나가 `takeAll` 로 가져갈 때 쌓기 한 번의 평균 ns 를 잽니다. */
            template <typename TLock>
            static int64 measureSwapQueue( uint32 producerCount )
            {
                static constexpr uint32                kPushesPerProducer = 200000;
                LockedSwapQueue<Command64Bytes, TLock> queue;
                atomic<uint32>                         doneProducerCount{ 0 };
                atomic<uint64>                         takenCount{ 0 };
                // 스레드 하나(0 번)가 소비자, 나머지가 생산자다.
                const int64 elapsed = runThreads( producerCount + 1, [&]( uint32 threadIndex )
                {
                    if ( threadIndex == 0 )
                    {
                        vector<Command64Bytes> listTaken;
                        listTaken.reserve( 4096 );
                        for ( ;; )
                        {
                            const bool bDone = doneProducerCount.load( std::memory_order_acquire ) == producerCount;
                            if ( queue.takeAll( listTaken ) )
                            {
                                takenCount.fetch_add( listTaken.size(), std::memory_order_relaxed );
                                listTaken.clear();
                            }
                            else if ( bDone )
                                break;
                            else
                                std::this_thread::yield();
                        }
                        return;
                    }
                    Command64Bytes command;
                    for ( uint32 push = 0; push < kPushesPerProducer; ++push )
                    {
                        command._arrValue[0] = push;
                        queue.push( command );
                    }
                    doneProducerCount.fetch_add( 1, std::memory_order_acq_rel );
                } );
                SW_EXPECT_EQUAL( static_cast<uint64>( producerCount ) * kPushesPerProducer, takenCount.load() );
                return elapsed / static_cast<int64>( kPushesPerProducer );
            }

            // ---- 3) Jolt 잡 큐 모양(MPMC, 넣고 곧 꺼냄) ----

            /** @brief 이번 감사 전의 Jolt 잡 큐 꼴(`SpinLock` + `deque`)입니다. */
            struct SpinDequeQueue
            {
                SpinLock           _lock;
                deque<void*> _list SW_GUARDED_BY( _lock );

                void push( void* pItem )
                {
                    ScopedLock<SpinLock> lock{ _lock };
                    _list.push_back( pItem );
                }
                void* pop()
                {
                    ScopedLock<SpinLock> lock{ _lock };
                    if ( _list.empty() )
                        return nullptr;
                    void* pItem = _list.front();
                    _list.pop_front();
                    return pItem;
                }
            };

            /** @brief 지금의 Jolt 잡 큐 꼴(실행 때 용량 `ConcurrentQueue`, 잠깐 false 는 다시 시도)입니다. */
            class LockFreeQueue
            {
            public:
                explicit LockFreeQueue( uint32 capacity )
                    : _queue{ capacity }
                {
                }

                void push( void* pItem )
                {
                    while ( _queue.enqueue( pItem ) == false )
                    {
                        std::this_thread::yield();
                    }
                }
                void* pop()
                {
                    void* pItem = nullptr;
                    while ( _queue.dequeue( pItem ) == false )
                    {
                        if ( _queue.empty() )
                            return nullptr;
                        std::this_thread::yield();
                    }
                    return pItem;
                }

            private:
                ConcurrentQueue<void*, kRuntimeQueueCapacity> _queue;
            };

            /** @brief 스레드마다 잡 하나를 넣고 아무 잡 하나를 꺼내는 일(태스크 하나가 잡 하나를 꺼내는 꼴)을 되풀이할 때 한 쌍의 평균 ns 를 잽니다. */
            template <typename TQueue>
            static int64 measureJobQueue( TQueue& queue, uint32 threadCount )
            {
                static constexpr uint32 kPairsPerThread = 200000;
                atomic<uint64>          poppedCount{ 0 };
                const int64             elapsed = runThreads( threadCount, [&]( uint32 threadIndex )
                            {
                    uint64 local = 0;
                    for ( uint32 pair = 0; pair < kPairsPerThread; ++pair )
                    {
                        queue.push( reinterpret_cast<void*>( static_cast<uintptr_t>( threadIndex * kPairsPerThread + pair + 1 ) ) );
                        local += queue.pop() != nullptr ? 1u : 0u;
                    }
                    poppedCount.fetch_add( local, std::memory_order_relaxed );
                } );
                // 남은 것을 비운다 — 다른 스레드가 먼저 꺼내 내 몫이 빈 경우가 있어 합만 맞춘다.
                uint64 remainingCount = 0;
                while ( queue.pop() != nullptr )
                {
                    ++remainingCount;
                }
                SW_EXPECT_EQUAL( static_cast<uint64>( threadCount ) * kPairsPerThread, poppedCount.load() + remainingCount );
                return elapsed / static_cast<int64>( kPairsPerThread );
            }
        };
    } // namespace
} // namespace sw

using namespace sw;

/**
 * @brief [LockKindBenchTest] RenderPSOCache 의 `findMaterialPSO` · `findLayout` 모양 — 4096 항목, 읽기만 · 1000 · 100 읽기마다 쓰기 하나.
 * @details 문턱(DesignBacklog 같은 절): 쓰기 비율 1/1000 · 1/100 에서 4 · 8 스레드의 shared_mutex 가 mutex 보다 20 % 이상 빠르고 2 스레드가 5 % 넘게 느리지 않으면 바꾼다.
 */
SW_TEST_CASE( LockKindBenchTest, RenderPSOCacheLookup, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 600 )
{
    LockKindBenchInternal::reportMap( "RenderPSOCache", 4096, vector<uint32>{ 0, 1000, 100 } );
}

/** @brief [LockKindBenchTest] CompressionCodecRegistry 의 `getCodec` 모양 — 8 항목, 읽기만(등록은 시작 때뿐). 문턱은 RenderPSOCacheLookup 과 같다(쓰기 없음 칸으로). */
SW_TEST_CASE( LockKindBenchTest, CodecRegistryLookup, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 600 )
{
    LockKindBenchInternal::reportMap( "CompressionCodecRegistry", 8, vector<uint32>{ 0 } );
}

/** @brief [LockKindBenchTest] ShaderBindingLayoutCache 의 `getOrBuild` 적중 모양 — 512 항목, 읽기만 · 100 · 10 읽기마다 쓰기 하나(셰이더 리로드 · 첫 빌드). 문턱은 같다. */
SW_TEST_CASE( LockKindBenchTest, ShaderBindingLayoutLookup, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 600 )
{
    LockKindBenchInternal::reportMap( "ShaderBindingLayoutCache", 512, vector<uint32>{ 0, 100, 10 } );
}

/**
 * @brief [LockKindBenchTest] LockedSwapQueue 쌓기 한 번 — 생산자 2 · 4 · 8 + 소비자 하나, 64 B 원소, 기본 `mutex` 와 `SpinLock`.
 * @details 문턱: 4 · 8 생산자에서 SpinLock 이 10 % 이상 빠르고 2 생산자에서 느리지 않으면 기본 잠금을 `SpinLock` 으로 바꾼다(교착 검출에서 빠지는 대가를 받는다).
 */
SW_TEST_CASE( LockKindBenchTest, LockedSwapQueuePush, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 600 )
{
    using Internal = LockKindBenchInternal;
    for ( const uint32 producerCount : Internal::kArrThreadCount )
    {
        const int64 mutexNanos = Internal::measureSwapQueue<mutex>( producerCount );
        const int64 spinNanos  = Internal::measureSwapQueue<SpinLock>( producerCount );
        SW_LOG_INFO_ID( "LockKindBenchTest.LockedSwapQueuePush", "producers=%#: mutex %# ns, SpinLock %# ns per push", producerCount, mutexNanos, spinNanos );
    }
}

/**
 * @brief [LockKindBenchTest] Jolt 잡 큐 넣기 + 꺼내기 한 쌍 — 스레드 2 · 4 · 8, 이전 꼴(`SpinLock` + `deque`)과 지금 꼴(실행 때 용량 `ConcurrentQueue`, 용량 2048 = `JoltPhysicsBackend` 의 `kMaxJobCount`).
 * @details 문턱: 어느 스레드 수에서든 ConcurrentQueue 가 10 % 넘게 느리면 `SpinLock` + `deque` 로 되돌린다.
 */
SW_TEST_CASE( LockKindBenchTest, JoltJobQueue, ._tags = "bench", ._disabledReason = "bench - Release 에서 --test-run-disabled 로 돌린다", ._timeoutSeconds = 600 )
{
    using Internal = LockKindBenchInternal;
    for ( const uint32 threadCount : Internal::kArrThreadCount )
    {
        Internal::SpinDequeQueue spinQueue;
        Internal::LockFreeQueue  lockFreeQueue{ 2048 };
        const int64              spinNanos     = Internal::measureJobQueue( spinQueue, threadCount );
        const int64              lockFreeNanos = Internal::measureJobQueue( lockFreeQueue, threadCount );
        SW_LOG_INFO_ID( "LockKindBenchTest.JoltJobQueue", "threads=%#: SpinLock+deque %# ns, ConcurrentQueue %# ns per push+pop", threadCount, spinNanos, lockFreeNanos );
    }
}
