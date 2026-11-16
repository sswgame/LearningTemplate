#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Observability/ServiceMetrics.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"

// OnlineProtocolTableTest — 프로토콜 표: 메서드 서술자의 번호와 호출 규칙, 메서드와 타입의 짝이 스키마 해시에 섞임, 번호로 메서드 찾기, 본문 없는 메시지,
// 저장 함수 결과(업무 결과와 공통 범주), 재시도 범주, 공통 범주 지표. 서버 · 클라이언트 끝단은 우편함 키트(`MailboxServiceTest`)가 본다. 파일 · 네트워크 없음(nogpu).

using namespace sw;

namespace
{
    enum class TestMethodName : uint8
    {
        Read,
        Write,
        Login,
        Count
    };

    enum class TestPushName : uint8
    {
        Changed,
        Count
    };

    enum class TestResult : uint8
    {
        Ok,
        NotFound,
        Count
    };

    struct TestMethod : OnlineMethodTable<TestMethodName, OnlineMethodRange::kGame, TestPushName>
    {
        using ReadCall  = Method<MethodName::Read, ServiceMailAttachment, LedgerBalance>;
        using WriteCall = Method<MethodName::Write, LedgerBalance, ServiceMailAttachment, OnlineMethodFlag::RequiresIdempotencyKey>;
        using LoginCall = Method<MethodName::Login, OnlineNoBody, OnlineNoBody, OnlineMethodFlag::Anonymous>;
        using Changed   = Push<PushName::Changed, LedgerBalance>;

        // 두 메서드의 타입만 맞바꾼 표 — 메시지 집합과 이름 순서가 같아도 짝이 다르다
        using SwappedRead  = Method<MethodName::Read, LedgerBalance, ServiceMailAttachment>;
        using SwappedWrite = Method<MethodName::Write, ServiceMailAttachment, LedgerBalance, OnlineMethodFlag::RequiresIdempotencyKey>;
        // 호출 규칙만 다른 메서드
        using UnkeyedWrite = Method<MethodName::Write, LedgerBalance, ServiceMailAttachment>;
    };

    using TestTable    = OnlineProtocolTable<TestMethod, OnlineMethodList<TestMethod::ReadCall, TestMethod::WriteCall, TestMethod::LoginCall>, OnlinePushList<TestMethod::Changed>>;
    using SwappedTable = OnlineProtocolTable<TestMethod, OnlineMethodList<TestMethod::SwappedRead, TestMethod::SwappedWrite, TestMethod::LoginCall>, OnlinePushList<TestMethod::Changed>>;
    using UnkeyedTable = OnlineProtocolTable<TestMethod, OnlineMethodList<TestMethod::ReadCall, TestMethod::UnkeyedWrite, TestMethod::LoginCall>, OnlinePushList<TestMethod::Changed>>;

    struct TestVisitor
    {
        int32 _lastIndex{ -1 };

        template <typename TDescriptor>
        void visit()
        {
            _lastIndex = TDescriptor::kIndex;
        }
    };

    struct NoResultReply
    {
        int32 _count{ 0 };
    };

    struct ResultReply
    {
        TestResult _result{ TestResult::Ok };
    };
} // namespace

/** @brief [OnlineProtocolTableTest] 서술자의 번호는 메서드 표와 같고(영역 + 1 부터 · 알림은 + 0x80 부터) 호출 규칙이 형식에 있다 */
SW_TEST_CASE( OnlineProtocolTableTest, DescriptorsCarryNumbersAndRules )
{
    SW_EXPECT_EQUAL( TestMethod::toMethod( TestMethodName::Write ), TestMethod::WriteCall::kID );
    SW_EXPECT_EQUAL( TestMethod::toPush( TestPushName::Changed ), TestMethod::Changed::kKind );
    SW_EXPECT_EQUAL( 1, TestMethod::WriteCall::kIndex );
    SW_EXPECT_TRUE( TestMethod::WriteCall::kRequiresIdempotencyKey );
    SW_EXPECT_FALSE( TestMethod::ReadCall::kRequiresIdempotencyKey );
    SW_EXPECT_TRUE( TestMethod::LoginCall::kAnonymous );
    SW_EXPECT_FALSE( TestMethod::WriteCall::kAnonymous );
    SW_EXPECT_TRUE( TestTable::isAnonymous( TestMethod::LoginCall::kID ) );
    SW_EXPECT_FALSE( TestTable::isAnonymous( TestMethod::ReadCall::kID ) );
}

/** @brief [OnlineProtocolTableTest] 해시는 메서드와 타입의 짝과 호출 규칙을 본다 - 같은 타입 집합이어도 짝을 맞바꾸거나 멱등 규칙을 빼면 값이 다르다 */
SW_TEST_CASE( OnlineProtocolTableTest, HashSeesMethodTypePairs )
{
    const uint64 hash = TestTable::computeMessageHash();
    SW_EXPECT_EQUAL( hash, TestTable::computeMessageHash() );
    SW_EXPECT_NOT_EQUAL( hash, SwappedTable::computeMessageHash() );
    SW_EXPECT_NOT_EQUAL( hash, UnkeyedTable::computeMessageHash() );
}

/** @brief [OnlineProtocolTableTest] 번호로 서술자를 찾아 부르고, 표에 없는 번호는 false 이다 */
SW_TEST_CASE( OnlineProtocolTableTest, VisitFindsDescriptorByNumber )
{
    TestVisitor visitor;
    SW_EXPECT_TRUE( TestTable::visitMethod( TestMethod::WriteCall::kID, visitor ) );
    SW_EXPECT_EQUAL( 1, visitor._lastIndex );
    SW_EXPECT_TRUE( TestTable::visitPush( TestMethod::Changed::kKind, visitor ) );
    SW_EXPECT_EQUAL( 0, visitor._lastIndex );
    visitor._lastIndex = -1;
    SW_EXPECT_FALSE( TestTable::visitMethod( static_cast<uint16>( OnlineMethodRange::kGame + 0x7F ), visitor ) );
    SW_EXPECT_FALSE( TestTable::visitPush( TestMethod::WriteCall::kID, visitor ) );
    SW_EXPECT_EQUAL( -1, visitor._lastIndex );
}

/** @brief [OnlineProtocolTableTest] 본문 없는 메시지는 와이어에 아무것도 쓰지 않고 읽기는 언제나 된다 */
SW_TEST_CASE( OnlineProtocolTableTest, NoBodyWritesNothing )
{
    using Messages = TestTable::MessageTypes;
    BitWriter writer;
    SW_EXPECT_TRUE( Messages::write( writer, OnlineNoBody{} ) );
    SW_EXPECT_EQUAL( 0, writer.getByteCount() );
    BitReader    reader( nullptr, 0 );
    OnlineNoBody body;
    SW_EXPECT_TRUE( Messages::read( reader, body ) == FixedReadResult::Ok );
}

/** @brief [OnlineProtocolTableTest] 저장 함수 결과: 공통 범주는 어떤 업무 결과와도 같지 않고, 재시도 범주는 일시 장애 · 과부하 · 충돌이다 */
SW_TEST_CASE( OnlineProtocolTableTest, ServiceResultSeparatesKitResultFromCommonCategory )
{
    const OnlineServiceResult<TestResult> found = TestResult::NotFound;
    SW_EXPECT_TRUE( found == TestResult::NotFound );
    SW_EXPECT_TRUE( found != TestResult::Ok );
    SW_EXPECT_FALSE( found.isFailed() );

    const OnlineServiceResult<TestResult> failed = OnlineServiceResult<TestResult>::fail( OnlineError::kBusy );
    SW_EXPECT_TRUE( failed.isFailed() );
    SW_EXPECT_TRUE( failed != TestResult::Ok );
    SW_EXPECT_TRUE( failed != TestResult::NotFound );

    SW_EXPECT_TRUE( OnlineError::isRetryable( OnlineError::kBusy ) );
    SW_EXPECT_TRUE( OnlineError::isRetryable( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( OnlineError::isRetryable( OnlineError::kRateLimited ) );
    SW_EXPECT_FALSE( OnlineError::isRetryable( OnlineError::kConflict ) );
    SW_EXPECT_FALSE( OnlineError::isRetryable( OnlineError::kInvalidRequest ) );
}

/** @brief [OnlineProtocolTableTest] 응답의 결과 칸 유무를 형식으로 안다 */
SW_TEST_CASE( OnlineProtocolTableTest, ReplyResultFieldIsDetected )
{
    SW_EXPECT_TRUE( OnlineReplyHasResult<ResultReply>::value );
    SW_EXPECT_FALSE( OnlineReplyHasResult<NoResultReply>::value );
    SW_EXPECT_FALSE( OnlineReplyHasResult<OnlineNoBody>::value );
}
