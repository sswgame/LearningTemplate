#include "pch.h"

#include "Core/String/StringUtil.h"

#include "EngineTest/GameFramework/Online/ServiceStoreContract.h"

#include "GameFramework/Kits/Storage/Server/SqlStore/ServiceStoreFactory.h"
#include "GameFramework/Kits/Storage/Server/SqlStore/SqlServiceStore.h"

// SQL 서비스 저장소(SQLite) — IServiceStore 계약 일곱(메모리 · PostgreSQL 과 같은 케이스), 다시 띄워도 데이터 · 판이 이어진다, 저장소 공장(memory · sqlite · 없는 드라이버).

using namespace sw;

namespace
{
    struct SqliteStoreFixture
    {
        string          _path;
        SqlServiceStore _store;
        bool            _bReady;

        SqliteStoreFixture()
            : _path{ test::makeTempPath( "servicestore.db" ) }
            , _store{}
            , _bReady{ false }
        {
            SqlConnectionPoolSettings settings;
            settings._connection  = _path;
            settings._workerCount = 2;
            string error;
            _bReady = _store.initialize( "sqlite", settings, SqlServiceStore::kMigrationFolder, 0, error );
            SW_EXPECT_TRUE_MSG( _bReady, error.c_str() );
        }

        ~SqliteStoreFixture()
        {
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        IServiceStore& getStore() { return _store; }

        hashed_string makeTableName( const utf8* pCaseName )
        {
            string name{ "contract_" };
            name += StringUtil::toLower( pCaseName );
            return hashed_string( name.c_str() );
        }
    };

    /** @brief 레코드 하나를 쓰거나 읽는 일 — 결과를 시험 스레드에 돌려준다. */
    class SqliteStorePutWork final : public IServiceStoreWork
    {
    public:
        SqliteStorePutWork( bool bWrite, ServiceRecord* pOutRecord, ServiceStoreResult* pOutResult, int32* pCompletedCount )
            : _pOutRecord{ pOutRecord }
            , _pOutResult{ pOutResult }
            , _pCompletedCount{ pCompletedCount }
            , _bWrite{ bWrite }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            static const hashed_string s_table{ "restart" };
            if ( _bWrite )
            {
                ServiceTransaction transaction;
                transaction.put( s_table, "k", vector<uint8>{ 7 }, ServiceRecord::kAbsentVersion );
                *_pOutResult = connection.commit( transaction );
                return;
            }
            *_pOutResult = connection.readRecord( s_table, "k", *_pOutRecord );
        }

        void complete() override { ++*_pCompletedCount; }

    private:
        ServiceRecord*      _pOutRecord;
        ServiceStoreResult* _pOutResult;
        int32*              _pCompletedCount;
        bool                _bWrite;
    };

    struct TestServiceStoreSqliteInternal
    {
        static void settle( IServiceStore& store, const int32& completedCount, int32 expectedCount )
        {
            const Deadline deadline = Deadline::afterMilliseconds( 10000 );
            while ( completedCount < expectedCount && deadline.isExpired() == false )
            {
                if ( store.pollCompletions() == 0 )
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
        }
    };
} // namespace

SW_SERVICE_STORE_CONTRACT_SUITE( ServiceStoreSqliteTest, SqliteStoreFixture )

SW_TEST_CASE( ServiceStoreSqliteTest, DataAndVersionsSurviveARestart )
{
    const string              path = test::makeTempPath( "restart.db" );
    SqlConnectionPoolSettings settings;
    settings._connection = path;
    string             error;
    ServiceRecord      record;
    ServiceStoreResult result         = ServiceStoreResult::Unavailable;
    int32              completedCount = 0;
    {
        SqlServiceStore store;
        SW_ASSERT_TRUE_MSG( store.initialize( "sqlite", settings, SqlServiceStore::kMigrationFolder, 0, error ), error.c_str() );
        store.submit( make_unique<SqliteStorePutWork>( true, &record, &result, &completedCount ) );
        TestServiceStoreSqliteInternal::settle( store, completedCount, 1 );
        SW_EXPECT_TRUE( result == ServiceStoreResult::Ok );
        store.shutdown();
    }
    SqlServiceStore store; // 다시 띄운다 — 마이그레이션은 이미 적용됐고 데이터는 그대로다
    SW_ASSERT_TRUE_MSG( store.initialize( "sqlite", settings, SqlServiceStore::kMigrationFolder, 0, error ), error.c_str() );
    store.submit( make_unique<SqliteStorePutWork>( false, &record, &result, &completedCount ) );
    TestServiceStoreSqliteInternal::settle( store, completedCount, 2 );
    SW_EXPECT_TRUE( result == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( record._bytes == vector<uint8>{ 7 } );
    SW_EXPECT_TRUE( record._version != ServiceRecord::kAbsentVersion );
    store.submit( make_unique<SqliteStorePutWork>( true, &record, &result, &completedCount ) );
    TestServiceStoreSqliteInternal::settle( store, completedCount, 3 );
    SW_EXPECT_TRUE( result == ServiceStoreResult::Conflict ); // "없어야 함" 이 이미 있는 키에 진다
    store.shutdown();
}

SW_TEST_CASE( ServiceStoreSqliteTest, FactoryPicksTheDriverByNameAndFailsLoudly )
{
    string                    error;
    unique_ptr<IServiceStore> memory = ServiceStoreFactory::createServiceStore( "memory", "", "", 1, 0, error );
    SW_EXPECT_NOT_NULL( memory );

    unique_ptr<IServiceStore> sqlite = ServiceStoreFactory::createServiceStore( "sqlite", test::makeTempPath( "factory.db" ), "", 1, 0, error );
    SW_EXPECT_TRUE_MSG( sqlite != nullptr, error.c_str() );
    if ( sqlite != nullptr )
        sqlite->shutdown();

    unique_ptr<IServiceStore> unknown = ServiceStoreFactory::createServiceStore( "mysql", "x", "", 1, 0, error );
    SW_EXPECT_NULL( unknown );
    SW_EXPECT_TRUE( error.find( "no SQL driver 'mysql'" ) != string::npos );
}
