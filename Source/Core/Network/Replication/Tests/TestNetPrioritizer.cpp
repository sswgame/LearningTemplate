#include "pch.h"

#include "Core/Network/Replication/NetPrioritizer.h"
#include "Core/Testing/TestFramework.h"

// 누적 우선도 — 늘 같은 순서, 보낸 것만 0 으로, 잃은 비신뢰 몫은 되돌린다.

using namespace sw;

/**
 * @brief [NetPrioritizerTest] 누적 우선도는 큰 것부터 · 같으면 id 순으로 늘 같은 순서이고, 보낸 것만 0 으로 돌아가 예산이 늘 차도 낮은 우선도가 차례를 얻는다
 */
SW_TEST_CASE( NetPrioritizerTest, PrioritizerOrderIsDeterministic )
{
    NetPrioritizer prioritizer;
    vector<uint32> listOrder;
    prioritizer.beginAccumulate();
    prioritizer.accumulate( 30, 2.0f, 0.5f );
    prioritizer.accumulate( 10, 1.0f, 1.0f );
    prioritizer.accumulate( 20, 4.0f, 1.0f );
    prioritizer.accumulate( 5, 1.0f, 1.0f );
    prioritizer.removeUntouched();
    prioritizer.collectOrder( listOrder );
    SW_REQUIRE_EQUAL( size_t{ 4 }, listOrder.size() );
    SW_EXPECT_EQUAL( 20u, listOrder[0] );
    SW_EXPECT_EQUAL( 5u, listOrder[1] ); // 1.0 셋 — id 순
    SW_EXPECT_EQUAL( 10u, listOrder[2] );
    SW_EXPECT_EQUAL( 30u, listOrder[3] );

    // 이번 틱에 쌓이지 않은 것(더는 관련 없음)은 잊는다.
    prioritizer.beginAccumulate();
    prioritizer.accumulate( 20, 4.0f, 1.0f );
    prioritizer.accumulate( 10, 1.0f, 1.0f );
    prioritizer.removeUntouched();
    SW_EXPECT_EQUAL( 2, prioritizer.getCount() );
    SW_EXPECT_NEAR( 8.0f, prioritizer.getAccumulated( 20 ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, prioritizer.getAccumulated( 5 ), 1.0e-6f );

    // 한 번에 둘만 보낼 수 있고 우선도 10 둘이 늘 있다 — 우선도 1 은 쌓여서 열 번째쯤 차례가 온다.
    prioritizer.clear();
    int32 firstLowRound = -1;
    for ( int32 round = 0; round < 30 && firstLowRound < 0; ++round )
    {
        prioritizer.beginAccumulate();
        prioritizer.accumulate( 1, 10.0f, 1.0f );
        prioritizer.accumulate( 2, 10.0f, 1.0f );
        prioritizer.accumulate( 3, 1.0f, 1.0f );
        prioritizer.removeUntouched();
        prioritizer.collectOrder( listOrder );
        for ( size_t index = 0; index < 2; ++index )
        {
            prioritizer.markSent( listOrder[index] );
            if ( listOrder[index] == 3 )
                firstLowRound = round;
        }
    }
    SW_EXPECT_TRUE( 8 <= firstLowRound && firstLowRound <= 12 );
    prioritizer.remove( 3 );
    SW_EXPECT_EQUAL( 2, prioritizer.getCount() );
}

/**
 * @brief [NetPrioritizerTest] 비신뢰로 실은 몫은 확인을 기다린다 — 잃었다고 판정하면 0 으로 돌렸던 몫이 돌아와 다음 차례에 바로 앞서고, 받았으면 그대로 0 이다
 */
SW_TEST_CASE( NetPrioritizerTest, PrioritizerRestoresAnUnconfirmedSendThatWasLost )
{
    NetPrioritizer prioritizer;
    prioritizer.beginAccumulate();
    prioritizer.accumulate( 1, 3.0f, 1.0f );
    prioritizer.accumulate( 2, 5.0f, 1.0f );
    prioritizer.removeUntouched();
    prioritizer.markSentUnconfirmed( 1, 10 );
    prioritizer.markSentUnconfirmed( 2, 10 );
    uint32 sentTick = 0;
    SW_REQUIRE_TRUE( prioritizer.findUnconfirmedSendTick( 1, sentTick ) );
    SW_EXPECT_EQUAL( 10u, sentTick );
    SW_EXPECT_NEAR( 0.0f, prioritizer.getAccumulated( 1 ), 1.0e-6f );

    // 확인 전에 또 실었다 — 몫은 더해지고 틱은 새 것.
    prioritizer.beginAccumulate();
    prioritizer.accumulate( 1, 3.0f, 1.0f );
    prioritizer.accumulate( 2, 5.0f, 1.0f );
    prioritizer.removeUntouched();
    prioritizer.markSentUnconfirmed( 1, 11 );
    SW_REQUIRE_TRUE( prioritizer.findUnconfirmedSendTick( 1, sentTick ) );
    SW_EXPECT_EQUAL( 11u, sentTick );

    prioritizer.resolveSend( 1, false ); // 잃었다 — 3 + 3 이 돌아온다
    prioritizer.resolveSend( 2, true );  // 받았다 — 이번 틱에 쌓인 5 만
    SW_EXPECT_NEAR( 6.0f, prioritizer.getAccumulated( 1 ), 1.0e-6f );
    SW_EXPECT_NEAR( 5.0f, prioritizer.getAccumulated( 2 ), 1.0e-6f );
    SW_EXPECT_FALSE( prioritizer.findUnconfirmedSendTick( 1, sentTick ) );
    prioritizer.resolveSend( 1, false ); // 기다리는 것이 없으면 아무것도 하지 않는다
    SW_EXPECT_NEAR( 6.0f, prioritizer.getAccumulated( 1 ), 1.0e-6f );

    // 받는 쪽이 이미 지금 상태 — 기억도 지운다.
    prioritizer.markSentUnconfirmed( 2, 12 );
    prioritizer.markSent( 2 );
    SW_EXPECT_FALSE( prioritizer.findUnconfirmedSendTick( 2, sentTick ) );
}
