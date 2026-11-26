// HubProfileRecordTest — 허브 진행 레코드는 진행 표(`HubProfileStore::getTable`)의 형식이다 — 칸 선언(`HubProfile` 의 범위)이 쓰기와 읽기를 막는다.
// 레코드는 왕복하고, 깨진 바이트 · 범위 밖 값은 쓰지도 읽지도 않는다(nogpu, 에셋 없음).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/Server/Tests/ServiceStoreEntryTestUtil.h"

#include "Games/Playground/Hub/Server/HubProfileStore.h"

using namespace sw;

/** @brief [HubProfileRecordTest] 레코드가 같은 값으로 돌아오고, 깨진 바이트와 범위 밖 진행은 거절된다. */
SW_TEST_CASE( HubProfileRecordTest, RecordRoundTripsAndRefusesCorruption )
{
    HubProfile saved;
    saved._level       = 7;
    saved._experience  = 1234;
    saved._coins       = 56;
    saved._herbCount   = 3;
    saved._defeatCount = 21;
    saved._position    = float3{ -12.5f, 0.0f, 40.25f };
    MemoryServiceDatabase database;
    ServiceTransaction    transaction;
    transaction.put( HubProfileStore::getTable(), "a1", saved );
    SW_REQUIRE_TRUE( ServiceCommitRunner::commitOnce( database, transaction ) == ServiceStoreResult::Ok );
    HubProfile loaded;
    SW_REQUIRE_TRUE( HubProfileStore::getTable().read( database, "a1", loaded ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 7, loaded._level );
    SW_EXPECT_EQUAL( 1234, loaded._experience );
    SW_EXPECT_EQUAL( 56, loaded._coins );
    SW_EXPECT_EQUAL( 3, loaded._herbCount );
    SW_EXPECT_EQUAL( 21, loaded._defeatCount );
    SW_EXPECT_TRUE( loaded._position == saved._position );

    SW_EXPECT_LOG( "ServiceRecordUtil.DurableRecordVersionBytes" );
    ServiceTransaction broken;
    ServiceStoreTestAccess::putBytes( broken, HubProfileStore::getTable(), "a2", vector<uint8>{ uint8( 0xFF ) } );
    SW_REQUIRE_TRUE( ServiceCommitRunner::commitOnce( database, broken ) == ServiceStoreResult::Ok );
    HubProfile untouched;
    untouched._coins = 5;
    SW_EXPECT_TRUE( HubProfileStore::getTable().read( database, "a2", untouched ) == ServiceStoreResult::Corrupt );
    SW_EXPECT_EQUAL( 5, untouched._coins );

    SW_EXPECT_LOG( "ServiceRecordUtil.RecordVersionNotWritten" );
    HubProfile levelZero;
    levelZero._level = 0; // `Min = 1` 밖
    vector<uint8> refused;
    SW_EXPECT_FALSE( HubProfileStore::encodeRecord( levelZero, refused ) );
    SW_EXPECT_TRUE( refused.empty() );
}
