#include "pch.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#if defined( SW_PLATFORM_WINDOWS )
	#include "Engine/Window/IWindow.h"
	#include "Engine/Window/NativeWindowEvent.h"

	#include <imgui.h>
	#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

namespace sw::editor
{
	class ImGuiWin32PlatformBackend : public IImGuiPlatformBackend
	{
	public:
		bool initialize( IWindow* pWindow, RHIBackend backendType ) override
		{
			if ( pWindow == nullptr )
				return false;

			HWND hWnd = static_cast<HWND>( pWindow->getNativeHandle() );
			if ( backendType == RHIBackend::OpenGL )
				return ImGui_ImplWin32_InitForOpenGL( hWnd );
			return ImGui_ImplWin32_Init( hWnd );
		}

		void shutdown() override
		{
			ImGui_ImplWin32_Shutdown();
		}

		void newFrame() override
		{
			ImGui_ImplWin32_NewFrame();
		}

		bool processEvent( const NativeWindowEvent& event ) override
		{
			HWND   hWnd = static_cast<HWND>( event._pNativeWindow );
			UINT   uMsg = event._message;
			WPARAM wp	= event._wParam;
			LPARAM lp	= event._lParam;
			return ImGui_ImplWin32_WndProcHandler( hWnd, uMsg, wp, lp ) != 0;
		}
	};

	unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackend()
	{
		return make_unique<ImGuiWin32PlatformBackend>();
	}
} // namespace sw::editor
#endif
