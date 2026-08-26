#include "pch.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#if defined( SW_PLATFORM_MACOS )
	#include "Engine/Window/IWindow.h"
	#include "Engine/Window/NativeWindowEvent.h"

	#include <imgui.h>

bool ImGui_ImplOSX_Init( void* pView );
void ImGui_ImplOSX_Shutdown();
void ImGui_ImplOSX_NewFrame( void* pView );
bool ImGui_ImplOSX_HandleEvent( void* pEvent, void* pView );

namespace sw::editor
{
	class ImGuiOSXPlatformBackend : public IImGuiPlatformBackend
	{
	public:
		bool initialize( IWindow* pWindow, RHIBackend backendType ) override
		{
			if ( pWindow == nullptr )
				return false;
			(void)backendType;
			return ImGui_ImplOSX_Init( pWindow->getNativeHandle() );
		}

		void shutdown() override
		{
			ImGui_ImplOSX_Shutdown();
		}

		void newFrame() override
		{
			ImGui_ImplOSX_NewFrame( nullptr );
		}

		bool processEvent( const NativeWindowEvent& event ) override
		{
			return ImGui_ImplOSX_HandleEvent( reinterpret_cast<void*>( static_cast<uintptr_t>( event._message ) ), event._pNativeWindow );
		}
	};

	unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackend()
	{
		return make_unique<ImGuiOSXPlatformBackend>();
	}
} // namespace sw::editor
#endif
