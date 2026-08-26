#include "pch.h"

#include "Engine/Window/NativeWindowEvent.h"

namespace sw
{
#if defined( SW_PLATFORM_WINDOWS )
	bool NativeWindowEvent::isMouseInput() const
	{
		return _message >= WM_MOUSEFIRST && _message <= WM_MOUSELAST;
	}

	bool NativeWindowEvent::isKeyboardInput() const
	{
		return _message >= WM_KEYFIRST && _message <= WM_KEYLAST;
	}

	bool NativeWindowEvent::isInputRelease() const
	{
		if ( isMouseInput() )
		{
			const bool bMouseUp = ( _message == WM_LBUTTONUP || _message == WM_RBUTTONUP || _message == WM_MBUTTONUP ||
									_message == WM_XBUTTONUP );
			return bMouseUp;
		}
		if ( isKeyboardInput() )
		{
			const bool bKeyUp = ( _message == WM_KEYUP || _message == WM_SYSKEYUP );
			return bKeyUp;
		}
		return false;
	}

#elif defined( SW_PLATFORM_LINUX )
	bool NativeWindowEvent::isMouseInput() const
	{
		if ( _message != kMessageX11 || _lParam == 0 )
			return false;
		const XEvent* pXev = reinterpret_cast<const XEvent*>( _lParam );
		return pXev->type == ButtonPress || pXev->type == ButtonRelease || pXev->type == MotionNotify;
	}

	bool NativeWindowEvent::isKeyboardInput() const
	{
		if ( _message != kMessageX11 || _lParam == 0 )
			return false;
		const XEvent* pXev = reinterpret_cast<const XEvent*>( _lParam );
		return pXev->type == KeyPress || pXev->type == KeyRelease;
	}

	bool NativeWindowEvent::isInputRelease() const
	{
		if ( _message != kMessageX11 || _lParam == 0 )
			return false;
		const XEvent* pXev = reinterpret_cast<const XEvent*>( _lParam );
		return pXev->type == ButtonRelease || pXev->type == KeyRelease;
	}

#else
	bool NativeWindowEvent::isMouseInput() const
	{
		return false;
	}

	bool NativeWindowEvent::isKeyboardInput() const
	{
		return false;
	}

	bool NativeWindowEvent::isInputRelease() const
	{
		return false;
	}
#endif
} // namespace sw
