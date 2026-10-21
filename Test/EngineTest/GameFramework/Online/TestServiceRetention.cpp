#include "pch.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Report/ServiceReport.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceKeyUtil.h"
#include "GameFramework/Base/Online/Store/ServiceRetention.h"

#include "TestFramework/TestFramework.h"

// 보존 기간 쓸기 — 기간이 지난 줄만 지운다, 시각을 못 읽는 레코드는 남긴다, 상한을 넘으면 커서로 이어 간다, 감사 · 내역 · 신고의 시각 읽기.

using namespace sw;

namespace
{
    struct TestServiceRetentionInternal
    {
        static constexpr int64 kRetentionMs = 1000;

        static void addAudit( MemoryServiceDatabase& database, string_view subject, int64 timeMs, uint64 unique )
        {
            ServiceAuditEntry entry;
            entry._actor   = "system";
            entry._action  = "test.action";
            entry._subject = string{ subject };
            entry._timeMs  = timeMs;
            ServiceTransaction transaction;
            ServiceAuditLog::stageEntry( transaction, entry, 0, unique );
            SW_ASSERT_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
        }

        static ServiceRetentionRule makeAuditRule()
        {
            ServiceRetentionRule rule;
            rule._table       = ServiceAuditLog::getTable();
            rule._retentionMs = kRetentionMs;
            rule._pfnReadTime = &ServiceAuditLog::readTimeMs;
            return rule;
        }
    };
} // namespace

SW_TEST_CASE( ServiceRetentionTest, DeletesOnlyRecordsPastRetention )
{
    MemoryServiceDatabase database;
    TestServiceRetentionInternal::addAudit( database, "acct.01", 100, 1 );  // 지난 줄
    TestServiceRetentionInternal::addAudit( database, "acct.02", 5000, 2 ); // 최근 줄
    TestServiceRetentionInternal::addAudit( database, "gm.01", 200, 3 );    // 지난 줄
    ServiceRetentionRule   rule = TestServiceRetentionInternal::makeAuditRule();
    ServiceRetentionReport report;
    SW_EXPECT_TRUE( ServiceRetention::sweep( database, rule, 5500, 1000, 1000, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 2, report._deletedCount );
    SW_EXPECT_EQUAL( 3, report._scannedCount );
    SW_EXPECT_EQUAL( 1, database.countRecords( ServiceAuditLog::getTable() ) );
}

SW_TEST_CASE( ServiceRetentionTest, KeepsRecordsWhoseTimeCannotBeRead )
{
    MemoryServiceDatabase database;
    ServiceTransaction    transaction;
    transaction.put( ServiceAuditLog::getTable(), "broken-key", vector<uint8>{ 1 } );
    SW_ASSERT_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
    ServiceRetentionRule   rule = TestServiceRetentionInternal::makeAuditRule();
    ServiceRetentionReport report;
    SW_EXPECT_TRUE( ServiceRetention::sweep( database, rule, 1000000, 1000, 1000, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 0, report._deletedCount );
    SW_EXPECT_EQUAL( 1, database.countRecords( ServiceAuditLog::getTable() ) );
}

SW_TEST_CASE( ServiceRetentionTest, DeleteLimitContinuesWithCursorNextRun )
{
    MemoryServiceDatabase database;
    for ( uint64 index = 1; index <= 5; ++index )
    {
        TestServiceRetentionInternal::addAudit( database, "acct.01", 10, index );
    }
    ServiceRetentionRule   rule = TestServiceRetentionInternal::makeAuditRule();
    ServiceRetentionReport first;
    SW_EXPECT_TRUE( ServiceRetention::sweep( database, rule, 5000, 1000, 2, first ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 2, first._deletedCount );
    SW_EXPECT_EQUAL( 3, database.countRecords( ServiceAuditLog::getTable() ) );
    ServiceRetentionReport second; // 커서가 이어져 나머지를 지운다
    SW_EXPECT_TRUE( ServiceRetention::sweep( database, rule, 5000, 1000, 100, second ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 3, second._deletedCount );
    SW_EXPECT_EQUAL( 0, database.countRecords( ServiceAuditLog::getTable() ) );
}

SW_TEST_CASE( ServiceRetentionTest, ReadsTimeFromHistoryAndClosedReportKeys )
{
    ServiceRecord history;
    history._key = "acct/";
    ServiceKeyUtil::appendHex64( history._key, 7 );
    history._key += "/";
    ServiceKeyUtil::appendHex64( history._key, 1234 );
    history._key += "/shop.buy/0123";
    int64 timeMs = 0;
    SW_EXPECT_TRUE( Ledger::readHistoryTimeMs( history, timeMs ) );
    SW_EXPECT_EQUAL( int64{ 1234 }, timeMs );

    ServiceRecord escrow; // 맡김 내역은 시각 위치를 키만으로 알 수 없어 읽지 않는다
    escrow._key = "esc/trade/abc/0000000000000001/x";
    SW_EXPECT_FALSE( Ledger::readHistoryTimeMs( escrow, timeMs ) );

    ServiceRecord closedReport;
    closedReport._key = "closed/";
    ServiceKeyUtil::appendHex64( closedReport._key, 555 );
    closedReport._key += "/x.y";
    SW_EXPECT_TRUE( ServiceReportStore::readClosedTimeMs( closedReport, timeMs ) );
    SW_EXPECT_EQUAL( int64{ 555 }, timeMs );

    ServiceRecord openReport;
    openReport._key = "open/";
    ServiceKeyUtil::appendHex64( openReport._key, 555 );
    openReport._key += "/x.y";
    SW_EXPECT_FALSE( ServiceReportStore::readClosedTimeMs( openReport, timeMs ) );
}
