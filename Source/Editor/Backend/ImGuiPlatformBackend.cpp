/**
 * @file ImGuiPlatformBackend.cpp
 * @brief ImGui 플랫폼 백엔드 구현
 */
#include "IImGuiPlatformBackend.h"
#include "Core/Window/IWindow.h"
#include "Core/Window/NativeWindowEvent.h"
#include "Core/Graphics/RHI/RHITypes.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include <imgui.h>
	#include <imgui_impl_win32.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
#elif defined( SW_PLATFORM_MACOS )
	#include <imgui.h>

bool ImGui_ImplOSX_Init( void* view );
void ImGui_ImplOSX_Shutdown();
void ImGui_ImplOSX_NewFrame( void* view );
bool ImGui_ImplOSX_HandleEvent( void* event, void* view );
#elif defined( SW_PLATFORM_LINUX )
	#include <imgui.h>
	#include <X11/Xlib.h>
	#include <X11/Xatom.h>
	#include <X11/keysym.h>
	#include "Core/Utility/Time/EngineTimer.h"

static Display*		s_X11Display = nullptr;
static Window		s_X11Window	 = 0;
static sw::CpuTimer s_X11Timer;

struct ImGui_ImplX11_ViewportData
{
	Window WindowHandle = 0;
	bool   Owned		= false;
};

static void ImGui_ImplX11_CreateWindow( ImGuiViewport* viewport )
{
	if ( s_X11Display == nullptr || viewport == nullptr )
		return;

	auto* vd	= IM_NEW( ImGui_ImplX11_ViewportData )();
	vd->Owned	= true;
	viewport->PlatformUserData = vd;

	const int w = (int)viewport->Size.x > 0 ? (int)viewport->Size.x : 1;
	const int h = (int)viewport->Size.y > 0 ? (int)viewport->Size.y : 1;
	Window	  root = DefaultRootWindow( s_X11Display );
	vd->WindowHandle = XCreateSimpleWindow( s_X11Display, root, (int)viewport->Pos.x, (int)viewport->Pos.y, (unsigned)w, (unsigned)h, 1,
											BlackPixel( s_X11Display, DefaultScreen( s_X11Display ) ),
											WhitePixel( s_X11Display, DefaultScreen( s_X11Display ) ) );
	XSelectInput( s_X11Display, vd->WindowHandle,
				  ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask | KeyPressMask | KeyReleaseMask );
	Atom wmDelete = XInternAtom( s_X11Display, "WM_DELETE_WINDOW", False );
	XSetWMProtocols( s_X11Display, vd->WindowHandle, &wmDelete, 1 );
	XMapWindow( s_X11Display, vd->WindowHandle );
	XFlush( s_X11Display );

	viewport->PlatformHandle	= s_X11Display;
	viewport->PlatformHandleRaw = reinterpret_cast<void*>( static_cast<uintptr_t>( vd->WindowHandle ) );
}

static void ImGui_ImplX11_DestroyWindow( ImGuiViewport* viewport )
{
	if ( viewport == nullptr )
		return;
	if ( auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData ) )
	{
		if ( vd->Owned && vd->WindowHandle != 0 && s_X11Display )
		{
			XDestroyWindow( s_X11Display, vd->WindowHandle );
			XFlush( s_X11Display );
		}
		IM_DELETE( vd );
		viewport->PlatformUserData	= nullptr;
		viewport->PlatformHandle	= nullptr;
		viewport->PlatformHandleRaw = nullptr;
	}
}

static void ImGui_ImplX11_ShowWindow( ImGuiViewport* viewport )
{
	auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData );
	if ( vd && s_X11Display && vd->WindowHandle )
		XMapRaised( s_X11Display, vd->WindowHandle );
}

static void ImGui_ImplX11_SetWindowPos( ImGuiViewport* viewport, ImVec2 pos )
{
	auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData );
	if ( vd && s_X11Display && vd->WindowHandle )
		XMoveWindow( s_X11Display, vd->WindowHandle, (int)pos.x, (int)pos.y );
}

static ImVec2 ImGui_ImplX11_GetWindowPos( ImGuiViewport* viewport )
{
	auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData );
	if ( vd == nullptr || s_X11Display == nullptr || vd->WindowHandle == 0 )
		return ImVec2( 0, 0 );
	Window		 child;
	int			 x = 0, y = 0;
	XTranslateCoordinates( s_X11Display, vd->WindowHandle, DefaultRootWindow( s_X11Display ), 0, 0, &x, &y, &child );
	return ImVec2( (float)x, (float)y );
}

static void ImGui_ImplX11_SetWindowSize( ImGuiViewport* viewport, ImVec2 size )
{
	auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData );
	if ( vd && s_X11Display && vd->WindowHandle )
		XResizeWindow( s_X11Display, vd->WindowHandle, (unsigned)( size.x > 1 ? size.x : 1 ), (unsigned)( size.y > 1 ? size.y : 1 ) );
}

static ImVec2 ImGui_ImplX11_GetWindowSize( ImGuiViewport* viewport )
{
	auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData );
	if ( vd == nullptr || s_X11Display == nullptr || vd->WindowHandle == 0 )
		return ImVec2( 0, 0 );
	XWindowAttributes attrs{};
	XGetWindowAttributes( s_X11Display, vd->WindowHandle, &attrs );
	return ImVec2( (float)attrs.width, (float)attrs.height );
}

static void ImGui_ImplX11_SetWindowTitle( ImGuiViewport* viewport, const char* title )
{
	auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData );
	if ( vd && s_X11Display && vd->WindowHandle && title )
		XStoreName( s_X11Display, vd->WindowHandle, title );
}

static void ImGui_ImplX11_SetWindowFocus( ImGuiViewport* viewport )
{
	auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData );
	if ( vd && s_X11Display && vd->WindowHandle )
		XSetInputFocus( s_X11Display, vd->WindowHandle, RevertToParent, CurrentTime );
}

static bool ImGui_ImplX11_GetWindowFocus( ImGuiViewport* viewport )
{
	auto* vd = static_cast<ImGui_ImplX11_ViewportData*>( viewport->PlatformUserData );
	if ( vd == nullptr || s_X11Display == nullptr || vd->WindowHandle == 0 )
		return false;
	Window focused = 0;
	int	   revert  = 0;
	XGetInputFocus( s_X11Display, &focused, &revert );
	return focused == vd->WindowHandle;
}

static bool ImGui_ImplX11_GetWindowMinimized( ImGuiViewport* viewport )
{
	(void)viewport;
	return false;
}

static void ImGui_ImplX11_UpdateMonitors()
{
	// ImGui docking requires PlatformIO.Monitors.Size > 0 before NewFrame.
	ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
	platformIO.Monitors.resize( 0 );
	if ( s_X11Display == nullptr )
		return;

	const int screenCount = ScreenCount( s_X11Display );
	for ( int screen = 0; screen < screenCount; ++screen )
	{
		ImGuiPlatformMonitor monitor{};
		monitor.MainPos	 = ImVec2( 0.0f, 0.0f );
		monitor.MainSize = ImVec2( (float)DisplayWidth( s_X11Display, screen ), (float)DisplayHeight( s_X11Display, screen ) );
		monitor.WorkPos	 = monitor.MainPos;
		monitor.WorkSize = monitor.MainSize;
		monitor.DpiScale = 1.0f;

		const int widthMm = DisplayWidthMM( s_X11Display, screen );
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

static bool ImGui_ImplX11_Init( Display* display, Window window )
{
	s_X11Display = display;
	s_X11Window	 = window;
	s_X11Timer.resetTimer();
	s_X11Timer.startTimer();
	ImGuiIO& io				= ImGui::GetIO();
	io.BackendPlatformName	= "imgui_impl_x11";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
	io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;

	ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	mainViewport->PlatformHandle	= display;
	mainViewport->PlatformHandleRaw = reinterpret_cast<void*>( static_cast<uintptr_t>( window ) );
	auto* mainVd					= IM_NEW( ImGui_ImplX11_ViewportData )();
	mainVd->WindowHandle			= window;
	mainVd->Owned					= false;
	mainViewport->PlatformUserData	= mainVd;

	ImGuiPlatformIO& platformIO		 = ImGui::GetPlatformIO();
	platformIO.Platform_CreateWindow = ImGui_ImplX11_CreateWindow;
	platformIO.Platform_DestroyWindow = ImGui_ImplX11_DestroyWindow;
	platformIO.Platform_ShowWindow	 = ImGui_ImplX11_ShowWindow;
	platformIO.Platform_SetWindowPos = ImGui_ImplX11_SetWindowPos;
	platformIO.Platform_GetWindowPos = ImGui_ImplX11_GetWindowPos;
	platformIO.Platform_SetWindowSize = ImGui_ImplX11_SetWindowSize;
	platformIO.Platform_GetWindowSize = ImGui_ImplX11_GetWindowSize;
	platformIO.Platform_SetWindowFocus = ImGui_ImplX11_SetWindowFocus;
	platformIO.Platform_GetWindowFocus = ImGui_ImplX11_GetWindowFocus;
	platformIO.Platform_GetWindowMinimized = ImGui_ImplX11_GetWindowMinimized;
	platformIO.Platform_SetWindowTitle = ImGui_ImplX11_SetWindowTitle;

	ImGui_ImplX11_UpdateMonitors();
	return true;
}

static void ImGui_ImplX11_Shutdown()
{
	ImGuiViewport* mainViewport = ImGui::GetMainViewport();
	if ( mainViewport && mainViewport->PlatformUserData )
	{
		IM_DELETE( static_cast<ImGui_ImplX11_ViewportData*>( mainViewport->PlatformUserData ) );
		mainViewport->PlatformUserData = nullptr;
	}
	s_X11Display = nullptr;
	s_X11Window	 = 0;
}

static void ImGui_ImplX11_NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	if ( !s_X11Display || !s_X11Window )
		return;

	if ( ImGui::GetPlatformIO().Monitors.empty() )
		ImGui_ImplX11_UpdateMonitors();

	XWindowAttributes attrs;
	XGetWindowAttributes( s_X11Display, s_X11Window, &attrs );
	io.DisplaySize = ImVec2( (float)attrs.width, (float)attrs.height );

	s_X11Timer.updateTimer();
	io.DeltaTime = s_X11Timer.getDeltaTime();
	if ( io.DeltaTime <= 0.0f )
		io.DeltaTime = 0.00001f;

	Window		 root_return, child_return;
	int			 root_x, root_y, win_x, win_y;
	unsigned int mask_return;
	if ( XQueryPointer( s_X11Display, s_X11Window, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask_return ) )
	{
		io.AddMousePosEvent( (float)win_x, (float)win_y );
		io.AddMouseButtonEvent( 0, ( mask_return & Button1Mask ) != 0 );
		io.AddMouseButtonEvent( 1, ( mask_return & Button3Mask ) != 0 );
		io.AddMouseButtonEvent( 2, ( mask_return & Button2Mask ) != 0 );
	}
}

static bool ImGui_ImplX11_ProcessEvent( XEvent* event )
{
	ImGuiIO& io = ImGui::GetIO();
	switch ( event->type )
	{
		case KeyPress:
		case KeyRelease:
		{
			bool   is_down = ( event->type == KeyPress );
			KeySym sym	   = XLookupKeysym( &event->xkey, 0 );

			if ( is_down && sym >= 0x20 && sym <= 0x7E )
				io.AddInputCharacter( (unsigned int)sym );
			return io.WantCaptureKeyboard;
		}
		case ButtonPress:
		case ButtonRelease:
		{
			bool is_down = ( event->type == ButtonPress );
			int	 button	 = 0;
			if ( event->xbutton.button == Button1 )
				button = 0;
			else if ( event->xbutton.button == Button3 )
				button = 1;
			else if ( event->xbutton.button == Button2 )
				button = 2;
			else if ( event->xbutton.button == Button4 )
			{
				if ( is_down )
					io.AddMouseWheelEvent( 0, 1.0f );
				return io.WantCaptureMouse;
			}
			else if ( event->xbutton.button == Button5 )
			{
				if ( is_down )
					io.AddMouseWheelEvent( 0, -1.0f );
				return io.WantCaptureMouse;
			}
			io.AddMouseButtonEvent( button, is_down );
			return io.WantCaptureMouse;
		}
	}
	return false;
}
#endif

namespace sw
{
	class ImGuiPlatformBackend : public IImGuiPlatformBackend
	{
	public:
		bool initialize( IWindow* window, RHIBackend backendType ) override
		{
#if defined( SW_PLATFORM_WINDOWS )
			if ( !window )
				return false;
			HWND hWnd = static_cast<HWND>( window->getNativeHandle() );
			if ( backendType == RHIBackend::OpenGL )
				return ImGui_ImplWin32_InitForOpenGL( hWnd );
			else
				return ImGui_ImplWin32_Init( hWnd );
#elif defined( SW_PLATFORM_MACOS )
			if ( !window )
				return false;
			void* nsView = window->getNativeHandle();
			return ImGui_ImplOSX_Init( nsView );
#elif defined( SW_PLATFORM_LINUX )
			if ( !window )
				return false;
			Display* display = static_cast<Display*>( window->getNativeDisplay() );
			Window	 win	 = static_cast<Window>( reinterpret_cast<uintptr_t>( window->getNativeHandle() ) );
			(void)backendType;
			return ImGui_ImplX11_Init( display, win );
#else
			(void)window;
			(void)backendType;
			SW_LOG_ERROR( "Unsupported Platform Backend!" );
			return false;
#endif
		}

		void shutdown() override
		{
#if defined( SW_PLATFORM_WINDOWS )
			ImGui_ImplWin32_Shutdown();
#elif defined( SW_PLATFORM_MACOS )
			ImGui_ImplOSX_Shutdown();
#elif defined( SW_PLATFORM_LINUX )
			ImGui_ImplX11_Shutdown();
#endif
		}

		void newFrame() override
		{
#if defined( SW_PLATFORM_WINDOWS )
			ImGui_ImplWin32_NewFrame();
#elif defined( SW_PLATFORM_MACOS )

			ImGui_ImplOSX_NewFrame( nullptr );
#elif defined( SW_PLATFORM_LINUX )
			ImGui_ImplX11_NewFrame();
#endif
		}

		bool processEvent( const NativeWindowEvent& event ) override
		{
#if defined( SW_PLATFORM_WINDOWS )
			HWND   hWnd = static_cast<HWND>( event.nativeWindow );
			UINT   uMsg = static_cast<UINT>( event.message );
			WPARAM wp	= static_cast<WPARAM>( event.wParam );
			LPARAM lp	= static_cast<LPARAM>( event.lParam );
			return ImGui_ImplWin32_WndProcHandler( hWnd, uMsg, wp, lp ) != 0;
#elif defined( SW_PLATFORM_MACOS )
			return ImGui_ImplOSX_HandleEvent( reinterpret_cast<void*>( static_cast<uintptr_t>( event.message ) ), event.nativeWindow );
#elif defined( SW_PLATFORM_LINUX )
			if ( event.message == 0x8001 )
			{
				XEvent* xevent = reinterpret_cast<XEvent*>( event.lParam );
				return ImGui_ImplX11_ProcessEvent( xevent );
			}
			return false;
#else
			(void)event;
			return false;
#endif
		}
	};

	std::unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackend()
	{
		return std::make_unique<ImGuiPlatformBackend>();
	}
} // namespace sw
