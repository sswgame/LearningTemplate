#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Engine/Input/InputKeyMap.h"
	#include "Engine/Window/IWindow.h"
	#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
	void InputManager::processNativeEvent( const NativeWindowEvent& event )
	{
		switch ( event._message )
		{
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				setKeyDown( InputKeyMap::mapWin32VirtualKey( event._wParam ), true );
				break;
			case WM_KEYUP:
			case WM_SYSKEYUP:
				setKeyDown( InputKeyMap::mapWin32VirtualKey( event._wParam ), false );
				break;
			case WM_LBUTTONDOWN:
			case WM_RBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_XBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDBLCLK:
			case WM_MBUTTONDBLCLK:
			case WM_XBUTTONDBLCLK:
			{
				const MouseButton btn = InputKeyMap::mapWin32MouseButton( event._message, event._wParam );
				if ( btn < MouseButton::Count )
					setMouseButtonDown( btn, true );
				break;
			}
			case WM_LBUTTONUP:
			case WM_RBUTTONUP:
			case WM_MBUTTONUP:
			case WM_XBUTTONUP:
			{
				const MouseButton btn = InputKeyMap::mapWin32MouseButton( event._message, event._wParam );
				if ( btn < MouseButton::Count )
					setMouseButtonDown( btn, false );
				break;
			}
			case WM_MOUSEMOVE:
				_mouseX = static_cast<int32>( static_cast<int16>( LOWORD( event._lParam ) ) );
				_mouseY = static_cast<int32>( static_cast<int16>( HIWORD( event._lParam ) ) );
				break;
			case WM_MOUSEWHEEL:
				_mouseWheelAccum += static_cast<float32>( GET_WHEEL_DELTA_WPARAM( event._wParam ) ) / 120.0f;
				break;
			case WM_KILLFOCUS:
				Memory::set( _arrKeys, 0, sizeof( _arrKeys ) );
				Memory::set( _arrMouseButtons, 0, sizeof( _arrMouseButtons ) );
				break;
			case WM_ACTIVATE:
				if ( LOWORD( event._wParam ) == WA_INACTIVE )
				{
					Memory::set( _arrKeys, 0, sizeof( _arrKeys ) );
					Memory::set( _arrMouseButtons, 0, sizeof( _arrMouseButtons ) );
				}
				break;
			default:
				break;
		}
	}

	void InputManager::pollPlatform()
	{
		uint32						  count{ 0 };
		const InputKeyMap::VkKeyPair* pTable = InputKeyMap::getWin32PollKeyTable( count );
		for ( uint32 eventIndex = 0; eventIndex < count; ++eventIndex )
		{
			setKeyDown( pTable[eventIndex]._key, ( GetAsyncKeyState( pTable[eventIndex]._vk ) & 0x8000 ) != 0 );
		}

		setMouseButtonDown( MouseButton::Left, ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0 );
		setMouseButtonDown( MouseButton::Right, ( GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) != 0 );
		setMouseButtonDown( MouseButton::Middle, ( GetAsyncKeyState( VK_MBUTTON ) & 0x8000 ) != 0 );
		setMouseButtonDown( MouseButton::X1, ( GetAsyncKeyState( VK_XBUTTON1 ) & 0x8000 ) != 0 );
		setMouseButtonDown( MouseButton::X2, ( GetAsyncKeyState( VK_XBUTTON2 ) & 0x8000 ) != 0 );

		POINT pt{};
		if ( GetCursorPos( &pt ) )
		{
			IWindow* pWindow = IWindow::getActiveWindow();
			if ( pWindow != nullptr )
			{
				HWND pHwnd = static_cast<HWND>( pWindow->getNativeHandle() );
				if ( pHwnd != nullptr )
					ScreenToClient( pHwnd, &pt );
			}
			_mouseX = static_cast<int32>( pt.x );
			_mouseY = static_cast<int32>( pt.y );
		}
	}
} // namespace sw

#endif
