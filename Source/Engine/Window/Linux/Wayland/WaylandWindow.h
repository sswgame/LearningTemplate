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
#include "Core/Container/string.h"
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
        [[nodiscard]] bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;
        void               destroy() override;
        /** @brief 쌓인 Wayland 이벤트를 처리합니다(막지 않는다). 종료 요청이면 false 입니다. */
        bool processMessages() override;
        /** @brief 보낼 요청을 내보내고 컴포지터 연결 소켓을 `poll` 로 기다립니다. */
        void waitForMessages( uint32 timeoutMilliseconds ) override;
        /** @brief 핸들은 wl_surface*, 디스플레이는 wl_display* 입니다. */
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::Wayland; }

        /** @brief 보이기는 첫 그림이 붙을 때 컴포지터가 한다 — 할 일이 없다. 숨기기는 Wayland 에 없어 오류를 남긴다. */
        void applyWindowVisibility( bool bShow ) override;
        /** @brief 컴포지터가 창을 한 번 배치(configure)했으면 true 입니다. */
        bool isVisible() const override;
        /**
         * @brief 화면 배율입니다. 컴포지터가 소수 배율(`wp_fractional_scale_v1.preferred_scale`, 1/120 단위)을 알렸으면 그 값이고,
         *        아니면 창이 걸친 `wl_output` 들의 정수 배율(`wl_output.scale`) 중 가장 큰 값이며, 걸친 출력도 없으면 1 입니다.
         * @details 알려 주기만 한다 — 버퍼 배율은 바꾸지 않아 컴포지터가 확대한다. 소수 배율 프로토콜이 없는 컴포지터는 정상이며(오래된 컴포지터) 정수 배율이 그 길이다.
         */
        float32 getContentScale() const override;
        /** @brief 컴포지터가 `wp_fractional_scale_manager_v1` 을 내놓았으면 true 입니다(없으면 `getContentScale` 은 정수 배율이다). */
        [[nodiscard]] bool supportsFractionalScale() const;
        /** @brief 컴포지터에 `zwp_pointer_constraints_v1` 과 `zwp_relative_pointer_manager_v1` 이 둘 다 있고 포인터 장치가 있으면 true 입니다. */
        [[nodiscard]] bool supportsPointerLock() const override;
        /**
         * @brief 포인터를 창에 묶거나(Confined: `zwp_confined_pointer_v1`) 제자리에 세우거나(Locked: `zwp_locked_pointer_v1` + `zwp_relative_pointer_v1`) 풉니다(Released).
         *        묶임은 컴포지터가 창에 포인터 포커스가 있을 때만 켠다(영속 수명이라 포커스가 돌아오면 다시 켜진다).
         * @return 요청대로 걸었으면(Released 는 항상) true, 컴포지터에 확장이 없거나 포인터 장치가 없어 못 걸었으면 false 입니다(Confined 는 `zwp_pointer_constraints_v1` 만, Locked 는 `supportsPointerLock` 이 필요하다). 같은 방식을 다시 부르면 아무것도 하지 않는다.
         */
        [[nodiscard]] bool setPointerLock( WindowPointerLock lock ) override;
        /** @brief 컴포지터에 `wp_cursor_shape_manager_v1` 이 있고 포인터 장치가 있으면 true 입니다. 없으면 `setCursorShape` 는 바꾸지 못한다(오래된 컴포지터 — 호출 쪽이 알린다). */
        [[nodiscard]] bool supportsCursorShape() const override;
        /** @brief 커서 모양을 `wp_cursor_shape_device_v1.set_shape` 로 바꿉니다. 포인터가 아직 창에 들어오지 않았거나 커서를 숨긴 동안은 기억만 하고 들어올 때 · 보일 때 적용한다. 확장이 없으면 false 입니다. */
        [[nodiscard]] bool setCursorShape( CursorShape shape ) override;
        /** @brief 클립보드(`wl_data_device` 선택)의 글을 @p outText 에 담습니다. 선택이 없거나 글 형식이 없거나 200 ms 안에 답이 없으면 false 입니다. */
        [[nodiscard]] bool readClipboardText( string& outText ) override;
        /** @brief 컴포지터에 데이터 장치(`wl_data_device_manager`)와 seat 가 있으면 true 입니다. 없으면(seat 없는 headless 컴포지터) UI 는 엔진 안 클립보드를 쓴다. */
        [[nodiscard]] bool supportsClipboard() const override;
        /** @brief @p text 를 클립보드 선택으로 내놓습니다. 글은 창이 들고 다른 프로그램이 청할 때(`wl_data_source.send`) 보냅니다. 입력 일련번호가 아직 없거나 데이터 장치가 없으면 false 입니다. */
        [[nodiscard]] bool writeClipboardText( string_view text ) override;
        /** @brief 전체 화면은 `xdg_toplevel_set_fullscreen`, 창 모드는 크기를 정하고 크기 통보를 보냅니다. */
        bool setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height ) override;

        /** @brief 포인터가 이 창 위에 있을 때 커서를 숨기거나 되돌립니다(wl_pointer_set_cursor; 보일 때는 커서 모양 확장이 있으면 기억해 둔 모양, 없으면 커서 테마의 `left_ptr`). 커서 테마가 없으면 false 입니다. */
        [[nodiscard]] bool setCursorVisible( bool bVisible ) override;

    protected:
        // 핸들 읽기는 기반의 보호 구역이다 — 바깥은 `getSurfaceHandle` · `getSurfaceDisplay` 를 종류(`getSurfaceKind`)와 함께 읽는다(여기서 다시 public 으로 열지 않는다).
        void* getNativeHandle() const override;
        void* getNativeDisplay() const override;

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
