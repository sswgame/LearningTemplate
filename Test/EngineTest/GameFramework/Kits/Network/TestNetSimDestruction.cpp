#include "pch.h"

#include "EngineTest/NetSimDestructionScenario.h"

#include "TestFramework/TestFramework.h"

// 깨끗한 회선 — 구조 수렴 · 덩어리 · 파편 레이어 · 늦은 참가 스냅숏 · 빠진 사건 복구. 시나리오는 NetSimDestructionScenario.h.

using namespace test;

/**
 * @brief [NetSimDestructionTest] 깨끗한 회선 — 클라이언트 셋이 서버와 같은 구조가 되고(덩어리는 서버를 따르고 파편은 캐릭터와 부딪히지 않는다), 다 부서진
 *        뒤 들어온 넷째는 스냅숏 하나로 같아지고, 사건 하나를 빼먹은 첫째는 해시 비교로 알아채 스냅숏으로 바로잡힌다
 */
SW_TEST_CASE( NetSimDestructionTest, CleanLinkConvergesLateJoinsAndRepairs )
{
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );
    ScenarioOptions options;
    options._lateJoinTick       = 360;
    options._skipEventClient    = 0;
    options._tickCount          = 480;
    const ScenarioResult result = runShowcase( document, options );
    logResult( "clean + late join + dropped event", result );
    expectConverged( result, 0.1f );
    SW_EXPECT_TRUE_MSG( 0 < result._lateJoinMatchTicks && result._lateJoinMatchTicks < 120, "the late joiner matches within two seconds" );
    SW_EXPECT_TRUE( result._hashMismatchCount >= 1 );
    SW_EXPECT_TRUE_MSG( 0 < result._desyncRecoverTicks && result._desyncRecoverTicks < 180, "the dropped event is repaired within three seconds" );
}
