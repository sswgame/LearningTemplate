#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_LINUX )
	#include "Engine/Input/InputKeyMap.h"
	#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
	void InputManager::pollPlatform()
	{
		// X11 path is primarily event-driven via processNativeEvent.
	}

	void InputManager::processNativeEvent( const NativeWindowEvent& event )
	{
		if ( event._message != NativeWindowEvent::kMessageX11 || event._lParam == 0 )
			return;

		const XEvent* pXev = reinterpret_cast<const XEvent*>( event._lParam );
		switch ( pXev->type )
		{
			case KeyPress:
			case KeyRelease:
			{
				KeySym keySym = XLookupKeysym( const_cast<XKeyEvent*>( &pXev->xkey ), 0 );
				setKeyDown( mapX11KeySym( static_cast<uint64>( keySym ) ), pXev->type == KeyPress );
				break;
			}
			case ButtonPress:
			case ButtonRelease:
			{
				const bool bDown = ( pXev->type == ButtonPress );
				switch ( pXev->xbutton.button )
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
					case Button4:
						if ( bDown )
							_mouseWheelAccum += 1.0f;
						break;
					case Button5:
						if ( bDown )
							_mouseWheelAccum -= 1.0f;
						break;
					case 8: // commonly Button8 / back
						setMouseButtonDown( MouseButton::X1, bDown );
						break;
					case 9: // commonly Button9 / forward
						setMouseButtonDown( MouseButton::X2, bDown );
						break;
					default:
						break;
				}
				_mouseX = static_cast<int32>( pXev->xbutton.x );
				_mouseY = static_cast<int32>( pXev->xbutton.y );
				break;
			}
			case MotionNotify:
				_mouseX = static_cast<int32>( pXev->xmotion.x );
				_mouseY = static_cast<int32>( pXev->xmotion.y );
				break;
			default:
				break;
		}
	}
} // namespace sw

#endif
