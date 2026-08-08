/**
 * @file ImGuiPlatformBackend.cpp
 * @brief ImGui 플랫폼 백엔드 디스패치 (Win32 / OSX / X11)
 */
#include "IImGuiPlatformBackend.h"
#include "Core/Window/IWindow.h"
#include "Core/Window/NativeWindowEvent.h"
#include "Core/Graphics/RHI/RHITypes.h"
#include "Core/Utility/Log/Logger.h"

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
	#include "Platform/ImGuiX11Platform.h"
#endif

namespace sw
{
	class ImGuiPlatformBackend : public IImGuiPlatformBackend
	{
	public:
		bool initialize( IWindow* window, RHIBackend backendType ) override
		{
#if defined( SW_PLATFORM_WINDOWS )
			if ( window == nullptr )
				return false;
			HWND hWnd = static_cast<HWND>( window->getNativeHandle() );
			if ( backendType == RHIBackend::OpenGL )
				return ImGui_ImplWin32_InitForOpenGL( hWnd );
			return ImGui_ImplWin32_Init( hWnd );
#elif defined( SW_PLATFORM_MACOS )
			if ( window == nullptr )
				return false;
			(void)backendType;
			return ImGui_ImplOSX_Init( window->getNativeHandle() );
#elif defined( SW_PLATFORM_LINUX )
			if ( window == nullptr )
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
			if ( event.message == NativeWindowEvent::kMessageX11 )
			{
				XEvent* xEvent = reinterpret_cast<XEvent*>( event.lParam );
				return ImGui_ImplX11_ProcessEvent( xEvent );
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
