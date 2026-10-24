/**
 * @file TestLogSinkBench.cpp
 * @brief 비동기 로그 싱크의 생산자 비용 — 로그 한 줄을 큐에 넣고 돌아오기까지(타임스탬프 · 포맷 · 리스너 · 깨우기), 한 스레드 · 여러 스레드, 리스너 없음 · 있음.
 * @details 숫자를 **찍기만** 한다(기계마다 다르다). Release 로 읽는다. 판마다 큐(4096 줄)보다 훨씬 적게 쓰고 판 사이에 쉬어 작업 스레드가 큐를
 *          비우게 한다 — 큐가 차면 생산자가 장치에 직접 쓰므로 콘솔 속도를 재게 된다. 구조 최적화 A ⑤(날짜 캐시 · 리스너 스냅샷 · 대기자 있을 때만 깨우기)의
 *          전/후를 이 케이스로 측정한다(표는 `docs/plans/StructuralOptimization.md`).
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/LogSink/AsyncLogSink.h"
#include "Core/Time/MonotonicClock.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( LogSinkBenchTest, "async log sink producer cost - one and several threads, with and without a listener" );
SW_TEST_TIME_BUDGET( LogSinkBenchTest, 4000, "판 사이 휴지(20 ms × 판 수)가 대부분 — 측정 전 추정, 첫 Release 실행 뒤 실제 최대의 네 배로 고친다" );

SW_LOG_CALLER( "LogSinkBench" );

namespace
{
    struct LogSinkBenchInternal
    {
        /** @brief 판 수입니다(p50 · p99 를 본다). */
        static constexpr uint32 kRoundCount = 10;
        /** @brief 한 스레드 케이스가 한 판에 쓰는 줄 수입니다. */
        static constexpr uint32 kSingleLineCount = 128;
        /** @brief 여러 스레드 케이스의 생산자 수입니다. */
        static constexpr uint32 kProducerCount = 4;
        /** @brief 여러 스레드 케이스에서 생산자 하나가 한 판에 쓰는 줄 수입니다. */
        static constexpr uint32 kLinePerProducer = 64;
        /** @brief 판 사이에 작업 스레드가 큐를 비우도록 쉬는 시간(밀리초)입니다. */
        static constexpr uint32 kDrainMilli = 20;

        /** @brief 리스너가 받은 줄 수입니다(에디터 콘솔 흉내 — 받기만 한다). */
        static inline sw::atomic<uint32> s_listenedCount{ 0 };

        static void onLogWritten( const sw::LogEntry& )
        {
            s_listenedCount.fetch_add( 1, std::memory_order_relaxed );
        }

        /** @brief 한 줄을 씁니다. 실제 호출처처럼 태그 · 호출처 · 파일 · 줄을 모두 채운다. */
        static void writeOneLine( sw::AsyncLogSink& sink )
        {
            sink.writeLog( sw::LogLevel::Trace, "Bench", "LogSinkBench", "producer cost probe line", __FILE__, __LINE__ );
        }

        /** @brief 한 스레드가 `kSingleLineCount` 줄씩 쓰는 판을 돌려 판마다의 시간(us)을 담습니다. */
        static void measureSingleProducer( sw::AsyncLogSink& sink, sw::vector<int64>& outListRound )
        {
            outListRound.clear();
            for ( uint32 round = 0; round < kRoundCount; ++round )
            {
                const sw::Stopwatch stopwatch;
                for ( uint32 index = 0; index < kSingleLineCount; ++index )
                {
                    writeOneLine( sink );
                }
                outListRound.push_back( stopwatch.getElapsedMicroseconds() );
                std::this_thread::sleep_for( std::chrono::milliseconds( kDrainMilli ) );
            }
        }

        /** @brief 생산자 `kProducerCount` 개가 동시에 `kLinePerProducer` 줄씩 쓰는 판을 돌려 판마다의 벽시계(us)를 담습니다. */
        static void measureConcurrentProducers( sw::AsyncLogSink& sink, sw::vector<int64>& outListRound )
        {
            outListRound.clear();
            for ( uint32 round = 0; round < kRoundCount; ++round )
            {
                sw::atomic<uint32>      readyCount{ 0 };
                sw::atomic<bool>        bGo{ false };
                sw::vector<std::thread> listThread;
                listThread.reserve( kProducerCount );
                for ( uint32 producer = 0; producer < kProducerCount; ++producer )
                {
                    listThread.emplace_back( [&sink, &readyCount, &bGo]()
                    {
                        readyCount.fetch_add( 1, std::memory_order_acq_rel );
                        while ( bGo.load( std::memory_order_acquire ) == false )
                        {
                            sw::cpuPause();
                        }
                        for ( uint32 index = 0; index < kLinePerProducer; ++index )
                        {
                            writeOneLine( sink );
                        }
                    } );
                }
                while ( readyCount.load( std::memory_order_acquire ) < kProducerCount )
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
                std::this_thread::sleep_for( std::chrono::milliseconds( kDrainMilli ) );
            }
        }
    };
} // namespace

/**
 * @brief [LogSinkBenchTest] 생산자 비용 — 한 스레드 · 여러 스레드, 리스너 없음 · 하나(에디터 콘솔 흉내)
 */
SW_TEST_CASE( LogSinkBenchTest, ProducerCost )
{
    sw::AsyncLogSink sink;
    sink.initialize();

    sw::vector<int64> listRound;
    LogSinkBenchInternal::measureSingleProducer( sink, listRound );
    test::logBenchSamples( "log producer, 1 thread x 128 lines, no listener (us per round)", listRound );
    LogSinkBenchInternal::measureConcurrentProducers( sink, listRound );
    test::logBenchSamples( "log producer, 4 threads x 64 lines, no listener (us per round)", listRound );

    const sw::DelegateHandle listener = sink.registerLogWrittenListener( SW_DELEGATE_FUNCTION( sw::LogWrittenDelegate, LogSinkBenchInternal::onLogWritten ) );
    LogSinkBenchInternal::measureSingleProducer( sink, listRound );
    test::logBenchSamples( "log producer, 1 thread x 128 lines, one listener (us per round)", listRound );
    LogSinkBenchInternal::measureConcurrentProducers( sink, listRound );
    test::logBenchSamples( "log producer, 4 threads x 64 lines, one listener (us per round)", listRound );
    sink.unregisterLogWrittenListener( listener );

    sink.shutdown();
    const uint32 expectedListened = LogSinkBenchInternal::kRoundCount *
                                    ( LogSinkBenchInternal::kSingleLineCount + LogSinkBenchInternal::kProducerCount * LogSinkBenchInternal::kLinePerProducer );
    SW_EXPECT_EQUAL( expectedListened, LogSinkBenchInternal::s_listenedCount.load() );
}
