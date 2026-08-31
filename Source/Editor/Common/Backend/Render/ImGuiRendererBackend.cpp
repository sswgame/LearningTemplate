#include "pch.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX11RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX12RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiOpenGLRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiVulkanRendererBackend.h"

#include <imgui.h>

namespace sw::editor
{
	void IImGuiRendererBackend::updatePendingTextures( void ( *pUpdateTexture )( ImTextureData* ) )
	{
		if ( pUpdateTexture == nullptr || ImGui::GetIO().BackendRendererUserData == nullptr )
			return;

		for ( ImTextureData* pTexture : ImGui::GetPlatformIO().Textures )
		{
			if ( pTexture != nullptr && pTexture->Status != ImTextureStatus_OK )
				pUpdateTexture( pTexture );
		}
	}

	unique_ptr<IImGuiRendererBackend> IImGuiRendererBackend::createRendererBackend( RHIBackend backend )
	{
		switch ( backend )
		{
			case RHIBackend::DirectX11:
				return make_unique<ImGuiDX11RendererBackend>();
			case RHIBackend::DirectX12:
				return make_unique<ImGuiDX12RendererBackend>();
			case RHIBackend::OpenGL:
				return make_unique<ImGuiOpenGLRendererBackend>();
			case RHIBackend::Vulkan:
				return make_unique<ImGuiVulkanRendererBackend>();
			default:
				return make_unique<ImGuiDX11RendererBackend>();
		}
	}
} // namespace sw::editor
