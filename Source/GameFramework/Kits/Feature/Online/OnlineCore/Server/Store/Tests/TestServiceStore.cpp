#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Store/ServiceIdempotency.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/Tests/ServiceStoreEntryTestUtil.h"

// ServiceStoreTest — 서비스 저장소 트랜잭션의 약속(원장 · 우편 · 거래가 기대는 것): "없어야 한다" 조건, 버전 조건, 원자성, 키 규칙,
// 그리고 그 위의 멱등 기록. 메모리 구현으로 본다(SQL 구현도 같은 약속을 지킨다)(nogpu).

namespace
{
    const sw::hashed_string kTable{ "test_table" };

    sw::vector<uint8> makeBytesOf( sw::string_view text )
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
    sw::ServiceStoreTestAccess::putBytes( create, kTable, "a", makeBytesOf( "1" ), sw::ServiceRecord::kAbsentVersion );
    SW_REQUIRE_TRUE( database.commit( create ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( database.commit( create ) == sw::ServiceStoreResult::Conflict );

    sw::ServiceRecord read;
    SW_REQUIRE_TRUE( database.readRecord( kTable, "a", read ) == sw::ServiceStoreResult::Ok );
    sw::ServiceTransaction other;
    sw::ServiceStoreTestAccess::putBytes( other, kTable, "a", makeBytesOf( "2" ) );
    SW_REQUIRE_TRUE( database.commit( other ) == sw::ServiceStoreResult::Ok );

    sw::ServiceTransaction stale;
    sw::ServiceStoreTestAccess::putBytes( stale, kTable, "a", makeBytesOf( "3" ), read._version );
    sw::ServiceCommitInfo info;
    SW_EXPECT_TRUE( database.commit( stale, &info ) == sw::ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 0, info._conflictIndex );
    SW_REQUIRE_TRUE( database.readRecord( kTable, "a", read ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( sw::ServiceStoreTestAccess::getBytes( read ) == makeBytesOf( "2" ) );
    SW_EXPECT_TRUE( database.readRecord( kTable, "missing", read ) == sw::ServiceStoreResult::NotFound );
}

/**
 * @brief [ServiceStoreTest] 쓰기 하나의 조건이 어긋나면 트랜잭션의 어떤 쓰기도 적용되지 않는다. 틀린 키 · 같은 키 둘은 Invalid 다
 */
SW_TEST_CASE( ServiceStoreTest, CommitsAreAtomic )
{
    sw::MemoryServiceDatabase database;
    sw::ServiceTransaction    seed;
    sw::ServiceStoreTestAccess::putBytes( seed, kTable, "taken", makeBytesOf( "x" ) );
    SW_REQUIRE_TRUE( database.commit( seed ) == sw::ServiceStoreResult::Ok );
    const uint64 hashBefore = database.computeContentHash();

    sw::ServiceTransaction mixed;
    sw::ServiceStoreTestAccess::putBytes( mixed, kTable, "fresh", makeBytesOf( "y" ), sw::ServiceRecord::kAbsentVersion );
    sw::ServiceStoreTestAccess::putBytes( mixed, kTable, "taken", makeBytesOf( "z" ), sw::ServiceRecord::kAbsentVersion ); // 어긋난다
    sw::ServiceCommitInfo info;
    SW_EXPECT_TRUE( database.commit( mixed, &info ) == sw::ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 1, info._conflictIndex );
    SW_EXPECT_EQUAL( hashBefore, database.computeContentHash() );
    sw::ServiceRecord read;
    SW_EXPECT_TRUE( database.readRecord( kTable, "fresh", read ) == sw::ServiceStoreResult::NotFound );

    sw::ServiceTransaction badKey;
    sw::ServiceStoreTestAccess::putBytes( badKey, kTable, "Upper Case", makeBytesOf( "k" ) );
    SW_EXPECT_TRUE( database.commit( badKey ) == sw::ServiceStoreResult::Invalid );
    sw::ServiceTransaction twice;
    sw::ServiceStoreTestAccess::putBytes( twice, kTable, "dup", makeBytesOf( "1" ) );
    sw::ServiceStoreTestAccess::putBytes( twice, kTable, "dup", makeBytesOf( "2" ) );
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

    const auto write = []( sw::IServiceStoreConnection& connection, const utf8* pEffect, const utf8* pReply ) -> sw::ServiceStoreResult
    {
        return sw::ServiceCommitRunner::run<sw::ServiceStoreResult>( connection, "Test.Idempotency", [&]( sw::ServiceCommitAttempt& attempt ) -> sw::optional<sw::ServiceStoreResult>
        {
            sw::ServiceTransaction transaction;
            sw::ServiceStoreTestAccess::putBytes( transaction, kTable, "effect", makeBytesOf( pEffect ) );
            return sw::ServiceIdempotency::commit( attempt, transaction, "a1", 7, 9, makeBytesOf( pReply ) ); // 충돌도 답으로 받는다
        } );
    };
    sw::UnreliableStoreConnection lostReply( database );
    lostReply._lostReplyCount = 1;
    SW_REQUIRE_TRUE( write( lostReply, "done", "reply-1" ) == sw::ServiceStoreResult::Ok ); // 쓰고 응답을 잃었다 — 기록이 있으면 적용된 것이다
    SW_REQUIRE_TRUE( sw::ServiceIdempotency::findReply( database, "a1", 7, 9, reply ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( reply == makeBytesOf( "reply-1" ) );

    SW_EXPECT_TRUE( write( database, "done-again", "reply-2" ) == sw::ServiceStoreResult::Conflict ); // 다른 서버가 같은 요청을 동시에 처리했다
    sw::ServiceRecord effect;
    SW_REQUIRE_TRUE( database.readRecord( kTable, "effect", effect ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( sw::ServiceStoreTestAccess::getBytes( effect ) == makeBytesOf( "done" ) );

    SW_EXPECT_TRUE( sw::ServiceIdempotency::findReply( database, "a2", 7, 9, reply ) == sw::ServiceStoreResult::NotFound ); // 범위가 다르면 다른 요청
}
