/**
 * @file NativeWindowEvent.h
 * @brief 플랫폼 메시지 페이로드입니다(Win32, X11 등이 같은 필드를 채웁니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/IRenderSurface.h"

namespace sw
{
    /// @brief OS 창 이벤트(리사이즈, 닫기, 포커스 등)입니다.
    struct NativeWindowEvent
    {
        /** @brief X11: processMessages 가 XEvent* 를 lParam 에 실을 때 쓰는 메시지 코드입니다. */
        static constexpr uint32 kMessageX11 = 0x8001;
        /** @brief Wayland: processMessages 가 `WaylandInputEvent*` 를 lParam 에 실을 때 쓰는 메시지 코드입니다(`Window/Linux/Wayland/WaylandInputEvent.h`). */
        static constexpr uint32 kMessageWayland = 0x8002;

        /** @brief 첫 번째 파라미터입니다(Win32: WPARAM). */
        uintptr_t _wParam{ 0 };
        /** @brief 두 번째 파라미터입니다(Win32: LPARAM). */
        intptr_t _lParam{ 0 };
        /** @brief 플랫폼 메시지 코드입니다(Win32: UINT msg, 다른 OS 는 비슷한 목적에 맞게 매핑합니다). */
        uint32 _message{ 0 };

        /** @brief 이벤트를 낸 창의 핸들과 그 창 시스템을 함께 싣습니다(Win32 `HWND` · X11 창 ID · Wayland `wl_surface*`). */
        void setNativeWindow( NativeSurfaceKind kind, void* pWindow )
        {
            _windowKind    = kind;
            _pNativeWindow = pWindow;
        }

        /**
         * @brief Win32 창이 낸 이벤트면 `HWND` 를 `void*` 로 반환하고, 아니면(X11 · Wayland) 또는 창이 실리지 않았으면 nullptr 입니다. 호출처가 `static_cast<HWND>` 로 읽는다.
         * @details 창 핸들은 창 시스템마다 뜻이 다른 `void*` 라서 종류 확인 없이 `HWND` 로 읽으면 다른 창 시스템의 값을 Win32 호출에 넘긴다.
         *          그래서 핸들은 비공개로 두고 종류 확인과 값 확인을 이 함수 한 곳에 모은다 — `IWindow::tryGetWin32Handle` 의 이벤트판이다.
         *          반환값을 버리면 안 되고, nullptr 이면 호출처가 건너뛴다(다른 핸들로 대신하지 않는다).
         */
        [[nodiscard]] void* tryGetWin32Window() const { return _windowKind == NativeSurfaceKind::Win32 ? _pNativeWindow : nullptr; }

        /** @brief 마우스 이동 · 버튼 · 휠 입력인지 반환합니다. */
        SW_API bool isMouseInput() const;
        /** @brief 키보드 입력인지 반환합니다. */
        SW_API bool isKeyboardInput() const;
        /** @brief 마우스 · 키를 뗀 입력인지 반환합니다(캡처 중에도 게임으로 통과시켜 눌림 고착을 막습니다). */
        SW_API bool isInputRelease() const;

    private:
        /** @brief 이벤트를 낸 창의 핸들입니다. 뜻은 `_windowKind` 가 정한다. 읽기는 `tryGetWin32Window` 로만 한다. */
        void* _pNativeWindow{ nullptr };
        /** @brief `_pNativeWindow` 의 창 시스템입니다. */
        NativeSurfaceKind _windowKind{ NativeSurfaceKind::Win32 };
    };
} // namespace sw
