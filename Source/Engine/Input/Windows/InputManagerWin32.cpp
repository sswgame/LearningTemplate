#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Engine/Input/Devices/KeyboardDevice.h"
	#include "Engine/Input/Devices/MouseDevice.h"
	#include "Engine/Input/Events/RawInputEvent.h"
	#include "Engine/Input/InputKeyMap.h"
	#include "Engine/Window/IWindow.h"
	#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
	void InputManager::onNativeWindowEvent( const NativeWindowEvent& event )
	{
		processNativeEvent( event );
	}

	void InputManager::processNativeEvent( const NativeWindowEvent& event )
	{
		switch ( event._message )
		{
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			{
				const Key key = InputKeyMap::mapWin32VirtualKey( event._wParam );
				if ( _pKeyboard != nullptr )
					_pKeyboard->setKeyDown( key, true );
				postRawEvent( RawInputEvent::makeKeyDown( key, static_cast<uint16>( event._wParam ) ) );
				break;
			}
			case WM_KEYUP:
			case WM_SYSKEYUP:
			{
				const Key key = InputKeyMap::mapWin32VirtualKey( event._wParam );
				if ( _pKeyboard != nullptr )
					_pKeyboard->setKeyDown( key, false );
				postRawEvent( RawInputEvent::makeKeyUp( key, static_cast<uint16>( event._wParam ) ) );
				break;
			}
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
				{
					if ( _pMouse != nullptr )
						_pMouse->setButtonDown( btn, true );
					postRawEvent( RawInputEvent::makeMouseButtonDown( btn ) );
				}
				break;
			}
			case WM_LBUTTONUP:
			case WM_RBUTTONUP:
			case WM_MBUTTONUP:
			case WM_XBUTTONUP:
			{
				const MouseButton btn = InputKeyMap::mapWin32MouseButton( event._message, event._wParam );
				if ( btn < MouseButton::Count )
				{
					if ( _pMouse != nullptr )
						_pMouse->setButtonDown( btn, false );
					postRawEvent( RawInputEvent::makeMouseButtonUp( btn ) );
				}
				break;
			}
			case WM_MOUSEMOVE:
			{
				const int32 mx = static_cast<int32>( static_cast<int16>( LOWORD( event._lParam ) ) );
				const int32 my = static_cast<int32>( static_cast<int16>( HIWORD( event._lParam ) ) );
				if ( _pMouse != nullptr )
					_pMouse->setPosition( mx, my );
				postRawEvent( RawInputEvent::makeMouseMove( mx, my ) );
				break;
			}
			case WM_MOUSEWHEEL:
			{
				const float32 delta = static_cast<float32>( GET_WHEEL_DELTA_WPARAM( event._wParam ) ) / 120.0f;
				postRawEvent( RawInputEvent::makeMouseWheel( delta ) );
				break;
			}
			case WM_CHAR:
			{
				if ( event._wParam > 0 && event._wParam < 0x10000 )
				{
					const utf16 wch = static_cast<utf16>( event._wParam );
					if ( wch >= 32 || wch == static_cast<utf16>( '\t' ) || wch == static_cast<utf16>( '\n' ) || wch == static_cast<utf16>( '\r' ) )
					{
						const utf8	 utf8Char = static_cast<utf8>( wch < 128 ? wch : '?' );
						const string strText( 1, utf8Char );
						onTextInput( strText );
						postRawEvent( RawInputEvent::makeTextInput( strText ) );
					}
				}
				break;
			}
			case WM_KILLFOCUS:
				onWindowFocusLost();
				break;
			case WM_ACTIVATE:
				if ( LOWORD( event._wParam ) == WA_INACTIVE )
					onWindowFocusLost();
				break;
			default:
				break;
		}
	}

	void InputManager::pollPlatform()
	{
		if ( _pKeyboard != nullptr )
		{
			uint32						  count{ 0 };
			const InputKeyMap::VkKeyPair* pTable = InputKeyMap::getWin32PollKeyTable( count );
			for ( uint32 eventIndex = 0; eventIndex < count; ++eventIndex )
			{
				_pKeyboard->setKeyDown( pTable[eventIndex]._key, ( GetAsyncKeyState( pTable[eventIndex]._vk ) & 0x8000 ) != 0 );
			}
		}

		if ( _pMouse != nullptr )
		{
			_pMouse->setButtonDown( MouseButton::Left, ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0 );
			_pMouse->setButtonDown( MouseButton::Right, ( GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) != 0 );
			_pMouse->setButtonDown( MouseButton::Middle, ( GetAsyncKeyState( VK_MBUTTON ) & 0x8000 ) != 0 );
			_pMouse->setButtonDown( MouseButton::X1, ( GetAsyncKeyState( VK_XBUTTON1 ) & 0x8000 ) != 0 );
			_pMouse->setButtonDown( MouseButton::X2, ( GetAsyncKeyState( VK_XBUTTON2 ) & 0x8000 ) != 0 );

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
				_pMouse->setPosition( static_cast<int32>( pt.x ), static_cast<int32>( pt.y ) );
			}
		}
	}
} // namespace sw

#endif
