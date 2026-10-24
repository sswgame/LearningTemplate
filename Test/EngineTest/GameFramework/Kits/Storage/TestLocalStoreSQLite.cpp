#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "EngineTest/GameFramework/Online/LocalStoreContract.h"

#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"
#include "GameFramework/Base/Online/Local/ThreadedLocalStore.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQLLocalSlotStorage.h"

// SQLite 로컬 저장(전용 스레드 + SQLite 바닥, GF_SQLStore) — 메모리 · 파일과 같은 계약 아홉 + 키트가 로드 때 올린 "sqlite" 를 공장으로 쓰기 + 이미지 해제 때 바닥이 빠지기.

using namespace sw;

namespace
{
    struct LocalStoreSQLiteTestInternal
    {
        static constexpr const utf8* kFailingStorageError = "unit backend refuses to open";

        static unique_ptr<ILocalSlotStorage> createFailingStorage( string_view rootPath, string& outError )
        {
            (void)rootPath;
            outError = kFailingStorageError;
            return nullptr;
        }
    };

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

SW_TEST_CASE( LocalStoreSQLiteTest, FactoryUsesTheBackendTheKitRegisteredOnLoad )
{
    // 키트 모듈이 로드될 때 정적 등록 객체가 "sqlite" 를 올렸다 — 게임이 등록을 부르지 않아도 설정만으로 쓴다.
    test::LocalStoreTestKey key;
    LocalStoreConfig        settings;
    settings._backend = SQLLocalSlotStorage::kBackendName;
    settings._root    = test::makeTempDirectory( "local_store_sqlite_factory" );
    string                  error;
    unique_ptr<ILocalStore> store = LocalStoreFactory::createLocalStore( "UnitGame", settings, test::LocalStoreContract::makeSealContext( key ), error );
    SW_ASSERT_TRUE_MSG( store != nullptr, error.c_str() );
    SW_EXPECT_TRUE( test::LocalStoreContract::write( *store, "save/a", test::LocalStoreContract::makeText( "sql" ) ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( test::LocalStoreContract::read( *store, "save/a" )._bytes == test::LocalStoreContract::makeText( "sql" ) );
    store.reset();
    SW_EXPECT_TRUE( FileUtil::isRegularFile( FileUtil::joinPath( settings._root, SQLLocalSlotStorage::kDatabaseFileName ) ) );
}

SW_TEST_CASE( LocalStoreSQLiteTest, BackendIsDroppedWhenItsImageIsReleased )
{
    // 핫 리로드는 이미지를 내리기 전에 그 범위의 코드를 뗀다(`ModuleImageUtil::releaseModuleCode`). 정적 등록 객체는 언로드가 미뤄져 나중에 소멸하므로
    // 바닥 표가 리스너로서 스스로 지워야 한다. 범위를 만들기 함수 한 바이트로 좁혀 이 테스트의 바닥만 뗀다.
    LocalStoreBackendRegistration registration{ "unit-released", LocalStoreSQLiteTestInternal::createFailingStorage };
    test::LocalStoreTestKey       key;
    LocalStoreConfig              settings;
    settings._backend = "unit-released";
    settings._root    = test::makeTempDirectory( "local_store_released_backend" );
    string error;
    SW_EXPECT_NULL( LocalStoreFactory::createLocalStore( "UnitGame", settings, test::LocalStoreContract::makeSealContext( key ), error ) );
    SW_EXPECT_EQUAL( string( LocalStoreSQLiteTestInternal::kFailingStorageError ), error );

    const utf8*                                  pCode = reinterpret_cast<const utf8*>( &LocalStoreSQLiteTestInternal::createFailingStorage );
    vector<IModuleUnloadListener::ReleaseResult> listResult;
    IModuleUnloadListener::removeAllWithin( pCode, pCode + 1, listResult );
    error.clear();
    SW_EXPECT_NULL( LocalStoreFactory::createLocalStore( "UnitGame", settings, test::LocalStoreContract::makeSealContext( key ), error ) );
    SW_EXPECT_TRUE( error.find( "is not registered" ) != string::npos );
}
