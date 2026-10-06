#include "pch.h"

#include "EngineTest/GameFramework/Online/LocalStoreContract.h"

#include "GameFramework/Base/Online/Local/MemoryLocalStore.h"

// 메모리 로컬 저장 — 계약 아홉(왕복 · 나열 · 압축 · 봉인 셋 · 이름 · 순서 · 내리기) + 쓰기 도중 꺼짐 · 슬롯 이름 규칙.

using namespace sw;

namespace
{
    struct MemoryLocalFixture
    {
        test::LocalStoreTestKey      _key;
        MemoryLocalDatabase          _database;
        unique_ptr<MemoryLocalStore> _store;

        MemoryLocalFixture()
            : _key{}
            , _database{}
            , _store{}
        {
            restart();
        }

        ILocalStore&             getStore() { return *_store; }
        ILocalSlotStorage&       getStorage() { return _database; }
        test::LocalStoreTestKey& getKey() { return _key; }
        void                     restart() { _store = sw::make_unique<MemoryLocalStore>( &_database, test::LocalStoreContract::makeSealContext( _key ) ); }
    };
} // namespace

SW_LOCAL_STORE_CONTRACT_SUITE( LocalStoreMemoryTest, MemoryLocalFixture )

SW_TEST_CASE( LocalStoreMemoryTest, WriteInterruptedMidwayLeavesTheOldContent )
{
    MemoryLocalFixture fixture;
    SW_EXPECT_TRUE( test::LocalStoreContract::write( fixture.getStore(), "save/slot0", test::LocalStoreContract::makeText( "old" ) ) == LocalStoreResult::Ok );
    fixture._database.failNextWrite(); // 다음 쓰기 도중 꺼짐
    SW_EXPECT_TRUE( test::LocalStoreContract::write( fixture.getStore(), "save/slot0", test::LocalStoreContract::makeText( "new" ) ) == LocalStoreResult::IoError );
    fixture.restart();
    SW_EXPECT_TRUE( test::LocalStoreContract::read( fixture.getStore(), "save/slot0" )._bytes == test::LocalStoreContract::makeText( "old" ) );
}

SW_TEST_CASE( LocalStoreMemoryTest, SlotNameRules )
{
    SW_EXPECT_TRUE( ILocalStore::isValidSlotName( "save/slot0" ) );
    SW_EXPECT_TRUE( ILocalStore::isValidSlotName( "profile.v2-a_b" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( "save/Slot0" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( "save\\slot0" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( "/slot" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( "save/.." ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( string( 65, 'a' ) ) );
    SW_EXPECT_TRUE( ILocalStore::isValidGroupPrefix( "" ) );
    SW_EXPECT_TRUE( ILocalStore::isValidGroupPrefix( "save/" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidGroupPrefix( "save" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidGroupPrefix( "a/b/" ) );
}
