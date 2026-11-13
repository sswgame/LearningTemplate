#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/deque.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Testing/TestWait.h"
#include "Core/Testing/ThreadTestAccess.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLConnectionPool.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/Tests/FakeSQLDriver.h"

namespace
{
    /** @brief 실패의 문장과 대상을 이은 글입니다(성공이면 빈 글). 오류 글을 확인하는 테스트가 씁니다. */
    template <typename T>
    sw::string describeError( const sw::expected<T>& result )
    {
        return result.has_value() ? sw::string{} : sw::string( result.error().message() ) + " " + sw::string( result.error().detail() );
    }
} // namespace

// SQLConnectionPoolTest — 연결 풀: 기동 오류, 워커 실행과 완료 회수, 끊긴 연결 다시 열기와 백오프, 종료가 남은 일을 취소로 끝내기, 완료 안에서 새 일 맡기기(가짜 드라이버, nogpu).

using namespace sw;

namespace
{
    /** @brief 일 하나가 지난 길입니다. 테스트가 들고 일이 채웁니다(완료 큐의 잠금을 지나 보인다). */
    struct PoolJobResult
    {
        SQLResult _result{ SQLResult::Error };
        int32     _runCount{ 0 };
        int32     _cancelCount{ 0 };
        int32     _completeCount{ 0 };
    };

    class PoolTestJob final : public ISQLJob
    {
    public:
        explicit PoolTestJob( PoolJobResult& serviceResult )
            : _serviceResult{ serviceResult }
        {
        }

        void run( ISQLConnection& connection ) override
        {
            ++_serviceResult._runCount;
            _serviceResult._result = connection.execute( "SELECT job", nullptr, 0, nullptr );
        }
        void complete() override { ++_serviceResult._completeCount; }
        void cancel() override { ++_serviceResult._cancelCount; }

    private:
        PoolJobResult& _serviceResult;
    };

    /** @brief 문이 열릴 때까지 워커를 잡아 두는 일입니다. */
    class PoolLatchJob final : public ISQLJob
    {
    public:
        PoolLatchJob( atomic<bool>& bEntered, atomic<bool>& bRelease )
            : _bEntered{ bEntered }
            , _bRelease{ bRelease }
        {
        }

        void run( ISQLConnection& connection ) override
        {
            (void)connection;
            _bEntered.store( true );
            (void)test::waitUntil( [this]()
            { return _bRelease.load(); }, 20.0 ); // 못 풀려도 돌아온다 — 케이스가 단언한다
        }
        void complete() override {}

    private:
        atomic<bool>& _bEntered;
        atomic<bool>& _bRelease;
    };

    /** @brief 완료 안에서 새 일을 맡기는 일입니다. */
    class PoolChainJob final : public ISQLJob
    {
    public:
        PoolChainJob( SQLConnectionPool& pool, PoolJobResult& nextResult )
            : _pool{ pool }
            , _nextResult{ nextResult }
        {
        }

        void run( ISQLConnection& connection ) override { (void)connection; }
        void complete() override { _pool.submit( sw::make_unique<PoolTestJob>( _nextResult ) ); }

    private:
        SQLConnectionPool& _pool;
        PoolJobResult&     _nextResult;
    };

    struct SQLConnectionPoolTestUtil
    {
        static SQLConnectionPoolConfig makeSettings( int32 workerCount )
        {
            SQLConnectionPoolConfig settings;
            settings._connection  = "fake";
            settings._workerCount = workerCount;
            return settings;
        }

        /** @brief 맡은 일이 모두 끝날 때까지 완료를 거둡니다(10 초 상한). 다 끝나면 true 입니다. */
        [[nodiscard]] static bool pollAll( SQLConnectionPool& pool )
        {
            return test::waitUntil( [&pool]()
            {
                (void)pool.pollCompletions(); // 거둔 수는 아래 맡은 수로 본다
                return pool.getPendingCount() == 0;
            } );
        }

        /** @brief 일 하나를 맡기고 끝날 때까지 기다린 결과입니다. */
        static PoolJobResult runOne( SQLConnectionPool& pool )
        {
            PoolJobResult serviceResult;
            pool.submit( sw::make_unique<PoolTestJob>( serviceResult ) );
            const bool bDrained = pollAll( pool );
            SW_EXPECT_TRUE( bDrained );
            return serviceResult;
        }
    };
} // namespace

SW_TEST_CASE( SQLConnectionPoolTest, InitializeRefusesWhatCannotRun )
{
    SQLConnectionPool   pool;
    test::FakeSQLDriver driver;
    string              error;
    error = describeError( pool.initialize( nullptr, SQLConnectionPoolTestUtil::makeSettings( 1 ) ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );

    error.clear();
    error = describeError( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 0 ) ) ); // 0 을 1 로 맞추지 않는다
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
    SW_EXPECT_EQUAL( 0, driver.getOpenCount() );

    error.clear();
    driver.setAllowedOpenCount( 0 );
    error = describeError( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 1 ) ) ); // 기동 오류를 바로 알린다
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.empty() == false );
}

/** @brief 시작하지 않은 풀에 맡긴 일도 정확히 한 번 돌고 완료된다 — 닫힌 연결로, 취소가 아니라 쓸 수 없는 풀로. */
SW_TEST_CASE( SQLConnectionPoolTest, JobOnAnUnstartedPoolFinishesAsConnectionLost )
{
    SQLConnectionPool   pool;
    const PoolJobResult serviceResult = SQLConnectionPoolTestUtil::runOne( pool );
    SW_EXPECT_TRUE( serviceResult._result == SQLResult::ConnectionLost );
    SW_EXPECT_EQUAL( 1, serviceResult._runCount );
    SW_EXPECT_EQUAL( 1, serviceResult._completeCount );
    SW_EXPECT_EQUAL( 0, serviceResult._cancelCount );
}

SW_TEST_CASE( SQLConnectionPoolTest, RunsOnWorkerAndCompletesOnPoll )
{
    test::FakeSQLDriver driver;
    SQLConnectionPool   pool;
    string              error;
    SW_REQUIRE_TRUE( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 2 ) ) );

    deque<PoolJobResult> listServiceResult( 16 );
    for ( PoolJobResult& serviceResult : listServiceResult )
    {
        pool.submit( sw::make_unique<PoolTestJob>( serviceResult ) );
    }
    SW_REQUIRE_TRUE( SQLConnectionPoolTestUtil::pollAll( pool ) );
    for ( const PoolJobResult& serviceResult : listServiceResult )
    {
        SW_EXPECT_TRUE( serviceResult._result == SQLResult::Ok );
        SW_EXPECT_EQUAL( 1, serviceResult._runCount );
        SW_EXPECT_EQUAL( 1, serviceResult._completeCount );
        SW_EXPECT_EQUAL( 0, serviceResult._cancelCount );
    }
    pool.shutdown();
    SW_EXPECT_EQUAL( 3, driver.getOpenCount() ); // 먼저 열어 본 하나 + 워커 둘 — 일마다 다시 열지 않는다
}

SW_TEST_CASE( SQLConnectionPoolTest, CompleteMaySubmitANewJob )
{
    test::FakeSQLDriver driver;
    SQLConnectionPool   pool;
    string              error;
    SW_REQUIRE_TRUE( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 1 ) ) );
    PoolJobResult next;
    pool.submit( sw::make_unique<PoolChainJob>( pool, next ) );
    SW_REQUIRE_TRUE( SQLConnectionPoolTestUtil::pollAll( pool ) );
    SW_EXPECT_EQUAL( 1, next._completeCount ); // 잠금 밖에서 complete 를 불러야 교착 없이 온다
    SW_EXPECT_TRUE( next._result == SQLResult::Ok );
}

/** @brief 끊긴 연결은 다음 일 앞에서 다시 연다 — 일 도중의 끊김과 밖에서 본 끊김 모두. */
SW_TEST_CASE( SQLConnectionPoolTest, ReconnectsBeforeTheNextJob )
{
    test::FakeSQLDriver driver;
    SQLConnectionPool   pool;
    string              error;
    SW_REQUIRE_TRUE( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 1 ) ) );
    SW_EXPECT_TRUE( SQLConnectionPoolTestUtil::runOne( pool )._result == SQLResult::Ok );
    const int32 openedBefore = driver.getOpenCount();

    driver.addResult( "SELECT job", SQLResult::ConnectionLost, 1 );
    SW_EXPECT_TRUE( SQLConnectionPoolTestUtil::runOne( pool )._result == SQLResult::ConnectionLost ); // 그 일은 결과를 모르는 채로 끝난다
    SW_EXPECT_TRUE( SQLConnectionPoolTestUtil::runOne( pool )._result == SQLResult::Ok );
    SW_EXPECT_EQUAL( openedBefore + 1, driver.getOpenCount() );

    driver.loseAllConnections();
    SW_EXPECT_TRUE( SQLConnectionPoolTestUtil::runOne( pool )._result == SQLResult::Ok );
    SW_EXPECT_EQUAL( openedBefore + 2, driver.getOpenCount() );
}

/**
 * @brief 다시 열지 못하는 동안의 일은 멈추지 않고 ConnectionLost 로 끝나고, 시도는 백오프 간격으로만 한다. 서버가 돌아오면 다시 붙는다.
 * @details 결함: 실패마다 간격을 늘린 뒤 기다려 첫 간격(100 ms)을 쓰는 일이 없었다. 간격 자체는 시간이라 재지 않고 "일마다 시도하지 않음" 을 본다.
 */
SW_TEST_CASE( SQLConnectionPoolTest, FailedReconnectBacksOffAndRecovers )
{
    SW_EXPECT_LOG( "SQLConnectionPool.SQLWorkerCouldNot2" );
    test::FakeSQLDriver driver;
    SQLConnectionPool   pool;
    string              error;
    SW_REQUIRE_TRUE( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 1 ) ) );
    SW_EXPECT_TRUE( SQLConnectionPoolTestUtil::runOne( pool )._result == SQLResult::Ok );

    driver.setAllowedOpenCount( 0 );
    driver.loseAllConnections();
    const int32          attemptsBefore = driver.getOpenCount();
    constexpr int32      kJobCount      = 32;
    deque<PoolJobResult> listServiceResult( kJobCount );
    for ( PoolJobResult& serviceResult : listServiceResult )
    {
        pool.submit( sw::make_unique<PoolTestJob>( serviceResult ) );
    }
    SW_REQUIRE_TRUE( SQLConnectionPoolTestUtil::pollAll( pool ) );
    for ( const PoolJobResult& serviceResult : listServiceResult )
    {
        SW_EXPECT_TRUE( serviceResult._result == SQLResult::ConnectionLost );
    }
    SW_EXPECT_TRUE( driver.getOpenCount() > attemptsBefore );
    SW_EXPECT_TRUE( driver.getOpenCount() - attemptsBefore < kJobCount / 2 );

    driver.setAllowedOpenCount( -1 );
    // 다시 붙는 것은 풀의 백오프(실제 시계 — 재시도 간격이라 판정이 아니다)가 정한다. 붙을 때까지 일을 맡겨 본다 — 상한(5 초)을 넘겨 기다린다.
    const bool bRecovered = test::waitUntil( [&pool]()
    { return SQLConnectionPoolTestUtil::runOne( pool )._result == SQLResult::Ok; }, 15.0 );
    SW_EXPECT_TRUE( bRecovered );
}

/** @brief 워커의 첫 연결이 실패해도 멈추지 않는다 — 일은 끊김으로 끝나고 뒤에 붙는다. */
SW_TEST_CASE( SQLConnectionPoolTest, WorkerThatCouldNotConnectStillAnswers )
{
    SW_EXPECT_LOG( "SQLConnectionPool.SQLWorkerCouldNot" );
    SW_EXPECT_LOG( "SQLConnectionPool.SQLWorkerCouldNot2" );
    test::FakeSQLDriver driver;
    driver.setAllowedOpenCount( 1 ); // 먼저 열어 보는 하나만 된다
    SQLConnectionPool pool;
    string            error;
    SW_REQUIRE_TRUE( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 1 ) ) );
    const PoolJobResult serviceResult = SQLConnectionPoolTestUtil::runOne( pool ); // 첫 일 앞에서 한 번 더 시도한다 — 둘째 경고
    SW_EXPECT_TRUE( serviceResult._result == SQLResult::ConnectionLost );
    SW_EXPECT_EQUAL( 0, serviceResult._cancelCount ); // 고장이지 종료가 아니다
}

/** @brief 종료는 돌던 일을 끝내고 남은 일을 취소로 끝낸다. 그 뒤 맡긴 일도 취소이고 모두 완료를 받는다. 다시 시작할 수 있다. */
SW_TEST_CASE( SQLConnectionPoolTest, ShutdownCancelsQueuedJobsAndCanRestart )
{
    test::FakeSQLDriver driver;
    SQLConnectionPool   pool;
    string              error;
    SW_REQUIRE_TRUE( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 1 ) ) );

    atomic<bool> bEntered{ false };
    atomic<bool> bRelease{ false };
    pool.submit( sw::make_unique<PoolLatchJob>( bEntered, bRelease ) );
    SW_REQUIRE_TRUE( test::waitUntil( [&bEntered]()
    { return bEntered.load(); } ) );

    PoolJobResult queued;
    pool.submit( sw::make_unique<PoolTestJob>( queued ) );
    sw::thread stopper{ ThreadTestAccess::getKey(), [&pool]()
    { pool.shutdown(); } };

    // 종료가 깃발을 세웠는지는 새 일이 그 자리에서 취소되는 것으로 본다. 그 전의 일은 줄에 서고 워커는 문에 잡혀 있어 칸을 만지지 않는다.
    deque<PoolJobResult> listProbe;
    const bool           bStopping = test::waitUntil( [&]()
    {
        PoolJobResult& probe = listProbe.emplace_back();
        pool.submit( sw::make_unique<PoolTestJob>( probe ) );
        return probe._cancelCount == 1;
    } );
    bRelease.store( true );
    stopper.join();
    SW_REQUIRE_TRUE( bStopping );

    SW_EXPECT_TRUE( pool.pollCompletions() >= 2 ); // 종료 뒤 한 번으로 모두 거둔다
    SW_EXPECT_EQUAL( 0, pool.getPendingCount() );
    SW_EXPECT_EQUAL( 1, queued._cancelCount );
    SW_EXPECT_EQUAL( 1, queued._completeCount );
    SW_EXPECT_TRUE( queued._result == SQLResult::ConnectionLost );
    for ( const PoolJobResult& probe : listProbe )
    {
        SW_EXPECT_EQUAL( 1, probe._cancelCount );
        SW_EXPECT_EQUAL( 1, probe._completeCount );
    }

    SW_REQUIRE_TRUE( pool.initialize( &driver, SQLConnectionPoolTestUtil::makeSettings( 1 ) ) ); // 설정을 비웠으니 다시 정할 수 있다
    SW_EXPECT_TRUE( SQLConnectionPoolTestUtil::runOne( pool )._result == SQLResult::Ok );
}
