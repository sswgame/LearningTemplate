#include "pch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Runtime/Online/Identity/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/ServiceMail.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"

// OnlineServiceTableTest — 온라인 서비스의 와이어 표: 메서드 영역(메서드 · 알림 절반), 메서드 표의 번호(toMethod · toPush · toIndex),
// 공통 범주 표(양쪽 방향, 클라이언트 전용 줄), 메서드 이름 순서 해시.

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
        { sw::OnlineError::kNotFound, TestResult::NotFound },
        { sw::OnlineError::kUnavailable, TestResult::Unavailable },
        { sw::OnlineError::kVersionMismatch, TestResult::UpdateRequired, true }, // 클라이언트만 쓰는 줄이다
    };
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
 * @brief [OnlineServiceTableTest] 공통 범주 표: 공통 오류 → 키트 결과는 표에 있으면 그 결과, 없으면 기본값이다. 키트 결과 → 공통 오류는 표에 없으면 kOk(업무 결과)이고 클라이언트 전용 줄을 보지 않는다
 */
SW_TEST_CASE( OnlineServiceTableTest, CommonResultTableMapsBothWays )
{
    SW_EXPECT_TRUE( sw::OnlineError::toResult( sw::OnlineError::kNotFound, kArrErrorRow, TestResult::Unavailable ) == TestResult::NotFound );
    SW_EXPECT_TRUE( sw::OnlineError::toResult( sw::OnlineError::kConflict, kArrErrorRow, TestResult::Unavailable ) == TestResult::Unavailable );
    SW_EXPECT_TRUE( sw::OnlineError::toResult( sw::OnlineError::kVersionMismatch, kArrErrorRow, TestResult::Unavailable ) == TestResult::UpdateRequired );

    SW_EXPECT_EQUAL( sw::OnlineError::kNotFound, sw::OnlineError::toErrorCode( TestResult::NotFound, kArrErrorRow ) );
    SW_EXPECT_EQUAL( sw::OnlineError::kOk, sw::OnlineError::toErrorCode( TestResult::Ok, kArrErrorRow ) );
    SW_EXPECT_EQUAL( sw::OnlineError::kOk, sw::OnlineError::toErrorCode( TestResult::UpdateRequired, kArrErrorRow ) ); // 서버는 보내지 않는 값이다
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
 * @brief [OnlineServiceTableTest] 메시지 타입 목록의 해시는 타입마다의 스키마 해시를 목록 순서대로 섞은 값이고, 타입이나 순서가 다르면 달라진다
 * @details 키트는 이 목록 하나로 메시지를 쓰고 읽으며 같은 목록이 등록 항목에 섞이므로, 와이어에 오르는 타입의 배치가 바뀌면 온라인 스키마 해시가 바뀐다.
 */
SW_TEST_CASE( OnlineServiceTableTest, MessageTypesHashFollowsTheTypesAndTheirOrder )
{
    using Balance      = sw::OnlineMessageTypes<sw::LedgerBalance>;
    using Attachment   = sw::OnlineMessageTypes<sw::ServiceMailAttachment>;
    using Both         = sw::OnlineMessageTypes<sw::LedgerBalance, sw::ServiceMailAttachment>;
    using BothReversed = sw::OnlineMessageTypes<sw::ServiceMailAttachment, sw::LedgerBalance>;
    SW_EXPECT_EQUAL( sw::HashUtil::combine( 0, sw::FixedLayout::getSchemaHash<sw::LedgerBalance>() ), Balance::computeSchemaHash() );
    SW_EXPECT_NOT_EQUAL( Balance::computeSchemaHash(), Attachment::computeSchemaHash() ); // 필드가 같아도 타입 이름이 정규 문자열에 들어간다
    SW_EXPECT_NOT_EQUAL( Both::computeSchemaHash(), BothReversed::computeSchemaHash() );
    SW_EXPECT_EQUAL( uint64( 0 ), sw::OnlineMessageTypes<>::computeSchemaHash() );
    SW_EXPECT_TRUE( Both::kContains<sw::LedgerBalance> );
    SW_EXPECT_FALSE( Both::kContains<sw::AccountIdentity> );
    SW_EXPECT_EQUAL( sw::OnlineProtocolRegistry::computeSchemaHash(), sw::OnlineProtocolRegistry::computeSchemaHash() );
}
