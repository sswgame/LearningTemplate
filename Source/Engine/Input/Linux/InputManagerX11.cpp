#include "pch.h"

#include "Engine/Input/InputManager.h"

#if defined( SW_PLATFORM_LINUX )
	#include "Engine/Input/Devices/KeyboardDevice.h"
	#include "Engine/Input/Devices/MouseDevice.h"
	#include "Engine/Input/Events/RawInputEvent.h"
	#include "Engine/Input/InputKeyMap.h"
	#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
	void InputManager::pollPlatform()
	{
		// X11 path is primarily event-driven via processNativeEvent.
	}

	void InputManager::onNativeWindowEvent( const NativeWindowEvent& event )
	{
		processNativeEvent( event );
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
				const KeySym keySym = XLookupKeysym( const_cast<XKeyEvent*>( &pXev->xkey ), 0 );
				const Key	 key	= InputKeyMap::mapX11KeySym( static_cast<uint64>( keySym ) );
				const bool	 bDown	= ( pXev->type == KeyPress );
				if ( _pKeyboard != nullptr )
					_pKeyboard->setKeyDown( key, bDown );
				if ( bDown )
					postRawEvent( RawInputEvent::makeKeyDown( key ) );
				else
					postRawEvent( RawInputEvent::makeKeyUp( key ) );
				break;
			}
			case ButtonPress:
			case ButtonRelease:
			{
				const bool bDown = ( pXev->type == ButtonPress );
				switch ( pXev->xbutton.button )
				{
					case Button1:
						if ( _pMouse != nullptr )
							_pMouse->setButtonDown( MouseButton::Left, bDown );
						postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::Left ) : RawInputEvent::makeMouseButtonUp( MouseButton::Left ) );
						break;
					case Button2:
						if ( _pMouse != nullptr )
							_pMouse->setButtonDown( MouseButton::Middle, bDown );
						postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::Middle ) : RawInputEvent::makeMouseButtonUp( MouseButton::Middle ) );
						break;
					case Button3:
						if ( _pMouse != nullptr )
							_pMouse->setButtonDown( MouseButton::Right, bDown );
						postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::Right ) : RawInputEvent::makeMouseButtonUp( MouseButton::Right ) );
						break;
					case Button4:
						if ( bDown )
						{
							if ( _pMouse != nullptr )
								_pMouse->addWheelDelta( 1.0f );
							postRawEvent( RawInputEvent::makeMouseWheel( 1.0f ) );
						}
						break;
					case Button5:
						if ( bDown )
						{
							if ( _pMouse != nullptr )
								_pMouse->addWheelDelta( -1.0f );
							postRawEvent( RawInputEvent::makeMouseWheel( -1.0f ) );
						}
						break;
					case 6:
						if ( bDown )
						{
							if ( _pMouse != nullptr )
								_pMouse->addHorizontalWheelDelta( -1.0f );
							postRawEvent( RawInputEvent::makeMouseHorizontalWheel( -1.0f ) );
						}
						break;
					case 7:
						if ( bDown )
						{
							if ( _pMouse != nullptr )
								_pMouse->addHorizontalWheelDelta( 1.0f );
							postRawEvent( RawInputEvent::makeMouseHorizontalWheel( 1.0f ) );
						}
						break;
					case 8:
						if ( _pMouse != nullptr )
							_pMouse->setButtonDown( MouseButton::X1, bDown );
						postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::X1 ) : RawInputEvent::makeMouseButtonUp( MouseButton::X1 ) );
						break;
					case 9:
						if ( _pMouse != nullptr )
							_pMouse->setButtonDown( MouseButton::X2, bDown );
						postRawEvent( bDown ? RawInputEvent::makeMouseButtonDown( MouseButton::X2 ) : RawInputEvent::makeMouseButtonUp( MouseButton::X2 ) );
						break;
					default:
						break;
				}
				break;
			}
			case MotionNotify:
			{
				const int32 mx = static_cast<int32>( pXev->xmotion.x );
				const int32 my = static_cast<int32>( pXev->xmotion.y );
				if ( _pMouse != nullptr )
					_pMouse->setPosition( mx, my );
				postRawEvent( RawInputEvent::makeMouseMove( mx, my ) );
				break;
			}
			case EnterNotify:
				if ( _pMouse != nullptr )
					_pMouse->setPointerInsideState( true );
				break;
			case LeaveNotify:
				if ( _pMouse != nullptr )
					_pMouse->setPointerInsideState( false );
				break;
			case FocusIn:
				onWindowFocusGained();
				break;
			case FocusOut:
				onWindowFocusLost();
				break;
			default:
				break;
		}
	}
} // namespace sw

#endif
