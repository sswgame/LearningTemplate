#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Online/Service/PendingRequestTable.h"

// PendingRequestTableTest — 바깥에 맡긴 요청의 표: 답은 그 요청의 완료를 한 번 부르고 지운다, 모르는 번호 · 같은 번호 두 번은 맡지 않는다,
// 주인 취소는 그 주인 것만, 전체 취소는 두 인자 완료에만 알린다, 완료 안에서 새 요청을 맡겨도 된다(nogpu).

using namespace sw;

/** @brief [PendingRequestTableTest] 답은 완료를 한 번 부르고 열린 수를 줄인다. 모르는 번호와 같은 번호 두 번은 거절한다 */
SW_TEST_CASE( PendingRequestTableTest, AnswerCallsTheCompletionOnce )
{
    PendingRequestTable<uint64, int32> table;
    int32                              answered = 0;
    SW_REQUIRE_TRUE( table.track( 7, 0, [&answered]( const int32& result )
    { answered += result; } ) );
    SW_EXPECT_FALSE( table.track( 7, 0, []( const int32& ) {} ) );
    SW_EXPECT_EQUAL( 1, table.getOpenCount() );
    SW_EXPECT_FALSE( table.answer( 8, 5 ) );
    SW_EXPECT_TRUE( table.answer( 7, 5 ) );
    SW_EXPECT_FALSE( table.answer( 7, 5 ) );
    SW_EXPECT_EQUAL( 5, answered );
    SW_EXPECT_TRUE( table.isIdle() );
}

/** @brief [PendingRequestTableTest] 주인 취소는 그 주인의 요청만 끝내고, 한 인자 완료는 취소를 받지 않는다 */
SW_TEST_CASE( PendingRequestTableTest, CancelOwnedByEndsOnlyThatOwner )
{
    PendingRequestTable<uint64, int32> table;
    int32                              cancelledCount = 0;
    int32                              answeredCount  = 0;
    const auto                         countEnd       = [&]( const int32&, PendingRequestEnd end )
    { ( end == PendingRequestEnd::Cancelled ? cancelledCount : answeredCount )++; };
    SW_REQUIRE_TRUE( table.track( 1, 100, countEnd ) );
    SW_REQUIRE_TRUE( table.track( 2, 200, countEnd ) );
    SW_REQUIRE_TRUE( table.track( 3, 100, [&answeredCount]( const int32& )
    { ++answeredCount; } ) );
    table.cancelOwnedBy( 100 );
    SW_EXPECT_EQUAL( 1, cancelledCount );
    SW_EXPECT_EQUAL( 0, answeredCount );
    SW_EXPECT_EQUAL( 1, table.getOpenCount() );
    table.cancelAll();
    SW_EXPECT_EQUAL( 2, cancelledCount );
    SW_EXPECT_TRUE( table.isIdle() );
}

/** @brief [PendingRequestTableTest] 완료 안에서 새 요청을 맡겨도 표가 깨지지 않는다(줄을 지운 뒤 부른다) */
SW_TEST_CASE( PendingRequestTableTest, CompletionMayTrackANewRequest )
{
    PendingRequestTable<uint64, int32> table;
    SW_REQUIRE_TRUE( table.track( 1, 0, [&table]( const int32& )
    { (void)table.track( 1, 0, []( const int32& ) {} ); } ) );
    SW_EXPECT_TRUE( table.answer( 1, 0 ) );
    SW_EXPECT_EQUAL( 1, table.getOpenCount() );
}
