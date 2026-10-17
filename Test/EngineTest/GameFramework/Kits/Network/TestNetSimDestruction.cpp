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

/**
 * @brief [NetSimDestructionTest] 위치 범위(`_positionRange`) 밖으로 나간 덩어리는 움직이는 자세를 보내지 않고 세며, 멈추면 비트 그대로 보내 서버와 같은 자리가 된다
 * @details 예전에는 범위 밖 위치를 경계로 잘라 보내 받는 쪽에서 덩어리가 경계에 붙어 보였다. 범위를 1 m 로 줄여 쇼케이스의 덩어리가 모두 범위 밖이 되게 한다.
 */
SW_TEST_CASE( NetSimDestructionTest, ChunksOutsideThePositionRangeAreNotSentMovingButRestExactly )
{
    SceneDocument document;
    SW_ASSERT_TRUE( loadShowcase( document ) );
    ScenarioOptions options;
    options._tickCount                        = 480;
    options._clientCount                      = 1;
    options._replicationConfig._positionRange = 1.0f;
    ScenarioResult result;
    {
        SW_TEST_DEFENSIVE_SCOPE( "a chunk moved outside the position range" );
        result = runShowcase( document, options );
    }
    logResult( "position range 1 m", result );
    SW_ASSERT_TRUE( result._bValid );
    SW_EXPECT_TRUE_MSG( result._outOfRangePoseCount > 0, "moving poses outside the range are counted, not clamped and sent" );
    SW_EXPECT_TRUE_MSG( result._convergedTick >= 0, "the structure still converges" );
    SW_EXPECT_TRUE_MSG( result._maxRestError < 1.0e-4f, "rested chunks are sent bit for bit and end where the server's are" );
}
