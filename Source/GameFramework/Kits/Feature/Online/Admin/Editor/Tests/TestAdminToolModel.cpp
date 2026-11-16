#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Admin/Editor/Model/AdminToolModel.h"

#include <limits>

// AdminToolModelTest — GM 도구 패널의 입력 칸 해석, 표 칸의 글, 기간 계산(에디터 케이스, `editor.unitTests`).

using namespace sw;
using namespace sw::editor;

SW_TEST_CASE( AdminToolModelTest, AccountIDsParseAsHex )
{
    AccountID accountID = kInvalidAccountID;
    SW_REQUIRE_TRUE( AdminToolModel::parseAccountID( "100", accountID ) );
    SW_EXPECT_EQUAL( AccountID( 0x100 ), accountID );
    SW_REQUIRE_TRUE( AdminToolModel::parseAccountID( "  FFff  ", accountID ) ); // 붙여 넣은 공백은 값이 아니다
    SW_EXPECT_EQUAL( AccountID( 0xFFFF ), accountID );
    SW_REQUIRE_TRUE( AdminToolModel::parseAccountID( AdminToolModel::formatAccountID( 0xDEADBEEFull ), accountID ) ); // 표에 보인 글을 그대로 칸에 넣을 수 있다
    SW_EXPECT_EQUAL( AccountID( 0xDEADBEEFull ), accountID );
}

SW_TEST_CASE( AdminToolModelTest, BadAccountTextIsRefusedAndKeepsTheOutput )
{
    AccountID accountID = 7;
    SW_EXPECT_FALSE( AdminToolModel::parseAccountID( "", accountID ) );
    SW_EXPECT_FALSE( AdminToolModel::parseAccountID( "   ", accountID ) );
    SW_EXPECT_FALSE( AdminToolModel::parseAccountID( "xyz", accountID ) );
    SW_EXPECT_FALSE( AdminToolModel::parseAccountID( "12 34", accountID ) );
    SW_EXPECT_FALSE( AdminToolModel::parseAccountID( "-1", accountID ) );
    SW_EXPECT_FALSE( AdminToolModel::parseAccountID( "0", accountID ) );                 // 0 은 계정이 아니다
    SW_EXPECT_FALSE( AdminToolModel::parseAccountID( "1FFFFFFFFFFFFFFFF", accountID ) ); // 64 비트를 넘는다
    SW_EXPECT_EQUAL( AccountID( 7 ), accountID );
}

SW_TEST_CASE( AdminToolModelTest, AccountIDsFormatAsFixedWidthHex )
{
    SW_EXPECT_EQUAL( string( "0000000000000100" ), AdminToolModel::formatAccountID( 0x100 ) ); // 감사 주체(`acct.<16진>/`)와 같은 모양이다
}

SW_TEST_CASE( AdminToolModelTest, BalanceShowsDebt )
{
    LedgerBalance balance;
    balance._amount = 150;
    SW_EXPECT_EQUAL( string( "150" ), AdminToolModel::formatBalance( balance ) );
    balance._amount = -5;
    SW_EXPECT_EQUAL( string( "-5 (debt)" ), AdminToolModel::formatBalance( balance ) );
}

/**
 * @brief 시각 칸은 두 자리로 채운 UTC 다.
 * @details 결함: 채움이 없어 09:05:07 이 "9:5:7" 로 보여 줄을 견주기 어려웠고, 1970 앞의 값(서버가 준 값)이면 음수 칸이 나왔다.
 */
SW_TEST_CASE( AdminToolModelTest, ClockIsZeroPaddedUTC )
{
    SW_EXPECT_EQUAL( string( "00:00:00" ), AdminToolModel::formatClock( 0 ) );
    SW_EXPECT_EQUAL( string( "09:05:07" ), AdminToolModel::formatClock( ( 9 * 3600 + 5 * 60 + 7 ) * 1000ll ) );
    SW_EXPECT_EQUAL( string( "23:59:59" ), AdminToolModel::formatClock( ( 3 * 86400 - 1 ) * 1000ll + 999 ) );
    SW_EXPECT_EQUAL( string( "23:59:59" ), AdminToolModel::formatClock( -1000 ) );
}

SW_TEST_CASE( AdminToolModelTest, SanctionStates )
{
    constexpr int64 kNowMs = 1000000;
    SW_EXPECT_EQUAL( string( "none" ), AdminToolModel::formatSanction( 0, kNowMs ) );
    SW_EXPECT_EQUAL( string( "permanent" ), AdminToolModel::formatSanction( ServiceSanctionState::kPermanentMs, kNowMs ) );
    SW_EXPECT_EQUAL( string( "expired" ), AdminToolModel::formatSanction( kNowMs - 1, kNowMs ) );
    SW_EXPECT_EQUAL( string( "expired" ), AdminToolModel::formatSanction( kNowMs + 999, kNowMs ) ); // 1 초가 안 남으면 끝난 것으로 보인다
    SW_EXPECT_EQUAL( string( "60 s left" ), AdminToolModel::formatSanction( kNowMs + 60000, kNowMs ) );
}

/** @brief 결함: 칸의 음수를 그대로 보내 Grant 가 회수하고 Revoke 가 지급했다(`-INT64_MIN` 은 정의되지 않은 동작이었다). */
SW_TEST_CASE( AdminToolModelTest, AdjustFieldTakesAPositiveMagnitude )
{
    SW_EXPECT_TRUE( AdminToolModel::isAdjustAmountValid( 1 ) );
    SW_EXPECT_FALSE( AdminToolModel::isAdjustAmountValid( 0 ) );
    SW_EXPECT_FALSE( AdminToolModel::isAdjustAmountValid( -1 ) );
    SW_EXPECT_FALSE( AdminToolModel::isAdjustAmountValid( std::numeric_limits<int64>::min() ) );
    SW_EXPECT_TRUE( AdminToolModel::isSanctionDurationValid( 1 ) );
    SW_EXPECT_FALSE( AdminToolModel::isSanctionDurationValid( 0 ) ); // 0 은 해제다 — 거는 단추로 보내지 않는다
    SW_EXPECT_FALSE( AdminToolModel::isSanctionDurationValid( -1 ) );
}

/** @brief 결함: 음수 기간은 0 과 같은 뜻(해제 · 끝날 때까지)이 되었고, 큰 기간은 넘쳐 음수 시각이 되었다. */
SW_TEST_CASE( AdminToolModelTest, EndTimeRefusesNegativeAndOverflow )
{
    int64 endMs = -1;
    SW_REQUIRE_TRUE( AdminToolModel::tryMakeEndMs( 5000, 0, endMs ) );
    SW_EXPECT_EQUAL( int64( 0 ), endMs ); // 끝 없음
    SW_REQUIRE_TRUE( AdminToolModel::tryMakeEndMs( 5000, 60, endMs ) );
    SW_EXPECT_EQUAL( int64( 65000 ), endMs );

    endMs = 123;
    SW_EXPECT_FALSE( AdminToolModel::tryMakeEndMs( 5000, -1, endMs ) );
    SW_EXPECT_FALSE( AdminToolModel::tryMakeEndMs( 5000, std::numeric_limits<int64>::min(), endMs ) );
    SW_EXPECT_FALSE( AdminToolModel::tryMakeEndMs( 5000, std::numeric_limits<int64>::max(), endMs ) );
    SW_EXPECT_FALSE( AdminToolModel::tryMakeEndMs( 5000, std::numeric_limits<int64>::max() / 1000, endMs ) ); // 곱은 맞아도 더하면 넘친다
    SW_EXPECT_EQUAL( int64( 123 ), endMs );
    const int64 maxSeconds = ( std::numeric_limits<int64>::max() - 5000 ) / 1000;
    SW_EXPECT_TRUE( AdminToolModel::tryMakeEndMs( 5000, maxSeconds, endMs ) ); // 경계는 들어간다
}

SW_TEST_CASE( AdminToolModelTest, MethodNames )
{
    SW_EXPECT_EQUAL( string( "LookupAccount" ), string( AdminToolModel::getMethodName( AdminMethod::LookupAccount::kID ) ) ); // 시나리오가 로그 줄로 센다(`admintool.scenario.xml`)
    SW_EXPECT_EQUAL( string( "AdjustAsset" ), string( AdminToolModel::getMethodName( AdminMethod::AdjustAsset::kID ) ) );
    SW_EXPECT_EQUAL( string( "SetSanction" ), string( AdminToolModel::getMethodName( AdminMethod::SetSanction::kID ) ) );
    SW_EXPECT_EQUAL( string( "ListAudit" ), string( AdminToolModel::getMethodName( AdminMethod::ListAudit::kID ) ) );
    SW_EXPECT_EQUAL( string( "Report" ), string( AdminToolModel::getMethodName( AdminMethod::ResolveReport::kID ) ) );
    SW_EXPECT_EQUAL( string( "ServerDirectory" ), string( AdminToolModel::getMethodName( AdminMethod::PostAnnouncement::kID ) ) );
    SW_EXPECT_EQUAL( string( "LiveEvent" ), string( AdminToolModel::getMethodName( AdminMethod::PutLiveEvent::kID ) ) );
    SW_EXPECT_EQUAL( string( "Command" ), string( AdminToolModel::getMethodName( AdminMethod::SendMail::kID ) ) );
}
