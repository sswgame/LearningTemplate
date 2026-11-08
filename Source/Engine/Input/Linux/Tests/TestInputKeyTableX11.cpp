#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Map/InputKeyTable.h"

// InputKeyTableX11Test - X11 · Wayland 키 이벤트를 Key 로 읽는 규칙(nogpu). X11 헤더 없이 프로토콜 번호로 적는다.

using namespace sw;

namespace
{
    struct InputKeyTableX11TestUtil
    {
        static constexpr uint32 kEvdevOffset = 8; ///< X 키 코드 = evdev 번호 + 8
    };
} // namespace

/**
 * @brief [InputKeyTableX11Test] 숫자 패드의 0 번 칸(NumLock 꺼진 쪽 - XK_KP_Home …)도 숫자와 같은 패드 키다. 표에 없어 키 이벤트가 나가지 않았다
 */
SW_TEST_CASE( InputKeyTableX11Test, KeypadLevelZeroIsTheSamePadKey )
{
    SW_EXPECT_TRUE( InputKeyTable::mapX11KeySym( 0xFF95 ) == Key::Numpad7 );       // XK_KP_Home
    SW_EXPECT_TRUE( InputKeyTable::mapX11KeySym( 0xFFB7 ) == Key::Numpad7 );       // XK_KP_7
    SW_EXPECT_TRUE( InputKeyTable::mapX11KeySym( 0xFF9D ) == Key::Numpad5 );       // XK_KP_Begin
    SW_EXPECT_TRUE( InputKeyTable::mapX11KeySym( 0xFF9E ) == Key::Numpad0 );       // XK_KP_Insert
    SW_EXPECT_TRUE( InputKeyTable::mapX11KeySym( 0xFF9F ) == Key::NumpadDecimal ); // XK_KP_Delete
}

/**
 * @brief [InputKeyTableX11Test] 표에 있는 키심은 그 키이고, 없는 키심(비라틴 배열)은 키 코드의 물리 자리로 읽는다
 */
SW_TEST_CASE( InputKeyTableX11Test, UnknownKeySymReadsThePhysicalKey )
{
    using Util                     = InputKeyTableX11TestUtil;
    constexpr uint32 kEvdevA       = 30;
    constexpr uint32 kEvdevDigit1  = 2;
    constexpr uint64 kCyrillicEf   = 0x6C6;
    constexpr uint64 kAmpersand    = 0x26;
    constexpr uint64 kLatinQ       = 0x71;
    constexpr uint32 kUnmappedCode = 1 + Util::kEvdevOffset; // KEY_ESC - 키심이 정하는 키라 물리 표에 없다

    SW_EXPECT_TRUE( InputKeyTable::mapX11Key( kCyrillicEf, kEvdevA + Util::kEvdevOffset ) == Key::A );
    SW_EXPECT_TRUE( InputKeyTable::mapX11Key( kAmpersand, kEvdevDigit1 + Util::kEvdevOffset ) == Key::Digit1 );
    SW_EXPECT_TRUE( InputKeyTable::mapX11Key( kLatinQ, kEvdevA + Util::kEvdevOffset ) == Key::Q ); // AZERTY: 키심이 이긴다
    SW_EXPECT_TRUE( InputKeyTable::mapX11Key( kCyrillicEf, kUnmappedCode ) == Key::Unknown );
    SW_EXPECT_TRUE( InputKeyTable::mapX11Key( kCyrillicEf, 0 ) == Key::Unknown ); // 입력기가 만든 가짜 키 이벤트
}
