#include "pch.h"

#include "Editor/Backend/IImGuiPlatformBackend.h"

#include "Engine/Window/IWindow.h"
#include "Engine/Window/NativeWindowEvent.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include <imgui.h>
	#include <imgui_impl_win32.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );
#elif defined( SW_PLATFORM_MACOS )
	#include <imgui.h>

bool ImGui_ImplOSX_Init( void* pView );
void ImGui_ImplOSX_Shutdown();
void ImGui_ImplOSX_NewFrame( void* pView );
bool ImGui_ImplOSX_HandleEvent( void* pEvent, void* pView );
#elif defined( SW_PLATFORM_LINUX )
	#include "Editor/Backend/Platform/ImGuiX11Platform.h"
#endif

namespace sw
{
	class ImGuiPlatformBackend : public IImGuiPlatformBackend
	{
	public:
		bool initialize( IWindow* pWindow, RHIBackend backendType ) override
		{
#if defined( SW_PLATFORM_WINDOWS )
			if ( pWindow == nullptr )
				return false;
			HWND hWnd = static_cast<HWND>( pWindow->getNativeHandle() );
			if ( backendType == RHIBackend::OpenGL )
				return ImGui_ImplWin32_InitForOpenGL( hWnd );
			return ImGui_ImplWin32_Init( hWnd );
#elif defined( SW_PLATFORM_MACOS )
			if ( pWindow == nullptr )
				return false;
			(void)backendType;
			return ImGui_ImplOSX_Init( pWindow->getNativeHandle() );
#elif defined( SW_PLATFORM_LINUX )
			if ( pWindow == nullptr )
				return false;
			Display* pDisplay = static_cast<Display*>( pWindow->getNativeDisplay() );
			Window	 win	  = static_cast<Window>( reinterpret_cast<uintptr_t>( pWindow->getNativeHandle() ) );
			(void)backendType;
			return ImGui_ImplX11_Init( pDisplay, win );
#else
			(void)pWindow;
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
			HWND   hWnd = static_cast<HWND>( event._pNativeWindow );
			UINT   uMsg = event._message;
			WPARAM wp	= event._wParam;
			LPARAM lp	= event._lParam;
			return ImGui_ImplWin32_WndProcHandler( hWnd, uMsg, wp, lp ) != 0;
#elif defined( SW_PLATFORM_MACOS )
			return ImGui_ImplOSX_HandleEvent( reinterpret_cast<void*>( static_cast<uintptr_t>( event._message ) ), event._pNativeWindow );
#elif defined( SW_PLATFORM_LINUX )
			if ( event._message == NativeWindowEvent::kMessageX11 )
			{
				XEvent* pXEvent = reinterpret_cast<XEvent*>( event._lParam );
				return ImGui_ImplX11_ProcessEvent( pXEvent );
			}
			return false;
#else
			(void)event;
			return false;
#endif
		}
	};

	unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackend()
	{
		return make_unique<ImGuiPlatformBackend>();
	}
} // namespace sw
