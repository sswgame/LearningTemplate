/**
 * @file ImGuiX11Platform.cpp
 * @brief ImGui X11 플랫폼 백엔드 구현
 */
#include "ImGuiX11Platform.h"

#if defined( SW_PLATFORM_LINUX )
	#include <imgui.h>
	#include <X11/Xatom.h>
	#include <X11/keysym.h>
	#include "Core/Utility/Time/EngineTimer.h"

namespace
{
	Display*	 s_display	  = nullptr;
	Window		 s_mainWindow = 0;
	Atom		 s_wmDelete	  = 0;
	sw::CpuTimer s_timer;

	struct ViewportData
	{
		Window windowHandle = 0;
		bool   bOwned		= false;
	};

	ImGuiViewport* findViewportByWindow( Window window )
	{
		if ( window == 0 )
			return nullptr;

		ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
		for ( int i = 0; i < platformIO.Viewports.Size; ++i )
		{
			ImGuiViewport* viewport = platformIO.Viewports[i];
			auto*		   data		= static_cast<ViewportData*>( viewport->PlatformUserData );
			if ( data != nullptr && data->windowHandle == window )
				return viewport;
		}
		return nullptr;
	}

	void createWindow( ImGuiViewport* viewport )
	{
		if ( s_display == nullptr || viewport == nullptr )
			return;

		auto* data					   = IM_NEW( ViewportData )();
		data->bOwned				   = true;
		viewport->PlatformUserData	   = data;

		const int width	 = (int)viewport->Size.x > 0 ? (int)viewport->Size.x : 1;
		const int height = (int)viewport->Size.y > 0 ? (int)viewport->Size.y : 1;
		Window	  root	 = DefaultRootWindow( s_display );
		data->windowHandle = XCreateSimpleWindow(
			s_display, root, (int)viewport->Pos.x, (int)viewport->Pos.y, (unsigned)width, (unsigned)height, 1,
			BlackPixel( s_display, DefaultScreen( s_display ) ),
			WhitePixel( s_display, DefaultScreen( s_display ) ) );

		XSelectInput( s_display, data->windowHandle,
					  ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask
						  | FocusChangeMask | KeyPressMask | KeyReleaseMask );
		if ( s_wmDelete == 0 )
			s_wmDelete = XInternAtom( s_display, "WM_DELETE_WINDOW", False );
		XSetWMProtocols( s_display, data->windowHandle, &s_wmDelete, 1 );
		XMapWindow( s_display, data->windowHandle );
		XFlush( s_display );

		viewport->PlatformHandle	= s_display;
		viewport->PlatformHandleRaw = reinterpret_cast<void*>( static_cast<uintptr_t>( data->windowHandle ) );
	}

	void destroyWindow( ImGuiViewport* viewport )
	{
		if ( viewport == nullptr )
			return;

		if ( auto* data = static_cast<ViewportData*>( viewport->PlatformUserData ) )
		{
			if ( data->bOwned && data->windowHandle != 0 && s_display != nullptr )
			{
				XDestroyWindow( s_display, data->windowHandle );
				XFlush( s_display );
			}
			IM_DELETE( data );
			viewport->PlatformUserData	= nullptr;
			viewport->PlatformHandle	= nullptr;
			viewport->PlatformHandleRaw = nullptr;
		}
	}

	void showWindow( ImGuiViewport* viewport )
	{
		auto* data = static_cast<ViewportData*>( viewport->PlatformUserData );
		if ( data != nullptr && s_display != nullptr && data->windowHandle != 0 )
			XMapRaised( s_display, data->windowHandle );
	}

	void setWindowPos( ImGuiViewport* viewport, ImVec2 pos )
	{
		auto* data = static_cast<ViewportData*>( viewport->PlatformUserData );
		if ( data != nullptr && s_display != nullptr && data->windowHandle != 0 )
			XMoveWindow( s_display, data->windowHandle, (int)pos.x, (int)pos.y );
	}

	ImVec2 getWindowPos( ImGuiViewport* viewport )
	{
		auto* data = static_cast<ViewportData*>( viewport->PlatformUserData );
		if ( data == nullptr || s_display == nullptr || data->windowHandle == 0 )
			return ImVec2( 0, 0 );

		Window child = 0;
		int	   x = 0, y = 0;
		XTranslateCoordinates( s_display, data->windowHandle, DefaultRootWindow( s_display ), 0, 0, &x, &y, &child );
		return ImVec2( (float)x, (float)y );
	}

	void setWindowSize( ImGuiViewport* viewport, ImVec2 size )
	{
		auto* data = static_cast<ViewportData*>( viewport->PlatformUserData );
		if ( data != nullptr && s_display != nullptr && data->windowHandle != 0 )
		{
			XResizeWindow( s_display, data->windowHandle,
						   (unsigned)( size.x > 1 ? size.x : 1 ),
						   (unsigned)( size.y > 1 ? size.y : 1 ) );
		}
	}

	ImVec2 getWindowSize( ImGuiViewport* viewport )
	{
		auto* data = static_cast<ViewportData*>( viewport->PlatformUserData );
		if ( data == nullptr || s_display == nullptr || data->windowHandle == 0 )
			return ImVec2( 0, 0 );

		XWindowAttributes attrs{};
		XGetWindowAttributes( s_display, data->windowHandle, &attrs );
		return ImVec2( (float)attrs.width, (float)attrs.height );
	}

	void setWindowTitle( ImGuiViewport* viewport, const char* title )
	{
		auto* data = static_cast<ViewportData*>( viewport->PlatformUserData );
		if ( data != nullptr && s_display != nullptr && data->windowHandle != 0 && title != nullptr )
			XStoreName( s_display, data->windowHandle, title );
	}

	void setWindowFocus( ImGuiViewport* viewport )
	{
		auto* data = static_cast<ViewportData*>( viewport->PlatformUserData );
		if ( data != nullptr && s_display != nullptr && data->windowHandle != 0 )
			XSetInputFocus( s_display, data->windowHandle, RevertToParent, CurrentTime );
	}

	bool getWindowFocus( ImGuiViewport* viewport )
	{
		auto* data = static_cast<ViewportData*>( viewport->PlatformUserData );
		if ( data == nullptr || s_display == nullptr || data->windowHandle == 0 )
			return false;

		Window focused = 0;
		int	   revert  = 0;
		XGetInputFocus( s_display, &focused, &revert );
		return focused == data->windowHandle;
	}

	bool getWindowMinimized( ImGuiViewport* viewport )
	{
		(void)viewport;
		return false;
	}

	void updateMonitors()
	{
		ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
		platformIO.Monitors.resize( 0 );
		if ( s_display == nullptr )
			return;

		const int screenCount = ScreenCount( s_display );
		for ( int screen = 0; screen < screenCount; ++screen )
		{
			ImGuiPlatformMonitor monitor{};
			monitor.MainPos	 = ImVec2( 0.0f, 0.0f );
			monitor.MainSize = ImVec2( (float)DisplayWidth( s_display, screen ), (float)DisplayHeight( s_display, screen ) );
			monitor.WorkPos	 = monitor.MainPos;
			monitor.WorkSize = monitor.MainSize;
			monitor.DpiScale = 1.0f;

			const int widthMm = DisplayWidthMM( s_display, screen );
			if ( widthMm > 0 && monitor.MainSize.x > 0.0f )
			{
				const float dpi = ( monitor.MainSize.x * 25.4f ) / (float)widthMm;
				if ( dpi > 0.0f )
					monitor.DpiScale = dpi / 96.0f;
			}
			if ( monitor.DpiScale <= 0.0f )
				continue;

			monitor.PlatformHandle = reinterpret_cast<void*>( static_cast<uintptr_t>( screen ) );
			platformIO.Monitors.push_back( monitor );
		}

		if ( platformIO.Monitors.empty() )
		{
			ImGuiPlatformMonitor fallback{};
			fallback.MainSize = fallback.WorkSize = ImVec2( 1280.0f, 720.0f );
			fallback.DpiScale					  = 1.0f;
			platformIO.Monitors.push_back( fallback );
		}
	}

	bool initImpl( Display* display, Window window )
	{
		s_display	 = display;
		s_mainWindow = window;
		s_wmDelete	 = XInternAtom( display, "WM_DELETE_WINDOW", False );
		s_timer.resetTimer();
		s_timer.startTimer();

		ImGuiIO& io			   = ImGui::GetIO();
		io.BackendPlatformName = "imgui_impl_x11";
		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
		io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;

		ImGuiViewport* mainViewport		= ImGui::GetMainViewport();
		mainViewport->PlatformHandle	= display;
		mainViewport->PlatformHandleRaw = reinterpret_cast<void*>( static_cast<uintptr_t>( window ) );

		auto* mainData				   = IM_NEW( ViewportData )();
		mainData->windowHandle		   = window;
		mainData->bOwned			   = false;
		mainViewport->PlatformUserData = mainData;

		ImGuiPlatformIO& platformIO			   = ImGui::GetPlatformIO();
		platformIO.Platform_CreateWindow	   = createWindow;
		platformIO.Platform_DestroyWindow	   = destroyWindow;
		platformIO.Platform_ShowWindow		   = showWindow;
		platformIO.Platform_SetWindowPos	   = setWindowPos;
		platformIO.Platform_GetWindowPos	   = getWindowPos;
		platformIO.Platform_SetWindowSize	   = setWindowSize;
		platformIO.Platform_GetWindowSize	   = getWindowSize;
		platformIO.Platform_SetWindowFocus	   = setWindowFocus;
		platformIO.Platform_GetWindowFocus	   = getWindowFocus;
		platformIO.Platform_GetWindowMinimized = getWindowMinimized;
		platformIO.Platform_SetWindowTitle	   = setWindowTitle;

		updateMonitors();
		return true;
	}

	void shutdownImpl()
	{
		ImGuiViewport* mainViewport = ImGui::GetMainViewport();
		if ( mainViewport != nullptr && mainViewport->PlatformUserData != nullptr )
		{
			IM_DELETE( static_cast<ViewportData*>( mainViewport->PlatformUserData ) );
			mainViewport->PlatformUserData = nullptr;
		}
		s_display	 = nullptr;
		s_mainWindow = 0;
		s_wmDelete	 = 0;
	}

	void newFrameImpl()
	{
		ImGuiIO& io = ImGui::GetIO();
		if ( s_display == nullptr || s_mainWindow == 0 )
			return;

		if ( ImGui::GetPlatformIO().Monitors.empty() )
			updateMonitors();

		XWindowAttributes attrs{};
		XGetWindowAttributes( s_display, s_mainWindow, &attrs );
		io.DisplaySize = ImVec2( (float)attrs.width, (float)attrs.height );

		s_timer.updateTimer();
		io.DeltaTime = s_timer.getDeltaTime();
		if ( io.DeltaTime <= 0.0f )
			io.DeltaTime = 0.00001f;

		Window		 rootReturn = 0, childReturn = 0;
		int			 rootX = 0, rootY = 0, winX = 0, winY = 0;
		unsigned int maskReturn = 0;
		if ( XQueryPointer( s_display, s_mainWindow, &rootReturn, &childReturn, &rootX, &rootY, &winX, &winY, &maskReturn ) )
		{
			io.AddMousePosEvent( (float)winX, (float)winY );
			io.AddMouseButtonEvent( 0, ( maskReturn & Button1Mask ) != 0 );
			io.AddMouseButtonEvent( 1, ( maskReturn & Button3Mask ) != 0 );
			io.AddMouseButtonEvent( 2, ( maskReturn & Button2Mask ) != 0 );
		}
	}

	bool processEventImpl( XEvent* event )
	{
		ImGuiIO& io = ImGui::GetIO();
		switch ( event->type )
		{
			case ClientMessage:
			{
				if ( s_wmDelete == 0 || event->xclient.data.l[0] != static_cast<long>( s_wmDelete ) )
					return false;

				if ( event->xclient.window == s_mainWindow )
					return false;

				if ( ImGuiViewport* viewport = findViewportByWindow( event->xclient.window ) )
				{
					viewport->PlatformRequestClose = true;
					return true;
				}
				return false;
			}
			case KeyPress:
			case KeyRelease:
			{
				const bool	 bIsDown = ( event->type == KeyPress );
				const KeySym sym	 = XLookupKeysym( &event->xkey, 0 );
				if ( bIsDown && sym >= 0x20 && sym <= 0x7E )
					io.AddInputCharacter( (unsigned int)sym );
				return io.WantCaptureKeyboard;
			}
			case ButtonPress:
			case ButtonRelease:
			{
				const bool bIsDown = ( event->type == ButtonPress );
				int		   button  = 0;
				if ( event->xbutton.button == Button1 )
					button = 0;
				else if ( event->xbutton.button == Button3 )
					button = 1;
				else if ( event->xbutton.button == Button2 )
					button = 2;
				else if ( event->xbutton.button == Button4 )
				{
					if ( bIsDown )
						io.AddMouseWheelEvent( 0, 1.0f );
					return io.WantCaptureMouse;
				}
				else if ( event->xbutton.button == Button5 )
				{
					if ( bIsDown )
						io.AddMouseWheelEvent( 0, -1.0f );
					return io.WantCaptureMouse;
				}
				io.AddMouseButtonEvent( button, bIsDown );
				return io.WantCaptureMouse;
			}
		}
		return false;
	}
} // namespace

bool ImGui_ImplX11_Init( Display* display, Window window )
{
	return initImpl( display, window );
}

void ImGui_ImplX11_Shutdown()
{
	shutdownImpl();
}

void ImGui_ImplX11_NewFrame()
{
	newFrameImpl();
}

bool ImGui_ImplX11_ProcessEvent( XEvent* event )
{
	return processEventImpl( event );
}

#endif // SW_PLATFORM_LINUX
