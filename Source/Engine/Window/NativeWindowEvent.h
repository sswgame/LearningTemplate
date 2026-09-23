/**
 * @file NativeWindowEvent.h
 * @brief 플랫폼 메시지 페이로드입니다(Win32 · X11 등이 같은 칸을 채웁니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /// @brief OS 창 이벤트(리사이즈, 닫기, 포커스 등)입니다.
    struct NativeWindowEvent
    {
        /** @brief X11: processMessages 가 XEvent* 를 lParam 에 실을 때 쓰는 메시지 코드입니다. */
        static constexpr uint32 kMessageX11 = 0x8001;

        /** @brief 네이티브 창 핸들입니다(Win32: HWND, macOS: NSWindow, X11: Window). */
        void* _pNativeWindow{ nullptr };
        /** @brief 첫 번째 파라미터입니다(Win32: WPARAM). */
        uintptr_t _wParam{ 0 };
        /** @brief 두 번째 파라미터입니다(Win32: LPARAM). */
        intptr_t _lParam{ 0 };
        /** @brief 플랫폼 메시지 코드입니다(Win32: UINT msg, 다른 OS 는 비슷한 목적에 맞게 매핑합니다). */
        uint32 _message{ 0 };

        /** @brief 마우스 이동 · 버튼 · 휠 입력인지 반환합니다. */
        SW_API bool isMouseInput() const;
        /** @brief 키보드 입력인지 반환합니다. */
        SW_API bool isKeyboardInput() const;
        /** @brief 마우스 · 키를 뗀 입력인지 반환합니다(캡처 중에도 게임으로 통과시켜 눌림 고착을 막습니다). */
        SW_API bool isInputRelease() const;
    };
} // namespace sw
