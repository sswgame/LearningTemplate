// ServiceRecordTableTest — 표 형식: 레코드 표는 값을 형식으로 쓰고 읽고(범위 밖은 쓰지 않아 커밋이 Invalid), 깨진 값은 Corrupt 로 없음과 갈린다.
// 연결 표는 id 를 왕복하고 0 · 깨진 값을 거절한다(결함: 깨진 연결을 0 = 없음으로 읽어 세션 폐기가 산 세션을 놓쳤다). 색인 표는 값이 없다.
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/Server/Tests/ServiceStoreEntryTestUtil.h"
#include "GameFramework/Runtime/Online/Store/Tests/ServiceRecordUtilTestTypes.h"

using namespace sw;

namespace
{
    const ServiceRecordTable<ServiceRecordTestRanged> kRangedTable{ "test_ranged" };
    const ServiceLinkTable                            kLinkTable{ "test_link" };
    const ServiceIndexTable                           kIndexTable{ "test_index" };

    ServiceStoreResult putBroken( MemoryServiceDatabase& database, const hashed_string& table, string_view key )
    {
        ServiceTransaction transaction;
        ServiceStoreTestAccess::putBytes( transaction, table, key, vector<uint8>{ uint8( 0xFF ) } ); // 끝나지 않은 가변 정수 · 잘린 판 머리
        return ServiceCommitRunner::commitOnce( database, transaction );
    }
} // namespace

/** @brief [ServiceRecordTableTest] 레코드는 형식으로 왕복하고, 없으면 NotFound(값 그대로), 깨졌으면 Corrupt(값 그대로)다 */
SW_TEST_CASE( ServiceRecordTableTest, RecordRoundTripsAndCorruptIsNotAbsent )
{
    MemoryServiceDatabase   database;
    ServiceRecordTestRanged written;
    written._count = 7;
    ServiceTransaction transaction;
    transaction.put( kRangedTable, "a", written );
    SW_REQUIRE_TRUE( ServiceCommitRunner::commitOnce( database, transaction ) == ServiceStoreResult::Ok );

    ServiceRecordTestRanged read;
    uint64                  version = 0;
    SW_REQUIRE_TRUE( kRangedTable.read( database, "a", read, &version ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 7, read._count );
    SW_EXPECT_NOT_EQUAL( ServiceRecord::kAbsentVersion, version );

    ServiceRecordTestRanged untouched;
    untouched._count = 3;
    SW_EXPECT_TRUE( kRangedTable.read( database, "missing", untouched, &version ) == ServiceStoreResult::NotFound );
    SW_EXPECT_EQUAL( ServiceRecord::kAbsentVersion, version );

    SW_REQUIRE_TRUE( putBroken( database, kRangedTable, "broken" ) == ServiceStoreResult::Ok );
    SW_EXPECT_LOG( "ServiceRecordUtil.DurableRecordVersionBytes" );
    SW_EXPECT_TRUE( kRangedTable.read( database, "broken", untouched ) == ServiceStoreResult::Corrupt );
    SW_EXPECT_EQUAL( 3, untouched._count );
}

/** @brief [ServiceRecordTableTest] 선언 범위 밖 값은 쓰지 않는다 — 그 트랜잭션은 Invalid 이고 같은 트랜잭션의 다른 쓰기도 남지 않는다 */
SW_TEST_CASE( ServiceRecordTableTest, OutOfRangeRecordFailsTheTransaction )
{
    SW_EXPECT_LOG( "ServiceRecordUtil.RecordVersionNotWritten" );
    MemoryServiceDatabase   database;
    ServiceRecordTestRanged bad;
    bad._count = 11;
    ServiceTransaction transaction;
    transaction.put( kIndexTable, "kept-out" );
    transaction.put( kRangedTable, "a", bad );
    SW_EXPECT_TRUE( ServiceCommitRunner::commitOnce( database, transaction ) == ServiceStoreResult::Invalid );
    SW_EXPECT_EQUAL( 0, database.countRecords( kIndexTable ) );
    SW_EXPECT_EQUAL( 0, database.countRecords( kRangedTable ) );
}

/** @brief [ServiceRecordTableTest] 연결은 id 를 왕복한다. id 0 은 쓰지 않고, 깨진 연결은 Corrupt 다(받을 id 는 그대로). 색인 줄은 값이 없다 */
SW_TEST_CASE( ServiceRecordTableTest, LinkRefusesZeroAndBrokenBytes )
{
    MemoryServiceDatabase database;
    ServiceTransaction    transaction;
    transaction.put( kLinkTable, "max", ~0ull );
    transaction.put( kIndexTable, "row" );
    SW_REQUIRE_TRUE( ServiceCommitRunner::commitOnce( database, transaction ) == ServiceStoreResult::Ok );
    uint64 id = 0;
    SW_REQUIRE_TRUE( kLinkTable.read( database, "max", id ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( ~0ull, id );
    ServiceRecord row;
    SW_REQUIRE_TRUE( database.readRecord( kIndexTable, "row", row ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( ServiceStoreTestAccess::getBytes( row ).empty() );

    SW_EXPECT_LOG( "ServiceLinkTable.LinkToNoID" );
    ServiceTransaction zero;
    zero.put( kLinkTable, "zero", 0 );
    SW_EXPECT_TRUE( ServiceCommitRunner::commitOnce( database, zero ) == ServiceStoreResult::Invalid );

    SW_REQUIRE_TRUE( putBroken( database, kLinkTable, "broken" ) == ServiceStoreResult::Ok );
    SW_EXPECT_LOG( "ServiceLinkTable.LinkRecordUnreadable" );
    uint64 untouched = 7;
    SW_EXPECT_TRUE( kLinkTable.read( database, "broken", untouched ) == ServiceStoreResult::Corrupt );
    SW_EXPECT_EQUAL( uint64( 7 ), untouched );
}
