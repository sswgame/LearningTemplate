/**
 * @file PlatformInputEvent.h
 * @brief 콜백으로 입력을 받는 플랫폼 창(웹 · Android)이 입력 관리자에 넘기는 입력 하나입니다(`NativeWindowEvent::kMessagePlatformInput` 의 `_lParam`).
 * @details 창 층과 입력 층은 같은 층(3)이라 서로의 형식을 모른다 — Wayland 창의 `WaylandInputEvent` 와 같은 모양으로 창이 기록을 채우고 입력 관리자의 플랫폼 파일
 *          (`Input/Web` · `Input/Android`)이 엔진 키 · 버튼 · 손가락으로 옮긴다. 좌표는 창 클라이언트 물리 픽셀이다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 플랫폼 입력의 종류입니다. */
    enum class PlatformInputKind : uint8
    {
        KeyDown = 0, ///< `_keyCode` 가 눌렸다(웹 · Android 의 키 번호 — 입력 플랫폼 파일이 옮긴다)
        KeyUp,
        PointerMove, ///< 마우스가 (`_x`, `_y`)로 움직였다
        PointerDown, ///< 마우스 버튼 `_button` 이 눌렸다(0 왼쪽 · 1 가운데 · 2 오른쪽 — DOM `MouseEvent.button` 순서)
        PointerUp,
        Wheel,     ///< 바퀴가 `_wheel` 만큼(위가 +, 한 칸 1) 돌았다
        TouchDown, ///< 손가락 `_pointerIndex` 가 닿았다
        TouchMove,
        TouchUp,
        Text,                ///< 확정 글자(`_arrText`, UTF-8 — 웹 `keypress` · Android `AKEY_EVENT` 의 유니코드)
        PointerRelative,     ///< 포인터가 잠긴 동안의 이동량(`_relativeX` · `_relativeY`, 물리 픽셀) — 위치는 서 있다(웹 Pointer Lock 의 movementX/Y)
        GamepadKeyDown,      ///< 패드 칸 `_pointerIndex` 의 버튼 `_keyCode`(Android `AKEYCODE_BUTTON_*` · D패드 키)가 눌렸다
        GamepadKeyUp,        ///< 그 버튼을 뗐다
        GamepadAxis,         ///< 패드 칸 `_pointerIndex` 의 축 `_keyCode`(Android `AMOTION_EVENT_AXIS_*`)가 `_wheel` 값이다(플랫폼 부호 그대로)
        GamepadConnected,    ///< 패드 칸 `_pointerIndex` 에 패드가 붙었다
        GamepadDisconnected, ///< 그 칸의 패드가 떨어졌다
    };

    /** @brief 콜백 창이 패드에 주는 칸 수입니다 — 입력 관리자의 패드 슬롯 수(`kMaxGamepadSlot`)와 같아야 한다(PlatformInputManager 가 static_assert 로 본다). 창 층은 입력 층 형식을 모른다. */
    inline constexpr uint32 kPlatformGamepadSlotCount = 4;

    /** @brief 플랫폼 입력 하나입니다. 키 번호의 뜻은 플랫폼마다다(웹은 `_arrText` 에 DOM `KeyboardEvent.code`, Android 는 `AKEYCODE_*`). */
    struct PlatformInputEvent
    {
        PlatformInputKind _kind{ PlatformInputKind::PointerMove };
        uint8             _button{ 0 };
        uint8             _bRepeat{ 0 };
        uint16            _pointerIndex{ 0 }; ///< 손가락 번호, 패드 종류에서는 패드 칸(0 ~ `kPlatformGamepadSlotCount` - 1)
        int32             _x{ 0 };
        int32             _y{ 0 };
        int32             _keyCode{ 0 };
        float32           _wheel{ 0.0f };     ///< 바퀴 칸 수, 패드 축 종류에서는 축 값
        float32           _relativeX{ 0.0f }; ///< `PointerRelative` 의 가로 이동량(물리 픽셀)
        float32           _relativeY{ 0.0f }; ///< 세로 이동량(아래가 +)
        utf8              _arrText[32]{};     ///< 웹 키는 DOM code("KeyA" · "ArrowLeft"), `Text` 는 글자
    };
} // namespace sw
