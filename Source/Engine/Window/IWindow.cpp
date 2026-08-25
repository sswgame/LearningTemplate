#include "pch.h"

#include "Engine/Window/IWindow.h"

#include "Engine/Window/Linux/X11Window.h"
#include "Engine/Window/Mac/CocoaWindow.h"
#include "Engine/Window/Windows/Win32Window.h"

namespace sw
{
	namespace
	{

		IWindow* s_pActiveWindow{ nullptr };

	} // namespace

	IWindow::IWindow()
		: _title{}
		, _customHandler{}
		, _onResize{}
		, _width{ 1280 }
		, _height{ 720 }
		, _bShouldClose{ false }
		, _arrReserved{}
	{
	}

	IWindow::~IWindow() = default;

	bool IWindow::recreate()
	{
		if ( _title.empty() )
			return false;

		const uint32 width	= _width;
		const uint32 height = _height;
		destroy();
		_bShouldClose = false;

		const string title = StringUtil::utf16ToUtf8( _title.c_str() );
		return initializeWindow( title.c_str(), width, height );
	}

	unique_ptr<IWindow> IWindow::createPlatformWindow()
	{
#if defined( SW_PLATFORM_WINDOWS )
		return make_unique<Win32Window>();
#elif defined( SW_PLATFORM_MACOS )
		return make_unique<CocoaWindow>();
#elif defined( SW_PLATFORM_LINUX )
		return make_unique<X11Window>();
#else
		return nullptr;
#endif
	}

	void IWindow::setActiveWindow( IWindow* pWindow )
	{
		s_pActiveWindow = pWindow;
	}

	IWindow* IWindow::getActiveWindow()
	{
		return s_pActiveWindow;
	}
} // namespace sw
