#include "pch.h"

#include "Core/File/FileUtil.h"

#include "EngineTest/GameFramework/Online/LocalStoreContract.h"

#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"
#include "GameFramework/Base/Online/Local/ThreadedLocalStore.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLLocalSlotStorage.h"

// SQLite 로컬 저장(전용 스레드 + SQLite 바닥, GF_SQLStore) — 메모리 · 파일과 같은 계약 아홉 + 공장에 "sqlite" 를 올려 쓰기.

using namespace sw;

namespace
{
    struct SQLiteLocalFixture
    {
        test::LocalStoreTestKey        _key;
        string                         _databasePath;
        SQLLocalSlotStorage            _directStorage; ///< 같은 DB 파일의 두 번째 연결 — 저장소가 쉬는 동안 봉투를 직접 본다
        unique_ptr<ThreadedLocalStore> _store;

        SQLiteLocalFixture()
            : _key{}
            , _databasePath{ FileUtil::joinPath( test::makeTempDirectory( "local_store_sqlite" ), SQLLocalSlotStorage::kDatabaseFileName ) }
            , _directStorage{}
            , _store{}
        {
            restart();
            string error;
            SW_EXPECT_TRUE_MSG( _directStorage.initialize( _databasePath, error ), error.c_str() );
        }

        ILocalStore&             getStore() { return *_store; }
        ILocalSlotStorage&       getStorage() { return _directStorage; }
        test::LocalStoreTestKey& getKey() { return _key; }

        void restart()
        {
            _store.reset();
            unique_ptr<SQLLocalSlotStorage> storage = sw::make_unique<SQLLocalSlotStorage>();
            string                          error;
            SW_EXPECT_TRUE_MSG( storage->initialize( _databasePath, error ), error.c_str() );
            _store = sw::make_unique<ThreadedLocalStore>( std::move( storage ), test::LocalStoreContract::makeSealContext( _key ) );
        }
    };
} // namespace

SW_LOCAL_STORE_CONTRACT_SUITE( LocalStoreSQLiteTest, SQLiteLocalFixture )

SW_TEST_CASE( LocalStoreSQLiteTest, FactoryUsesTheRegisteredSQLiteBackend )
{
    SW_ASSERT_TRUE( SQLLocalSlotStorage::registerLocalStoreBackend() );
    SW_EXPECT_TRUE( SQLLocalSlotStorage::registerLocalStoreBackend() ); // 같은 함수를 다시 올려도 된다
    test::LocalStoreTestKey key;
    LocalStoreSettings      settings;
    settings._backend = SQLLocalSlotStorage::kBackendName;
    settings._root    = test::makeTempDirectory( "local_store_sqlite_factory" );
    string                  error;
    unique_ptr<ILocalStore> store = LocalStoreFactory::createLocalStore( "UnitGame", settings, test::LocalStoreContract::makeSealContext( key ), error );
    SW_ASSERT_TRUE_MSG( store != nullptr, error.c_str() );
    SW_EXPECT_TRUE( test::LocalStoreContract::write( *store, "save/a", test::LocalStoreContract::makeText( "sql" ) ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( test::LocalStoreContract::read( *store, "save/a" )._bytes == test::LocalStoreContract::makeText( "sql" ) );
    store.reset();
    SW_EXPECT_TRUE( FileUtil::isRegularFile( FileUtil::joinPath( settings._root, SQLLocalSlotStorage::kDatabaseFileName ) ) );
    SQLLocalSlotStorage::unregisterLocalStoreBackend();
    error.clear();
    SW_EXPECT_TRUE( LocalStoreFactory::createLocalStore( "UnitGame", settings, test::LocalStoreContract::makeSealContext( key ), error ) == nullptr );
}
