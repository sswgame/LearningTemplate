#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceIdempotency.h"

// ServiceStoreTest — 서비스 저장소 트랜잭션의 약속(원장 · 우편 · 거래가 기대는 것): "없어야 한다" 조건, 버전 조건, 원자성, 키 규칙,
// 그리고 그 위의 멱등 기록. 메모리 구현으로 본다(SQL 구현도 같은 약속을 지킨다)(nogpu).

namespace
{
    const sw::hashed_string kTable{ "test_table" };

    sw::vector<uint8> bytesOf( sw::string_view text )
    {
        return sw::vector<uint8>( text.begin(), text.end() );
    }
} // namespace

/**
 * @brief [ServiceStoreTest] "없어야 한다" 조건은 한 번만 이기고, 읽은 버전으로 건 조건은 그 사이 다른 커밋이 있으면 Conflict 다
 */
SW_TEST_CASE( ServiceStoreTest, VersionConditions )
{
    sw::MemoryServiceDatabase database;
    sw::ServiceTransaction    create;
    create.put( kTable, "a", bytesOf( "1" ), sw::ServiceRecord::kAbsentVersion );
    SW_REQUIRE_TRUE( database.commit( create ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( database.commit( create ) == sw::ServiceStoreResult::Conflict );

    sw::ServiceRecord read;
    SW_REQUIRE_TRUE( database.readRecord( kTable, "a", read ) == sw::ServiceStoreResult::Ok );
    sw::ServiceTransaction other;
    other.put( kTable, "a", bytesOf( "2" ) );
    SW_REQUIRE_TRUE( database.commit( other ) == sw::ServiceStoreResult::Ok );

    sw::ServiceTransaction stale;
    stale.put( kTable, "a", bytesOf( "3" ), read._version );
    sw::ServiceCommitInfo info;
    SW_EXPECT_TRUE( database.commit( stale, &info ) == sw::ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 0, info._conflictIndex );
    SW_REQUIRE_TRUE( database.readRecord( kTable, "a", read ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( read._bytes == bytesOf( "2" ) );
    SW_EXPECT_TRUE( database.readRecord( kTable, "missing", read ) == sw::ServiceStoreResult::NotFound );
}

/**
 * @brief [ServiceStoreTest] 쓰기 하나의 조건이 어긋나면 트랜잭션의 어떤 쓰기도 적용되지 않는다. 틀린 키 · 같은 키 둘은 Invalid 다
 */
SW_TEST_CASE( ServiceStoreTest, CommitsAreAtomic )
{
    sw::MemoryServiceDatabase database;
    sw::ServiceTransaction    seed;
    seed.put( kTable, "taken", bytesOf( "x" ) );
    SW_REQUIRE_TRUE( database.commit( seed ) == sw::ServiceStoreResult::Ok );
    const uint64 hashBefore = database.computeContentHash();

    sw::ServiceTransaction mixed;
    mixed.put( kTable, "fresh", bytesOf( "y" ), sw::ServiceRecord::kAbsentVersion );
    mixed.put( kTable, "taken", bytesOf( "z" ), sw::ServiceRecord::kAbsentVersion ); // 어긋난다
    sw::ServiceCommitInfo info;
    SW_EXPECT_TRUE( database.commit( mixed, &info ) == sw::ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 1, info._conflictIndex );
    SW_EXPECT_EQUAL( hashBefore, database.computeContentHash() );
    sw::ServiceRecord read;
    SW_EXPECT_TRUE( database.readRecord( kTable, "fresh", read ) == sw::ServiceStoreResult::NotFound );

    sw::ServiceTransaction badKey;
    badKey.put( kTable, "Upper Case", bytesOf( "k" ) );
    SW_EXPECT_TRUE( database.commit( badKey ) == sw::ServiceStoreResult::Invalid );
    sw::ServiceTransaction twice;
    twice.put( kTable, "dup", bytesOf( "1" ) );
    twice.put( kTable, "dup", bytesOf( "2" ) );
    SW_EXPECT_TRUE( database.commit( twice ) == sw::ServiceStoreResult::Invalid );
    SW_EXPECT_TRUE( database.commit( sw::ServiceTransaction{} ) == sw::ServiceStoreResult::Invalid );
    SW_EXPECT_EQUAL( hashBefore, database.computeContentHash() );
}

/**
 * @brief [ServiceStoreTest] 멱등 기록 — 처음은 NotFound, 효과와 같은 트랜잭션으로 넣은 뒤에는 그때의 응답을 돌려준다. 같은 키로 둘째 기록은 Conflict 다
 */
SW_TEST_CASE( ServiceStoreTest, IdempotencyRecordsTheFirstReply )
{
    sw::MemoryServiceDatabase database;
    sw::vector<uint8>         reply;
    SW_EXPECT_TRUE( sw::ServiceIdempotency::findReply( database, "a1", 7, 9, reply ) == sw::ServiceStoreResult::NotFound );

    sw::ServiceTransaction first;
    first.put( kTable, "effect", bytesOf( "done" ) );
    sw::ServiceIdempotency::addReply( first, "a1", 7, 9, bytesOf( "reply-1" ) );
    SW_REQUIRE_TRUE( database.commit( first ) == sw::ServiceStoreResult::Ok );
    SW_REQUIRE_TRUE( sw::ServiceIdempotency::findReply( database, "a1", 7, 9, reply ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( reply == bytesOf( "reply-1" ) );

    sw::ServiceTransaction retry; // 다른 서버가 같은 요청을 동시에 처리했다
    retry.put( kTable, "effect", bytesOf( "done-again" ) );
    sw::ServiceIdempotency::addReply( retry, "a1", 7, 9, bytesOf( "reply-2" ) );
    SW_EXPECT_TRUE( database.commit( retry ) == sw::ServiceStoreResult::Conflict );
    sw::ServiceRecord effect;
    SW_REQUIRE_TRUE( database.readRecord( kTable, "effect", effect ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( effect._bytes == bytesOf( "done" ) );

    SW_EXPECT_TRUE( sw::ServiceIdempotency::findReply( database, "a2", 7, 9, reply ) == sw::ServiceStoreResult::NotFound ); // 범위가 다르면 다른 요청
}
