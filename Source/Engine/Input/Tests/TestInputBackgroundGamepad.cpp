#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/InputManager.h"

/**
 * @brief [InputManagerTest] 창이 배경이면 정책(`EngineConfig::_bBackgroundGamepadInput`)이 허락할 때만 패드를 읽는다 — XInput · evdev 는 창과 상관없이 읽혀 다른 창을 쓰는 동안 패드가 게임을 움직였다(감사 P-10).
 */
SW_TEST_CASE( InputManagerTest, BackgroundPadFollowsPolicy )
{
    SW_EXPECT_TRUE( sw::InputManager::canPollGamepad( true, false ) );
    SW_EXPECT_TRUE( sw::InputManager::canPollGamepad( true, true ) );
    SW_EXPECT_FALSE( sw::InputManager::canPollGamepad( false, false ) );
    SW_EXPECT_TRUE( sw::InputManager::canPollGamepad( false, true ) );
}
