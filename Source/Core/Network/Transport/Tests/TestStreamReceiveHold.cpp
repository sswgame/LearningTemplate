#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Network/Transport/StreamReceiveHold.h"
#include "Core/Testing/TestFramework.h"

// 브라우저 WebSocket 의 받는 쪽 배압 — 멈춘 동안은 들고, 다시 열면 받은 순서대로 넘기고, 상한을 넘으면 Overflow 다.

using namespace sw;

/** @brief [StreamReceiveHoldTest] 멈추지 않았으면 바로 넘기고, 멈춘 동안 받은 것은 들었다가 다시 열면 순서대로 한 번에 꺼낸다. */
SW_TEST_CASE( StreamReceiveHoldTest, HoldsWhilePausedAndReleasesInOrder )
{
    StreamReceiveHold hold;
    const uint8       arrFirst[]  = { 1, 2 };
    const uint8       arrSecond[] = { 3 };
    const uint8       arrThird[]  = { 4, 5 };
    SW_EXPECT_TRUE( hold.accept( arrFirst, 2, 16 ) == StreamReceiveHoldResult::Deliver );

    hold.setPaused( true );
    SW_EXPECT_TRUE( hold.accept( arrFirst, 2, 16 ) == StreamReceiveHoldResult::Held );
    SW_EXPECT_TRUE( hold.accept( arrSecond, 1, 16 ) == StreamReceiveHoldResult::Held );
    SW_EXPECT_TRUE( hold.takeReleasable().empty() ); // 멈춘 동안은 꺼내지 않는다

    hold.setPaused( false );
    // 다시 열었어도 든 것이 넘어가기 전에 온 바이트는 뒤에 붙인다(순서).
    SW_EXPECT_TRUE( hold.accept( arrThird, 2, 16 ) == StreamReceiveHoldResult::Held );
    const vector<uint8> released = hold.takeReleasable();
    SW_EXPECT_TRUE( released == ( vector<uint8>{ 1, 2, 3, 4, 5 } ) );
    SW_EXPECT_EQUAL( 0, hold.getHeldBytes() );
    SW_EXPECT_TRUE( hold.accept( arrSecond, 1, 16 ) == StreamReceiveHoldResult::Deliver );
}

/** @brief [StreamReceiveHoldTest] 상한을 넘게 되는 바이트는 들지 않고 Overflow 다 — 든 것은 그대로다. */
SW_TEST_CASE( StreamReceiveHoldTest, OverflowBeyondLimitKeepsHeldBytes )
{
    StreamReceiveHold hold;
    const uint8       arrBytes[] = { 1, 2, 3, 4 };
    hold.setPaused( true );
    SW_EXPECT_TRUE( hold.accept( arrBytes, 4, 6 ) == StreamReceiveHoldResult::Held );
    SW_EXPECT_TRUE( hold.accept( arrBytes, 4, 6 ) == StreamReceiveHoldResult::Overflow );
    SW_EXPECT_EQUAL( 4, hold.getHeldBytes() );

    hold.clear();
    SW_EXPECT_FALSE( hold.isPaused() );
    SW_EXPECT_EQUAL( 0, hold.getHeldBytes() );
}
