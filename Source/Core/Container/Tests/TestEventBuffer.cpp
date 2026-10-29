#include "pch.h"

#include "Core/Container/EventBuffer.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

/**
 * @brief [EventBufferTest] 알림 버퍼는 빈 목록에 저장소를 맞바꿔 넘기고(복사 없음 — 두 버퍼가 번갈아 돈다), 찬 목록에는 뒤에 붙인다
 */
SW_TEST_CASE( EventBufferTest, SwapsIntoAnEmptyListAndAppendsToAFullOne )
{
    EventBuffer<int32> buffer;
    vector<int32>      listEvent;
    listEvent.reserve( 8 );
    const int32* pReceiving = listEvent.data();
    buffer.push( 1 );
    buffer.push( 2 );
    buffer.getLast() = 3; // 쌓은 뒤 칸을 고친다
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), buffer.getCount() );
    const int32* pPending = buffer.getPending().data();

    // 빈 목록 — 맞바꾼다. 받는 쪽은 쌓던 저장소를, 쌓는 쪽은 받는 쪽이 쓰던(비운) 저장소를 갖는다.
    buffer.drainTo( listEvent );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), listEvent.size() );
    SW_EXPECT_EQUAL( 1, listEvent[0] );
    SW_EXPECT_EQUAL( 3, listEvent[1] );
    SW_EXPECT_TRUE( listEvent.data() == pPending );
    SW_EXPECT_TRUE( buffer.isEmpty() );
    buffer.push( 4 );
    SW_EXPECT_TRUE( buffer.getPending().data() == pReceiving );

    // 찬 목록 — 뒤에 붙이고 비운다(여러 곳의 알림을 한 목록에 모으는 쪽).
    buffer.drainTo( listEvent );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 3 ), listEvent.size() );
    SW_EXPECT_EQUAL( 4, listEvent[2] );
    SW_EXPECT_TRUE( buffer.isEmpty() );

    buffer.push( 5 );
    buffer.clear();
    vector<int32> listOther;
    buffer.drainTo( listOther );
    SW_EXPECT_TRUE( listOther.empty() );
}

/**
 * @brief [EventBufferTest] 꺼내기 표를 쥔 쪽만 꺼내고, 복사본은 표 없는 새 큐이며, 표를 놓으면 다음 표가 받는다
 */
SW_TEST_CASE( EventBufferTest, LeaseHolderDrainsAndCopiesCarryNoLease )
{
    EventBuffer<int32> buffer;
    DrainLease         first;
    DrainLease         second;
    SW_REQUIRE_TRUE( buffer.claim( first ) );
    SW_EXPECT_TRUE( buffer.claim( first ) ); // 이미 쥔 표는 다시 받아도 true

    buffer.push( 1 );
    buffer.push( 2 );
    vector<int32> listEvent;
    buffer.drainTo( first, listEvent );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 2 ), listEvent.size() );
    SW_EXPECT_TRUE( buffer.isEmpty() );

    // 복사본은 쌓인 알림만 물려받고 표는 아무도 안 쥔다.
    buffer.push( 3 );
    EventBuffer<int32> copy = buffer;
    DrainLease         copyLease;
    SW_REQUIRE_TRUE( copy.claim( copyLease ) );
    listEvent.clear();
    copy.drainTo( copyLease, listEvent );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), listEvent.size() );
    SW_EXPECT_EQUAL( static_cast<size_t>( 1 ), buffer.getCount() ); // 원본은 그대로

    first.release();
    SW_REQUIRE_TRUE( buffer.claim( second ) );
    listEvent.clear();
    buffer.drainTo( second, listEvent );
    SW_EXPECT_EQUAL( 3, listEvent[0] );
}
