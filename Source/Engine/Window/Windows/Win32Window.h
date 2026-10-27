/**
 * @file Win32Window.h
 * @brief Windows 전용(Win32 API 기반) IWindow 구현입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Window/IWindow.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
#else
using HWND    = void*;
using UINT    = uint32;
using WPARAM  = uint64;
using LPARAM  = int64;
using LRESULT = int64;
    #define CALLBACK
#endif

namespace sw::constant::window
{
    /** @brief 실행 파일 리소스의 앱 아이콘 id 입니다(`Source/App/Resources/App.rc`). 리소스가 없는 실행 파일(테스트)은 Windows 기본 아이콘입니다. */
    inline constexpr uint16 kAppIconResourceID = 1;
} // namespace sw::constant::window

namespace sw
{
    /**
     * @class Win32Window
     * @brief Win32 HWND 창과 메시지 펌프입니다.
     */
    class Win32Window : public IWindow
    {
    public:
        /** @brief HWND 없이 시작합니다. */
        Win32Window();
        /** @brief HWND를 파괴합니다. */
        virtual ~Win32Window() override;

        /** @brief Win32 창을 만듭니다. 화면에 띄우는 것은 showWindow() 입니다. */
        [[nodiscard]] bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;

        /** @brief 만든 창(HWND)을 파괴합니다. */
        void destroy() override;

        /** @brief Windows 메시지 큐(PeekMessage)를 처리합니다. */
        bool processMessages() override;
        /** @brief `MsgWaitForMultipleObjects` 로 메시지가 올 때까지 잡니다. */
        void waitForMessages( uint32 timeoutMilliseconds ) override;
        /** @brief 핸들은 HWND 입니다. */
        NativeSurfaceKind getSurfaceKind() const override { return NativeSurfaceKind::Win32; }

        /** @brief 창을 화면에 띄우거나 숨깁니다. */
        void applyWindowVisibility( bool bShow ) override;
        /** @brief 창이 지금 보이는지 반환합니다. */
        bool isVisible() const override;

        /** @brief 창 스타일(WS_OVERLAPPEDWINDOW, WS_POPUP)과 위치, 크기를 바꿉니다. 크기 통보는 이 호출 안의 WM_SIZE 로 옵니다. */
        bool setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height ) override;

        /** @brief `GetDpiForWindow` / 96 입니다(프로세스가 PerMonitorV2 이며 `WindowsProcess.manifest` 에서 정합니다). 창이 없으면 1 입니다. */
        float32 getContentScale() const override;

    protected:
        /** @brief 네이티브 창 핸들(HWND)을 반환합니다. 바깥은 `IWindow::tryGetWin32Handle` 로만 읽는다(기반의 보호 구역을 다시 열지 않는다). */
        void* getNativeHandle() const override { return _hWnd; }

        /** @brief `GetWindowRect` 로 지금 위치를 담습니다. */
        void captureRestorePosition() override;
        /** @brief 복원 위치를 `CW_USEDEFAULT` 로 되돌립니다. */
        void clearRestorePosition() override;
        /** @brief `SetWindowTextW` 로 제목 줄을 바꿉니다. */
        void applyTitle() override;

    private:
        static LRESULT CALLBACK wndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
        /** @brief `WM_SIZE` 를 처리합니다. 최소화(0x0)는 창 크기로 기억하지 않고, 재진입한 메시지는 크기만 적고 콜백을 건너뜁니다. */
        void onSizeMessage( WPARAM wParam, LPARAM lParam );
        /**
         * @brief `WM_SIZE` · `WM_ACTIVATEAPP` 로 최소화 · 포커스 상태를 적습니다.
         * @details 외부 처리기(ImGui)보다 먼저 부른다 — 처리기가 메시지를 먹어도 상태는 놓치지 않는다.
         */
        void trackActivityMessage( UINT msg, WPARAM wParam );
        /** @brief `WM_GETMINMAXINFO` 의 최소 추적 크기를 `setMinimumClientSize` 로 채웁니다. 창 모드가 아니거나 하한이 없으면 false 입니다(기본 처리로 간다). */
        bool fillMinimumTrackSize( LPARAM lParam ) const;

    private:
        HWND                    _hWnd;
        [[maybe_unused]] uint8  _bResizing     : 1;
        [[maybe_unused]] uint8  _reservedWin32 : 6;
        [[maybe_unused]] uint16 _padding;
    };
} // namespace sw
