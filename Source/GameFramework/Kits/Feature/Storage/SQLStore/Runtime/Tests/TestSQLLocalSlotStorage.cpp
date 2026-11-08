#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQLLocalSlotStorage.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/Tests/LocalStoreContract.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/ThreadedLocalStore.h"

// LocalStoreSQLiteTest · SQLLocalSlotStorageTest — SQLite 바닥이 메모리 · 파일 바닥과 같은 계약 아홉을 지키고(`LocalStoreContract.h` 머리말의 약속), 바닥 자체의 슬롯 왕복 · 묶음 나열 · 주입 없음을 지킨다(nogpu).

using namespace sw;

namespace
{
    /**
     * @brief 전용 스레드 앞 + SQLite 파일 바닥입니다. 바닥을 직접 보는 둘째 연결을 함께 듭니다(SQLite 는 연결마다 한 스레드라 저장소가 쉬는 동안만 쓴다).
     * @details 암호 제공자는 키트 밖에 내보내지 않으므로(`OpenSSLNetSecurityProvider` 는 export 없음) 기반이 공개한 서비스를 쓴다.
     */
    struct SQLiteLocalFixture
    {
        test::LocalStoreTestKey         _key{};
        string                          _path{ test::makeScratchPath( SQLLocalSlotStorage::kDatabaseFileName ) };
        unique_ptr<SQLLocalSlotStorage> _directStorage{};
        unique_ptr<ThreadedLocalStore>  _store{};

        SQLiteLocalFixture()
        {
            _directStorage = open( _path );
            restart();
        }

        ILocalStore&             getStore() { return *_store; }
        ILocalSlotStorage&       getStorage() { return *_directStorage; }
        test::LocalStoreTestKey& getKey() { return _key; }

        void restart()
        {
            _store.reset();
            LocalSealContext context;
            context._pSecurityProvider = &game::getSubsystem<INetSecurityProvider>();
            context._pKeyProvider      = &_key;
            _store                     = sw::make_unique<ThreadedLocalStore>( open( _path ), context );
        }

        static unique_ptr<SQLLocalSlotStorage> open( string_view path )
        {
            unique_ptr<SQLLocalSlotStorage> storage = sw::make_unique<SQLLocalSlotStorage>();
            const expected<void>            opened  = storage->initialize( path );
            SW_EXPECT_TRUE_MSG( opened.has_value(), opened.has_value() ? string_view{} : opened.error().message() );
            return storage;
        }
    };

    struct SQLLocalSlotStorageTestUtil
    {
        static vector<uint8> makeBytes( string_view text ) { return vector<uint8>( text.begin(), text.end() ); }
    };
} // namespace

SW_LOCAL_STORE_CONTRACT_SUITE( LocalStoreSQLiteTest, SQLiteLocalFixture )

SW_TEST_CASE( SQLLocalSlotStorageTest, SlotRoundTripAndMissingSlots )
{
    unique_ptr<SQLLocalSlotStorage> storage = SQLiteLocalFixture::open( SQLLocalSlotStorage::kMemoryDatabasePath );
    vector<uint8>                   bytes;
    SW_EXPECT_TRUE( storage->readSlot( "a", bytes ) == LocalStoreResult::NotFound );
    SW_EXPECT_TRUE( storage->eraseSlot( "a" ) == LocalStoreResult::NotFound );

    const vector<uint8> binary{ 0x00, 0xFF, 0x00, 0x10 };
    SW_EXPECT_TRUE( storage->writeSlot( "a", binary ) == LocalStoreResult::Ok );
    SW_REQUIRE_TRUE( storage->readSlot( "a", bytes ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( bytes == binary );

    SW_EXPECT_TRUE( storage->writeSlot( "a", SQLLocalSlotStorageTestUtil::makeBytes( "second" ) ) == LocalStoreResult::Ok ); // upsert
    SW_REQUIRE_TRUE( storage->readSlot( "a", bytes ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( bytes == SQLLocalSlotStorageTestUtil::makeBytes( "second" ) );

    SW_EXPECT_TRUE( storage->writeSlot( "empty", vector<uint8>{} ) == LocalStoreResult::Ok ); // 빈 몸도 슬롯이다(NOT NULL 표에 들어간다)
    SW_REQUIRE_TRUE( storage->readSlot( "empty", bytes ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( bytes.empty() );

    SW_EXPECT_TRUE( storage->eraseSlot( "a" ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( storage->readSlot( "a", bytes ) == LocalStoreResult::NotFound );
}

/** @brief 나열은 묶음 접두어의 범위다 — 접두어가 같은 다른 묶음(`save2/`)이나 LIKE 와일드카드 글자가 끼지 않고, 크기와 쓴 시각을 채운다. */
SW_TEST_CASE( SQLLocalSlotStorageTest, ListIsARangeInNameOrder )
{
    unique_ptr<SQLLocalSlotStorage> storage = SQLiteLocalFixture::open( SQLLocalSlotStorage::kMemoryDatabasePath );
    SW_REQUIRE_TRUE( storage->writeSlot( "save/b", SQLLocalSlotStorageTestUtil::makeBytes( "bbb" ) ) == LocalStoreResult::Ok );
    SW_REQUIRE_TRUE( storage->writeSlot( "save/a", SQLLocalSlotStorageTestUtil::makeBytes( "a" ) ) == LocalStoreResult::Ok );
    SW_REQUIRE_TRUE( storage->writeSlot( "save2/x", SQLLocalSlotStorageTestUtil::makeBytes( "x" ) ) == LocalStoreResult::Ok );
    SW_REQUIRE_TRUE( storage->writeSlot( "cfg", SQLLocalSlotStorageTestUtil::makeBytes( "c" ) ) == LocalStoreResult::Ok );

    vector<LocalSlotInfo> listInfo;
    SW_REQUIRE_TRUE( storage->listSlots( "save/", listInfo ) == LocalStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 2 ), listInfo.size() );
    SW_EXPECT_EQUAL( string( "save/a" ), listInfo[0]._slot );
    SW_EXPECT_EQUAL( string( "save/b" ), listInfo[1]._slot );
    SW_EXPECT_EQUAL( int64( 3 ), listInfo[1]._byteCount );
    SW_EXPECT_TRUE( listInfo[1]._writtenAtMs > 0 );

    listInfo.clear();
    SW_REQUIRE_TRUE( storage->listSlots( "", listInfo ) == LocalStoreResult::Ok );
    SW_EXPECT_EQUAL( size_t( 4 ), listInfo.size() );

    listInfo.clear();
    SW_REQUIRE_TRUE( storage->listSlots( "%", listInfo ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( listInfo.empty() );
}

/** @brief 슬롯 이름은 바닥까지 값으로 간다 — 앞의 이름 규칙을 지나지 않고 바닥을 직접 불러도 표가 그대로다. */
SW_TEST_CASE( SQLLocalSlotStorageTest, HostileSlotNameStaysData )
{
    unique_ptr<SQLLocalSlotStorage> storage = SQLiteLocalFixture::open( SQLLocalSlotStorage::kMemoryDatabasePath );
    const string                    hostile = "x'; DROP TABLE sw_local_slot; --";
    SW_EXPECT_TRUE( storage->writeSlot( hostile, SQLLocalSlotStorageTestUtil::makeBytes( "v" ) ) == LocalStoreResult::Ok );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( storage->readSlot( hostile, bytes ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( bytes == SQLLocalSlotStorageTestUtil::makeBytes( "v" ) );
    SW_EXPECT_TRUE( storage->eraseSlot( hostile ) == LocalStoreResult::Ok );
}

/** @brief 없는 폴더는 바닥이 만들고, 다시 열면 데이터가 남고 마이그레이션은 다시 돌지 않는다. */
SW_TEST_CASE( SQLLocalSlotStorageTest, FileDatabaseSurvivesReopening )
{
    const string path = FileUtil::joinPath( test::makeScratchPath( "nested" ), SQLLocalSlotStorage::kDatabaseFileName );
    {
        unique_ptr<SQLLocalSlotStorage> storage = SQLiteLocalFixture::open( path );
        SW_REQUIRE_TRUE( storage->writeSlot( "kept", SQLLocalSlotStorageTestUtil::makeBytes( "v" ) ) == LocalStoreResult::Ok );
    }
    unique_ptr<SQLLocalSlotStorage> storage = SQLiteLocalFixture::open( path );
    vector<uint8>                   bytes;
    SW_REQUIRE_TRUE( storage->readSlot( "kept", bytes ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( bytes == SQLLocalSlotStorageTestUtil::makeBytes( "v" ) );
}
