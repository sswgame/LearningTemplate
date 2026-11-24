// GameTimeScaleTest — 정지 요청의 소유 · 겹침과, 시간을 함께 쓰는 동안 플레이어 정지가 듣지 않는 것(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Config/GameTimeScale.h"

using namespace sw;

/** @brief [GameTimeScaleTest] 요청은 쌓이고, 구독이 사라지면 풀린다 — 겹친 요청은 하나가 풀려도 멈춰 있다 */
SW_TEST_CASE( GameTimeScaleTest, PauseRequestsStackAndEndWithTheirOwner )
{
    GameTimeScale timeScale;
    Subscription  menu = timeScale.addPauseRequest( GamePauseReason::Player );
    {
        const Subscription photoMode = timeScale.addPauseRequest( GamePauseReason::Player );
        SW_EXPECT_EQUAL( 2u, timeScale.getPauseRequestCount() );
    }
    SW_EXPECT_TRUE( timeScale.isPaused() );
    menu.reset();
    SW_EXPECT_FALSE( timeScale.isPaused() );
    SW_EXPECT_EQUAL( 0u, timeScale.getPauseRequestCount() );
}

/** @brief [GameTimeScaleTest] 네트워크 세션이 시간을 함께 쓰는 동안 플레이어 정지는 듣지 않고 호스트 붙잡기는 듣는다. 세션이 끝나면 남은 요청이 다시 듣는다 */
SW_TEST_CASE( GameTimeScaleTest, SharedWorldTimeIgnoresPlayerPause )
{
    GameTimeScale      timeScale;
    const Subscription menu    = timeScale.addPauseRequest( GamePauseReason::Player );
    Subscription       session = timeScale.shareWorldTime();
    SW_EXPECT_FALSE( timeScale.isPaused() );
    {
        const Subscription hold = timeScale.addPauseRequest( GamePauseReason::Host );
        SW_EXPECT_TRUE( timeScale.isPaused() );
    }
    SW_EXPECT_FALSE( timeScale.isPaused() );
    session.reset();
    SW_EXPECT_TRUE( timeScale.isPaused() );
}
