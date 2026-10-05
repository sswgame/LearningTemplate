#include "pch.h"

#include "EngineTest/NetSimDestructionScenario.h"

#include "TestFramework/TestFramework.h"

// 나쁜 회선 둘(100 ms · 지터 20 ms · 손실 5 %, 250 ms · 손실 15 % · 중복 · 순서 바뀜)에서 같은 구조로 수렴하는지 · 덩어리 오차 · 사건 지연 · 대역폭.
// 회선마다 한 케이스다 — nogpu 조각(`EngineTest_NoGPU_Shard*`)이 두 케이스를 서로 다른 조각에 나눈다(Debug 에서 케이스마다 20 초 안팎).

using namespace test;

/**
 * @brief [NetSimDestructionMatrixTest] 100 ms · 지터 20 ms · 손실 5 % — 신뢰 순서 채널과 사건 번호가 순서를 지켜 같은 구조가 되고, 덩어리는 지연만큼 늦게
 *        서버를 따르며, 멈추면 서버와 같은 자리다. 어긋남은 없다
 */
SW_TEST_CASE( NetSimDestructionMatrixTest, MediumLossLinkConverges )
{
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );
    ScenarioOptions options;
    options._conditions         = makeConditions( 0.1, 0.02, 0.05f, 0.0f, 0.0f );
    const ScenarioResult result = runShowcase( document, options );
    logResult( "100ms/20ms/5%", result );
    expectConverged( result, 0.25f );
    SW_EXPECT_EQUAL( 0u, result._hashMismatchCount );
}

/**
 * @brief [NetSimDestructionMatrixTest] 250 ms · 손실 15 % · 중복 5 % · 순서 바뀜 5 % — 같은 구조로 수렴하고 어긋남이 없다
 */
SW_TEST_CASE( NetSimDestructionMatrixTest, HeavyLossDuplicateReorderLinkConverges )
{
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );
    ScenarioOptions options;
    options._conditions         = makeConditions( 0.25, 0.0, 0.15f, 0.05f, 0.05f );
    const ScenarioResult result = runShowcase( document, options );
    logResult( "250ms/15%/dup/reorder", result );
    expectConverged( result, 0.4f );
    SW_EXPECT_EQUAL( 0u, result._hashMismatchCount );
}
