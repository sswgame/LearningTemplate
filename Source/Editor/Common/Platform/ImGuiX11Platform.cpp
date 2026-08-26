#include "pch.h"

#include "Editor/Common/Platform/ImGuiX11Platform.h"

#if defined( SW_PLATFORM_LINUX )
	#include <imgui.h>
	#include "Core/Time/CpuTimer.h"
	#include <X11/Xatom.h>
	#include <X11/keysym.h>

namespace sw
{
	namespace
	{
		Display*	 s_pDisplay{ nullptr };
		Window		 s_mainWindow{ 0 };
		Atom		 s_wmDelete{ 0 };
		sw::CpuTimer s_timer;

		struct ViewportData
		{
			Window _windowHandle{ 0 };
			bool   _bOwned{ false };
		};

		ImGuiViewport* findViewportByWindow( Window window )
		{
			if ( window == 0 )
				return nullptr;

			ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
			for ( int32 viewportIndex = 0; viewportIndex < platformIO.Viewports.Size; ++viewportIndex )
			{
				ImGuiViewport* pViewport = platformIO.Viewports[viewportIndex];
				ViewportData*  pData	 = static_cast<ViewportData*>( pViewport->PlatformUserData );
				if ( pData != nullptr && pData->_windowHandle == window )
					return pViewport;
			}
			return nullptr;
		}

		void createWindow( ImGuiViewport* pViewport )
		{
			if ( s_pDisplay == nullptr || pViewport == nullptr )
				return;

			ViewportData* pData			= IM_NEW( ViewportData )();
			pData->_bOwned				= true;
			pViewport->PlatformUserData = pData;

			const int32 width	 = static_cast<int32>( pViewport->Size.x ) > 0 ? static_cast<int32>( pViewport->Size.x ) : 1;
			const int32 height	 = static_cast<int32>( pViewport->Size.y ) > 0 ? static_cast<int32>( pViewport->Size.y ) : 1;
			Window		root	 = DefaultRootWindow( s_pDisplay );
			pData->_windowHandle = XCreateSimpleWindow(
				s_pDisplay, root, static_cast<int32>( pViewport->Pos.x ), static_cast<int32>( pViewport->Pos.y ), static_cast<uint32>( width ), static_cast<uint32>( height ), 1,
				BlackPixel( s_pDisplay, DefaultScreen( s_pDisplay ) ),
				WhitePixel( s_pDisplay, DefaultScreen( s_pDisplay ) ) );

			XSelectInput( s_pDisplay, pData->_windowHandle,
						  ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask | KeyPressMask | KeyReleaseMask );
			if ( s_wmDelete == 0 )
				s_wmDelete = XInternAtom( s_pDisplay, "WM_DELETE_WINDOW", False );
			XSetWMProtocols( s_pDisplay, pData->_windowHandle, &s_wmDelete, 1 );
			XMapWindow( s_pDisplay, pData->_windowHandle );
			XFlush( s_pDisplay );

			pViewport->PlatformHandle	 = s_pDisplay;
			pViewport->PlatformHandleRaw = reinterpret_cast<void*>( static_cast<uintptr_t>( pData->_windowHandle ) );
		}

		void destroyWindow( ImGuiViewport* pViewport )
		{
			if ( pViewport == nullptr )
				return;

			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData != nullptr )
			{
				if ( pData->_bOwned && pData->_windowHandle != 0 && s_pDisplay != nullptr )
				{
					XDestroyWindow( s_pDisplay, pData->_windowHandle );
					XFlush( s_pDisplay );
				}
				IM_DELETE( pData );
				pViewport->PlatformUserData	 = nullptr;
				pViewport->PlatformHandle	 = nullptr;
				pViewport->PlatformHandleRaw = nullptr;
			}
		}

		void showWindow( ImGuiViewport* pViewport )
		{
			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData != nullptr && s_pDisplay != nullptr && pData->_windowHandle != 0 )
				XMapRaised( s_pDisplay, pData->_windowHandle );
		}

		void setWindowPos( ImGuiViewport* pViewport, ImVec2 pos )
		{
			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData != nullptr && s_pDisplay != nullptr && pData->_windowHandle != 0 )
				XMoveWindow( s_pDisplay, pData->_windowHandle, static_cast<int32>( pos.x ), static_cast<int32>( pos.y ) );
		}

		ImVec2 getWindowPos( ImGuiViewport* pViewport )
		{
			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData == nullptr || s_pDisplay == nullptr || pData->_windowHandle == 0 )
				return ImVec2( 0, 0 );

			Window child{ 0 };
			int32  x = 0, y = 0;
			XTranslateCoordinates( s_pDisplay, pData->_windowHandle, DefaultRootWindow( s_pDisplay ), 0, 0, &x, &y, &child );
			return ImVec2( static_cast<float32>( x ), static_cast<float32>( y ) );
		}

		void setWindowSize( ImGuiViewport* pViewport, ImVec2 size )
		{
			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData != nullptr && s_pDisplay != nullptr && pData->_windowHandle != 0 )
			{
				XResizeWindow( s_pDisplay, pData->_windowHandle,
							   static_cast<uint32>( size.x > 1 ? size.x : 1 ),
							   static_cast<uint32>( size.y > 1 ? size.y : 1 ) );
			}
		}

		ImVec2 getWindowSize( ImGuiViewport* pViewport )
		{
			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData == nullptr || s_pDisplay == nullptr || pData->_windowHandle == 0 )
				return ImVec2( 0, 0 );

			XWindowAttributes attrs{};
			XGetWindowAttributes( s_pDisplay, pData->_windowHandle, &attrs );
			return ImVec2( static_cast<float32>( attrs.width ), static_cast<float32>( attrs.height ) );
		}

		void setWindowTitle( ImGuiViewport* pViewport, const utf8* pTitle )
		{
			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData != nullptr && s_pDisplay != nullptr && pData->_windowHandle != 0 && pTitle != nullptr )
				XStoreName( s_pDisplay, pData->_windowHandle, pTitle );
		}

		void setWindowFocus( ImGuiViewport* pViewport )
		{
			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData != nullptr && s_pDisplay != nullptr && pData->_windowHandle != 0 )
				XSetInputFocus( s_pDisplay, pData->_windowHandle, RevertToParent, CurrentTime );
		}

		bool getWindowFocus( ImGuiViewport* pViewport )
		{
			ViewportData* pData = static_cast<ViewportData*>( pViewport->PlatformUserData );
			if ( pData == nullptr || s_pDisplay == nullptr || pData->_windowHandle == 0 )
				return false;

			Window focused{ 0 };
			int32  revert{ 0 };
			XGetInputFocus( s_pDisplay, &focused, &revert );
			return focused == pData->_windowHandle;
		}

		bool getWindowMinimized( ImGuiViewport* pViewport )
		{
			(void)pViewport;
			return false;
		}

		void updateMonitors()
		{
			ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
			platformIO.Monitors.resize( 0 );
			if ( s_pDisplay == nullptr )
				return;

			const int32 screenCount = ScreenCount( s_pDisplay );
			for ( int32 screen = 0; screen < screenCount; ++screen )
			{
				ImGuiPlatformMonitor monitor{};
				monitor.MainPos	 = ImVec2( 0.0f, 0.0f );
				monitor.MainSize = ImVec2( static_cast<float32>( DisplayWidth( s_pDisplay, screen ) ), static_cast<float32>( DisplayHeight( s_pDisplay, screen ) ) );
				monitor.WorkPos	 = monitor.MainPos;
				monitor.WorkSize = monitor.MainSize;
				monitor.DpiScale = 1.0f;

				const int32 widthMm = DisplayWidthMM( s_pDisplay, screen );
				if ( widthMm > 0 && monitor.MainSize.x > 0.0f )
				{
					const float32 dpi = ( monitor.MainSize.x * 25.4f ) / static_cast<float32>( widthMm );
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

		bool initImpl( Display* pDisplay, Window window )
		{
			s_pDisplay	 = pDisplay;
			s_mainWindow = window;
			s_wmDelete	 = XInternAtom( pDisplay, "WM_DELETE_WINDOW", False );
			s_timer.resetTimer();
			s_timer.startTimer();

			ImGuiIO& io			   = ImGui::GetIO();
			io.BackendPlatformName = "imgui_impl_x11";
			io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
			io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;

			ImGuiViewport* pMainViewport	 = ImGui::GetMainViewport();
			pMainViewport->PlatformHandle	 = pDisplay;
			pMainViewport->PlatformHandleRaw = reinterpret_cast<void*>( static_cast<uintptr_t>( window ) );

			ViewportData* pMainData			= IM_NEW( ViewportData )();
			pMainData->_windowHandle		= window;
			pMainData->_bOwned				= false;
			pMainViewport->PlatformUserData = pMainData;

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
			ImGuiViewport* pMainViewport = ImGui::GetMainViewport();
			if ( pMainViewport != nullptr && pMainViewport->PlatformUserData != nullptr )
			{
				IM_DELETE( static_cast<ViewportData*>( pMainViewport->PlatformUserData ) );
				pMainViewport->PlatformUserData = nullptr;
			}
			s_pDisplay	 = nullptr;
			s_mainWindow = 0;
			s_wmDelete	 = 0;
		}

		void newFrameImpl()
		{
			ImGuiIO& io = ImGui::GetIO();
			if ( s_pDisplay == nullptr || s_mainWindow == 0 )
				return;

			if ( ImGui::GetPlatformIO().Monitors.empty() )
				updateMonitors();

			XWindowAttributes attrs{};
			XGetWindowAttributes( s_pDisplay, s_mainWindow, &attrs );
			io.DisplaySize = ImVec2( static_cast<float32>( attrs.width ), static_cast<float32>( attrs.height ) );

			s_timer.updateTimer();
			io.DeltaTime = s_timer.getDeltaTime();
			if ( io.DeltaTime <= 0.0f )
				io.DeltaTime = 0.00001f;

			Window rootReturn = 0, childReturn = 0;
			int32  rootX = 0, rootY = 0, winX = 0, winY = 0;
			uint32 maskReturn{ 0 };
			if ( XQueryPointer( s_pDisplay, s_mainWindow, &rootReturn, &childReturn, &rootX, &rootY, &winX, &winY, &maskReturn ) )
			{
				io.AddMousePosEvent( static_cast<float32>( winX ), static_cast<float32>( winY ) );
				io.AddMouseButtonEvent( 0, ( maskReturn & Button1Mask ) != 0 );
				io.AddMouseButtonEvent( 1, ( maskReturn & Button3Mask ) != 0 );
				io.AddMouseButtonEvent( 2, ( maskReturn & Button2Mask ) != 0 );
			}
		}

		bool processEventImpl( XEvent* pEvent )
		{
			ImGuiIO& io = ImGui::GetIO();
			switch ( pEvent->type )
			{
				case ClientMessage:
				{
					if ( s_wmDelete == 0 || pEvent->xclient.data.l[0] != static_cast<int32>( s_wmDelete ) )
						return false;

					if ( pEvent->xclient.window == s_mainWindow )
						return false;

					ImGuiViewport* pViewport = findViewportByWindow( pEvent->xclient.window );
					if ( pViewport != nullptr )
					{
						pViewport->PlatformRequestClose = true;
						return true;
					}
					return false;
				}
				case KeyPress:
				case KeyRelease:
				{
					const bool	 bIsDown = ( pEvent->type == KeyPress );
					const KeySym sym	 = XLookupKeysym( &pEvent->xkey, 0 );
					if ( bIsDown && sym >= 0x20 && sym <= 0x7E )
						io.AddInputCharacter( static_cast<uint32>( sym ) );
					return io.WantCaptureKeyboard;
				}
				case ButtonPress:
				case ButtonRelease:
				{
					const bool bIsDown = ( pEvent->type == ButtonPress );
					int32	   button{ 0 };
					if ( pEvent->xbutton.button == Button1 )
						button = 0;
					else if ( pEvent->xbutton.button == Button3 )
						button = 1;
					else if ( pEvent->xbutton.button == Button2 )
						button = 2;
					else if ( pEvent->xbutton.button == Button4 )
					{
						if ( bIsDown )
							io.AddMouseWheelEvent( 0, 1.0f );
						return io.WantCaptureMouse;
					}
					else if ( pEvent->xbutton.button == Button5 )
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
} // namespace sw

bool ImGui_ImplX11_Init( Display* pDisplay, Window window )
{
	return sw::initImpl( pDisplay, window );
}

void ImGui_ImplX11_Shutdown()
{
	sw::shutdownImpl();
}

void ImGui_ImplX11_NewFrame()
{
	sw::newFrameImpl();
}

bool ImGui_ImplX11_ProcessEvent( XEvent* pEvent )
{
	return sw::processEventImpl( pEvent );
}

#endif // SW_PLATFORM_LINUX
