/**
 * @file TestTaskManagerBench.cpp
 * @brief TaskManager 마이크로벤치 — 디스패치 바닥 · 포크-조인 지연 · 작은 태스크 처리량 · CPU 바운드 배속.
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(기계마다 다르다). 회귀의 근거는 `docs/06_Backlog.md` 에
 *          Release 로 잰 표로 남긴다 — 이 케이스는 그 표를 같은 코드로 다시 만들기 위한 자리다.
 *          판정은 정합성만 본다(모든 인덱스가 한 번씩 · 모든 태스크가 돌았다).
 *
 *          재는 것:
 *          - 잠든 워커를 깨워 포크-조인 하나를 끝내는 시간(`runParallel`, 본문은 거의 비어 있다) — 디스패치 바닥.
 *          - 연달아 부를 때의 포크-조인 시간 — 정상 상태의 디스패치 비용.
 *          - 렌더 그래프 모양(스테이지 + High 태스크 몇 개 + waitStage).
 *          - 작은 독립 태스크 수천 개의 처리량(태스크당 ns).
 *          - CPU 바운드 본문의 직렬 대 병렬 배속(워커 + 호출 스레드가 전부 일하는가).
 *
 * @note Release 로 읽는다. Debug 의 컨테이너 레이스 탐지기·프로파일러가 숫자를 다른 것으로 만든다.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Task/TaskManager.h"

#include "TestFramework/TestFramework.h"

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

    /** @brief 정렬한 표본의 백분위 값. */
    int64 percentile( sw::vector<int64>& listSample, uint32 percent )
    {
        if ( listSample.empty() )
            return 0;
        std::sort( listSample.begin(), listSample.end() );
        size_t rank = ( listSample.size() * percent ) / 100;
        if ( rank >= listSample.size() )
            rank = listSample.size() - 1;
        return listSample[rank];
    }

    int64 elapsedMicro( const std::chrono::steady_clock::time_point& start )
    {
        return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - start ).count();
    }

    /** @brief 표본 하나를 [min · p50 · p90 · max] 로 찍습니다. Shipping 은 Info 로그가 컴파일에서 빠져 값만 계산하고 만다. */
    void logSamples( [[maybe_unused]] const utf8* pLabel, sw::vector<int64>& listSample )
    {
        [[maybe_unused]] const int64 minValue = percentile( listSample, 0 );
        [[maybe_unused]] const int64 p50      = percentile( listSample, 50 );
        [[maybe_unused]] const int64 p90      = percentile( listSample, 90 );
        [[maybe_unused]] const int64 maxValue = percentile( listSample, 100 );
        SW_LOG_INFO( "[Bench] %#  min %# us  p50 %# us  p90 %# us  max %# us  (%# samples)", pLabel, minValue, p50, p90, maxValue, listSample.size() );
    }

    struct BenchBody
    {
        /** @brief 거의 빈 본문 — 인덱스마다 표에 한 번 찍는다. 디스패치 비용만 남긴다. */
        static void touchRange( uint32 start, uint32 end )
        {
            for ( uint32 index = start; index < end; ++index )
                s_pHit[index].fetch_add( 1, std::memory_order_relaxed );
        }

        /** @brief 요소당 약 1 us 의 계산 본문. */
        static void computeRange( uint32 start, uint32 end )
        {
            uint32 accumulated = 0;
            for ( uint32 index = start; index < end; ++index )
                accumulated += spinWork( index, 1500 );
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
    };

    /** @brief 표를 @p count 칸으로 다시 잡고 0 으로 채웁니다. */
    void resetHitTable( sw::vector<sw::atomic<uint32>>& listHit, uint32 count )
    {
        listHit.clear();
        listHit.resize( count );
        s_pHit = listHit.data();
        for ( uint32 index = 0; index < count; ++index )
            s_pHit[index].store( 0, std::memory_order_relaxed );
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
        manager.runParallel( kCount, 1, body );

    sw::vector<int64> listCold;
    listCold.reserve( kColdRound );
    for ( uint32 round = 0; round < kColdRound; ++round )
    {
        std::this_thread::sleep_for( std::chrono::microseconds( kSleepGapMicro ) );
        const auto start = std::chrono::steady_clock::now();
        manager.runParallel( kCount, 1, body );
        listCold.push_back( elapsedMicro( start ) );
    }
    logSamples( "forkJoin cold (workers asleep), 4096 x touch", listCold );

    sw::vector<int64> listHot;
    listHot.reserve( kHotRound );
    for ( uint32 round = 0; round < kHotRound; ++round )
    {
        const auto start = std::chrono::steady_clock::now();
        manager.runParallel( kCount, 1, body );
        listHot.push_back( elapsedMicro( start ) );
    }
    logSamples( "forkJoin hot (back to back), 4096 x touch", listHot );

    SW_EXPECT_EQUAL( 0u, countWrongHits( kCount, 16 + kColdRound + kHotRound ) );
    manager.shutdown();
}

/**
 * @brief [TaskManagerBenchTest] 렌더 그래프 모양 — 스테이지에 High 태스크 넷을 넣고 한 번 깨워 기다린다
 */
SW_TEST_CASE( TaskManagerBenchTest, StageWaveLikeRenderGraph )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    constexpr uint32 kPassCount = 4;
    constexpr uint32 kRound     = 200;

    // 패스 하나의 본문 시간 — 병렬 웨이브의 이상적인 값이다.
    sw::vector<int64> listSerialPass;
    for ( uint32 round = 0; round < 20; ++round )
    {
        const auto start = std::chrono::steady_clock::now();
        BenchBody::recordPass();
        listSerialPass.push_back( elapsedMicro( start ) );
    }
    logSamples( "one pass body (ideal wave time)", listSerialPass );

    const sw::TaskDelegate record   = SW_DELEGATE_FUNCTION( sw::TaskDelegate, BenchBody::recordPass );
    auto                   waveOnce = [&manager, &record]()
    {
        sw::TaskStageHandle stage = manager.createAnonymousStage( "BenchWave" );
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
        waveOnce();

    sw::vector<int64> listCold;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        std::this_thread::sleep_for( std::chrono::microseconds( kSleepGapMicro ) );
        const auto start = std::chrono::steady_clock::now();
        waveOnce();
        listCold.push_back( elapsedMicro( start ) );
    }
    logSamples( "stage wave cold, 4 x High pass", listCold );

    sw::vector<int64> listHot;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        const auto start = std::chrono::steady_clock::now();
        waveOnce();
        listHot.push_back( elapsedMicro( start ) );
    }
    logSamples( "stage wave hot, 4 x High pass", listHot );

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
        const auto start = std::chrono::steady_clock::now();
        for ( uint32 index = 0; index < kTaskCount; ++index )
        {
            sw::TaskHandle handle = manager.emplaceTask( "Tiny", tiny );
            manager.submitWithoutWake( handle );
        }
        manager.wakeSleepingWorkers();
        SW_ASSERT_TRUE( manager.waitAll( 5000 ) );
        listRound.push_back( elapsedMicro( start ) );
        SW_EXPECT_EQUAL( kTaskCount, s_ranCount.load() );
    }
    [[maybe_unused]] const int64 p50 = percentile( listRound, 50 );
    SW_LOG_INFO( "[Bench] 4096 tiny tasks: p50 %# us per round = %# ns per task", p50, ( p50 * 1000 ) / kTaskCount );

    manager.shutdown();
}

/**
 * @brief [TaskManagerBenchTest] CPU 바운드 본문의 직렬 대 병렬 — 배속
 */
SW_TEST_CASE( TaskManagerBenchTest, CpuBoundSpeedup )
{
    sw::TaskManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    constexpr uint32                kCount = 2048; // 요소당 ~1 us → 직렬 ~2 ms
    constexpr uint32                kRound = 15;
    const sw::ParallelBlockDelegate body   = SW_DELEGATE_FUNCTION( sw::ParallelBlockDelegate, BenchBody::computeRange );

    sw::vector<int64> listSerial;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        const auto start = std::chrono::steady_clock::now();
        BenchBody::computeRange( 0, kCount );
        listSerial.push_back( elapsedMicro( start ) );
    }

    for ( uint32 round = 0; round < 4; ++round )
        manager.runParallel( kCount, 1, body );

    sw::vector<int64> listParallel;
    for ( uint32 round = 0; round < kRound; ++round )
    {
        const auto start = std::chrono::steady_clock::now();
        manager.runParallel( kCount, 1, body );
        listParallel.push_back( elapsedMicro( start ) );
    }

    const int64                  serialP50   = percentile( listSerial, 50 );
    const int64                  parallelP50 = percentile( listParallel, 50 );
    [[maybe_unused]] const int64 speedupX100 = parallelP50 > 0 ? ( serialP50 * 100 ) / parallelP50 : 0;
    SW_LOG_INFO( "[Bench] cpu-bound 2048 x ~1us: serial p50 %# us, parallel p50 %# us, speedup %#.%#x (threads %#)",
                 serialP50, parallelP50, speedupX100 / 100, speedupX100 % 100, manager.getWorkerCount() + 1 );

    manager.shutdown();
}
