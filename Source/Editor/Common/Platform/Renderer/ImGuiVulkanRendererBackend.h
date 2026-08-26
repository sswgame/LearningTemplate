/**
 * @file ImGuiVulkanRendererBackend.h
 * @brief ImGui Vulkan 렌더러 백엔드
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Container/unordered_map.h"

#include "Editor/Common/Platform/IImGuiRendererBackend.h"

struct VkDescriptorPool_T;
struct VkDevice_T;
struct VkSampler_T;

namespace sw
{
	class IRHIDevice;
} // namespace sw

namespace sw::editor
{
	/** @brief ImGui Vulkan 렌더러 (디스크립터 풀 · 샘플러) */
	class ImGuiVulkanRendererBackend : public IImGuiRendererBackend
	{
	public:
		// ------------------------------------------------------------------------------
		// 1) 생명주기 — 생성은 기본값, GPU 리소스는 initialize / shutdown
		// ------------------------------------------------------------------------------
		/** @brief 멤버만 기본값으로 둡니다. 실제 생성은 initialize()에서 합니다. */
		ImGuiVulkanRendererBackend() = default;
		/** @brief 리소스는 shutdown()에서 해제합니다. */
		virtual ~ImGuiVulkanRendererBackend() override = default;

		/** @brief Vulkan ImGui 렌더러를 초기화합니다. */
		bool initialize( IRHIDevice* pRhiDevice ) override;
		/** @brief Vulkan ImGui 렌더러를 종료합니다. */
		void shutdown() override;

		// ------------------------------------------------------------------------------
		// 2) IImGuiRendererBackend — 프레임/텍스처
		// ------------------------------------------------------------------------------
		/** @brief ImGui Vulkan 프레임을 시작합니다. */
		void newFrame() override;
		/** @brief ImGui draw data를 Vulkan으로 그립니다. */
		void render( IRHIDevice* pRhiDevice ) override;

		/** @brief RHI 텍스처를 ImGui용으로 등록합니다. */
		void* registerTexture( RHITextureHandle texture ) override;
		/** @brief 등록된 ImGui 텍스처를 해제합니다. */
		void unregisterTexture( void* pTextureID ) override;

	private:
		VkDescriptorPool_T*					   _pImguiDescriptorPool{ nullptr };
		VkDevice_T*							   _pDevice{ nullptr };
		VkSampler_T*						   _pSampler{ nullptr };
		IRHIDevice*							   _pRHIDevice{ nullptr };
		unordered_map<void*, RHITextureHandle> _mapTextureIds;
	};
} // namespace sw::editor
