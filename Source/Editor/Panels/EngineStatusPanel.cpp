/**
 * @file EngineStatusPanel.cpp
 */
#include "Panels/EngineStatusPanel.h"
#include "Runtime/EditorUIContext.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include <imgui.h>

namespace sw
{
	void EngineStatusPanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
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
		ImGui::BulletText( "-VULKAN      (or -vk)           : Vulkan Backend" );
		ImGui::BulletText( "-OPENGL      (or -gl)           : OpenGL Backend" );

		ImGui::End();
	}
} // namespace sw
