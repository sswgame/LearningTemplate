#include "pch.h"

#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_MACOS )

namespace sw
{
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
} // namespace sw

#endif
