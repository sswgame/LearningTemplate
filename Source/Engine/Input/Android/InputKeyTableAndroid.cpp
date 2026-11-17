/**
 * @file InputKeyTableAndroid.cpp
 * @brief 손으로 옮긴 Android 키 번호(`AndroidKeyCode`)가 NDK 값(`AKEYCODE_*`)과 같은지 Android 빌드가 본다. 키 표 자체는 Map/InputKeyTable.cpp(모든 플랫폼, PlatformInputTest)에 있다.
 */
#include "pch.h"

#include "Engine/Input/Map/AndroidKeyCode.h"

#include <android/keycodes.h>

namespace sw
{
    static_assert( AndroidKeyCode::kBack == AKEYCODE_BACK, "AndroidKeyCode::kBack must equal AKEYCODE_BACK" );
    static_assert( AndroidKeyCode::kDigit0 == AKEYCODE_0, "AndroidKeyCode::kDigit0 must equal AKEYCODE_0" );
    static_assert( AndroidKeyCode::kDigit9 == AKEYCODE_9, "AndroidKeyCode::kDigit9 must equal AKEYCODE_9" );
    static_assert( AndroidKeyCode::kDPadUp == AKEYCODE_DPAD_UP, "AndroidKeyCode::kDPadUp must equal AKEYCODE_DPAD_UP" );
    static_assert( AndroidKeyCode::kDPadDown == AKEYCODE_DPAD_DOWN, "AndroidKeyCode::kDPadDown must equal AKEYCODE_DPAD_DOWN" );
    static_assert( AndroidKeyCode::kDPadLeft == AKEYCODE_DPAD_LEFT, "AndroidKeyCode::kDPadLeft must equal AKEYCODE_DPAD_LEFT" );
    static_assert( AndroidKeyCode::kDPadRight == AKEYCODE_DPAD_RIGHT, "AndroidKeyCode::kDPadRight must equal AKEYCODE_DPAD_RIGHT" );
    static_assert( AndroidKeyCode::kVolumeUp == AKEYCODE_VOLUME_UP, "AndroidKeyCode::kVolumeUp must equal AKEYCODE_VOLUME_UP" );
    static_assert( AndroidKeyCode::kA == AKEYCODE_A, "AndroidKeyCode::kA must equal AKEYCODE_A" );
    static_assert( AndroidKeyCode::kZ == AKEYCODE_Z, "AndroidKeyCode::kZ must equal AKEYCODE_Z" );
    static_assert( AndroidKeyCode::kComma == AKEYCODE_COMMA, "AndroidKeyCode::kComma must equal AKEYCODE_COMMA" );
    static_assert( AndroidKeyCode::kPeriod == AKEYCODE_PERIOD, "AndroidKeyCode::kPeriod must equal AKEYCODE_PERIOD" );
    static_assert( AndroidKeyCode::kAltLeft == AKEYCODE_ALT_LEFT, "AndroidKeyCode::kAltLeft must equal AKEYCODE_ALT_LEFT" );
    static_assert( AndroidKeyCode::kAltRight == AKEYCODE_ALT_RIGHT, "AndroidKeyCode::kAltRight must equal AKEYCODE_ALT_RIGHT" );
    static_assert( AndroidKeyCode::kShiftLeft == AKEYCODE_SHIFT_LEFT, "AndroidKeyCode::kShiftLeft must equal AKEYCODE_SHIFT_LEFT" );
    static_assert( AndroidKeyCode::kShiftRight == AKEYCODE_SHIFT_RIGHT, "AndroidKeyCode::kShiftRight must equal AKEYCODE_SHIFT_RIGHT" );
    static_assert( AndroidKeyCode::kTab == AKEYCODE_TAB, "AndroidKeyCode::kTab must equal AKEYCODE_TAB" );
    static_assert( AndroidKeyCode::kSpace == AKEYCODE_SPACE, "AndroidKeyCode::kSpace must equal AKEYCODE_SPACE" );
    static_assert( AndroidKeyCode::kEnter == AKEYCODE_ENTER, "AndroidKeyCode::kEnter must equal AKEYCODE_ENTER" );
    static_assert( AndroidKeyCode::kDel == AKEYCODE_DEL, "AndroidKeyCode::kDel must equal AKEYCODE_DEL" );
    static_assert( AndroidKeyCode::kMinus == AKEYCODE_MINUS, "AndroidKeyCode::kMinus must equal AKEYCODE_MINUS" );
    static_assert( AndroidKeyCode::kEquals == AKEYCODE_EQUALS, "AndroidKeyCode::kEquals must equal AKEYCODE_EQUALS" );
    static_assert( AndroidKeyCode::kSlash == AKEYCODE_SLASH, "AndroidKeyCode::kSlash must equal AKEYCODE_SLASH" );
    static_assert( AndroidKeyCode::kButtonA == AKEYCODE_BUTTON_A, "AndroidKeyCode::kButtonA must equal AKEYCODE_BUTTON_A" );
    static_assert( AndroidKeyCode::kButtonB == AKEYCODE_BUTTON_B, "AndroidKeyCode::kButtonB must equal AKEYCODE_BUTTON_B" );
    static_assert( AndroidKeyCode::kButtonX == AKEYCODE_BUTTON_X, "AndroidKeyCode::kButtonX must equal AKEYCODE_BUTTON_X" );
    static_assert( AndroidKeyCode::kButtonY == AKEYCODE_BUTTON_Y, "AndroidKeyCode::kButtonY must equal AKEYCODE_BUTTON_Y" );
    static_assert( AndroidKeyCode::kButtonL1 == AKEYCODE_BUTTON_L1, "AndroidKeyCode::kButtonL1 must equal AKEYCODE_BUTTON_L1" );
    static_assert( AndroidKeyCode::kButtonR1 == AKEYCODE_BUTTON_R1, "AndroidKeyCode::kButtonR1 must equal AKEYCODE_BUTTON_R1" );
    static_assert( AndroidKeyCode::kButtonL2 == AKEYCODE_BUTTON_L2, "AndroidKeyCode::kButtonL2 must equal AKEYCODE_BUTTON_L2" );
    static_assert( AndroidKeyCode::kButtonThumbL == AKEYCODE_BUTTON_THUMBL, "AndroidKeyCode::kButtonThumbL must equal AKEYCODE_BUTTON_THUMBL" );
    static_assert( AndroidKeyCode::kButtonThumbR == AKEYCODE_BUTTON_THUMBR, "AndroidKeyCode::kButtonThumbR must equal AKEYCODE_BUTTON_THUMBR" );
    static_assert( AndroidKeyCode::kButtonStart == AKEYCODE_BUTTON_START, "AndroidKeyCode::kButtonStart must equal AKEYCODE_BUTTON_START" );
    static_assert( AndroidKeyCode::kButtonSelect == AKEYCODE_BUTTON_SELECT, "AndroidKeyCode::kButtonSelect must equal AKEYCODE_BUTTON_SELECT" );
    static_assert( AndroidKeyCode::kEscape == AKEYCODE_ESCAPE, "AndroidKeyCode::kEscape must equal AKEYCODE_ESCAPE" );
    static_assert( AndroidKeyCode::kForwardDel == AKEYCODE_FORWARD_DEL, "AndroidKeyCode::kForwardDel must equal AKEYCODE_FORWARD_DEL" );
    static_assert( AndroidKeyCode::kCtrlLeft == AKEYCODE_CTRL_LEFT, "AndroidKeyCode::kCtrlLeft must equal AKEYCODE_CTRL_LEFT" );
    static_assert( AndroidKeyCode::kCtrlRight == AKEYCODE_CTRL_RIGHT, "AndroidKeyCode::kCtrlRight must equal AKEYCODE_CTRL_RIGHT" );
    static_assert( AndroidKeyCode::kF1 == AKEYCODE_F1, "AndroidKeyCode::kF1 must equal AKEYCODE_F1" );
    static_assert( AndroidKeyCode::kF12 == AKEYCODE_F12, "AndroidKeyCode::kF12 must equal AKEYCODE_F12" );
    static_assert( AndroidKeyCode::kNumpad0 == AKEYCODE_NUMPAD_0, "AndroidKeyCode::kNumpad0 must equal AKEYCODE_NUMPAD_0" );
    static_assert( AndroidKeyCode::kNumpad9 == AKEYCODE_NUMPAD_9, "AndroidKeyCode::kNumpad9 must equal AKEYCODE_NUMPAD_9" );
} // namespace sw
