// TradeTypesTest — 거래 스냅샷의 쪽 찾기: 신청한 쪽이 0, 받은 쪽이 1, 거래 밖 계정과 무효 계정은 없음이다. 둘 다 확정해야 확정이다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeTypes.h"

using namespace sw;

/** @brief [TradeTypesTest] 쪽 번호는 계정으로 찾고, 거래 밖 · 무효 계정은 없음이다. 두 쪽이 모두 확정해야 확정이다 */
SW_TEST_CASE( TradeTypesTest, FindsSideByAccountAndNeedsBothConfirmations )
{
    TradeSnapshot snapshot;
    snapshot._arrSide[0]._accountID = 7;
    snapshot._arrSide[1]._accountID = 9;
    SW_EXPECT_EQUAL( 0, snapshot.findSideIndex( 7 ).value_or( -1 ) );
    SW_EXPECT_EQUAL( 1, snapshot.findSideIndex( 9 ).value_or( -1 ) );
    SW_EXPECT_FALSE( snapshot.findSideIndex( 8 ).has_value() );
    SW_EXPECT_FALSE( snapshot.findSideIndex( kInvalidAccountID ).has_value() );

    SW_EXPECT_FALSE( snapshot.isBothConfirmed() );
    snapshot._arrSide[0]._bConfirmed = SW_TRUE;
    SW_EXPECT_FALSE( snapshot.isBothConfirmed() );
    snapshot._arrSide[1]._bConfirmed = SW_TRUE;
    SW_EXPECT_TRUE( snapshot.isBothConfirmed() );
}
