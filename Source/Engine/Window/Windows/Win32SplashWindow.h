/**
 * @file Win32SplashWindow.h
 * @brief Microsoft Windows OS 전용(Win32 GDI 기반) ISplashWindow 구현체 헤더
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Window/ISplashWindow.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
#else
using HWND    = void*;
using UINT    = uint32;
using WPARAM  = uint64;
using LPARAM  = int64;
using LRESULT = int64;
    #if !defined( CALLBACK )
        #define CALLBACK
    #endif
#endif

namespace sw
{
    /**
     * @class Win32SplashWindow
     * @brief Windows Win32 API 기반의 경량 스플래시 창
     */
    class Win32SplashWindow : public ISplashWindow
    {
    public:
        Win32SplashWindow();
        virtual ~Win32SplashWindow() override;

        bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) override;
        void updateStatus( const utf8* pStatus, float32 progress = -1.0f ) override;
        void setProgress( float32 progress ) override;
        void dismiss() override;
#if defined( SW_PLATFORM_WINDOWS )
        /**
         * @brief `WM_PAINT` 한 번을 그립니다 — 창 프로시저가 `BeginPaint` · `EndPaint` 사이에서 부른다.
         * @details 늘린 배경 그림은 **처음 한 번** 메모리 DC 에 그려 두고 무효 영역만 복사한다. 예전에는 상태 줄이 바뀔 때마다
         *          창 전체를 HALFTONE `StretchDIBits` 로 다시 늘리고 글꼴을 새로 만들었다 — 시작 시간 게임 스레드의 13 % 가
         *          스플래시 다시 그리기였다(2026-09-23 프로파일).
         */
        void paintWindow( HDC hDC, const RECT& rcPaint );
#endif

    private:
        static LRESULT CALLBACK splashWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
#if defined( SW_PLATFORM_WINDOWS )
        /** @brief 배경 캐시(메모리 DC · 비트맵)와 상태 글꼴을 놓습니다. */
        void releasePaintCache();
#endif

    private:
        [[maybe_unused]] HWND   _hWnd;
        [[maybe_unused]] uint64 _gdiplusToken;
#if defined( SW_PLATFORM_WINDOWS )
        HDC     _hBackgroundDC;             ///< 창 크기로 늘려 둔 배경 그림을 든 메모리 DC
        HBITMAP _hBackgroundBitmap;         ///< `_hBackgroundDC` 에 골라 둔 비트맵
        HGDIOBJ _hBackgroundPreviousBitmap; ///< 골라 넣기 전의 비트맵 — 놓을 때 되돌린다
        HFONT   _hStatusFont;               ///< 상태 줄 글꼴 (처음 그릴 때 한 번 만든다)
        int32   _backgroundWidth;           ///< 캐시를 만든 창 너비
        int32   _backgroundHeight;          ///< 캐시를 만든 창 높이
#endif
    };
} // namespace sw
