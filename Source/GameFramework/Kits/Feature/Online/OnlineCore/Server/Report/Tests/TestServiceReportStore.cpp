// ServiceReportStoreTest — 신고 레코드: 코드 · 신고 규칙의 경계, 키 순서와 닫힌 키의 시각 읽기, 열기 → 처리(열린 키를 지우고 닫힌 키로 옮김)와
// 두 GM 의 동시 처리, 잘못된 처리 요청 · 깨진 레코드 거절(건너뛰지 않는다)(nogpu). 채팅 신고 길은 ChatService 테스트가 본다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Report/Server/ServiceReportStore.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/Tests/ServiceStoreEntryTestUtil.h"

using namespace sw;

namespace
{
    struct ServiceReportStoreTestUtil
    {
        static constexpr AccountID kReporter = 0x11;
        static constexpr AccountID kTarget   = 0x22;
        static constexpr AccountID kGM       = 0x33;
        static constexpr int64     kNowMs    = 1700000000000ll;

        static ServiceReport makeReport( int64 createdMs )
        {
            ServiceReport report;
            report._category        = "chat";
            report._reason          = "abuse.spam";
            report._context         = "ch.1";
            report._reporterID      = kReporter;
            report._targetID        = kTarget;
            report._createdMs       = createdMs;
            ServiceReportLine& line = report._listLine.emplace_back();
            line._senderName        = "target";
            line._text              = "buy gold";
            line._senderID          = kTarget;
            line._sentMs            = createdMs - 10;
            return report;
        }

        /** @brief 열린 신고 하나를 커밋하고 그 키를 돌려줍니다. */
        static string open( MemoryServiceDatabase& database, int64 createdMs, uint64 uniqueID )
        {
            ServiceReport      report = makeReport( createdMs );
            ServiceTransaction transaction;
            SW_EXPECT_TRUE( ServiceReportStore::stageOpen( transaction, report, uniqueID ) );
            SW_EXPECT_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
            return report._reportKey;
        }
    };
    using Util = ServiceReportStoreTestUtil;
} // namespace

/** @brief [ServiceReportStoreTest] 코드는 `[0-9a-z_.]` 1..상한, 신고는 줄 1..상한 · 문맥 상한 · 신고자 · 음수 아닌 시각을 요구한다 */
SW_TEST_CASE( ServiceReportStoreTest, CodeAndReportLimitsAreInclusive )
{
    // 틀리면: 문자 · 길이 · 줄 수 · 문맥 · 신고자 · 시각 검사 중 하나를 빼면 해당 거짓 단언이, 상한을 하나 줄이면 상한 그대로인 참 단언이 진다.
    SW_EXPECT_TRUE( ServiceReportStore::isValidCode( "abuse.spam_2", 16 ) );
    SW_EXPECT_TRUE( ServiceReportStore::isValidCode( string( 16, 'a' ), 16 ) );
    SW_EXPECT_FALSE( ServiceReportStore::isValidCode( string( 17, 'a' ), 16 ) );
    SW_EXPECT_FALSE( ServiceReportStore::isValidCode( "", 16 ) );
    SW_EXPECT_FALSE( ServiceReportStore::isValidCode( "Abuse", 16 ) );
    SW_EXPECT_FALSE( ServiceReportStore::isValidCode( "abuse-spam", 16 ) );

    SW_EXPECT_TRUE( ServiceReportStore::isValidReport( Util::makeReport( Util::kNowMs ) ) );

    ServiceReport fullLines = Util::makeReport( Util::kNowMs );
    fullLines._listLine.resize( static_cast<size_t>( ServiceReportConstant::kMaxLineCount ) );
    fullLines._context.assign( static_cast<size_t>( ServiceReportConstant::kMaxContextSize ), 'c' );
    SW_EXPECT_TRUE( ServiceReportStore::isValidReport( fullLines ) );

    ServiceReport tooManyLines = fullLines;
    tooManyLines._listLine.emplace_back();
    SW_EXPECT_FALSE( ServiceReportStore::isValidReport( tooManyLines ) );
    ServiceReport noLines = Util::makeReport( Util::kNowMs );
    noLines._listLine.clear();
    SW_EXPECT_FALSE( ServiceReportStore::isValidReport( noLines ) );
    ServiceReport longContext = fullLines;
    longContext._context.push_back( 'c' );
    SW_EXPECT_FALSE( ServiceReportStore::isValidReport( longContext ) );
    ServiceReport anonymous = Util::makeReport( Util::kNowMs );
    anonymous._reporterID   = kInvalidAccountID;
    SW_EXPECT_FALSE( ServiceReportStore::isValidReport( anonymous ) );
    SW_EXPECT_FALSE( ServiceReportStore::isValidReport( Util::makeReport( -1 ) ) );
}

/** @brief [ServiceReportStoreTest] 열린 키는 시각 순으로 정렬되고, 보존 쓸기는 닫힌 키에서만 시각을 읽는다(열린 · 깨진 키는 거절) */
SW_TEST_CASE( ServiceReportStoreTest, KeysSortByTimeAndOnlyClosedKeysCarryARetentionTime )
{
    // 틀리면: 시각을 고정폭 16 진으로 쓰지 않으면 순서 단언이, 접두 검사를 빼면 열린 키 단언이, 16 진 검사를 빼면 깨진 키 단언이 진다.
    SW_EXPECT_TRUE( ServiceReportStore::makeOpenKey( 0x0f, Util::kReporter, 1 ) < ServiceReportStore::makeOpenKey( 0x100, Util::kReporter, 0 ) );

    MemoryServiceDatabase database;
    const string          openKey = Util::open( database, 0x1234, 7 );
    ServiceTransaction    transaction;
    ServiceReport         resolved;
    SW_REQUIRE_TRUE( ServiceReportStore::stageResolve( database, openKey, ServiceReportStatus::Dismissed, Util::kGM, "", Util::kNowMs, transaction, resolved ) ==
                     ServiceStoreResult::Ok );

    ServiceRecord record;
    int64         timeMs = 0;
    record._key          = resolved._reportKey;
    SW_REQUIRE_TRUE( ServiceReportStore::readClosedTimeMs( record, timeMs ) );
    SW_EXPECT_EQUAL( int64( 0x1234 ), timeMs );
    record._key = openKey;
    SW_EXPECT_FALSE( ServiceReportStore::readClosedTimeMs( record, timeMs ) );
    record._key = "closed/not-a-time-at-all/0";
    SW_EXPECT_FALSE( ServiceReportStore::readClosedTimeMs( record, timeMs ) );
    record._key = "closed/12";
    SW_EXPECT_FALSE( ServiceReportStore::readClosedTimeMs( record, timeMs ) );
}

/** @brief [ServiceReportStoreTest] 처리는 열린 키를 지우고 닫힌 키에 처리 결과를 쓰며, 같은 판을 읽은 두 번째 GM 의 처리는 Conflict 다 */
SW_TEST_CASE( ServiceReportStoreTest, ResolveMovesTheReportOnceEvenWithTwoGMs )
{
    // 틀리면: 열린 키를 버전 조건 없이 지우면 두 번째 커밋이 Ok 라 Conflict 단언이, 옮기지 않으면 열린 · 닫힌 목록 단언이 진다.
    MemoryServiceDatabase database;
    const string          openKey = Util::open( database, Util::kNowMs, 1 );

    ServiceTransaction first;
    ServiceTransaction second;
    ServiceReport      firstReport;
    ServiceReport      secondReport;
    SW_REQUIRE_TRUE( ServiceReportStore::stageResolve( database, openKey, ServiceReportStatus::Resolved, Util::kGM, "muted", Util::kNowMs + 5, first, firstReport ) ==
                     ServiceStoreResult::Ok );
    SW_REQUIRE_TRUE( ServiceReportStore::stageResolve( database, openKey, ServiceReportStatus::Dismissed, Util::kGM + 1, "", Util::kNowMs + 6, second, secondReport ) ==
                     ServiceStoreResult::Ok );
    SW_REQUIRE_TRUE( database.commit( first ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( database.commit( second ) == ServiceStoreResult::Conflict );

    vector<ServiceReport> listReport;
    string                cursor;
    SW_REQUIRE_TRUE( ServiceReportStore::listReports( database, true, "", 10, listReport, cursor ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( listReport.empty() );
    SW_REQUIRE_TRUE( ServiceReportStore::listReports( database, false, "", 10, listReport, cursor ) == ServiceStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), listReport.size() );
    SW_EXPECT_TRUE( listReport[0]._status == ServiceReportStatus::Resolved );
    SW_EXPECT_EQUAL( Util::kGM, listReport[0]._resolverID );
    SW_EXPECT_EQUAL( string( "muted" ), listReport[0]._resolutionMemo );
    SW_EXPECT_EQUAL( firstReport._reportKey, listReport[0]._reportKey );
    SW_EXPECT_EQUAL( string( "buy gold" ), listReport[0]._listLine[0]._text );

    ServiceTransaction again; // 이미 닫힌 신고를 다시 처리하면 열린 키가 없다
    ServiceReport      againReport;
    SW_EXPECT_TRUE( ServiceReportStore::stageResolve( database, openKey, ServiceReportStatus::Resolved, Util::kGM, "", Util::kNowMs, again, againReport ) ==
                    ServiceStoreResult::NotFound );
}

/** @brief [ServiceReportStoreTest] GM 이 보낸 처리 요청은 믿지 않는다: Open 으로 처리 · 열린 키가 아닌 키 · 키 규칙 위반 · 상한을 넘는 메모는 읽기 전에 Invalid 다 */
SW_TEST_CASE( ServiceReportStoreTest, ResolveRejectsBadRequestsBeforeReading )
{
    // 틀리면: 상태 · 접두 · 키 규칙 · 메모 검사 중 하나를 빼면 그 줄이 Ok(쓰기가 붙는다) 또는 NotFound 라 해당 단언과 isEmpty 단언이 진다.
    MemoryServiceDatabase database;
    const string          openKey = Util::open( database, Util::kNowMs, 1 );
    const string          atLimitMemo( static_cast<size_t>( ServiceReportConstant::kMaxMemoSize ), 'm' );
    const string          oversizedMemo( static_cast<size_t>( ServiceReportConstant::kMaxMemoSize ) + 1, 'm' );
    ServiceTransaction    transaction;
    ServiceReport         report;

    SW_EXPECT_TRUE( ServiceReportStore::stageResolve( database, openKey, ServiceReportStatus::Open, Util::kGM, "", Util::kNowMs, transaction, report ) ==
                    ServiceStoreResult::Invalid );
    string closedKey{ "closed/" };
    closedKey.append( openKey.substr( 5 ) );
    SW_EXPECT_TRUE( ServiceReportStore::stageResolve( database, closedKey, ServiceReportStatus::Resolved, Util::kGM, "", Util::kNowMs, transaction,
                                                      report ) == ServiceStoreResult::Invalid );
    SW_EXPECT_TRUE( ServiceReportStore::stageResolve( database, "open/ABC", ServiceReportStatus::Resolved, Util::kGM, "", Util::kNowMs, transaction, report ) ==
                    ServiceStoreResult::Invalid );
    SW_EXPECT_TRUE( ServiceReportStore::stageResolve( database, openKey, ServiceReportStatus::Resolved, Util::kGM, oversizedMemo, Util::kNowMs, transaction, report ) ==
                    ServiceStoreResult::Invalid );
    SW_EXPECT_TRUE( transaction.isEmpty() );

    SW_EXPECT_TRUE( ServiceReportStore::stageResolve( database, openKey, ServiceReportStatus::Resolved, Util::kGM, atLimitMemo, Util::kNowMs, transaction, report ) ==
                    ServiceStoreResult::Ok );
}

/** @brief [ServiceReportStoreTest] 규칙 밖 신고는 트랜잭션에 아무것도 넣지 않는다 */
SW_TEST_CASE( ServiceReportStoreTest, InvalidReportIsNotStaged )
{
    // 틀리면: stageOpen 이 검사 없이 쓰면 false 단언과 isEmpty 단언이 진다.
    ServiceReport report = Util::makeReport( Util::kNowMs );
    report._reason       = "Spam";
    ServiceTransaction transaction;
    SW_EXPECT_FALSE( ServiceReportStore::stageOpen( transaction, report, 1 ) );
    SW_EXPECT_TRUE( transaction.isEmpty() );
}

/** @brief [ServiceReportStoreTest] 깨진 신고 레코드는 목록에서 건너뛰지 않고 Invalid 이며, 처리도 Invalid 다 */
SW_TEST_CASE( ServiceReportStoreTest, CorruptRecordIsInvalidNotSkipped )
{
    // 틀리면: 목록이 깨진 레코드를 건너뛰거나 처리가 빈 신고로 옮기면 해당 Invalid 단언이 진다.
    SW_EXPECT_LOG( "ServiceRecordUtil.DurableRecordVersionBytes" );
    MemoryServiceDatabase database;
    Util::open( database, Util::kNowMs, 1 );
    const string       brokenKey = ServiceReportStore::makeOpenKey( Util::kNowMs + 1, Util::kReporter, 2 );
    ServiceTransaction seed;
    ServiceStoreTestAccess::putBytes( seed, ServiceReportStore::getTable(), brokenKey, vector<uint8>{ 0, 0, 0, 0 } ); // 버전 머리만 있고 몸이 없다
    SW_REQUIRE_TRUE( database.commit( seed ) == ServiceStoreResult::Ok );

    vector<ServiceReport> listReport;
    string                cursor;
    SW_EXPECT_TRUE( ServiceReportStore::listReports( database, true, "", 10, listReport, cursor ) == ServiceStoreResult::Invalid );

    ServiceTransaction transaction;
    ServiceReport      report;
    SW_EXPECT_TRUE( ServiceReportStore::stageResolve( database, brokenKey, ServiceReportStatus::Resolved, Util::kGM, "", Util::kNowMs, transaction, report ) ==
                    ServiceStoreResult::Invalid );
    SW_EXPECT_TRUE( transaction.isEmpty() );
}
