#include "pch.h"

#include "Engine/Window/ISplashWindow.h"

#include "Engine/Window/Linux/X11SplashWindow.h"
#include "Engine/Window/Mac/CocoaSplashWindow.h"
#include "Engine/Window/Windows/Win32SplashWindow.h"

namespace sw
{
	ISplashWindow::ISplashWindow()
		: _title{}
		, _status{}
		, _width{ 480 }
		, _height{ 280 }
		, _bOpen{ false }
	{
	}

	ISplashWindow::~ISplashWindow() = default;

	unique_ptr<ISplashWindow> ISplashWindow::createPlatformSplash()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return make_unique<Win32SplashWindow>();
#elif defined( SW_PLATFORM_MACOS )
		return make_unique<CocoaSplashWindow>();
#elif defined( SW_PLATFORM_LINUX )
		return make_unique<X11SplashWindow>();
#else
		return nullptr;
#endif
	}
} // namespace sw
