#include "pch.h"

#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Testing/TestFramework.h"

// 보내기 예산 — 메시지 상한으로 자르기, 키트 틱 예산이 연결 상한을 따름.

using namespace sw;

/**
 * @brief [NetSendBudgetTest] 보내기 예산은 메시지 상한으로 잘리고, 안 들어가는 항목은 세지 않아 작은 다음 항목이 들어갈 수 있다
 */
SW_TEST_CASE( NetSendBudgetTest, SendBudgetClampsToMessageLimit )
{
    NetSendBudget budget( 4000 );
    SW_EXPECT_EQUAL( NetConnection::kMaxSingleMessageSize * 8, budget.getMaxBits() );
    budget.reserveBits( 8 );
    SW_EXPECT_FALSE( budget.tryReserveBits( NetConnection::kMaxSingleMessageSize * 8 ) );
    SW_EXPECT_EQUAL( 8, budget.getUsedBits() );
    SW_EXPECT_TRUE( budget.tryReserveBits( NetConnection::kMaxSingleMessageSize * 8 - 8 ) );
    SW_EXPECT_EQUAL( 0, budget.getRemainingBits() );
    SW_EXPECT_FALSE( budget.tryReserveBits( 1 ) );
    SW_EXPECT_FALSE( budget.hasExceeded() );
    budget.reserveBits( 1 );
    SW_EXPECT_TRUE( budget.hasExceeded() );

    // 신뢰 순서 메시지 하나에 묶는 예산 — 상한 인자를 준 만큼, 그래도 신뢰 상한(64 KB)을 넘지 않는다.
    const NetSendBudget reliable( 100000, NetConnection::kMaxReliableMessageSize );
    SW_EXPECT_EQUAL( NetConnection::kMaxReliableMessageSize * 8, reliable.getMaxBits() );
    const NetSendBudget beyond( 100000, 1 << 20 );
    SW_EXPECT_EQUAL( NetConnection::kMaxReliableMessageSize * 8, beyond.getMaxBits() );
    SW_EXPECT_EQUAL( 0, NetSendBudget( -5 ).getMaxBits() );
}

/**
 * @brief [NetSendBudgetTest] 키트 틱 예산은 설정과 연결 상한의 몫(초당 바이트 × 틱 간격 × 0.5) 중 작은 것이다 — 상한 · 간격을 모르면 설정 그대로
 */
SW_TEST_CASE( NetSendBudgetTest, KitTickBudgetFollowsTheConnectionCap )
{
    SW_EXPECT_EQUAL( 1000, NetSendBudget::computeTickBudget( 1000, NetEndpointConfig{}._maxBytesPerSecond, 1.0 / 30.0 ) ); // 몫 1666 — 설정이 작다
    SW_EXPECT_EQUAL( 333, NetSendBudget::computeTickBudget( 1000, 20000, 1.0 / 30.0 ) );
    SW_EXPECT_EQUAL( 1000, NetSendBudget::computeTickBudget( 1000, 0, 1.0 / 30.0 ) );
    SW_EXPECT_EQUAL( 1000, NetSendBudget::computeTickBudget( 1000, 20000, 0.0 ) );
    SW_EXPECT_EQUAL( 600, NetSendBudget::computeTickBudget( 600, 100000, 1.0 / 60.0 ) ); // MMO 기본 · 60 Hz — 몫 833
}
