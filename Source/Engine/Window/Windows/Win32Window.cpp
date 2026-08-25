#include "pch.h"

#include "Engine/Window/Windows/Win32Window.h"

#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
	Win32Window::Win32Window()
		: _hWnd{ nullptr }
		, _bRecreating{ false }
		, _restoreX{ CW_USEDEFAULT }
		, _restoreY{ CW_USEDEFAULT }
	{
	}

	Win32Window::~Win32Window()
	{
		destroy();
	}

	/**
	 * @brief Win32 윈도우 클래스를 등록하고 오버랩 윈도우(WS_OVERLAPPEDWINDOW)를 생성합니다.
	 */
	bool Win32Window::initializeWindow( const utf8* pTitle, uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;
		_title	= StringUtil::isNullOrEmpty( pTitle ) ? L"" : StringUtil::utf8ToUtf16( pTitle );

		HINSTANCE hInstance = GetModuleHandle( nullptr );

		// CS_OWNDC: DXGI↔OpenGL 핫스왑 시 WGL GetDC/SwapBuffers의 안정성을 보장하기 위해 필수
		WNDCLASSEXW wc{};
		wc.cbSize		 = sizeof( WNDCLASSEXW );
		wc.style		 = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc	 = wndProc;
		wc.hInstance	 = hInstance;
		wc.hCursor		 = LoadCursor( nullptr, IDC_ARROW );
		wc.lpszClassName = L"SWEngineWindowClass_OWNDC";

		RegisterClassExW( &wc );

		RECT rc = { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) };
		AdjustWindowRect( &rc, WS_OVERLAPPEDWINDOW, FALSE );

		_hWnd = CreateWindowExW(
			0,
			L"SWEngineWindowClass_OWNDC",
			_title.c_str(),
			WS_OVERLAPPEDWINDOW,
			_restoreX,
			_restoreY,
			rc.right - rc.left,
			rc.bottom - rc.top,
			nullptr,
			nullptr,
			hInstance,
			this );

		if ( _hWnd == nullptr )
			return false;

		ShowWindow( _hWnd, SW_SHOWNA );
		UpdateWindow( _hWnd );

		SW_LOG_INFO( "[Win32Window] Native Win32 Window created successfully! (%#x%#)", width, height );
		return true;
	}

	/**
	 * @brief Win32 윈도우 핸들을 파괴하고 리소스를 정리합니다.
	 */
	void Win32Window::destroy()
	{
		if ( _hWnd != nullptr )
		{
			DestroyWindow( _hWnd );
			_hWnd = nullptr;
		}
	}

	/**
	 * @brief RHI 백엔드 핫스왑 등을 위해 이전 윈도우 좌표를 유지한 채 윈도우를 다시 생성합니다.
	 */
	bool Win32Window::recreate()
	{
		if ( _title.empty() )
			return false;

		if ( _hWnd != nullptr )
		{
			RECT windowRect{};
			if ( GetWindowRect( _hWnd, &windowRect ) )
			{
				_restoreX = windowRect.left;
				_restoreY = windowRect.top;
			}
		}

		const uint32 width	= _width;
		const uint32 height = _height;
		_bRecreating		= true;
		destroy();
		_bRecreating	   = false;
		_bShouldClose	   = false;
		const string title = StringUtil::utf16ToUtf8( _title.c_str() );

		const bool ok = initializeWindow( title.c_str(), width, height );
		_restoreX	  = CW_USEDEFAULT;
		_restoreY	  = CW_USEDEFAULT;
		return ok;
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
		return _bShouldClose == false;
	}

	LRESULT CALLBACK Win32Window::wndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
	{
		Win32Window* pThis{ nullptr };
		if ( msg == WM_NCCREATE )
		{
			CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>( lParam );
			pThis				  = reinterpret_cast<Win32Window*>( pCreate->lpCreateParams );
			SetWindowLongPtr( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pThis ) );
			pThis->_hWnd = hWnd;
		}
		else
			pThis = reinterpret_cast<Win32Window*>( GetWindowLongPtr( hWnd, GWLP_USERDATA ) );

		if ( pThis != nullptr )
		{
			if ( pThis->_customHandler.isBound() )
			{
				NativeWindowEvent event{};
				event._pNativeWindow = hWnd;
				event._message		 = msg;
				event._wParam		 = wParam;
				event._lParam		 = lParam;
				if ( pThis->_customHandler( event ) )
					return true;
			}

			switch ( msg )
			{
				case WM_SIZE:
					pThis->_width  = LOWORD( lParam );
					pThis->_height = HIWORD( lParam );
					if ( pThis->_onResize.isBound() )
						pThis->_onResize( pThis->_width, pThis->_height );
					return 0;

				case WM_CLOSE:
					if ( pThis->_bRecreating == false )
						pThis->_bShouldClose = true;
					DestroyWindow( hWnd );
					return 0;

				case WM_DESTROY:
					if ( pThis->_bRecreating == false )
						pThis->_bShouldClose = true;
					return 0;

				default:
					break;
			}
		}

		return DefWindowProc( hWnd, msg, wParam, lParam );
	}
} // namespace sw
#else
namespace sw
{
	Win32Window::Win32Window()
		: _hWnd{ nullptr }
		, _bRecreating{ false }
		, _restoreX{ 0 }
		, _restoreY{ 0 }
	{
	}

	Win32Window::~Win32Window() = default;

	bool Win32Window::create( const utf8*, uint32 width, uint32 height )
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

	bool Win32Window::recreate()
	{
		return false;
	}

	bool Win32Window::processMessages()
	{
		return _bShouldClose == false;
	}

	LRESULT CALLBACK Win32Window::wndProc( HWND, UINT, WPARAM, LPARAM )
	{
		return 0;
	}
} // namespace sw
#endif
