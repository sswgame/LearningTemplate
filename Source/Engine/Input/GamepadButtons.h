/**
 * @file GamepadButtons.h
 * @brief 디지털 게임패드 버튼 열거형과 이름 변환입니다(플랫폼 래퍼와 분리).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief ActionMap 이 쓰는 디지털 게임패드 버튼입니다. */
    enum class GamepadButton : uint8
    {
        A = 0,
        B,
        X,
        Y,
        DPadUp,
        DPadDown,
        DPadLeft,
        DPadRight,
        Start,
        Back,
        LeftShoulder,
        RightShoulder,
        LeftThumb,
        RightThumb,
        Count
    };

    /** @brief GamepadButton 이름 변환입니다. */
    struct SW_API GamepadButtons
    {
        /** @brief 대소문자를 무시하고 열거형 이름을 GamepadButton 으로 바꿉니다. 모르는 이름이면 Count 입니다. */
        static GamepadButton fromName( string_view name );
        /** @brief GamepadButton 을 안정적인 열거형 이름으로 바꿉니다. Count 나 범위 밖이면 nullptr 입니다. */
        static const utf8* toName( GamepadButton button );
    };
} // namespace sw
