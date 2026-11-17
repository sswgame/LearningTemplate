/**
 * @file Win32SplashWindow.h
 * @brief Windows 전용(Win32 GDI 기반) ISplashWindow 구현입니다.
 */
#pragma once
#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/ThreadSafety.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Window/ISplashWindow.h"

#include <condition_variable>

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Foundation/EnginePlatformHeaders.h"
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
     * @brief Win32 API 기반의 가벼운 스플래시 창입니다. 창과 메시지 루프는 전용 스레드가 가진다.
     * @details 예전에는 기동 스레드가 창을 갖고 `updateStatus` 안에서만 메시지를 처리했다 — 단계 하나(RHI 장치 생성 · 셰이더 컴파일)가 5 초를 넘으면
     *          Windows 가 창을 "응답 없음" 으로 덮었다(감사 P-13). 언리얼 `FWindowsSplash` 처럼 스플래시 스레드가 `GetMessageW` 로 늘 답하고,
     *          기동 스레드는 상태만 적고 상태 띠를 무효로 한다(다른 스레드의 창에도 `InvalidateRect` 는 된다 — 그리기는 창 스레드의 `WM_PAINT`).
     */
    class Win32SplashWindow : public ISplashWindow
    {
    public:
        Win32SplashWindow();
        virtual ~Win32SplashWindow() override;

        [[nodiscard]] bool initialize( const utf8* pTitle, const utf8* pInitialStatus, uint32 width, uint32 height ) override;
        void               updateStatus( const utf8* pStatus, float32 progress = -1.0f ) override;
        void               setProgress( float32 progress ) override;
        void               dismiss() override;
#if defined( SW_PLATFORM_WINDOWS )
        /**
         * @brief `WM_PAINT` 한 번을 그립니다. 창 프로시저가 `BeginPaint` 와 `EndPaint` 사이에서 부릅니다(스플래시 스레드).
         * @details 늘린 배경 이미지는 **처음 한 번** 메모리 DC 에 그려 두고 무효 영역만 복사합니다. 상태 줄이 바뀔 때마다 창 전체를
         *          HALFTONE `StretchDIBits` 로 다시 늘리고 폰트를 새로 만들면 시작 시간 게임 스레드의 13 % 가 그것이 됩니다.
         */
        void paintWindow( HDC hDC, const RECT& rcPaint );
        /** @brief 배경 캐시(메모리 DC, 비트맵)와 상태 폰트를 해제합니다. 창이 부서질 때 스플래시 스레드가 부른다. */
        void releasePaintCache();
#endif

    private:
        static LRESULT CALLBACK splashWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
        /** @brief 스플래시 스레드 본문입니다 — 창을 만들고 준비를 알린 뒤 `WM_QUIT` 까지 메시지를 처리한다. */
        void runMessageLoop( wstring title );
        /** @brief 스플래시 스레드가 그릴 상태 · 진행률을 적습니다(기동 스레드). */
        void storePaintState();

    private:
        [[maybe_unused]] HWND   _hWnd; ///< 스플래시 스레드가 만든 창(준비를 알린 뒤에는 바뀌지 않는다 — 기동 스레드는 `InvalidateRect` · `PostMessageW` 만 한다)
        [[maybe_unused]] uint64 _gdiplusToken;
#if defined( SW_PLATFORM_WINDOWS )
        HDC     _hBackgroundDC;             ///< 창 크기로 늘려 둔 배경 이미지를 가진 메모리 DC 입니다(스플래시 스레드만).
        HBITMAP _hBackgroundBitmap;         ///< `_hBackgroundDC` 에 선택해 둔 비트맵입니다.
        HGDIOBJ _hBackgroundPreviousBitmap; ///< 선택하기 전의 비트맵입니다. 해제할 때 되돌립니다.
        HFONT   _hStatusFont;               ///< 상태 줄 폰트입니다(처음 그릴 때 한 번 만듭니다).
        int32   _backgroundWidth;           ///< 캐시를 만든 창 너비입니다.
        int32   _backgroundHeight;          ///< 캐시를 만든 창 높이입니다.
#endif
        mutex                       _paintMutex;
        std::condition_variable_any _readyCondition;                       ///< 스플래시 스레드가 창을 만들었거나 실패했다
        string _paintStatus         SW_GUARDED_BY( _paintMutex );          ///< 그릴 상태 글(기동 스레드가 적고 스플래시 스레드가 읽는다)
        float32 _paintProgress      SW_GUARDED_BY( _paintMutex ){ 0.0f };  ///< 그릴 진행률
        bool _bWindowReady          SW_GUARDED_BY( _paintMutex ){ false }; ///< NOLINT(sw-bool-flag): 조건 변수가 잠금 아래 읽는 값이다 — 비트필드는 잠금 계약 주석을 달 수 없다
        bool _bWindowFailed         SW_GUARDED_BY( _paintMutex ){ false }; ///< NOLINT(sw-bool-flag): 위와 같다
        thread                      _thread;                               ///< 창과 메시지 루프의 스레드. 마지막에 둔다 — 다른 칸이 다 선 뒤에 시작하고, 먼저 합류한다
    };
} // namespace sw
