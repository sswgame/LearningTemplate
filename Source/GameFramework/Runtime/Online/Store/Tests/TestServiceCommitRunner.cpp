#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceCommitRunner.h"
#include "GameFramework/Runtime/Online/Store/Server/Tests/ServiceStoreEntryTestUtil.h"

// ServiceCommitRunnerTest — 커밋의 유일한 길: 충돌이면 시도 함수를 다시 부르고 시도마다 다시 읽으며, 상한까지 충돌하면 답은 kBusy 하나이고 로그를 남기고, 적용됐는지 모르는 실패는 다시 하지 않는다.

using namespace sw;

namespace
{
    const hashed_string kTable{ "runner_test" };

    enum class ProbeResult : uint8
    {
        Ok = 0,
        Taken
    };
    using ProbeServiceResult = OnlineServiceResult<ProbeResult>;

    /** @brief 레코드 하나의 바이트를 하나 늘립니다(읽고 덮어쓴다). 부른 시도 수를 셉니다. */
    ProbeServiceResult appendByte( IServiceStoreConnection& connection, int32& outAttemptCount )
    {
        outAttemptCount = 0;
        return ServiceCommitRunner::run<ProbeServiceResult>( connection, "Test.Append", [&]( ServiceCommitAttempt& attempt ) -> optional<ProbeServiceResult>
        {
            ++outAttemptCount;
            ServiceRecord record;
            if ( attempt.readRecord( kTable, "row", record ) == ServiceStoreResult::Unavailable )
                return ProbeServiceResult::fail( OnlineError::kUnavailable );
            record._bytes.push_back( 1 );
            ServiceTransaction transaction;
            transaction.put( kTable, "row", record._bytes ); // 조건을 적지 않아도 시도의 읽기 집합이 읽은 버전을 건다
            const ServiceStoreResult committed = attempt.commit( transaction );
            if ( committed == ServiceStoreResult::Conflict )
                return nullopt;
            return committed == ServiceStoreResult::Ok ? ProbeServiceResult{ ProbeResult::Ok } : ProbeServiceResult::fail( OnlineError::kUnavailable );
        } );
    }

    size_t readRowSize( MemoryServiceDatabase& database )
    {
        ServiceRecord record;
        return database.readRecord( kTable, "row", record ) == ServiceStoreResult::Ok ? record._bytes.size() : 0;
    }
} // namespace

/** @brief [ServiceCommitRunnerTest] 충돌이면 다시 읽어 다시 하고, 한 번만 쓴다 */
SW_TEST_CASE( ServiceCommitRunnerTest, ConflictRunsTheAttemptAgain )
{
    MemoryServiceDatabase     database;
    UnreliableStoreConnection unreliable( database );
    unreliable._conflictCount = 3;
    int32 attemptCount        = 0;
    SW_EXPECT_TRUE( appendByte( unreliable, attemptCount ) == ProbeResult::Ok );
    SW_EXPECT_EQUAL( 4, attemptCount );
    SW_EXPECT_EQUAL( size_t{ 1 }, readRowSize( database ) );
}

/** @brief [ServiceCommitRunnerTest] 상한까지 충돌하면 답은 kBusy 하나이고 쓰지 않으며 일 이름으로 알린다 */
SW_TEST_CASE( ServiceCommitRunnerTest, ExhaustedConflictsAnswerBusyAndWriteNothing )
{
    SW_EXPECT_LOG( "ServiceStore.ConflictRetriesExhausted" );
    MemoryServiceDatabase     database;
    UnreliableStoreConnection unreliable( database );
    unreliable._conflictCount             = 1000;
    int32                    attemptCount = 0;
    const ProbeServiceResult result       = appendByte( unreliable, attemptCount );
    SW_EXPECT_TRUE( result.hasError( OnlineError::kBusy ) );
    SW_EXPECT_EQUAL( OnlineLimit::kMaxConflictRetry, attemptCount );
    SW_EXPECT_EQUAL( OnlineLimit::kMaxConflictRetry, unreliable._commitCount );
    SW_EXPECT_EQUAL( size_t{ 0 }, readRowSize( database ) );
}

/** @brief [ServiceCommitRunnerTest] 적용됐는지 모르는 커밋은 다시 하지 않는다 — 다시 하면 두 번 쓴다 */
SW_TEST_CASE( ServiceCommitRunnerTest, UnknownResultIsNotRetried )
{
    MemoryServiceDatabase     database;
    UnreliableStoreConnection unreliable( database );
    unreliable._lostReplyCount = 1;
    int32 attemptCount         = 0;
    SW_EXPECT_TRUE( appendByte( unreliable, attemptCount ).hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( 1, attemptCount );
    SW_EXPECT_EQUAL( size_t{ 1 }, readRowSize( database ) );
}

/** @brief [ServiceCommitRunnerTest] 시도 사이에 남이 쓴 값 위에 이어 쓴다 — 첫 시도의 읽기로 덮지 않는다 */
SW_TEST_CASE( ServiceCommitRunnerTest, EveryAttemptReadsAgain )
{
    MemoryServiceDatabase    database;
    int32                    attemptCount = 0;
    const ServiceStoreResult result       = ServiceCommitRunner::run<ServiceStoreResult>( database, "Test.Race", [&]( ServiceCommitAttempt& attempt ) -> optional<ServiceStoreResult>
          {
        ServiceRecord record;
        (void)attempt.readRecord( kTable, "row", record );
        if ( ++attemptCount == 1 ) // 읽은 뒤 커밋 앞에 다른 서버가 쓴다
        {
            ServiceTransaction other;
            other.put( kTable, "row", vector<uint8>{ 7, 7 } );
            SW_EXPECT_TRUE( database.commit( other ) == ServiceStoreResult::Ok );
        }
        record._bytes.push_back( 1 );
        ServiceTransaction transaction;
        transaction.put( kTable, "row", record._bytes );
        const ServiceStoreResult committed = attempt.commit( transaction );
        if ( committed == ServiceStoreResult::Conflict )
            return nullopt;
        return committed;
    } );
    SW_EXPECT_TRUE( result == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( 2, attemptCount );
    SW_EXPECT_EQUAL( size_t{ 3 }, readRowSize( database ) );
}
