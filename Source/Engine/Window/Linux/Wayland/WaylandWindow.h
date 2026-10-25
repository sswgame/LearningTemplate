/**
 * @file WaylandWindow.h
 * @brief Linux Wayland 네이티브 창입니다(xdg-shell 최상위 창 · wl_seat 키보드 · 포인터). `SW_ENABLE_WAYLAND` 로 지은 리눅스 클라이언트에서만 실제로 돈다.
 * @details 고르는 것은 `IWindow::createPlatformWindow` 다 — 환경 변수 `SW_WINDOW_BACKEND=wayland` 이고 `WAYLAND_DISPLAY` 가 있을 때만 이 창이고, 아니면 X11 창이다
 *          (XWayland 가 있는 데스크톱에서는 X11 이 지금까지의 길이라 기본을 바꾸지 않는다). 확인은 CI 리눅스 잡의 headless 컴포지터(weston)에서만 한다.
 *          라이브러리 헤더(wayland-client · xkbcommon · 생성된 xdg-shell)는 .cpp 에만 있다. 입력은 `WaylandInputEvent` 로 모아 `NativeWindowEvent::kMessageWayland` 로 넘긴다.
 *
 *          Wayland 에서 할 수 없는 것(클라이언트가 창 자리를 정하기 · 창 숨기기)은 하지 않는다 — 자리 함수는 아무것도 하지 않고, 숨기기는 오류를 남긴다.
 *          OpenGL 은 GLX 라 이 창에 붙지 않는다(EGL 길이 없다) — Vulkan 만 그린다(`VK_KHR_wayland_surface`).
 */
#pragma once
#include "Core/Memory/Memory.h"

#include "Engine/Window/IWindow.h"

namespace sw
{
    /** @class WaylandWindow @brief 파일 머리말 참고. */
    class WaylandWindow : public IWindow
    {
    public:
        WaylandWindow();
        ~WaylandWindow() override;

        /**
         * @brief @p pWindow 가 Wayland 창이면 `WaylandWindow*`, 아니면(nullptr · X11 · Win32) nullptr 입니다.
         * @details 엔진은 RTTI 를 끄므로 종류(`getSurfaceKind`) 확인과 캐스트를 이 함수 한 곳에 둔다(`X11Window::tryCast` 와 같은 모양) — 다른 창 시스템의 창을 `WaylandWindow*` 로 읽는 `static_cast` 를 바깥에 두지 않는다.
         */
        static WaylandWindow* tryCast( IWindow* pWindow ) { return pWindow != nullptr && pWindow->getSurfaceKind() == NativeSurfaceKind::Wayland ? static_cast<WaylandWindow*>( pWindow ) : nullptr; }

        /** @brief 컴포지터에 붙어 최상위 창을 만듭니다. 컴포지터가 없거나 xdg-shell · 합성기가 없으면 오류를 남기고 false 입니다. */
        bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        void destroy() override;
        /** @brief 쌓인 Wayland 이벤트를 처리합니다(막지 않는다). 종료 요청이면 false 입니다. */
        bool processMessages() override;
        /** @brief 핸들은 wl_surface*, 디스플레이는 wl_display* 입니다. */
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::Wayland; }

        /** @brief 보이기는 첫 그림이 붙을 때 컴포지터가 한다 — 할 일이 없다. 숨기기는 Wayland 에 없어 오류를 남긴다. */
        void applyWindowVisibility( bool bShow ) override;
        /** @brief 컴포지터가 창을 한 번 배치(configure)했으면 true 입니다. */
        bool isVisible() const override;
        /** @brief 화면 배율입니다. 지금은 1 입니다(wl_output 배율 · fractional-scale 을 읽지 않는다). */
        float32 getContentScale() const override { return 1.0f; }
        /** @brief 전체 화면은 `xdg_toplevel_set_fullscreen`, 창 모드는 크기를 정하고 크기 통보를 보냅니다. */
        bool setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height ) override;

        void* getNativeHandle() const override;
        void* getNativeDisplay() const override;

        /** @brief 포인터가 이 창 위에 있을 때 커서를 숨기거나 기본 커서로 되돌립니다(wl_pointer_set_cursor · 커서 테마). */
        void setCursorVisible( bool bVisible );

    protected:
        /** @brief Wayland 클라이언트는 창 자리를 모른다 — 할 일이 없다. */
        void captureRestorePosition() override {}
        /** @brief 위와 같다. */
        void clearRestorePosition() override;
        /** @brief `xdg_toplevel_set_min_size` 입니다. */
        void applyMinimumClientSize() override;
        /** @brief `xdg_toplevel_set_title` 입니다. */
        void applyTitle() override;

    public:
        /** @brief 라이브러리 객체 · 리스너가 쌓은 입력입니다(.cpp 에만 정의가 있다). 리스너 묶음(.cpp 의 익명 구조체)이 이름을 쓰도록 공개 선언만 둔다. */
        struct State;

    private:
        unique_ptr<State> _pState; ///< 라이브러리 객체 · 리스너가 쌓은 입력(.cpp 에만 있다)
    };
} // namespace sw
