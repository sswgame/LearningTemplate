/**
 * @file Win32Window.h
 * @brief Microsoft Windows OS 전용(Win32 API 기반) IWindow 구현체 헤더
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Window/IWindow.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Engine/Common/EnginePlatformHeaders.h"
#else
using HWND	  = void*;
using UINT	  = uint32;
using WPARAM  = uint64;
using LPARAM  = int64;
using LRESULT = int64;
	#define CALLBACK
#endif

namespace sw
{
	/**
	 * @class Win32Window
	 * @brief Win32 HWND 창과 메시지 펌프
	 */
	class Win32Window : public IWindow
	{
	public:
		/** @brief HWND 없이 시작합니다. */
		Win32Window();
		/** @brief HWND를 파괴합니다. */
		virtual ~Win32Window() override;

		/** @brief Win32 창을 생성하고 화면에 표시합니다. */
		bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) override;

		/** @brief 생성된 윈도우(HWND)를 파괴합니다. */
		void destroy() override;

		/** @brief 기존 창의 크기와 위치를 유지한 채 핸들을 재생성합니다 (컨텍스트 핫스왑용). */
		bool recreate() override;

		/** @brief Windows 메시지 큐(PeekMessage)를 처리합니다. */
		bool processMessages() override;

		/** @brief 윈도우를 화면에 표시하거나 숨깁니다. */
		void showWindow( bool bShow ) override;
		/** @brief 윈도우 표시 여부를 반환합니다. */
		bool isVisible() const override;

		/** @brief 네이티브 윈도우 핸들(HWND)을 반환합니다. */
		void* getNativeHandle() const override { return _hWnd; }

		/** @brief Win32 전용 HWND 핸들을 반환합니다. */
		HWND getHWND() const { return _hWnd; }

	private:
		static LRESULT CALLBACK wndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

	private:
		HWND					_hWnd;
		[[maybe_unused]] int32	_restoreX;
		[[maybe_unused]] int32	_restoreY;
		[[maybe_unused]] uint8	_bRecreating   : 1;
		[[maybe_unused]] uint8	_reservedWin32 : 7;
		[[maybe_unused]] uint16 _padding;
	};
} // namespace sw
