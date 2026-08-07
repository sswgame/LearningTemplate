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

#include "Core/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Core/Common/Common.h"

namespace sw
{
	bool ImGuiVulkanRendererBackend::initialize( class IRHIDevice* rhiDevice )
	{
		auto vkDevice = dynamic_cast<VulkanRHIDevice*>( rhiDevice );
		if ( vkDevice == nullptr )
			return false;

		_device = vkDevice->getDevice();

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

		ImGui_ImplVulkan_InitInfo init_info	   = {};
		init_info.Instance					   = vkDevice->getInstance();
		init_info.PhysicalDevice			   = vkDevice->getPhysicalDevice();
		init_info.Device					   = vkDevice->getDevice();
		init_info.QueueFamily				   = 0;
		init_info.Queue						   = vkDevice->getGraphicsQueue();
		init_info.PipelineCache				   = VK_NULL_HANDLE;
		init_info.DescriptorPool			   = _imguiDescriptorPool;
		init_info.PipelineInfoMain.RenderPass  = vkDevice->getRenderPass();
		init_info.PipelineInfoMain.Subpass	   = 0;
		init_info.MinImageCount				   = 2;
		init_info.ImageCount				   = 2;
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

			VkXlibSurfaceCreateInfoKHR create_info = {};
			create_info.sType					   = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
			create_info.dpy						   = (Display*)vp->PlatformHandle;
			create_info.window					   = (Window)(uintptr_t)vp->PlatformHandleRaw;
			VkResult err						   = vkCreateXlibSurfaceKHR( reinterpret_cast<VkInstance>( vk_inst ), &create_info, static_cast<const VkAllocationCallbacks*>( vk_allocators ), reinterpret_cast<VkSurfaceKHR*>( out_vk_surface ) );
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
		{
			return false;
		}

		return true;
	}

	void ImGuiVulkanRendererBackend::shutdown()
	{
		if ( _device )
			vkDeviceWaitIdle( _device );

		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplVulkan_Shutdown();
		if ( _imguiDescriptorPool && _device )
		{
			vkDestroyDescriptorPool( _device, _imguiDescriptorPool, nullptr );
			_imguiDescriptorPool = nullptr;
		}
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
}
