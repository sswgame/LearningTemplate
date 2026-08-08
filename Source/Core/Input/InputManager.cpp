/**
 * @file InputManager.cpp
 * @brief Cross-platform keyboard / mouse state
 */
#include "InputManager.h"
#include "Core/Input/GamepadXInput.h"
#include "Core/Window/NativeWindowEvent.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Window/IWindow.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Core/Common/PlatformHeaders.h"
#elif defined( SW_PLATFORM_LINUX )
	#include "Core/Common/PlatformHeaders.h"
	#include <X11/keysym.h>
#endif

namespace sw
{
	InputManager::InputManager()
		: _bInitialized{ 0 }
		, _bPollGamepad{ 0 }
		, _reservedFlags{ 0 }
	{
	}

	InputManager::~InputManager() = default;

	bool InputManager::initialize()
	{
		std::memset( _keys, 0, sizeof( _keys ) );
		std::memset( _prevKeys, 0, sizeof( _prevKeys ) );
		std::memset( _mouseButtons, 0, sizeof( _mouseButtons ) );
		std::memset( _prevMouseButtons, 0, sizeof( _prevMouseButtons ) );
		_mouseX		  = 0;
		_mouseY		  = 0;
		_gamepad	  = std::make_unique<GamepadXInput>();
		_bPollGamepad = 1;
		_bInitialized = 1;
		SW_LOG_INFO( "[InputManager] Initialized." );
		return true;
	}

	void InputManager::shutdown()
	{
		_gamepad.reset();
		_bPollGamepad = 0;
		_bInitialized = 0;
		SW_LOG_INFO( "[InputManager] Shut down." );
	}

	void InputManager::setGamepadPollingEnabled( bool enabled )
	{
		_bPollGamepad = enabled ? 1 : 0;
		if ( enabled && _gamepad == nullptr )
			_gamepad = std::make_unique<GamepadXInput>();
	}

	void InputManager::beginFrame()
	{
		if ( _bInitialized == 0 )
			return;

		std::memcpy( _prevKeys, _keys, sizeof( _keys ) );
		std::memcpy( _prevMouseButtons, _mouseButtons, sizeof( _mouseButtons ) );
		pollPlatform();
		if ( _bPollGamepad != 0 && _gamepad != nullptr )
			_gamepad->poll( 0 );
	}

	void InputManager::endFrame()
	{
		// Edge state is derived from prev/current; nothing to clear.
	}

	void InputManager::setKeyDown( Key key, bool bDown )
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return;
		_keys[static_cast<size_t>( key )] = bDown;
	}

	void InputManager::setMouseButtonDown( MouseButton button, bool bDown )
	{
		if ( button >= MouseButton::Count )
			return;
		_mouseButtons[static_cast<size_t>( button )] = bDown;
	}

	bool InputManager::isKeyDown( Key key ) const
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return false;
		return _keys[static_cast<size_t>( key )];
	}

	bool InputManager::wasKeyPressed( Key key ) const
	{
		if ( key == Key::Unknown || key >= Key::Count )
			return false;
		const size_t i = static_cast<size_t>( key );
		return _keys[i] && ( _prevKeys[i] == false );
	}

	void InputManager::getMousePosition( int32& outX, int32& outY ) const
	{
		outX = _mouseX;
		outY = _mouseY;
	}

	bool InputManager::isMouseButtonDown( MouseButton button ) const
	{
		if ( button >= MouseButton::Count )
			return false;
		return _mouseButtons[static_cast<size_t>( button )];
	}

#if defined( SW_PLATFORM_WINDOWS )
	Key InputManager::mapWin32VirtualKey( uintptr_t vk )
	{
		switch ( vk )
		{
			case 'W':
			case 'w':
				return Key::W;
			case 'A':
			case 'a':
				return Key::A;
			case 'S':
			case 's':
				return Key::S;
			case 'D':
			case 'd':
				return Key::D;
			case 'C':
			case 'c':
				return Key::C;
			case 'E':
			case 'e':
				return Key::E;
			case 'Z':
			case 'z':
				return Key::Z;
			case '1':
				return Key::Digit1;
			case '2':
				return Key::Digit2;
			case VK_SPACE:
				return Key::Space;
			case VK_ESCAPE:
				return Key::Escape;
			case VK_RETURN:
				return Key::Enter;
			case VK_LEFT:
				return Key::Left;
			case VK_RIGHT:
				return Key::Right;
			case VK_UP:
				return Key::Up;
			case VK_DOWN:
				return Key::Down;
			case VK_F5:
				return Key::F5;
			case VK_F9:
				return Key::F9;
			default:
				return Key::Unknown;
		}
	}

	MouseButton InputManager::mapWin32MouseButton( uint32 message, uintptr_t wParam )
	{
		switch ( message )
		{
			case WM_LBUTTONDOWN:
			case WM_LBUTTONUP:
				return MouseButton::Left;
			case WM_RBUTTONDOWN:
			case WM_RBUTTONUP:
				return MouseButton::Right;
			case WM_MBUTTONDOWN:
			case WM_MBUTTONUP:
				return MouseButton::Middle;
			case WM_XBUTTONDOWN:
			case WM_XBUTTONUP:
				return ( GET_XBUTTON_WPARAM( wParam ) == XBUTTON1 ) ? MouseButton::Left : MouseButton::Right;
			default:
				return MouseButton::Count;
		}
	}

	void InputManager::pollPlatform()
	{
		static const struct
		{
			int vk;
			Key key;
		} kMap[] = {
			{ 'W', Key::W },
			{ 'A', Key::A },
			{ 'S', Key::S },
			{ 'D', Key::D },
			{ 'C', Key::C },
			{ 'E', Key::E },
			{ 'Z', Key::Z },
			{ '1', Key::Digit1 },
			{ '2', Key::Digit2 },
			{ VK_SPACE, Key::Space },
			{ VK_ESCAPE, Key::Escape },
			{ VK_RETURN, Key::Enter },
			{ VK_LEFT, Key::Left },
			{ VK_RIGHT, Key::Right },
			{ VK_UP, Key::Up },
			{ VK_DOWN, Key::Down },
			{ VK_F5, Key::F5 },
			{ VK_F9, Key::F9 },
		};

		for ( const auto& entry : kMap )
			setKeyDown( entry.key, ( GetAsyncKeyState( entry.vk ) & 0x8000 ) != 0 );

		setMouseButtonDown( MouseButton::Left, ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0 );
		setMouseButtonDown( MouseButton::Right, ( GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) != 0 );
		setMouseButtonDown( MouseButton::Middle, ( GetAsyncKeyState( VK_MBUTTON ) & 0x8000 ) != 0 );

		POINT pt{};
		if ( GetCursorPos( &pt ) )
		{
			if ( IWindow* window = IWindow::getActiveWindow() )
			{
				HWND hwnd = static_cast<HWND>( window->getNativeHandle() );
				if ( hwnd != nullptr )
					ScreenToClient( hwnd, &pt );
			}
			_mouseX = static_cast<int32>( pt.x );
			_mouseY = static_cast<int32>( pt.y );
		}
	}

	void InputManager::processNativeEvent( const NativeWindowEvent& event )
	{
		switch ( event.message )
		{
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				setKeyDown( mapWin32VirtualKey( event.wParam ), true );
				break;
			case WM_KEYUP:
			case WM_SYSKEYUP:
				setKeyDown( mapWin32VirtualKey( event.wParam ), false );
				break;
			case WM_LBUTTONDOWN:
			case WM_RBUTTONDOWN:
			case WM_MBUTTONDOWN:
			{
				const MouseButton btn = mapWin32MouseButton( event.message, event.wParam );
				if ( btn < MouseButton::Count )
					setMouseButtonDown( btn, true );
				break;
			}
			case WM_LBUTTONUP:
			case WM_RBUTTONUP:
			case WM_MBUTTONUP:
			{
				const MouseButton btn = mapWin32MouseButton( event.message, event.wParam );
				if ( btn < MouseButton::Count )
					setMouseButtonDown( btn, false );
				break;
			}
			case WM_MOUSEMOVE:
				_mouseX = static_cast<int32>( static_cast<int16>( LOWORD( event.lParam ) ) );
				_mouseY = static_cast<int32>( static_cast<int16>( HIWORD( event.lParam ) ) );
				break;
			default:
				break;
		}
	}

	Key InputManager::mapX11KeySym( uint64 )
	{
		return Key::Unknown;
	}

#elif defined( SW_PLATFORM_LINUX )
	Key InputManager::mapWin32VirtualKey( uintptr_t )
	{
		return Key::Unknown;
	}

	MouseButton InputManager::mapWin32MouseButton( uint32, uintptr_t )
	{
		return MouseButton::Count;
	}

	Key InputManager::mapX11KeySym( uint64 keySym )
	{
		switch ( keySym )
		{
			case XK_w:
			case XK_W:
				return Key::W;
			case XK_a:
			case XK_A:
				return Key::A;
			case XK_s:
			case XK_S:
				return Key::S;
			case XK_d:
			case XK_D:
				return Key::D;
			case XK_c:
			case XK_C:
				return Key::C;
			case XK_e:
			case XK_E:
				return Key::E;
			case XK_z:
			case XK_Z:
				return Key::Z;
			case XK_1:
			case XK_KP_1:
				return Key::Digit1;
			case XK_2:
			case XK_KP_2:
				return Key::Digit2;
			case XK_space:
				return Key::Space;
			case XK_Escape:
				return Key::Escape;
			case XK_Return:
			case XK_KP_Enter:
				return Key::Enter;
			case XK_Left:
				return Key::Left;
			case XK_Right:
				return Key::Right;
			case XK_Up:
				return Key::Up;
			case XK_Down:
				return Key::Down;
			case XK_F5:
				return Key::F5;
			case XK_F9:
				return Key::F9;
			default:
				return Key::Unknown;
		}
	}

	void InputManager::pollPlatform()
	{
		// X11 path is primarily event-driven via processNativeEvent.
	}

	void InputManager::processNativeEvent( const NativeWindowEvent& event )
	{
		if ( event.message != NativeWindowEvent::kMessageX11 || event.lParam == 0 )
			return;

		const XEvent* xev = reinterpret_cast<const XEvent*>( event.lParam );
		switch ( xev->type )
		{
			case KeyPress:
			case KeyRelease:
			{
				KeySym keySym = XLookupKeysym( const_cast<XKeyEvent*>( &xev->xkey ), 0 );
				setKeyDown( mapX11KeySym( static_cast<uint64>( keySym ) ), xev->type == KeyPress );
				break;
			}
			case ButtonPress:
			case ButtonRelease:
			{
				const bool bDown = ( xev->type == ButtonPress );
				switch ( xev->xbutton.button )
				{
					case Button1:
						setMouseButtonDown( MouseButton::Left, bDown );
						break;
					case Button2:
						setMouseButtonDown( MouseButton::Middle, bDown );
						break;
					case Button3:
						setMouseButtonDown( MouseButton::Right, bDown );
						break;
					default:
						break;
				}
				_mouseX = static_cast<int32>( xev->xbutton.x );
				_mouseY = static_cast<int32>( xev->xbutton.y );
				break;
			}
			case MotionNotify:
				_mouseX = static_cast<int32>( xev->xmotion.x );
				_mouseY = static_cast<int32>( xev->xmotion.y );
				break;
			default:
				break;
		}
	}

#else
	Key InputManager::mapWin32VirtualKey( uintptr_t )
	{
		return Key::Unknown;
	}

	MouseButton InputManager::mapWin32MouseButton( uint32, uintptr_t )
	{
		return MouseButton::Count;
	}

	Key InputManager::mapX11KeySym( uint64 )
	{
		return Key::Unknown;
	}

	void InputManager::pollPlatform() {}

	void InputManager::processNativeEvent( const NativeWindowEvent& ) {}
#endif
} // namespace sw
