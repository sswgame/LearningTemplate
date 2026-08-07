/**
 * @file IWindow.cpp
 * @brief IWindow 팩토리·활성 윈도우 구현
 */
#include "IWindow.h"
#include "Win32Window.h"
#include "CocoaWindow.h"
#include "X11Window.h"

namespace sw
{
	namespace
	{
		IWindow* s_activeWindow = nullptr;
	} // namespace

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
		s_activeWindow = window;
	}

	IWindow* IWindow::getActiveWindow()
	{
		return s_activeWindow;
	}

	bool IWindow::recreate()
	{
		if ( _title.empty() )
			return false;

		const uint32 width	= _width;
		const uint32 height = _height;
		destroy();
		_bShouldClose = false;

		const std::string& title = StringUtil::utf16ToUtf8( _title.c_str() );
		return create( title.c_str(), width, height );
	}
} // namespace sw
