#include "pch.h"

#include "Core/Time/MonotonicClock.h"

#include "EngineTest/GameFramework/Kits/Storage/SqlDriverContract.h"

#include "GameFramework/Kits/Storage/SqlStore/Driver/Sqlite/SqliteDriver.h"
#include "GameFramework/Kits/Storage/SqlStore/Sql/SqlConnectionPool.h"
#include "GameFramework/Kits/Storage/SqlStore/Sql/SqlDriverRegistry.h"

#include <thread>

// SQL 드라이버(SQLite) — 드라이버 계약 다섯 + 연결 풀(완료는 거두는 스레드에서 · 내린 뒤 맡긴 일) + 드라이버 등록부(없는 이름은 분명한 오류).

using namespace sw;

namespace
{
    struct SqliteDriverFixture
    {
        string                     _path;
        unique_ptr<ISqlConnection> _connection;

        SqliteDriverFixture()
            : _path{ test::makeTempPath( "driver.db" ) }
            , _connection{}
        {
            string error;
            _connection = SqliteDriver::getInstance().openConnection( _path, "", error );
        }

        bool            isReady() const { return _connection != nullptr; }
        ISqlConnection& getConnection() { return *_connection; }
        string          makeTableName( const utf8* pCaseName ) { return string( "drv_" ) + pCaseName; }
    };

    /** @brief 풀 시험의 일 — 워커에서 행 하나를 넣고, 거둘 때 거둔 스레드와 결과를 적는다. */
    class SqlPoolCountingJob final : public ISqlJob
    {
    public:
        SqlPoolCountingJob( int32 jobIndex, int32* pCompletedCount, int32* pOffThreadCount, int32* pLostCount )
            : _threadId{}
            , _pCompletedCount{ pCompletedCount }
            , _pOffThreadCount{ pOffThreadCount }
            , _pLostCount{ pLostCount }
            , _jobIndex{ jobIndex }
            , _result{ SqlResult::Ok }
        {
        }

        void run( ISqlConnection& connection ) override
        {
            const SqlValue arrParam[] = { SqlValue::makeInt64( _jobIndex ) };
            _result                   = connection.execute( "INSERT INTO pool_job ( id ) VALUES ( ? )", arrParam, 1, nullptr );
        }

        void complete() override
        {
            ++*_pCompletedCount;
            if ( std::this_thread::get_id() != _threadId )
                ++*_pOffThreadCount;
            if ( _result == SqlResult::ConnectionLost )
                ++*_pLostCount;
        }

        std::thread::id _threadId;

    private:
        int32*    _pCompletedCount;
        int32*    _pOffThreadCount;
        int32*    _pLostCount;
        int32     _jobIndex;
        SqlResult _result;
    };
} // namespace

SW_SQL_DRIVER_CONTRACT_SUITE( SqlDriverSqliteTest, SqliteDriverFixture )

SW_TEST_CASE( SqlDriverSqliteTest, PoolCompletesOnThePollingThreadAndSurvivesShutdown )
{
    SqliteDriverFixture fixture;
    SW_ASSERT_TRUE( fixture.isReady() );
    SW_ASSERT_TRUE( fixture.getConnection().executeScript( "CREATE TABLE pool_job ( id BIGINT PRIMARY KEY )" ) == SqlResult::Ok );

    SqlConnectionPool         pool;
    SqlConnectionPoolSettings settings;
    settings._connection  = fixture._path;
    settings._workerCount = 2;
    string error;
    SW_ASSERT_TRUE_MSG( pool.initialize( &SqliteDriver::getInstance(), settings, error ), error.c_str() );

    constexpr int32 kJobCount      = 100;
    int32           completedCount = 0;
    int32           offThreadCount = 0;
    int32           lostCount      = 0;
    for ( int32 jobIndex = 0; jobIndex < kJobCount; ++jobIndex )
    {
        unique_ptr<SqlPoolCountingJob> job = make_unique<SqlPoolCountingJob>( jobIndex, &completedCount, &offThreadCount, &lostCount );
        job->_threadId                     = std::this_thread::get_id();
        pool.submit( std::move( job ) );
    }
    const Deadline deadline = Deadline::afterMilliseconds( 10000 );
    while ( completedCount < kJobCount && deadline.isExpired() == false )
    {
        if ( pool.pollCompletions() == 0 )
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }
    SW_EXPECT_EQUAL( kJobCount, completedCount );
    SW_EXPECT_EQUAL( 0, offThreadCount ); // complete 는 모두 거둔 스레드(여기)에서
    SW_EXPECT_EQUAL( 0, lostCount );
    SW_EXPECT_EQUAL( 0, pool.getPendingCount() );

    pool.shutdown();
    unique_ptr<SqlPoolCountingJob> late = make_unique<SqlPoolCountingJob>( kJobCount, &completedCount, &offThreadCount, &lostCount );
    late->_threadId                     = std::this_thread::get_id();
    pool.submit( std::move( late ) );
    SW_EXPECT_EQUAL( 1, pool.pollCompletions() );
    SW_EXPECT_EQUAL( 1, lostCount ); // 내린 뒤 맡긴 일은 닫힌 연결로 정확히 한 번

    SqlRowSet rowSet;
    SW_ASSERT_TRUE( fixture.getConnection().execute( "SELECT COUNT(*) FROM pool_job", nullptr, 0, &rowSet ) == SqlResult::Ok );
    SW_EXPECT_EQUAL( int64( kJobCount ), rowSet._listRow[0][0]._integer );
}

SW_TEST_CASE( SqlDriverSqliteTest, UnknownDriverFailsLoudlyWithTheBuiltDrivers )
{
    string error;
    SW_EXPECT_NULL( SqlDriverRegistry::findDriver( "mysql", error ) );
    SW_EXPECT_TRUE( error.find( "no SQL driver 'mysql'" ) != string::npos );
    SW_EXPECT_TRUE( error.find( "sqlite" ) != string::npos ); // 있는 드라이버를 알려 준다
    SW_EXPECT_TRUE( SqlDriverRegistry::findDriver( "sqlite", error ) == &SqliteDriver::getInstance() );

    unique_ptr<ISqlConnection> connection = SqliteDriver::getInstance().openConnection( "", "", error );
    SW_EXPECT_NULL( connection );
}
