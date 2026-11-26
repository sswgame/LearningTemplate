// IntentReplayBarTest: 되감기 막대를 열기 전의 계약: 호스트는 평소처럼 돌고 닫기 · 갱신은 할 일이 없으며, 틱 길이 · 되감기 틱 수가 0 이면 열지 않는다(화면 없음 — nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/UISystem.h"

#include "GameFramework/Client/Intent/IntentReplayBar.h"
#include "GameFramework/Runtime/Character/Control/Intent/IntentRewindSession.h"

using namespace sw;

/** @brief [IntentReplayBarTest] 닫힌 막대는 호스트의 틱을 막지 않고, 닫기 · 갱신을 불러도 그대로다 */
SW_TEST_CASE( IntentReplayBarTest, ClosedBarLeavesTheHostRunning )
{
    IntentReplayBar bar;
    SW_EXPECT_FALSE( bar.isOpen() );
    SW_EXPECT_FALSE( bar.isPaused() );
    SW_EXPECT_TRUE( bar.shouldHostStep() );
    bar.update();
    bar.close();
    SW_EXPECT_FALSE( bar.isOpen() );
    SW_EXPECT_TRUE( bar.shouldHostStep() );
}

/** @brief [IntentReplayBarTest] 틱 길이나 프레임마다 되감을 틱 수가 양수가 아니면 문서를 열기 전에 거절한다 */
SW_TEST_CASE( IntentReplayBarTest, RefusesToOpenWithoutPositiveSteps )
{
    SW_EXPECT_LOG( "IntentReplayBar.IntentReplayBarRefusedOpenTick" );
    UISystem            uiSystem;
    IntentRewindSession session;
    IntentReplayBar     bar;
    SW_EXPECT_FALSE( bar.open( uiSystem, "common/ui/replaybar.ui.xml", session, 0.0f, 4 ) );
    SW_EXPECT_FALSE( bar.open( uiSystem, "common/ui/replaybar.ui.xml", session, 1.0f / 60.0f, 0 ) );
    SW_EXPECT_FALSE( bar.isOpen() );
    SW_EXPECT_TRUE( bar.shouldHostStep() );
}
