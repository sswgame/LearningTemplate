/**
 * @file EngineStatusPanel.cpp
 */
#include "Panels/EngineStatusPanel.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/RHI/RHICapabilities.h"
#include <imgui.h>

namespace sw
{
	void EngineStatusPanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		const char* backendName = ( ctx.rhiDevice != nullptr ) ? ctx.rhiDevice->getBackendName() : "Unknown";
		ImGui::TextColored( ImVec4( 0.2f, 0.8f, 1.0f, 1.0f ), "Active RHI Backend: %s", backendName );
		ImGui::Separator();

		ImGui::TextUnformatted( "Supported Command Line Arguments to switch RHI Backend:" );
		ImGui::BulletText( "-DIRECTX_11  (or -dx11, -d3d11) : Direct3D 11 Backend" );
		ImGui::BulletText( "-DIRECTX_12  (or -dx12, -d3d12) : Direct3D 12 Backend" );

		const bool bVkEditor  = RHIAvailability::query( RHIBackend::Vulkan )._bEditorSupported;
		const bool bGlEditor  = RHIAvailability::query( RHIBackend::OpenGL )._bEditorSupported;
		if ( bVkEditor )
			ImGui::BulletText( "-VULKAN      (or -vk)           : Vulkan Backend" );
		else
			ImGui::BulletText( "-VULKAN      (or -vk)           : Vulkan (runtime OK; editor unsupported)" );

		if ( bGlEditor )
			ImGui::BulletText( "-OPENGL      (or -gl)           : OpenGL Backend" );
		else
			ImGui::BulletText( "-OPENGL      (or -gl)           : OpenGL (runtime OK; editor unsupported)" );

		ImGui::End();
	}
} // namespace sw
