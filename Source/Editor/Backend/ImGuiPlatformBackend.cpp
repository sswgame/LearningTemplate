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
	#include <X11/keysym.h>
	#include "Core/Utility/Time/EngineTimer.h"

static Display*		s_X11Display = nullptr;
static Window		s_X11Window	 = 0;
static sw::CpuTimer s_X11Timer;

static bool ImGui_ImplX11_Init( Display* display, Window window )
{
	s_X11Display = display;
	s_X11Window	 = window;
	s_X11Timer.resetTimer();
	s_X11Timer.startTimer();
	ImGuiIO& io			   = ImGui::GetIO();
	io.BackendPlatformName = "imgui_impl_x11";
	return true;
}

static void ImGui_ImplX11_Shutdown()
{
	s_X11Display = nullptr;
	s_X11Window	 = 0;
}

static void ImGui_ImplX11_NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();
	if ( !s_X11Display || !s_X11Window )
		return;

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
