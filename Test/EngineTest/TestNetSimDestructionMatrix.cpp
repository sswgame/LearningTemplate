#include "pch.h"

#include "EngineTest/NetSimDestructionScenario.h"

#include "TestFramework/TestFramework.h"

// 나쁜 회선 둘(100 ms · 지터 20 ms · 손실 5 %, 250 ms · 손실 15 % · 중복 · 순서 바뀜)에서 같은 구조로 수렴하는지 · 덩어리 오차 · 사건 지연 · 대역폭.
// 월드 넷 × 600 틱 × 두 번이라 Debug 에서 50 초쯤 걸린다 — nogpu 의 EngineTest 시간 한도(180 초)를 지키려고 호스트 스위트로 둔다(Shipping 에서 몇 초).

SW_TEST_REQUIRES_HOST( NetSimDestructionMatrixTest, "slow multi-world scenario (about 50 s in Debug) - run with the host suites in Shipping" );

using namespace test;

/**
 * @brief [NetSimDestructionMatrixTest] 나쁜 회선 둘 — 100 ms · 지터 20 ms · 손실 5 %, 250 ms · 손실 15 % · 중복 5 % · 순서 바뀜 5 %. 신뢰 순서 채널과 사건 번호가
 *        순서를 지켜 같은 구조가 되고, 덩어리는 지연만큼 늦게 서버를 따르며, 멈추면 서버와 같은 자리다. 어긋남은 없다
 */
SW_TEST_CASE( NetSimDestructionMatrixTest, LossyLinksConverge )
{
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );
    ScenarioOptions medium;
    medium._conditions         = makeConditions( 0.1, 0.02, 0.05f, 0.0f, 0.0f );
    const ScenarioResult first = runShowcase( document, medium );
    logResult( "100ms/20ms/5%", first );
    expectConverged( first, 0.25f );
    SW_EXPECT_EQUAL( 0u, first._hashMismatchCount );

    ScenarioOptions heavy;
    heavy._conditions           = makeConditions( 0.25, 0.0, 0.15f, 0.05f, 0.05f );
    const ScenarioResult second = runShowcase( document, heavy );
    logResult( "250ms/15%/dup/reorder", second );
    expectConverged( second, 0.4f );
    SW_EXPECT_EQUAL( 0u, second._hashMismatchCount );
}
