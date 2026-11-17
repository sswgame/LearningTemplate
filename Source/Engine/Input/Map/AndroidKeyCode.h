/**
 * @file AndroidKeyCode.h
 * @brief 엔진이 옮기는 Android 키 번호(`AKEYCODE_*`, NDK `android/keycodes.h`)입니다. 키 표(`InputKeyTable`)가 모든 플랫폼에서 짓도록 NDK 헤더 없이 값을 둔다.
 * @details 값은 Android 의 공개 ABI 라 바뀌지 않는다(https://developer.android.com/ndk/reference/group/input). 그래도 손으로 옮긴 값이라 Android 빌드가 NDK 값과
 *          하나씩 같은지 `static_assert` 로 본다(Input/Android/InputKeyTableAndroid.cpp) — 데스크톱 단위 테스트(PlatformInputTest)가 표를 보고, Android 빌드가 값을 본다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief Android 키 번호입니다. 표에 쓰는 것만 둔다(전체 목록은 NDK). 플랫폼 이벤트의 키 번호(`AKeyEvent_getKeyCode` — int32)와 그대로 견준다. */
    struct AndroidKeyCode
    {
        static constexpr int32 kBack         = 4;
        static constexpr int32 kDigit0       = 7;
        static constexpr int32 kDigit9       = 16;
        static constexpr int32 kDPadUp       = 19;
        static constexpr int32 kDPadDown     = 20;
        static constexpr int32 kDPadLeft     = 21;
        static constexpr int32 kDPadRight    = 22;
        static constexpr int32 kVolumeUp     = 24;
        static constexpr int32 kA            = 29;
        static constexpr int32 kZ            = 54;
        static constexpr int32 kComma        = 55;
        static constexpr int32 kPeriod       = 56;
        static constexpr int32 kAltLeft      = 57;
        static constexpr int32 kAltRight     = 58;
        static constexpr int32 kShiftLeft    = 59;
        static constexpr int32 kShiftRight   = 60;
        static constexpr int32 kTab          = 61;
        static constexpr int32 kSpace        = 62;
        static constexpr int32 kEnter        = 66;
        static constexpr int32 kDel          = 67; ///< 백스페이스(앞 글자 지우기)
        static constexpr int32 kMinus        = 69;
        static constexpr int32 kEquals       = 70;
        static constexpr int32 kSlash        = 76;
        static constexpr int32 kButtonA      = 96;
        static constexpr int32 kButtonB      = 97;
        static constexpr int32 kButtonX      = 99;
        static constexpr int32 kButtonY      = 100;
        static constexpr int32 kButtonL1     = 102;
        static constexpr int32 kButtonR1     = 103;
        static constexpr int32 kButtonL2     = 104;
        static constexpr int32 kButtonThumbL = 106;
        static constexpr int32 kButtonThumbR = 107;
        static constexpr int32 kButtonStart  = 108;
        static constexpr int32 kButtonSelect = 109;
        static constexpr int32 kEscape       = 111;
        static constexpr int32 kForwardDel   = 112; ///< Delete(뒤 글자 지우기)
        static constexpr int32 kCtrlLeft     = 113;
        static constexpr int32 kCtrlRight    = 114;
        static constexpr int32 kF1           = 131;
        static constexpr int32 kF12          = 142;
        static constexpr int32 kNumpad0      = 144;
        static constexpr int32 kNumpad9      = 153;
    };
} // namespace sw
