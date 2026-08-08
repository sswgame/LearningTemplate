/**
 * @file ImGuiVulkanRendererBackend.cpp
 * @brief ImGui Vulkan 렌더러 구현
 */
#include "ImGuiVulkanRendererBackend.h"
#include <imgui.h>
#include <vulkan/vulkan.h>
#if defined( SW_PLATFORM_WINDOWS )
	#include <vulkan/vulkan_win32.h>
#elif defined( SW_PLATFORM_LINUX )
	#include <vulkan/vulkan_xlib.h>
#elif defined( SW_PLATFORM_MACOS )
	#include <vulkan/vulkan_metal.h>
#endif
#include <imgui_impl_vulkan.h>

#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Common/Common.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	bool ImGuiVulkanRendererBackend::initialize( class IRHIDevice* rhiDevice )
	{
		if ( rhiDevice == nullptr )
			return false;

		RHIVulkanImGuiNative vkNative{};
		if ( rhiDevice->queryVulkanImGuiNative( vkNative ) == false || vkNative._device == nullptr )
			return false;

		_rhiDevice = rhiDevice;
		_device	   = static_cast<VkDevice>( vkNative._device );

		VkDescriptorPoolSize pool_sizes[] = {
			{			   VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
			{		  VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
			{		  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
			{  VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
			{  VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
			{		  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
			{		  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
			{	  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
		  };
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType						 = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags						 = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets					 = 1000 * IM_ARRAYSIZE( pool_sizes );
		pool_info.poolSizeCount				 = static_cast<uint32_t>( IM_ARRAYSIZE( pool_sizes ) );
		pool_info.pPoolSizes				 = pool_sizes;
		if ( vkCreateDescriptorPool( _device, &pool_info, nullptr, &_imguiDescriptorPool ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create Vulkan descriptor pool for ImGui" );
			return false;
		}

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType		 = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter	 = VK_FILTER_LINEAR;
		samplerInfo.minFilter	 = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.maxLod		 = 1000.0f;
		if ( vkCreateSampler( _device, &samplerInfo, nullptr, &_sampler ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create Vulkan sampler for ImGui textures" );
			return false;
		}

		const uint32_t imageCount = vkNative._imageCount >= 2 ? vkNative._imageCount : 2;
		const uint32_t minImages  = vkNative._minImageCount >= 2 ? vkNative._minImageCount : 2;

		ImGui_ImplVulkan_InitInfo init_info	   = {};
		init_info.Instance					   = static_cast<VkInstance>( vkNative._instance );
		init_info.PhysicalDevice			   = static_cast<VkPhysicalDevice>( vkNative._physicalDevice );
		init_info.Device					   = _device;
		init_info.QueueFamily				   = vkNative._queueFamily;
		init_info.Queue						   = static_cast<VkQueue>( vkNative._graphicsQueue );
		init_info.PipelineCache				   = VK_NULL_HANDLE;
		init_info.DescriptorPool			   = _imguiDescriptorPool;
		init_info.PipelineInfoMain.RenderPass  = static_cast<VkRenderPass>( vkNative._renderPass );
		init_info.PipelineInfoMain.Subpass	   = 0;
		init_info.MinImageCount				   = minImages;
		init_info.ImageCount				   = imageCount;
		init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.Allocator					   = nullptr;
		init_info.CheckVkResultFn			   = nullptr;

		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
#if defined( SW_PLATFORM_WINDOWS )
		platform_io.Platform_CreateVkSurface = []( ImGuiViewport* vp, ImU64 vk_inst, const void* vk_allocators, ImU64* out_vk_surface ) -> int
		{
			VkWin32SurfaceCreateInfoKHR create_info = {};
			create_info.sType						= VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			create_info.hwnd						= static_cast<HWND>( vp->PlatformHandleRaw );
			create_info.hinstance					= GetModuleHandle( nullptr );
			VkResult err							= vkCreateWin32SurfaceKHR( reinterpret_cast<VkInstance>( vk_inst ), &create_info, static_cast<const VkAllocationCallbacks*>( vk_allocators ), reinterpret_cast<VkSurfaceKHR*>( out_vk_surface ) );
			return static_cast<int>( err );
		};
#elif defined( SW_PLATFORM_LINUX )
		platform_io.Platform_CreateVkSurface = []( ImGuiViewport* vp, ImU64 vk_inst, const void* vk_allocators, ImU64* out_vk_surface ) -> int
		{
			const VkInstance instance = reinterpret_cast<VkInstance>( vk_inst );
			auto*			 createFn = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
				vkGetInstanceProcAddr( instance, "vkCreateXlibSurfaceKHR" ) );
			if ( createFn == nullptr )
				return static_cast<int>( VK_ERROR_EXTENSION_NOT_PRESENT );

			VkXlibSurfaceCreateInfoKHR create_info = {};
			create_info.sType					   = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
			create_info.dpy						   = static_cast<Display*>( vp->PlatformHandle );
			create_info.window					   = static_cast<Window>( reinterpret_cast<uintptr_t>( vp->PlatformHandleRaw ) );
			VkResult err						   = createFn( instance, &create_info, static_cast<const VkAllocationCallbacks*>( vk_allocators ), reinterpret_cast<VkSurfaceKHR*>( out_vk_surface ) );
			return static_cast<int>( err );
		};
#elif defined( SW_PLATFORM_MACOS )
		platform_io.Platform_CreateVkSurface = []( ImGuiViewport* vp, ImU64 vk_inst, const void* vk_allocators, ImU64* out_vk_surface ) -> int
		{
			VkMetalSurfaceCreateInfoEXT create_info = {};
			create_info.sType						= VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
			create_info.pLayer						= vp->PlatformHandleRaw;
			VkResult err							= vkCreateMetalSurfaceEXT( reinterpret_cast<VkInstance>( vk_inst ), &create_info, static_cast<const VkAllocationCallbacks*>( vk_allocators ), reinterpret_cast<VkSurfaceKHR*>( out_vk_surface ) );
			return static_cast<int>( err );
		};
#endif

		if ( !ImGui_ImplVulkan_Init( &init_info ) )
			return false;

		return true;
	}

	void ImGuiVulkanRendererBackend::shutdown()
	{
		if ( _device )
			vkDeviceWaitIdle( _device );

		for ( auto& pair : _textureIds )
		{
			if ( pair.first != nullptr )
				ImGui_ImplVulkan_RemoveTexture( static_cast<VkDescriptorSet>( pair.first ) );
		}
		_textureIds.clear();

		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplVulkan_Shutdown();

		if ( _sampler && _device )
		{
			vkDestroySampler( _device, _sampler, nullptr );
			_sampler = nullptr;
		}
		if ( _imguiDescriptorPool && _device )
		{
			vkDestroyDescriptorPool( _device, _imguiDescriptorPool, nullptr );
			_imguiDescriptorPool = nullptr;
		}
		_rhiDevice = nullptr;
		_device	   = nullptr;
	}

	void* ImGuiVulkanRendererBackend::registerTexture( RHITextureHandle texture )
	{
		if ( texture == 0 || _rhiDevice == nullptr || _sampler == nullptr )
			return nullptr;

		void* imageViewPtr = nullptr;
		if ( _rhiDevice->queryVulkanTextureView( texture, imageViewPtr ) == false || imageViewPtr == nullptr )
		{
			SW_LOG_ERROR( "[ImGuiVulkan] Failed to resolve VkImageView for RHI handle %#", texture );
			return nullptr;
		}

		VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(
			_sampler,
			static_cast<VkImageView>( imageViewPtr ),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		if ( set == VK_NULL_HANDLE )
			return nullptr;

		void* textureID = reinterpret_cast<void*>( set );
		_textureIds[textureID] = texture;
		return textureID;
	}

	void ImGuiVulkanRendererBackend::unregisterTexture( void* textureID )
	{
		if ( textureID == nullptr )
			return;

		auto it = _textureIds.find( textureID );
		if ( it == _textureIds.end() )
			return;

		ImGui_ImplVulkan_RemoveTexture( static_cast<VkDescriptorSet>( textureID ) );
		_textureIds.erase( it );
	}

	void ImGuiVulkanRendererBackend::newFrame()
	{
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplVulkan_NewFrame();
	}

	void ImGuiVulkanRendererBackend::render( class IRHIDevice* rhiDevice )
	{
		VkCommandBuffer cmdBuffer = static_cast<VkCommandBuffer>( rhiDevice->getNativeContext() );
		if ( cmdBuffer != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplVulkan_RenderDrawData( ImGui::GetDrawData(), cmdBuffer );
	}
} // namespace sw
