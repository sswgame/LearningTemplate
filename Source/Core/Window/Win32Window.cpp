/**
 * @file Win32Window.cpp
 * @brief Win32 윈도우 구현
 */
#include "Win32Window.h"

namespace sw
{
	Win32Window::Win32Window() = default;

	Win32Window::~Win32Window()
	{
		destroy();
	}

#if defined( SW_PLATFORM_WINDOWS )
	// ============================================================================
	// @function wndProc
	// @brief Windows OS로부터 수신된 네이티브 메시지를 처리하는 정적 콜백 함수
	// ============================================================================
	LRESULT CALLBACK Win32Window::wndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
	{
		Win32Window* pThis = nullptr;
		if ( msg == WM_NCCREATE )
		{
			CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>( lParam );
			pThis				  = reinterpret_cast<Win32Window*>( pCreate->lpCreateParams );
			SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pThis ) );
			pThis->_hWnd = hWnd;
		}
		else
		{
			pThis = reinterpret_cast<Win32Window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );
		}

		if ( pThis != nullptr )
		{
			if ( pThis->_customHandler.isBound() == true )
			{
				NativeWindowEvent ev{};
				ev.nativeWindow = hWnd;
				ev.message		= static_cast<uint32>( msg );
				ev.wParam		= static_cast<uintptr_t>( wParam );
				ev.lParam		= static_cast<intptr_t>( lParam );
				if ( pThis->_customHandler( ev ) == true )
					return true;
			}

			switch ( msg )
			{
				case WM_SIZE:
					pThis->_width  = LOWORD( lParam );
					pThis->_height = HIWORD( lParam );
					if ( pThis->_onResize.isBound() == true )
						pThis->_onResize( pThis->_width, pThis->_height );

					return 0;

				case WM_CLOSE:
					pThis->_bShouldClose = true;
					DestroyWindow( hWnd );
					return 0;

				case WM_DESTROY:
					pThis->_bShouldClose = true;
					return 0;

				default:
					break;
			}
		}

		return DefWindowProc( hWnd, msg, wParam, lParam );
	}

	// ============================================================================
	// @function create
	// @brief Windows API(CreateWindowEx)를 사용해 실제 데스크톱 윈도우 창을 만듭니다.
	// ============================================================================
	bool Win32Window::create( const utf16* title, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;

		HINSTANCE hInstance = GetModuleHandle( nullptr );

		WNDCLASSEXW wc{};
		wc.cbSize		 = sizeof( WNDCLASSEXW );
		wc.style		 = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc	 = wndProc;
		wc.hInstance	 = hInstance;
		wc.hCursor		 = LoadCursor( nullptr, IDC_ARROW );
		wc.lpszClassName = L"ToyEngineWindowClass";

		RegisterClassExW( &wc );

		RECT rc = { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) };
		AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );

		_hWnd = CreateWindowExW(
			0,
			L"ToyEngineWindowClass",
			title,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			rc.right - rc.left,
			rc.bottom - rc.top,
			nullptr,
			nullptr,
			hInstance,
			this );

		if ( _hWnd == nullptr )
			return false;

		ShowWindow( _hWnd, SW_SHOWDEFAULT );
		UpdateWindow( _hWnd );

		SW_LOG_INFO( "[Win32Window] Native Win32 Window created successfully! (%#x%#)", width, height );
		return true;
	}

	void Win32Window::destroy()
	{
		if ( _hWnd != nullptr )
		{
			DestroyWindow( _hWnd );
			_hWnd = nullptr;
		}
	}

	bool Win32Window::processMessages()
	{
		MSG msg{};
		while ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) != 0 )
		{
			if ( msg.message == WM_QUIT )
				return false;

			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		return !_bShouldClose;
	}
#else
	LRESULT CALLBACK Win32Window::wndProc( HWND, UINT, WPARAM, LPARAM )
	{
		return 0;
	}

	bool Win32Window::create( const utf16*, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;
		_hWnd	= nullptr;
		return true;
	}

	void Win32Window::destroy()
	{
		_hWnd = nullptr;
	}

	bool Win32Window::processMessages()
	{
		return !_bShouldClose;
	}
#endif
} // namespace sw
