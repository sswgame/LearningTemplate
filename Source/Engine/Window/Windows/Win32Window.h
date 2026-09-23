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
        bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;

        /** @brief 만든 창(HWND)을 파괴합니다. */
        void destroy() override;

        /** @brief Windows 메시지 큐(PeekMessage)를 처리합니다. */
        bool processMessages() override;

        /** @brief 창을 화면에 띄우거나 숨깁니다. */
        void applyWindowVisibility( bool bShow ) override;
        /** @brief 창이 지금 보이는지 반환합니다. */
        bool isVisible() const override;

        /** @brief 네이티브 창 핸들(HWND)을 반환합니다. */
        void* getNativeHandle() const override { return _hWnd; }

        /** @brief Win32 전용 HWND 핸들을 반환합니다. */
        HWND getHwnd() const { return _hWnd; }

    protected:
        /** @brief `GetWindowRect` 로 지금 위치를 담습니다. */
        void captureRestorePosition() override;
        /** @brief 복원 위치를 `CW_USEDEFAULT` 로 되돌립니다. */
        void clearRestorePosition() override;

    private:
        static LRESULT CALLBACK wndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

    private:
        HWND                    _hWnd;
        [[maybe_unused]] uint8  _bResizing     : 1;
        [[maybe_unused]] uint8  _reservedWin32 : 6;
        [[maybe_unused]] uint16 _padding;
    };
} // namespace sw
