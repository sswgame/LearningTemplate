/**
 * @file ImGuiRendererBackend.cpp
 * @brief ImGuiRendererBackend 구현
 */
#include "IImGuiRendererBackend.h"
#include "Renderer/ImGuiDX11RendererBackend.h"
#include "Renderer/ImGuiDX12RendererBackend.h"
#include "Renderer/ImGuiOpenGLRendererBackend.h"
#include "Renderer/ImGuiVulkanRendererBackend.h"

namespace sw
{
	std::unique_ptr<IImGuiRendererBackend> IImGuiRendererBackend::createRendererBackend( RHIBackend backend )
	{
		switch ( backend )
		{
			case RHIBackend::DirectX11:
				return std::make_unique<ImGuiDX11RendererBackend>();
			case RHIBackend::DirectX12:
				return std::make_unique<ImGuiDX12RendererBackend>();
			case RHIBackend::OpenGL:
				return std::make_unique<ImGuiOpenGLRendererBackend>();
			case RHIBackend::Vulkan:
				return std::make_unique<ImGuiVulkanRendererBackend>();
			default:
				return std::make_unique<ImGuiDX11RendererBackend>();
		}
	}
}
