#include "pch.h"

#include "Editor/Backend/IImGuiRendererBackend.h"
#include "Editor/Backend/Renderer/ImGuiDX11RendererBackend.h"
#include "Editor/Backend/Renderer/ImGuiDX12RendererBackend.h"
#include "Editor/Backend/Renderer/ImGuiOpenGLRendererBackend.h"
#include "Editor/Backend/Renderer/ImGuiVulkanRendererBackend.h"

namespace sw
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
} // namespace sw
