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
using HWND	  = void*;
using UINT	  = uint32;
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
		void updateStatus( const utf8* pStatus ) override;
		void dismiss() override;

	private:
		static LRESULT CALLBACK splashWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

	private:
		[[maybe_unused]] HWND _hWnd;
	};
} // namespace sw
