/**
 * @file InspectorPanel.cpp
 */
#include "Panels/InspectorPanel.h"
#include "Core/Graphics/Material/Material.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include <imgui.h>

namespace sw
{
	void InspectorPanel::draw( const EditorUIContext& ctx )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
		{
			ImGui::End();
			return;
		}

		if ( ctx.rhiDevice )
			ImGui::Text( "Active RHI Backend : %s", ctx.rhiDevice->getBackendName() );

		if ( ctx.playerSpeed )
			ImGui::SliderFloat( "Player Speed", ctx.playerSpeed, 0.0f, 20.0f );
		if ( ctx.clearColor )
			ImGui::ColorEdit3( "Clear Color", ctx.clearColor );

		if ( ctx.material )
			renderMaterialUI( ctx.material, ctx.rhiDevice );

		if ( ImGui::Button( "Reset Settings" ) )
		{
			if ( ctx.playerSpeed )
				*ctx.playerSpeed = 5.0f;
			if ( ctx.clearColor )
			{
				ctx.clearColor[0] = 0.12f;
				ctx.clearColor[1] = 0.15f;
				ctx.clearColor[2] = 0.18f;
			}

			if ( ctx.material && ctx.rhiDevice )
			{
				ctx.material->loadFromFile( "Material/DefaultMaterial.material" );
				ctx.material->setPropertyData( ctx.rhiDevice, 0, static_cast<uint32>( ctx.material->getBuffer().size() ),
											   ctx.material->getBuffer().data() );
			}
		}

		ImGui::End();
	}

	void InspectorPanel::renderMaterialUI( Material* material, IRHIDevice* rhiDevice )
	{
		if ( material == nullptr || rhiDevice == nullptr )
			return;

		ImGui::PushID( material );

		const auto& props  = material->getProperties();
		const auto& buffer = material->getBuffer();

		bool			   bChanged	  = false;
		std::vector<uint8> tempBuffer = buffer;

		for ( const auto& prop : props )
		{
			ImGui::PushID( prop.name.c_str() );

			if ( prop.type == MaterialPropertyType::Float )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat( prop.name.c_str(), ptr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float2 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat2( prop.name.c_str(), ptr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float3 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::DragFloat3( prop.name.c_str(), ptr, 0.01f ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float4 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				if ( ImGui::ColorEdit4( prop.name.c_str(), ptr ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Float4x4 )
			{
				float32* ptr = reinterpret_cast<float32*>( tempBuffer.data() + prop.offset );
				ImGui::Text( "%s", prop.name.c_str() );
				if ( ImGui::DragFloat4( "##r0", ptr, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r1", ptr + 4, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r2", ptr + 8, 0.01f ) )
					bChanged = true;
				if ( ImGui::DragFloat4( "##r3", ptr + 12, 0.01f ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint || prop.type == MaterialPropertyType::Int )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt( prop.name.c_str(), ptr ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint2 || prop.type == MaterialPropertyType::Int2 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt2( prop.name.c_str(), ptr ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint3 || prop.type == MaterialPropertyType::Int3 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt3( prop.name.c_str(), ptr ) )
					bChanged = true;
			}
			else if ( prop.type == MaterialPropertyType::Uint4 || prop.type == MaterialPropertyType::Int4 )
			{
				int* ptr = reinterpret_cast<int*>( tempBuffer.data() + prop.offset );
				if ( ImGui::InputInt4( prop.name.c_str(), ptr ) )
					bChanged = true;
			}

			ImGui::PopID();
		}

		if ( bChanged )
			material->setPropertyData( rhiDevice, 0, static_cast<uint32>( tempBuffer.size() ), tempBuffer.data() );

		ImGui::PopID();
	}
}
