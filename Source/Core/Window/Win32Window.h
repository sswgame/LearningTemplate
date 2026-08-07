#pragma once
/**
 * @file Win32Window.h
 * @brief Microsoft Windows OS 전용(Win32 API 기반) IWindow 구현체 헤더
 */
#include "IWindow.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Common/PlatformHeaders.h"
#else
using HWND	  = void*;
using UINT	  = uint32;
using WPARAM  = uintptr_t;
using LPARAM  = intptr_t;
using LRESULT = intptr_t;
	#define CALLBACK
#endif

namespace sw
{
	/**
	 * @class Win32Window
	 * @brief Windows 플랫폼에서 네이티브 창(HWND)을 생성하고 메시지를 처리하는 클래스입니다.
	 */
	class Win32Window : public IWindow
	{
	public:
		Win32Window();
		~Win32Window() override;

		bool create( const utf16* title, uint32 width, uint32 height ) override;
		void destroy() override;
		bool processMessages() override;

		void* getNativeHandle() const override { return _hWnd; }
		HWND  getHWND() const { return _hWnd; }

	private:
		static LRESULT CALLBACK wndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

		HWND _hWnd = nullptr;
	};
}
