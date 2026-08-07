/**
 * @file ConsolePanel.cpp
 */
#include "Panels/ConsolePanel.h"
#include "Core/Graphics/Material/Material.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/Shader/ShaderReflection.h"
#include <imgui.h>

namespace sw
{
	void ConsolePanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted( "[LIVE CODING] EditorModule.dll loaded via shadow copy (triggerReload to hot-reload)." );
		if ( ctx.rhiDevice )
			ImGui::Text( "[INFO] RHI Device Type: %s", ctx.rhiDevice->getBackendName() );

		if ( ctx.material )
			ImGui::Text( "[BINDLESS] Material Descriptor Index: %u", ctx.material->getDescriptorIndex() );

		if ( ctx.reflectionData && ctx.reflectionData->_constantBuffers.empty() == false )
		{
			const ShaderBufferInfo& cb = ctx.reflectionData->_constantBuffers[0];
			ImGui::Text( "[REFLECTION] CBuffer: %s (Size: %u bytes)", cb._name.c_str(), cb._totalSize );
		}
		ImGui::TextUnformatted( "[RHI COMPUTE] Direct Dispatch (4x1x1) & Indirect Command (drawIndirect) Ready." );

		ImGui::End();
	}
}
