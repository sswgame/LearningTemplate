#pragma once
/**
 * @file ImGuiVulkanRendererBackend.h
 * @brief Auto-generated documentation header
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

		/**
		 * @brief initialize 처리를 수행합니다.
		 */
		bool initialize( IRHIDevice* rhiDevice ) override;
		/**
		 * @brief shutdown 처리를 수행합니다.
		 */
		void shutdown() override;
		/**
		 * @brief newFrame 처리를 수행합니다.
		 */
		void newFrame() override;
		/**
		 * @brief render 처리를 수행합니다.
		 */
		void render( IRHIDevice* rhiDevice ) override;

		void* registerTexture( RHITextureHandle /*texture*/ ) override { return nullptr; }

	private:
		VkDescriptorPool_T* _imguiDescriptorPool = nullptr;
		VkDevice_T*			_device				 = nullptr;
	};
}
