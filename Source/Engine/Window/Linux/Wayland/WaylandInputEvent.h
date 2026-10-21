/**
 * @file WaylandInputEvent.h
 * @brief Wayland 창(`WaylandWindow`)이 입력 하나를 엔진에 넘기는 값입니다 — `NativeWindowEvent::kMessageWayland` 의 `_lParam` 이 가리킵니다.
 * @details X11 은 `XEvent` 를 그대로 넘기지만 Wayland 는 장치마다 콜백이라 넘길 한 덩어리가 없다. 창이 콜백을 이 값 하나로 모아 넘기고, 입력 관리자
 *          (`InputManagerX11.cpp` 의 Wayland 갈래)가 읽는다. 라이브러리 타입이 없어 Wayland 헤더 없이도 읽힌다.
 *          키는 xkbcommon 의 keysym 이다 — X11 KeySym 과 같은 번호라 키 표(`InputKeyTable::mapX11KeySym`)를 함께 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief Wayland 입력의 종류입니다. */
    enum class WaylandInputKind : uint8
    {
        KeyDown = 0,
        KeyUp,
        Text,        ///< `_arrText` 에 커밋된 UTF-8(키 하나가 낸 글자)
        PointerMove, ///< `_x` · `_y` 는 표면 픽셀
        PointerDown, ///< `_button` 은 리눅스 이벤트 버튼 번호(BTN_LEFT 0x110 …)
        PointerUp,
        PointerWheel, ///< `_wheel` 은 세로 칸(+ 위), `_horizontalWheel` 은 가로 칸
        PointerEnter,
        PointerLeave,
        FocusIn,
        FocusOut,
    };

    /** @brief 입력 하나입니다. */
    struct WaylandInputEvent
    {
        static constexpr uint32 kTextCapacity = 32;

        WaylandInputKind _kind{ WaylandInputKind::KeyDown };
        uint32           _keySym{ 0 }; ///< xkb keysym(= X11 KeySym)
        uint32           _button{ 0 }; ///< 리눅스 이벤트 버튼 번호
        int32            _x{ 0 };
        int32            _y{ 0 };
        float32          _wheel{ 0.0f };
        float32          _horizontalWheel{ 0.0f };
        utf8             _arrText[kTextCapacity]{};
    };
} // namespace sw
