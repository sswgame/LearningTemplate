/**
 * @file PlatformGamepadLayout.h
 * @brief 콜백 입력 플랫폼(웹 · Android)의 패드 배치를 엔진 버튼 · 축으로 옮기는 순수 표입니다. 장치 없이 돌아 단위 테스트가 본다.
 * @details 웹은 W3C Gamepad 의 "standard" 배치(https://w3c.github.io/gamepad/#remapping — 버튼 17 · 축 4, 세로축은 아래가 +),
 *          Android 는 `AMOTION_EVENT_AXIS_HAT_X/Y`(D패드를 축으로 주는 패드 — -1 · 0 · 1)를 쓴다. 엔진 스틱은 XInput 과 같이 위가 + 다(`XInputGamepadDevice`).
 *          언리얼 · 유니티도 브라우저 패드는 standard 배치만 고정 표로 옮긴다(기억) — 배치가 다른 패드는 연결하지 않는다(`WebGamepadDevice`).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Input/GamepadButtonUtil.h"

namespace sw
{
    /** @brief 플랫폼 패드 배치 표입니다. */
    struct SW_API PlatformGamepadLayout
    {
        static constexpr uint32  kStandardButtonCount  = 17;   ///< W3C standard 배치의 버튼 수(16 은 가운데 홈 — 엔진 버튼이 없다)
        static constexpr uint32  kStandardAxisCount    = 4;    ///< 왼 스틱 X · Y, 오른 스틱 X · Y
        static constexpr uint32  kStandardLeftTrigger  = 6;    ///< 아날로그 방아쇠(버튼 값 0 ~ 1 — 축 4 로 넣는다)
        static constexpr uint32  kStandardRightTrigger = 7;    ///< 축 5
        static constexpr float32 kHatThreshold         = 0.5f; ///< 해트 축이 이 값을 넘으면 그 방향 D패드를 누른 것이다

        /** @brief standard 배치의 버튼 번호 @p index 의 엔진 버튼입니다. 엔진 버튼이 없는 번호(방아쇠 6 · 7, 홈 16, 범위 밖)는 `GamepadButton::Count` 입니다. */
        static GamepadButton mapStandardButton( uint32 index );
        /** @brief standard 배치의 눌림 @p pDigital (@p count 개)를 엔진 버튼 비트(`1 << GamepadButton`)로 모읍니다. */
        static uint32 makeStandardButtonMask( const bool* pDigital, uint32 count );
        /** @brief 플랫폼 세로축(아래가 + — 웹 standard · Android AXIS_Y/RZ)을 엔진 세로축(위가 +)으로 바꿉니다. */
        static float32 toEngineStickY( float32 platformY ) { return -platformY; }
        /** @brief 해트 축 값 @p value 가 음의 방향(@p outNegative — 왼쪽 · 위)인지 양의 방향(@p outPositive — 오른쪽 · 아래)인지 나눕니다. 가운데면 둘 다 거짓입니다. */
        static void splitHatAxis( float32 value, bool& outNegative, bool& outPositive );
    };
} // namespace sw
