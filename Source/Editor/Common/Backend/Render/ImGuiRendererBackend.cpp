#include "pch.h"

#include "Editor/Common/Backend/IImGuiRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX11RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiDX12RendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiOpenGLRendererBackend.h"
#include "Editor/Common/Backend/Render/ImGuiVulkanRendererBackend.h"

namespace sw::editor
{
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
