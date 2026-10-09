#include "pch.h"

#include "Core/String/StringUtil.h"

#include "EngineTest/GameFramework/Kits/Storage/PostgresTestSchema.h"
#include "EngineTest/GameFramework/Online/ServiceStoreContract.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Server/Driver/Postgres/PostgresDriver.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Server/ServiceStoreFactory.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Server/SqlServiceStore.h"

// SQL 서비스 저장소(PostgreSQL) — 메모리 · SQLite 와 같은 IServiceStore 계약 일곱(픽스처마다 무작위 스키마). 서버가 있어야 돈다(SW_TEST_POSTGRES_URL).

using namespace sw;

SW_TEST_REQUIRES_ENVIRONMENT( ServiceStorePostgresTest, "SW_TEST_POSTGRES_URL", "needs a PostgreSQL server" );

namespace
{
    struct PostgresStoreFixture
    {
        test::PostgresTestSchema  _schema;
        MemoryServiceDatabase     _closedDatabase;
        MemoryServiceStore        _closedStore; ///< 저장소를 못 만들었을 때 — 모든 일이 Unavailable 로 돌아 케이스가 깨끗이 진다
        unique_ptr<IServiceStore> _store;

        PostgresStoreFixture()
            : _schema{}
            , _closedDatabase{}
            , _closedStore{ &_closedDatabase }
            , _store{}
        {
            _closedStore.shutdown();
            SW_ASSERT_TRUE( _schema.isReady() );
            string error;
            _store = ServiceStoreFactory::createServiceStore( "postgres", _schema.getConnection(), "", 2, 0, error );
            SW_EXPECT_TRUE_MSG( _store != nullptr, error.c_str() );
        }

        ~PostgresStoreFixture()
        {
            if ( _store == nullptr )
                return;
            _store->shutdown();
            (void)_store->pollCompletions();
            _store.reset(); // 스키마를 지우기 전에 연결을 닫는다
        }

        IServiceStore& getStore() { return _store != nullptr ? *_store : _closedStore; }

        hashed_string makeTableName( const utf8* pCaseName )
        {
            string name{ "contract_" };
            name += StringUtil::toLower( pCaseName );
            return hashed_string( name.c_str() );
        }
    };
} // namespace

SW_SERVICE_STORE_CONTRACT_SUITE( ServiceStorePostgresTest, PostgresStoreFixture )

SW_TEST_CASE( ServiceStorePostgresTest, MigrationsApplyOnceInTheTestSchema )
{
    test::PostgresTestSchema schema;
    SW_ASSERT_TRUE( schema.isReady() );
    string error;
    for ( int32 startIndex = 0; startIndex < 2; ++startIndex ) // 두 번째 기동은 적용된 마이그레이션의 체크섬만 본다
    {
        unique_ptr<IServiceStore> store = ServiceStoreFactory::createServiceStore( "postgres", schema.getConnection(), "", 1, 0, error );
        SW_ASSERT_TRUE_MSG( store != nullptr, error.c_str() );
        store->shutdown();
    }
    unique_ptr<ISqlConnection> connection = PostgresDriver::getInstance().openConnection( schema.getConnection(), "", error );
    SW_ASSERT_TRUE_MSG( connection != nullptr, error.c_str() );
    SqlRowSet rowSet;
    SW_ASSERT_TRUE( connection->execute( "SELECT COUNT(*) FROM sw_schema_migration", nullptr, 0, &rowSet ) == SqlResult::Ok );
    SW_EXPECT_EQUAL( int64( 2 ), rowSet._listRow[0][0]._integer );
}
