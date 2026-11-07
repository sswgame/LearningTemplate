#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Devices/PlatformGamepadLayout.h"

using namespace sw;

/** @brief [PlatformGamepadLayoutTest] W3C standard 배치의 얼굴 · 어깨 · D패드 · 가운데 버튼이 엔진 버튼으로 가고, 방아쇠 · 홈 · 범위 밖은 버튼이 없다 */
SW_TEST_CASE( PlatformGamepadLayoutTest, StandardButtonsMapToEngineButtons )
{
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 0 ) == GamepadButton::A );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 3 ) == GamepadButton::Y );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 5 ) == GamepadButton::RightShoulder );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 8 ) == GamepadButton::Back );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 9 ) == GamepadButton::Start );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 12 ) == GamepadButton::DPadUp );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 15 ) == GamepadButton::DPadRight );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( PlatformGamepadLayout::kStandardLeftTrigger ) == GamepadButton::Count );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( PlatformGamepadLayout::kStandardRightTrigger ) == GamepadButton::Count );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 16 ) == GamepadButton::Count );
    SW_EXPECT_TRUE( PlatformGamepadLayout::mapStandardButton( 64 ) == GamepadButton::Count );
}

/** @brief [PlatformGamepadLayoutTest] 눌림 배열이 엔진 버튼 비트로 모이고, 방아쇠 · 홈의 눌림은 비트가 없다 */
SW_TEST_CASE( PlatformGamepadLayoutTest, StandardMaskSkipsTriggersAndHome )
{
    bool arrDigital[PlatformGamepadLayout::kStandardButtonCount]{};
    arrDigital[0]     = true; // A
    arrDigital[7]     = true; // 오른 방아쇠 — 축으로 간다
    arrDigital[14]    = true; // D패드 왼쪽
    arrDigital[16]    = true; // 홈 — 엔진 버튼 없음
    const uint32 mask = PlatformGamepadLayout::makeStandardButtonMask( arrDigital, PlatformGamepadLayout::kStandardButtonCount );
    SW_EXPECT_EQUAL( ( 1u << static_cast<uint32>( GamepadButton::A ) ) | ( 1u << static_cast<uint32>( GamepadButton::DPadLeft ) ), mask );
    SW_EXPECT_EQUAL( 0u, PlatformGamepadLayout::makeStandardButtonMask( nullptr, 4 ) );
}

/** @brief [PlatformGamepadLayoutTest] 플랫폼 세로축(아래 +)은 엔진 세로축(위 +)으로 뒤집히고, 해트 축은 문턱으로 두 방향을 나눈다 */
SW_TEST_CASE( PlatformGamepadLayoutTest, StickYFlipsAndHatSplits )
{
    SW_EXPECT_NEAR( 0.75f, PlatformGamepadLayout::toEngineStickY( -0.75f ), 1e-6f );
    bool bNegative = false;
    bool bPositive = false;
    PlatformGamepadLayout::splitHatAxis( -1.0f, bNegative, bPositive );
    SW_EXPECT_TRUE( bNegative );
    SW_EXPECT_FALSE( bPositive );
    PlatformGamepadLayout::splitHatAxis( 0.2f, bNegative, bPositive );
    SW_EXPECT_FALSE( bNegative );
    SW_EXPECT_FALSE( bPositive );
    PlatformGamepadLayout::splitHatAxis( 1.0f, bNegative, bPositive );
    SW_EXPECT_FALSE( bNegative );
    SW_EXPECT_TRUE( bPositive );
}
