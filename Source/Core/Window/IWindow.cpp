/**
 * @file IWindow.cpp
 * @brief Auto-generated documentation header
 */
#include "IWindow.h"
#include "Win32Window.h"
#include "CocoaWindow.h"
#include "X11Window.h"

namespace sw
{
	namespace
	{
		IWindow* g_activeWindow = nullptr;
	}

	std::unique_ptr<IWindow> IWindow::createPlatformWindow()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return std::make_unique<Win32Window>();
#elif defined( SW_PLATFORM_MACOS )
		return std::make_unique<CocoaWindow>();
#elif defined( SW_PLATFORM_LINUX )
		return std::make_unique<X11Window>();
#else
		return nullptr;
#endif
	}

	void IWindow::setActiveWindow( IWindow* window )
	{
		g_activeWindow = window;
	}

	IWindow* IWindow::getActiveWindow()
	{
		return g_activeWindow;
	}
}
