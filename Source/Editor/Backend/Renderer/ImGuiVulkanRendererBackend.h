#pragma once
/**
 * @file ImGuiVulkanRendererBackend.h
 * @brief ImGui Vulkan 렌더러 백엔드
 */

#include "Editor/Backend/IImGuiRendererBackend.h"

struct VkDescriptorPool_T;
struct VkDevice_T;

namespace sw
{
	class IRHIDevice;

	class ImGuiVulkanRendererBackend : public IImGuiRendererBackend
	{
	public:
		ImGuiVulkanRendererBackend()		   = default;
		~ImGuiVulkanRendererBackend() override = default;

		/** @brief Vulkan ImGui 렌더러를 초기화합니다. */
		bool initialize( IRHIDevice* rhiDevice ) override;
		/** @brief Vulkan ImGui 렌더러를 종료합니다. */
		void shutdown() override;
		/** @brief ImGui Vulkan 프레임을 시작합니다. */
		void newFrame() override;
		/** @brief ImGui draw data를 Vulkan으로 그립니다. */
		void render( IRHIDevice* rhiDevice ) override;

		void* registerTexture( RHITextureHandle /*texture*/ ) override { return nullptr; }
		void  unregisterTexture( void* /*textureID*/ ) override {}

	private:
		VkDescriptorPool_T* _imguiDescriptorPool = nullptr;
		VkDevice_T*			_device				 = nullptr;
	};
} // namespace sw
