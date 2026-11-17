// Win32SplashWindowTest — 스플래시 창은 기동 스레드가 막혀도 답한다(감사 P-13). Windows 클라이언트 빌드만(데스크톱 세션이 있어야 창을 만든다).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Window/Windows/Win32SplashWindow.h"

using namespace sw;

namespace
{
    /** @brief 이 프로세스의 스플래시 창을 찾습니다(창 종류 이름 — Win32SplashWindow.cpp 의 `kSplashClassName`). 없으면 nullptr 입니다. */
    struct Win32SplashWindowTestUtil
    {
        HWND _hFound{ nullptr };

        static BOOL CALLBACK visitWindow( HWND hWnd, LPARAM lParam )
        {
            DWORD processID = 0;
            (void)GetWindowThreadProcessId( hWnd, &processID );
            wchar_t arrClassName[64]{};
            if ( processID != GetCurrentProcessId() || GetClassNameW( hWnd, arrClassName, 64 ) == 0 || wcscmp( arrClassName, L"SWSplashWindowClass" ) != 0 )
                return TRUE;
            reinterpret_cast<Win32SplashWindowTestUtil*>( lParam )->_hFound = hWnd;
            return FALSE;
        }

        static HWND findSplashWindow()
        {
            Win32SplashWindowTestUtil util;
            (void)EnumWindows( &Win32SplashWindowTestUtil::visitWindow, reinterpret_cast<LPARAM>( &util ) );
            return util._hFound;
        }
    };
} // namespace

/**
 * @brief [Win32SplashWindowTest] 스플래시 창은 부른 스레드가 아닌 전용 스레드가 갖고, 부른 스레드가 메시지를 돌리지 않아도 메시지에 답한다.
 * @details 예전에는 기동 스레드가 창을 갖고 `updateStatus` 안에서만 메시지를 처리해 5 초를 넘는 단계(RHI 장치 생성)마다 "응답 없음" 이 떴다.
 *          부른 스레드의 창이면 `SendMessageTimeoutW` 가 프로시저를 바로 불러 늘 답하므로, 소유 스레드를 먼저 본다.
 */
SW_TEST_CASE( Win32SplashWindowTest, WindowAnswersWithoutTheCallerPumping )
{
    Win32SplashWindow splash;
    SW_REQUIRE_TRUE( splash.initialize( "SplashTest", "Testing", 160, 90 ) );
    const HWND hWnd = Win32SplashWindowTestUtil::findSplashWindow();
    SW_REQUIRE_TRUE( hWnd != nullptr );
    SW_EXPECT_TRUE( GetWindowThreadProcessId( hWnd, nullptr ) != GetCurrentThreadId() );

    splash.updateStatus( "Still testing", 0.5f );
    DWORD_PTR result = 0;
    SW_EXPECT_TRUE( SendMessageTimeoutW( hWnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &result ) != 0 );

    splash.dismiss();
    SW_EXPECT_FALSE( IsWindow( hWnd ) != FALSE );
    SW_EXPECT_FALSE( splash.isOpen() );
}
