/**
 * @file InputGlyphStyle.h
 * @brief 지금 활성인 입력 장치 종류(UI 글리프 자동 전환)입니다. 입력 매니저를 쥐지 않는 쪽(에디터 View · 글리프 표)이 이 열거만 쓰도록 떼어 둔 잎 헤더입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 지금 활성인 입력 장치 타입입니다(UI 글리프 자동 전환용). */
    enum class InputGlyphStyle : uint8
    {
        KeyboardMouse = 0,
        GamepadXbox,
        GamepadPlayStation,
        GamepadSwitch
    };
} // namespace sw
