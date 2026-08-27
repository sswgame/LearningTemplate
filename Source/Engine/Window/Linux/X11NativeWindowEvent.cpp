#include "pch.h"

#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_LINUX )
	#include <X11/Xlib.h>
	#include "Core/Common/X11MacroUndef.h"

namespace sw
{
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
} // namespace sw

#endif
