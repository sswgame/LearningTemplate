// PlatformInputTest — 웹(DOM KeyboardEvent.code) · Android(AKEYCODE_*) 키 표를 데스크톱 단위 테스트(nogpu)로 본다.
// 예전에는 두 표가 `#if SW_PLATFORM_WEB / ANDROID` 안에 있어 배포 빌드(시나리오 실행기 없음)에서만 지어졌고, 표의 결함을 자동으로 볼 길이 없었다(감사 P-9).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Map/AndroidKeyCode.h"
#include "Engine/Input/Map/InputKeyTable.h"

using namespace sw;

namespace
{
    Key mapAndroid( int32 code )
    {
        return InputKeyTable::mapAndroidKeyCode( code );
    }

    GamepadButton mapAndroidGamepad( int32 code )
    {
        return InputKeyTable::mapAndroidGamepadKeyCode( code );
    }
} // namespace

/**
 * @brief [PlatformInputTest] DOM code 는 이름 규칙(KeyA → A, Digit1, F5, Numpad0, Enter)과 다른 이름 표(ArrowLeft → Left, ShiftRight → RightShift)로 옮긴다. 모르는 code 는 Unknown 이다.
 */
SW_TEST_CASE( PlatformInputTest, WebDomCodesMapToEngineKeys )
{
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "KeyA" ) == Key::A );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "KeyZ" ) == Key::Z );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "Digit1" ) == Key::Digit1 );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "F5" ) == Key::F5 );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "Numpad0" ) == Key::Numpad0 );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "Enter" ) == Key::Enter );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "ArrowLeft" ) == Key::Left );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "ShiftRight" ) == Key::RightShift );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "MetaLeft" ) == Key::LeftSuper );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "Backquote" ) == Key::Grave );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "Fn" ) == Key::Unknown );
    SW_EXPECT_TRUE( InputKeyTable::mapWebKeyCode( "" ) == Key::Unknown );
}

/**
 * @brief [PlatformInputTest] Android 키 번호는 범위(글자 · 숫자 · F · 숫자판)와 표로 옮기고, 뒤로 가기는 Escape 다. 엔진 키가 없는 키(볼륨)는 Unknown 이라 창이 시스템에 돌려준다.
 */
SW_TEST_CASE( PlatformInputTest, AndroidKeyCodesMapToEngineKeys )
{
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kA ) == Key::A );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kZ ) == Key::Z );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kDigit0 ) == Key::Digit0 );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kDigit9 ) == Key::Digit9 );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kF1 ) == Key::F1 );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kF12 ) == Key::F12 );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kNumpad9 ) == Key::Numpad9 );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kBack ) == Key::Escape );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kDel ) == Key::Backspace );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kForwardDel ) == Key::Delete );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kDPadLeft ) == Key::Left );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kCtrlRight ) == Key::RightControl );
    SW_EXPECT_TRUE( mapAndroid( AndroidKeyCode::kVolumeUp ) == Key::Unknown );
}

/**
 * @brief [PlatformInputTest] Android 패드 키는 엔진 패드 버튼으로 옮긴다. 엔진 버튼이 없는 키(L2 디지털 — 방아쇠는 축이 준다)는 Count 다.
 */
SW_TEST_CASE( PlatformInputTest, AndroidGamepadKeyCodesMapToButtons )
{
    SW_EXPECT_TRUE( mapAndroidGamepad( AndroidKeyCode::kButtonA ) == GamepadButton::A );
    SW_EXPECT_TRUE( mapAndroidGamepad( AndroidKeyCode::kButtonY ) == GamepadButton::Y );
    SW_EXPECT_TRUE( mapAndroidGamepad( AndroidKeyCode::kButtonR1 ) == GamepadButton::RightShoulder );
    SW_EXPECT_TRUE( mapAndroidGamepad( AndroidKeyCode::kButtonThumbL ) == GamepadButton::LeftThumb );
    SW_EXPECT_TRUE( mapAndroidGamepad( AndroidKeyCode::kButtonSelect ) == GamepadButton::Back );
    SW_EXPECT_TRUE( mapAndroidGamepad( AndroidKeyCode::kDPadDown ) == GamepadButton::DPadDown );
    SW_EXPECT_TRUE( mapAndroidGamepad( AndroidKeyCode::kButtonL2 ) == GamepadButton::Count );
}
