/**
 * @file ImGuiDX11RendererBackend.cpp
 * @brief Auto-generated documentation header
 */
#include "ImGuiDX11RendererBackend.h"
#include <imgui.h>

#if defined( SW_PLATFORM_WINDOWS )
	#include <d3d11.h>
	#include <imgui_impl_dx11.h>
#endif

#include "Core/Graphics/RHI/IRHIDevice.h"

namespace sw
{
	bool ImGuiDX11RendererBackend::initialize( class IRHIDevice* rhiDevice )
	{
#if defined( SW_PLATFORM_WINDOWS )
		ID3D11Device*		 device	 = static_cast<ID3D11Device*>( rhiDevice->getNativeDevice() );
		ID3D11DeviceContext* context = static_cast<ID3D11DeviceContext*>( rhiDevice->getNativeContext() );
		if ( device != nullptr && context != nullptr )
			return ImGui_ImplDX11_Init( device, context );
#else
		(void)rhiDevice;
#endif
		return true;
	}

	void ImGuiDX11RendererBackend::shutdown()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplDX11_Shutdown();
#endif
	}

	void ImGuiDX11RendererBackend::newFrame()
	{
#if defined( SW_PLATFORM_WINDOWS )
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplDX11_NewFrame();
#endif
	}

	void ImGuiDX11RendererBackend::render( class IRHIDevice* rhiDevice )
	{
		(void)rhiDevice;
#if defined( SW_PLATFORM_WINDOWS )
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
#endif
	}
}
