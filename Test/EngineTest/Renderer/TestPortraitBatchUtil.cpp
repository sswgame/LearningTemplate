#include "pch.h"

#include "Engine/Input/ShellInputActions.h"
#include "Engine/Renderer/Capture/PortraitBatchUtil.h"

#include "TestFramework/TestFramework.h"

// Engine 에서 떼어 낸 순수 로직 — 초상화 파일 이름 줄기 · 셸 액션 맵이 아직 없을 때(디바이스 · 입력 매니저 없이).

/**
 * @brief [PortraitBatchUtilTest] 파일 이름 줄기는 경로의 마지막 조각에서 첫 점 앞까지다
 */
SW_TEST_CASE( PortraitBatchUtilTest, FileStemIsLastSegmentBeforeFirstDot )
{
    SW_EXPECT_TRUE( sw::PortraitBatchUtil::makeFileStem( "game/heroes/hero.prefab.xml" ) == "hero" );
    SW_EXPECT_TRUE( sw::PortraitBatchUtil::makeFileStem( "hero.prefab.xml" ) == "hero" );
    SW_EXPECT_TRUE( sw::PortraitBatchUtil::makeFileStem( "game/heroes/hero" ) == "hero" );
    SW_EXPECT_TRUE( sw::PortraitBatchUtil::makeFileStem( "game/.hidden" ).empty() );
}

/**
 * @brief [ShellInputActionsTest] 처음 update 전에는 맵이 없고 어떤 액션도 발동하지 않는다
 */
SW_TEST_CASE( ShellInputActionsTest, NoMapBeforeFirstUpdate )
{
    sw::ShellInputActions actions;
    SW_EXPECT_TRUE( actions.getInputMap() == nullptr );
    SW_EXPECT_FALSE( actions.wasActionTriggered( "ReloadShaders" ) );
    actions.reset();
    SW_EXPECT_TRUE( actions.getInputMap() == nullptr );
}
