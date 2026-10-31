#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/Base/Online/Service/OnlineReplyUtil.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"

// OnlineServiceTableTest — 온라인 서비스의 와이어 표: 메서드 영역(메서드 · 알림 절반), 메서드 표의 번호(toMethod · toPush · toIndex),
// 공통 오류 → 키트 결과 표, 버전 불일치를 UpdateRequired 로 바꾸는 응답 도우미, 메서드 이름 순서 해시, 응답 대기 표(티켓은 1 부터, 한 번만 꺼낸다).

namespace
{
    enum class TestMethodName : uint8
    {
        Login,
        Logout,
        Refresh,
        Count
    };

    enum class TestPushName : uint8
    {
        Kicked,
        Count
    };

    using TestMethod = sw::OnlineMethodTable<TestMethodName, sw::OnlineMethodRange::kAccount, TestPushName>;

    enum class TestResult : uint8
    {
        Ok,
        NotFound,
        Unavailable,
        UpdateRequired
    };

    constexpr sw::OnlineErrorResult<TestResult> kArrErrorRow[] = {
        {       sw::OnlineError::kNotFound, TestResult::NotFound},
        {sw::OnlineError::kVersionMismatch, TestResult::NotFound}, // 키트 표가 틀리게 적어도 응답 도우미가 먼저 막는다
    };

    struct TestProtocol
    {
        static TestResult fromErrorCode( uint16 errorCode ) { return sw::OnlineError::toResult( errorCode, kArrErrorRow, TestResult::Unavailable ); }
    };

    struct TestReply
    {
        TestResult _result{ TestResult::Ok };
    };

    sw::NetRequestToken makeToken( uint64 requestID, uint16 method )
    {
        sw::NetRequestToken token;
        token._requestID = requestID;
        token._method    = method;
        token._serial    = static_cast<uint32>( requestID * 10 );
        return token;
    }
} // namespace

/**
 * @brief [OnlineServiceTableTest] 영역은 256 개 단위이고, 아래 절반(0x00..0x7F)은 메서드, 위 절반은 알림이다
 */
SW_TEST_CASE( OnlineServiceTableTest, MethodRangeSplitsMethodAndPushHalves )
{
    SW_EXPECT_TRUE( sw::OnlineMethodRange::isInRange( 0x0100, sw::OnlineMethodRange::kAccount ) );
    SW_EXPECT_TRUE( sw::OnlineMethodRange::isInRange( 0x01FF, sw::OnlineMethodRange::kAccount ) );
    SW_EXPECT_FALSE( sw::OnlineMethodRange::isInRange( 0x0200, sw::OnlineMethodRange::kAccount ) );
    SW_EXPECT_FALSE( sw::OnlineMethodRange::isInRange( 0x00FF, sw::OnlineMethodRange::kAccount ) );
    SW_EXPECT_EQUAL( sw::OnlineMethodRange::kChat, sw::OnlineMethodRange::getRangeBase( 0x07AB ) );
    SW_EXPECT_EQUAL( sw::OnlineMethodRange::kGame, sw::OnlineMethodRange::getRangeBase( 0x8001 ) );
    SW_EXPECT_TRUE( sw::OnlineMethodRange::isMethod( 0x017F ) );
    SW_EXPECT_FALSE( sw::OnlineMethodRange::isMethod( 0x0180 ) );
    SW_EXPECT_TRUE( sw::OnlineMethodRange::isMethod( sw::OnlineHostMethod::kHello ) );
    SW_EXPECT_TRUE( sw::OnlineMethodRange::isMethod( sw::OnlineHostMethod::kPing ) );
}

/**
 * @brief [OnlineServiceTableTest] 메서드 번호는 영역 + 1 부터 열거자 순서, 알림은 영역 + 0x80 부터. toIndex 는 표 밖(영역 첫 값 · 알림 · 다른 영역)이면 -1
 */
SW_TEST_CASE( OnlineServiceTableTest, MethodTableNumbersFollowEnumeratorOrder )
{
    SW_EXPECT_EQUAL( uint16( 0x0101 ), TestMethod::toMethod( TestMethodName::Login ) );
    SW_EXPECT_EQUAL( uint16( 0x0103 ), TestMethod::toMethod( TestMethodName::Refresh ) );
    SW_EXPECT_EQUAL( uint16( 0x0180 ), TestMethod::toPush( TestPushName::Kicked ) );
    SW_EXPECT_TRUE( sw::OnlineMethodRange::isMethod( TestMethod::toMethod( TestMethodName::Refresh ) ) );
    SW_EXPECT_FALSE( sw::OnlineMethodRange::isMethod( TestMethod::toPush( TestPushName::Kicked ) ) );

    SW_EXPECT_EQUAL( 0, TestMethod::toIndex( 0x0101 ) );
    SW_EXPECT_EQUAL( 2, TestMethod::toIndex( 0x0103 ) );
    SW_EXPECT_EQUAL( -1, TestMethod::toIndex( 0x0100 ) ); // 영역 첫 값은 메서드가 아니다
    SW_EXPECT_EQUAL( -1, TestMethod::toIndex( 0x0104 ) ); // 표 끝 다음
    SW_EXPECT_EQUAL( -1, TestMethod::toIndex( 0x0180 ) ); // 알림
    SW_EXPECT_EQUAL( -1, TestMethod::toIndex( 0x0201 ) ); // 다른 영역
    SW_EXPECT_EQUAL( 3, TestMethod::kCount );
    SW_EXPECT_EQUAL( 1, TestMethod::kPushCount );
}

/**
 * @brief [OnlineServiceTableTest] 공통 오류 → 키트 결과: 표에 있으면 그 결과, 없으면 기본값. 응답 도우미는 버전 불일치를 키트 표보다 먼저 UpdateRequired 로 바꾼다
 */
SW_TEST_CASE( OnlineServiceTableTest, ErrorCodesMapToKitResultsWithVersionMismatchFirst )
{
    SW_EXPECT_TRUE( TestProtocol::fromErrorCode( sw::OnlineError::kNotFound ) == TestResult::NotFound );
    SW_EXPECT_TRUE( TestProtocol::fromErrorCode( sw::OnlineError::kConflict ) == TestResult::Unavailable );
    SW_EXPECT_TRUE( TestProtocol::fromErrorCode( sw::OnlineError::kVersionMismatch ) == TestResult::NotFound ); // 표만 보면 틀린 값이 나온다

    TestReply reply;
    sw::OnlineReplyUtil::markError<TestProtocol>( sw::OnlineError::kVersionMismatch, reply );
    SW_EXPECT_TRUE( reply._result == TestResult::UpdateRequired ); // 다시 시도를 부르지 않는다
    sw::OnlineReplyUtil::markError<TestProtocol>( sw::OnlineError::kNotFound, reply );
    SW_EXPECT_TRUE( reply._result == TestResult::NotFound );
    sw::OnlineReplyUtil::markError<TestProtocol>( sw::OnlineError::kInternal, reply );
    SW_EXPECT_TRUE( reply._result == TestResult::Unavailable );
}

/**
 * @brief [OnlineServiceTableTest] 메서드 이름 순서 해시: 같은 입력은 같은 값, 두 이름을 맞바꾸거나 메서드와 알림의 경계를 옮기거나 이름을 나눠 붙이면 다른 값이다
 */
SW_TEST_CASE( OnlineServiceTableTest, NameOrderHashSeesOrderAndListBoundaries )
{
    using sw::OnlineProtocolEntry;
    const uint64 base = OnlineProtocolEntry::computeNameOrderHash( { "Lock", "Cancel" }, { "Changed" } );
    SW_EXPECT_EQUAL( base, OnlineProtocolEntry::computeNameOrderHash( { "Lock", "Cancel" }, { "Changed" } ) );
    SW_EXPECT_NOT_EQUAL( base, OnlineProtocolEntry::computeNameOrderHash( { "Cancel", "Lock" }, { "Changed" } ) );
    SW_EXPECT_NOT_EQUAL( base, OnlineProtocolEntry::computeNameOrderHash( { "Lock" }, { "Cancel", "Changed" } ) );
    SW_EXPECT_NOT_EQUAL( OnlineProtocolEntry::computeNameOrderHash( { "ab", "c" }, {} ), OnlineProtocolEntry::computeNameOrderHash( { "a", "bc" }, {} ) );
    SW_EXPECT_NOT_EQUAL( OnlineProtocolEntry::computeNameOrderHash( { "a" }, {} ), OnlineProtocolEntry::computeNameOrderHash( {}, { "a" } ) );
    SW_EXPECT_NOT_EQUAL( OnlineProtocolEntry::computeNameOrderHash( {}, {} ), OnlineProtocolEntry::computeNameOrderHash( { "a" }, {} ) );
}

/**
 * @brief [OnlineServiceTableTest] 응답 대기 표: 티켓은 1 부터 오르고, 꺼내면 넣은 토큰 그대로이며 한 번만 꺼낸다. 지워도 티켓 번호는 다시 쓰지 않는다
 */
SW_TEST_CASE( OnlineServiceTableTest, PendingTableIssuesTicketsAndTakesOnce )
{
    sw::ServicePendingTable table;
    const uint64            first  = table.add( makeToken( 100, 0x0101 ) );
    const uint64            second = table.add( makeToken( 200, 0x0102 ) );
    SW_EXPECT_EQUAL( uint64( 1 ), first );
    SW_EXPECT_EQUAL( uint64( 2 ), second );
    SW_EXPECT_EQUAL( 2, table.getCount() );

    sw::NetRequestToken token;
    SW_REQUIRE_TRUE( table.take( second, token ) );
    SW_EXPECT_EQUAL( uint64( 200 ), token._requestID );
    SW_EXPECT_EQUAL( uint16( 0x0102 ), token._method );
    SW_EXPECT_EQUAL( uint32( 2000 ), token._serial );
    SW_EXPECT_FALSE( table.take( second, token ) ); // 한 번만
    SW_EXPECT_FALSE( table.take( 0, token ) );      // 0 은 "티켓 없음"
    SW_EXPECT_FALSE( table.take( 99, token ) );
    SW_EXPECT_EQUAL( 1, table.getCount() );

    table.clear();
    SW_EXPECT_EQUAL( 0, table.getCount() );
    SW_EXPECT_FALSE( table.take( first, token ) );
    SW_EXPECT_EQUAL( uint64( 3 ), table.add( makeToken( 300, 0x0103 ) ) ); // 지운 뒤에도 옛 티켓과 겹치지 않는다
}
