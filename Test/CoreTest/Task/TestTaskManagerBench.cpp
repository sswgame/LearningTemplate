/**
 * @file TestTaskManagerBench.cpp
 * @brief TaskManager 마이크로벤치 — 디스패치 바닥 · 포크-조인 지연 · 작은 태스크 처리량 · CPU 바운드 배속.
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(기계마다 다르다). 회귀의 근거는 `docs/08_Verification.md` 에
 *          Release 로 측정한 표로 남긴다 — 이 케이스는 그 표를 같은 코드로 다시 만들기 위한 자리다.
 *          판정은 정합성만 본다(모든 인덱스가 한 번씩 · 모든 태스크가 돌았다).
 *
 *          측정하는 것:
 *          - 잠든 워커를 깨워 포크-조인 하나를 끝내는 시간(`runParallel`, 본문은 거의 비어 있다) — 디스패치 바닥.
 *          - 연달아 부를 때의 포크-조인 시간 — 정상 상태의 디스패치 비용.
 *          - 렌더 그래프 모양(스테이지 + High 태스크 몇 개 + waitStage).
 *          - 작은 독립 태스크 수천 개의 처리량(태스크당 ns).
 *          - CPU 바운드 본문의 직렬 대 병렬 배속(워커 + 호출 스레드가 전부 일하는가).
 *          - 빈 데크의 pop · steal 한 번(훔치기 한 바퀴의 바닥, 구조 최적화 A ①).
 *          - 바깥 스레드 둘(게임 · 렌더 흉내)이 동시에 포크-조인을 기다릴 때(대기자 스핀, A ②).
 *          - 워커가 태스크를 만들고 끝내는 나무(활성 태스크 수를 모든 스레드가 올리고 내림, A ④).
 *          구조 최적화 단위와 벤치의 짝, 전/후 결과 표는 `docs/plans/StructuralOptimization.md`.
 *
 * @note Release 로 읽는다. Debug 의 컨테이너 레이스 탐지기·프로파일러가 숫자를 다른 것으로 만든다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/WorkStealingDeque.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( TaskManagerBenchTest, "task manager dispatch floor, fork-join latency, throughput and speedup" );
SW_TEST_TIME_BUDGET( TaskManagerBenchTest, 13000, "Release 케이스 최대 3162 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배 — CI 러너 여유" );

SW_LOG_CALLER( "TaskManagerBench" );

namespace
{
    /** @brief 잠든 워커를 재우는 데 쓰는 틈. 워커의 스핀 예산(수 us)보다 넉넉히 길다. */
    constexpr uint32 kSleepGapMicro = 2000;

    /** @brief 계산 본문이 남기는 값 — 최적화기가 루프를 지우지 못하게 원자에 더한다. */
    sw::atomic<uint32> s_workSink{ 0 };

    /** @brief 세는 표 — 워커 안에서는 `data()` 로 받은 주소만 만진다(sw::vector 첨자의 레이스 탐지기를 피한다). */
    sw::atomic<uint32>* s_pHit{ nullptr };

    /** @brief 작은 태스크 처리량 케이스의 실행 수. */
    sw::atomic<uint32> s_ranCount{ 0 };

    /** @brief 백그라운드 I/O 흉내 태스크 중 아직 끝나지 않은 수. */
    sw::atomic<uint32> s_lowInFlight{ 0 };

    /**
     * @brief 요소 하나에 @p iterationCount 만큼의 의존 연산(xorshift)을 돌립니다 — 요소당 시간을 고르게 만든다.
     */
    uint32 spinWork( uint32 seed, uint32 iterationCount )
    {
        uint32 value = seed | 1u;
        for ( uint32 iteration = 0; iteration < iterationCount; ++iteration )
        {
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
        }
        return value;
    }

    struct BenchBody
    {
        /** @brief 거의 빈 본문 — 인덱스마다 표에 한 번 찍는다. 디스패치 비용만 남긴다. */
        static void touchRange( uint32 start, uint32 end )
        {
            for ( uint32 index = start; index < end; ++index )
            {
                s_pHit[index].fetch_add( 1, std::memory_order_relaxed );
            }
        }

        /** @brief 요소당 약 1 us 의 계산 본문. */
        static void computeRange( uint32 start, uint32 end )
        {
            uint32 accumulated = 0;
            for ( uint32 index = start; index < end; ++index )
            {
                accumulated += spinWork( index, 1500 );
            }
            s_workSink.fetch_add( accumulated, std::memory_order_relaxed );
        }

        /** @brief 렌더 패스 기록 하나의 흉내 — 20 us 쯤 걸리는 태스크. */
        static void recordPass()
        {
            s_workSink.fetch_add( spinWork( 7, 30000 ), std::memory_order_relaxed );
        }

        /** @brief 작은 독립 태스크. */
        static void tiny()
        {
            s_ranCount.fetch_add( 1, std::memory_order_relaxed );
        }

        /** @brief 나무의 가지 — 자식 `kTreeLeafPerBranch` 개를 만든다(본문 안에서 만들었으니 이 태스크의 자식이다). */
        static void treeBranch()
        {
            s_ranCount.fetch_add( 1, std::memory_order_relaxed );
            sw::TaskManager& manager = *s_pTreeManager;
            for ( uint32 leaf = 0; leaf < kTreeLeafPerBranch; ++leaf )
            {
                sw::TaskHandle handle = manager.emplaceTask( "BenchLeaf", SW_DELEGATE_FUNCTION( sw::TaskDelegate, BenchBody::tiny ) );
                manager.submitWithoutWake( handle );
            }
            manager.wakeSleepingWorkers( kTreeLeafPerBranch );
        }

        /** @brief 나무 가지 하나가 만드는 잎 수입니다. */
        static constexpr uint32 kTreeLeafPerBranch = 32;
        /** @brief 나무 케이스가 쓰는 매니저(가지 본문이 잎을 만든다). */
        static inline sw::TaskManager* s_pTreeManager{ nullptr };

        /** @brief 백그라운드 I/O 흉내 — 2 ms 를 바쁘게 돈다(잠들면 워커가 비어 그 자리를 다른 일이 채운다). */
        static void backgroundIO()
        {
            const sw::Stopwatch stopwatch;
            while ( stopwatch.getElapsedMicroseconds() < 2000 )
            {
                sw::cpuPause();
            }
            s_lowInFlight.fetch_sub( 1, std::memory_order_relaxed );
        }
    };

    /** @brief 표를 @p count 칸으로 다시 잡고 0 으로 채웁니다. */
    void resetHitTable( sw::vector<sw::atomic<uint32>>& listHit, uint32 count )
    {
        listHit.clear();
        listHit.resize( count );
        s_pHit = listHit.data();
        for ( uint32 index = 0; index < count; ++index )
        {
            s_pHit[index].store( 0, std::memory_order_relaxed );
        }
    }

    /** @brief 표의 모든 칸이 정확히 @p expected 인지. */
    uint32 countWrongHits( uint32 count, uint32 expected )
    {
        uint32 wrongCount = 0;
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( s_pHit[index].load( std::memory_order_relaxed ) != expected )
                ++wrongCount;
        }
        return wrongCount;
    }
} // namespace

/**
 * @brief [TaskManagerBenchTest] 포크-조인 지연 — 잠든 워커를 깨울 때와 연달아 부를 때
 */
SW_TEST_CASE( TaskManagerBenchTest, ForkJoinLatency )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    SW_LOG_INFO( "[Bench] workers %#", manager.getWorkerCount() );

    constexpr uint32               kCount     = 4096;
    constexpr uint32               kColdRound = 100;
    constexpr uint32               kHotRound  = 2000;
    sw::vector<sw::atomic<uint32>> listHit;
    resetHitTable( listHit, kCount );
    const sw::ParallelBlockDelegate body = SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, BenchBody::touchRange );

    // 워밍업 — 풀 슬랩·스테이지 노드는 처음 한 번만 잡는다.
    for ( uint32 round = 0; round < 16; ++round )
    {
        manager.runParallel( kCount, 1, body );
    }

    sw::vector<int64> listCold;
    listCold.reserve( kColdRound );
    for ( uint32 round = 0; round < kColdRound; ++round )
    {
        std::this_thread::sleep_for( std::chrono::microseconds( kSleepGapMicro ) );
        const sw::Stopwatch stopwatch;
        manager.runParallel( kCount, 1, body );
        listCold.push_back( stopwatch.getElapsedMicroseconds() );
    }
    test::logBenchSamples( "forkJoin cold (workers asleep), 4096 x touch", listCold );

    sw::vector<int64> listHot;
    listHot.reserve( kHotRound );
    for ( uint32 round = 0; round < kHotRound; ++round )
    {
        const sw::Stopwatch stopwatch;
        manager.runParallel( kCount, 1, body );
        listHot.push_back( stopwatch.getElapsedMicroseconds() );
    }
    test::logBenchSamples( "forkJoin hot (back to back), 4096 x touch", listHot );

    SW_EXPECT_EQUAL( 0u, countWrongHits( kCount, 16 + kColdRound + kHotRound ) );
    manager.shutdown();
}

/**
 * @brief [TaskManagerBenchTest] 백그라운드 I/O(Low 줄)가 늘 차 있을 때의 포크-조인 — 합류 대기가 Low 를 도우면 최악이 I/O 태스크 하나만큼 는다
 */
SW_TEST_CASE( TaskManagerBenchTest, ForkJoinUnderBackgroundIO )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    constexpr uint32                kCount    = 256; // 요소당 ~1 us
    constexpr uint32                kRound    = 300;
    const uint32                    lowTarget = manager.getWorkerCount();
    const sw::ParallelBlockDelegate body      = SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, BenchBody::computeRange );

    sw::vector<int64> listSample;
    listSample.reserve( kRound );
    for ( uint32 round = 0; round < kRound; ++round )
    {
        while ( s_lowInFlight.load( std::memory_order_relaxed ) < lowTarget )
        {
            s_lowInFlight.fetch_add( 1, std::memory_order_relaxed );
            sw::TaskHandle low = manager.emplaceTask( "BenchBackgroundIO", SW_DELEGATE_FUNCTION( sw::TaskDelegate, BenchBody::backgroundIO ) );
            low.setPriority( sw::TaskPriority::Low );
            low.submit();
        }
        const sw::Stopwatch stopwatch;
        manager.runParallel( kCount, 1, body );
        listSample.push_back( stopwatch.getElapsedMicroseconds() );
    }
    test::logBenchSamples( "forkJoin under background io, 256 x ~1us", listSample );

    SW_EXPECT_TRUE( manager.waitAll( 10000 ) );
    manager.shutdown();
}

/**
 * @brief [TaskManagerBenchTest] 렌더 그래프 모양 — 스테이지에 High 태스크 넷을 넣고 한 번 깨워 기다린다
 */
SW_TEST_CASE( TaskManagerBenchTest, StageLikeRenderGraphLevel )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    constexpr uint32 kPassCount = 4;
    constexpr uint32 kRound     = 200;

    // 패스 하나의 본문 시간 — 병렬 레벨의 이상적인 값이다.
    sw::vector<int64> listSerialPass;
    for ( uint32 round = 0; round < 20; ++round )
    {
        const sw::Stopwatch stopwatch;
        BenchBody::recordPass();
        listSerialPass.push_back( stopwatch.getElapsedMicroseconds() );
    }
    test::logBenchSamples( "one pass body (ideal level time)", listSerialPass );

    const sw::TaskDelegate record    = SW_DELEGATE_FUNCTION( sw::TaskDelegate, BenchBody::recordPass );
    auto                   levelOnce = [&manager, &record]()
    {
        sw::TaskStageHandle stage = manager.createStage();
        for ( uint32 pass = 0; pass < kPassCount; ++pass )
        {
            sw::TaskHandle handle = manager.emplaceTask( "BenchPass", record );
            handle.setPriority( sw::TaskPriority::High );
            stage.addTask( handle );
            manager.submitWithoutWake( handle );
        }
        manager.wakeSleepingWorkers( kPassCount );
        manager.waitStage( stage );
    };

    for ( uint32 round = 0; round < 16; ++round )
    {
        levelOnce();
    }

    sw::vector<int64> listCold;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        std::this_thread::sleep_for( std::chrono::microseconds( kSleepGapMicro ) );
        const sw::Stopwatch stopwatch;
        levelOnce();
        listCold.push_back( stopwatch.getElapsedMicroseconds() );
    }
    test::logBenchSamples( "stage level cold, 4 x High pass", listCold );

    sw::vector<int64> listHot;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        const sw::Stopwatch stopwatch;
        levelOnce();
        listHot.push_back( stopwatch.getElapsedMicroseconds() );
    }
    test::logBenchSamples( "stage level hot, 4 x High pass", listHot );

    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    manager.shutdown();
}

/**
 * @brief [TaskManagerBenchTest] 작은 독립 태스크 수천 개 — 태스크당 비용
 */
SW_TEST_CASE( TaskManagerBenchTest, SmallTaskThroughput )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    constexpr uint32       kTaskCount = 4096;
    constexpr uint32       kRound     = 20;
    const sw::TaskDelegate tiny       = SW_DELEGATE_FUNCTION( sw::TaskDelegate, BenchBody::tiny );

    sw::vector<int64> listRound;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        s_ranCount.store( 0, std::memory_order_relaxed );
        const sw::Stopwatch stopwatch;
        for ( uint32 index = 0; index < kTaskCount; ++index )
        {
            sw::TaskHandle handle = manager.emplaceTask( "Tiny", tiny );
            manager.submitWithoutWake( handle );
        }
        manager.wakeSleepingWorkers();
        SW_ASSERT_TRUE( manager.waitAll( 5000 ) );
        listRound.push_back( stopwatch.getElapsedMicroseconds() );
        SW_EXPECT_EQUAL( kTaskCount, s_ranCount.load() );
    }
    [[maybe_unused]] const int64 p50 = test::getPercentile( listRound, 50 );
    SW_LOG_INFO( "[Bench] 4096 tiny tasks: p50 %# us per round = %# ns per task", p50, ( p50 * 1000 ) / kTaskCount );

    manager.shutdown();
}

/**
 * @brief [TaskManagerBenchTest] CPU 바운드 본문의 직렬 대 병렬 — 배속
 */
SW_TEST_CASE( TaskManagerBenchTest, CPUBoundSpeedup )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    constexpr uint32                kCount = 2048; // 요소당 ~1 us → 직렬 ~2 ms
    constexpr uint32                kRound = 15;
    const sw::ParallelBlockDelegate body   = SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, BenchBody::computeRange );

    sw::vector<int64> listSerial;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        const sw::Stopwatch stopwatch;
        BenchBody::computeRange( 0, kCount );
        listSerial.push_back( stopwatch.getElapsedMicroseconds() );
    }

    for ( uint32 round = 0; round < 4; ++round )
    {
        manager.runParallel( kCount, 1, body );
    }

    sw::vector<int64> listParallel;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        const sw::Stopwatch stopwatch;
        manager.runParallel( kCount, 1, body );
        listParallel.push_back( stopwatch.getElapsedMicroseconds() );
    }

    const int64                  serialP50   = test::getPercentile( listSerial, 50 );
    const int64                  parallelP50 = test::getPercentile( listParallel, 50 );
    [[maybe_unused]] const int64 speedupX100 = parallelP50 > 0 ? ( serialP50 * 100 ) / parallelP50 : 0;
    SW_LOG_INFO( "[Bench] cpu-bound 2048 x ~1us: serial p50 %# us, parallel p50 %# us, speedup %#.%#x (threads %#)",
                 serialP50, parallelP50, speedupX100 / 100, speedupX100 % 100, manager.getWorkerCount() + 1 );

    manager.shutdown();
}

/**
 * @brief [TaskManagerBenchTest] 빈 데크의 pop · steal 한 번 — 할 일 없는 워커 · 대기자가 큐를 훑을 때마다 데크 수만큼 치르는 바닥(구조 최적화 A ①)
 */
SW_TEST_CASE( TaskManagerBenchTest, EmptyDequeProbe )
{
    constexpr uint64                 kProbeCount = 1000000;
    constexpr uint32                 kRound      = 5;
    sw::WorkStealingDeque<uintptr_t> deque{ 4096 };

    // 한 번 넣고 빼서 위치를 0 이 아닌 곳으로 옮긴다(실제 데크는 오래 돌아 위치가 0 이 아니다).
    SW_ASSERT_TRUE( deque.push( 8 ) );
    uintptr_t item{ 0 };
    SW_ASSERT_TRUE( deque.pop( item ) );

    uint32      foundCount    = 0;
    const int64 popDeciNano   = test::measureBestDeciNanosPerOp( kProbeCount, kRound, [&deque, &foundCount]()
      {
        uintptr_t probe{ 0 };
        for ( uint64 index = 0; index < kProbeCount; ++index )
        {
            foundCount += deque.pop( probe ) ? 1u : 0u;
        }
    } );
    const int64 stealDeciNano = test::measureBestDeciNanosPerOp( kProbeCount, kRound, [&deque, &foundCount]()
    {
        uintptr_t probe{ 0 };
        for ( uint64 index = 0; index < kProbeCount; ++index )
        {
            foundCount += deque.steal( probe ) ? 1u : 0u;
        }
    } );
    test::logBenchDeciNanos( "empty deque pop (owner)", popDeciNano );
    test::logBenchDeciNanos( "empty deque steal (thief)", stealDeciNano );
    SW_EXPECT_EQUAL( 0u, foundCount );
}

/**
 * @brief [TaskManagerBenchTest] 바깥 스레드 둘(게임 · 렌더 흉내)이 동시에 포크-조인을 연달아 기다릴 때 — 대기자의 스핀이 큐를 두드리는 비용(구조 최적화 A ②)
 */
SW_TEST_CASE( TaskManagerBenchTest, ConcurrentForkJoinWaiters )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    constexpr uint32                kCount = 512; // 요소당 ~1 us — 합류를 기다리는 시간이 생긴다
    constexpr uint32                kRound = 400;
    const sw::ParallelBlockDelegate body   = SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, BenchBody::computeRange );

    // 두 번째 스레드는 `data()` 로 받은 주소에만 쓴다(sw::vector 의 레이스 탐지기는 다른 스레드의 쓰기를 잡는다).
    sw::vector<int64> listSecond;
    listSecond.resize( kRound );
    int64*      pSecond = listSecond.data();
    std::thread second( [&manager, &body, pSecond]()
    {
        for ( uint32 round = 0; round < kRound; ++round )
        {
            const sw::Stopwatch stopwatch;
            manager.runParallel( kCount, 1, body );
            pSecond[round] = stopwatch.getElapsedMicroseconds();
        }
        manager.releaseCurrentThreadHelperSlot();
    } );

    sw::vector<int64> listMain;
    listMain.reserve( kRound );
    for ( uint32 round = 0; round < kRound; ++round )
    {
        const sw::Stopwatch stopwatch;
        manager.runParallel( kCount, 1, body );
        listMain.push_back( stopwatch.getElapsedMicroseconds() );
    }
    second.join();

    test::logBenchSamples( "two waiters forkJoin, main, 512 x ~1us", listMain );
    test::logBenchSamples( "two waiters forkJoin, second, 512 x ~1us", listSecond );
    SW_EXPECT_EQUAL( 0u, manager.getActiveTaskCount() );
    manager.shutdown();
}

/**
 * @brief [TaskManagerBenchTest] 워커가 태스크를 만들고 끝내는 나무 — 활성 태스크 수를 모든 스레드가 올리고 내린다(구조 최적화 A ④)
 */
SW_TEST_CASE( TaskManagerBenchTest, WorkerSpawnedTaskTree )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    BenchBody::s_pTreeManager = &manager;

    constexpr uint32       kBranchCount = 128; // 가지 128 × 잎 32 = 태스크 4224 개
    constexpr uint32       kRound       = 30;
    const sw::TaskDelegate branch       = SW_DELEGATE_FUNCTION( sw::TaskDelegate, BenchBody::treeBranch );
    constexpr uint32       kTaskCount   = kBranchCount * ( 1 + BenchBody::kTreeLeafPerBranch );

    sw::vector<int64> listRound;
    listRound.reserve( kRound );
    for ( uint32 round = 0; round < kRound; ++round )
    {
        s_ranCount.store( 0, std::memory_order_relaxed );
        const sw::Stopwatch stopwatch;
        for ( uint32 index = 0; index < kBranchCount; ++index )
        {
            sw::TaskHandle handle = manager.emplaceTask( "BenchBranch", branch );
            manager.submitWithoutWake( handle );
        }
        manager.wakeSleepingWorkers();
        SW_ASSERT_TRUE( manager.waitAll( 5000 ) );
        listRound.push_back( stopwatch.getElapsedMicroseconds() );
        SW_EXPECT_EQUAL( kTaskCount, s_ranCount.load() );
    }
    test::logBenchSamples( "worker-spawned task tree, 128 branches x 32 leaves", listRound );

    BenchBody::s_pTreeManager = nullptr;
    manager.shutdown();
}
