#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

// ServiceAuditLogTest — 감사 로그: 효과와 같은 트랜잭션(효과가 지면 줄도 없다), 같은 고유 값은 한 줄, 최근 것부터 · 커서, 상한을 넘는 줄은 효과까지 Invalid,
// 키의 시각 읽기(형식이 틀리면 거절)(nogpu).

using namespace sw;

namespace
{
    const hashed_string& getEffectTable()
    {
        static const hashed_string s_table{ "audit_effect" };
        return s_table;
    }

    ServiceAuditEntry makeEntry( string_view subject, int64 timeMs )
    {
        ServiceAuditEntry entry;
        entry._actor   = "gm.0000000000000001";
        entry._action  = "admin.sanction";
        entry._subject = string{ subject };
        entry._before  = "{\"banned\":false}";
        entry._after   = "{\"banned\":true}";
        entry._memo    = "spam";
        entry._timeMs  = timeMs;
        return entry;
    }
} // namespace

SW_TEST_CASE( ServiceAuditLogTest, EntryCommitsOnlyWithItsEffect )
{
    MemoryServiceDatabase database;
    ServiceTransaction    seed;
    seed.put( getEffectTable(), "balance", vector<uint8>{ 1 } );
    SW_REQUIRE_TRUE( database.commit( seed ) == ServiceStoreResult::Ok );

    ServiceTransaction lost; // 효과가 Conflict 면 감사 줄도 없다
    lost.put( getEffectTable(), "balance", vector<uint8>{ 2 }, ServiceRecord::kAbsentVersion );
    ServiceAuditLog::stageEntry( lost, makeEntry( "acct.01", 10 ), 1, 1 );
    SW_EXPECT_TRUE( database.commit( lost ) == ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 0, database.countRecords( ServiceAuditLog::getTable() ) );

    ServiceTransaction applied;
    applied.put( getEffectTable(), "balance", vector<uint8>{ 3 } );
    ServiceAuditLog::stageEntry( applied, makeEntry( "acct.01", 10 ), 1, 1 );
    SW_EXPECT_TRUE( database.commit( applied ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 1, database.countRecords( ServiceAuditLog::getTable() ) );

    ServiceTransaction retried; // 같은 고유 값(멱등 키)의 재시도는 줄을 둘 만들지 않는다
    retried.put( getEffectTable(), "balance", vector<uint8>{ 4 } );
    ServiceAuditLog::stageEntry( retried, makeEntry( "acct.01", 10 ), 1, 1 );
    SW_EXPECT_TRUE( database.commit( retried ) == ServiceStoreResult::Conflict );
    SW_EXPECT_EQUAL( 1, database.countRecords( ServiceAuditLog::getTable() ) );
}

SW_TEST_CASE( ServiceAuditLogTest, ListsNewestFirstWithCursor )
{
    MemoryServiceDatabase database;
    for ( int64 timeMs = 1; timeMs <= 5; ++timeMs )
    {
        ServiceTransaction transaction;
        ServiceAuditLog::stageEntry( transaction, makeEntry( "acct.01", timeMs ), 0, static_cast<uint64>( timeMs ) );
        SW_REQUIRE_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
    }
    ServiceTransaction other;
    ServiceAuditLog::stageEntry( other, makeEntry( "acct.02", 9 ), 0, 9 );
    SW_REQUIRE_TRUE( database.commit( other ) == ServiceStoreResult::Ok );

    vector<ServiceAuditEntry> listEntry;
    string                    cursor;
    SW_REQUIRE_TRUE( ServiceAuditLog::listEntries( database, "acct.01/", "", 2, listEntry, cursor ) == ServiceStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 2 ), listEntry.size() );
    SW_EXPECT_EQUAL( int64( 5 ), listEntry[0]._timeMs );
    SW_EXPECT_EQUAL( int64( 4 ), listEntry[1]._timeMs );
    SW_EXPECT_TRUE( listEntry[0]._actor == "gm.0000000000000001" && listEntry[0]._action == "admin.sanction" && listEntry[0]._subject == "acct.01" );
    SW_EXPECT_TRUE( listEntry[0]._before == "{\"banned\":false}" && listEntry[0]._after == "{\"banned\":true}" && listEntry[0]._memo == "spam" );
    SW_EXPECT_FALSE( cursor.empty() );

    listEntry.clear();
    const string secondCursor = cursor;
    SW_REQUIRE_TRUE( ServiceAuditLog::listEntries( database, "acct.01/", secondCursor, 10, listEntry, cursor ) == ServiceStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 3 ), listEntry.size() ); // 다른 주체(acct.02)는 섞이지 않는다
    SW_EXPECT_EQUAL( int64( 1 ), listEntry[2]._timeMs );
    SW_EXPECT_TRUE( cursor.empty() );
}

SW_TEST_CASE( ServiceAuditLogTest, OversizedEntryMakesTheWholeCommitInvalid )
{
    MemoryServiceDatabase database;
    ServiceAuditEntry     entry = makeEntry( "acct.01", 1 );
    entry._memo.assign( static_cast<size_t>( ServiceAuditEntry::kMaxMemoSize ) + 1, 'x' );
    SW_EXPECT_FALSE( ServiceAuditLog::isValidEntry( entry ) );
    ServiceTransaction transaction;
    transaction.put( getEffectTable(), "balance", vector<uint8>{ 1 } );
    ServiceAuditLog::stageEntry( transaction, entry, 0, 1 );
    SW_EXPECT_TRUE( database.commit( transaction ) == ServiceStoreResult::Invalid ); // 자르지 않고 효과까지 막는다
    SW_EXPECT_EQUAL( 0, database.countRecords( getEffectTable() ) );
}

/** @brief [ServiceAuditLogTest] 경계 값: 상한 그대로인 줄은 받고, 동작 코드의 대문자 · 음수 시각 · 빈 주체는 거절한다 */
SW_TEST_CASE( ServiceAuditLogTest, EntryLimitsAreInclusiveAndBadPropertiesAreRejected )
{
    ServiceAuditEntry atLimit = makeEntry( "acct.01", 1 );
    atLimit._memo.assign( static_cast<size_t>( ServiceAuditEntry::kMaxMemoSize ), 'x' );
    atLimit._before.assign( static_cast<size_t>( ServiceAuditEntry::kMaxStateSize ), 'b' );
    SW_EXPECT_TRUE( ServiceAuditLog::isValidEntry( atLimit ) );

    ServiceAuditEntry upperAction = makeEntry( "acct.01", 1 );
    upperAction._action           = "Admin.Sanction";
    SW_EXPECT_FALSE( ServiceAuditLog::isValidEntry( upperAction ) );

    ServiceAuditEntry negativeTime = makeEntry( "acct.01", -1 );
    SW_EXPECT_FALSE( ServiceAuditLog::isValidEntry( negativeTime ) );

    ServiceAuditEntry emptySubject = makeEntry( "", 1 );
    SW_EXPECT_FALSE( ServiceAuditLog::isValidEntry( emptySubject ) );
}

/** @brief [ServiceAuditLogTest] 보존 쓸기는 키의 시각을 읽고, 시각 칸이 16 진 16 자가 아닌 키는 거절한다 */
SW_TEST_CASE( ServiceAuditLogTest, ReadTimeFromKeyRejectsAMalformedKey )
{
    const ServiceAuditEntry entry = makeEntry( "acct.01", 0x1234 );
    ServiceRecord           record;
    record._key  = ServiceAuditLog::makeKey( entry, 0, 7 );
    int64 timeMs = 0;
    SW_REQUIRE_TRUE( ServiceAuditLog::readTimeMs( record, timeMs ) );
    SW_EXPECT_EQUAL( int64( 0x1234 ), timeMs );

    record._key = "acct.01/not-a-time/00000000000000000000000000000007";
    SW_EXPECT_FALSE( ServiceAuditLog::readTimeMs( record, timeMs ) );
    record._key = "acct.01";
    SW_EXPECT_FALSE( ServiceAuditLog::readTimeMs( record, timeMs ) );
}
