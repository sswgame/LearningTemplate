#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiVulkanRendererBackend.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#if defined( SW_PLATFORM_WINDOWS )
	#include <vulkan/vulkan_win32.h>
#elif defined( SW_PLATFORM_LINUX )
	#include <X11/Xlib-xcb.h>
	#include <vulkan/vulkan_xcb.h>
	#include <vulkan/vulkan_xlib.h>
	#include <xcb/xcb.h>
#elif defined( SW_PLATFORM_MACOS )
	#include <vulkan/vulkan_metal.h>
#endif

namespace sw::editor
{
	namespace
	{
		struct ImGuiVulkanRendererBackendInternal
		{
			inline static void ( *s_OrigVkCreateWindow )( ImGuiViewport* )			= nullptr;
			inline static void ( *s_OrigVkSetWindowSize )( ImGuiViewport*, ImVec2 ) = nullptr;

			static void GuardedVkCreateWindow( ImGuiViewport* pViewport )
			{
				if ( pViewport == nullptr || s_OrigVkCreateWindow == nullptr )
					return;

				if ( pViewport->Size.x < 1.0f )
					pViewport->Size.x = 1.0f;
				if ( pViewport->Size.y < 1.0f )
					pViewport->Size.y = 1.0f;

				s_OrigVkCreateWindow( pViewport );
			}

			static void GuardedVkSetWindowSize( ImGuiViewport* pViewport, ImVec2 size )
			{
				if ( pViewport == nullptr || s_OrigVkSetWindowSize == nullptr )
					return;

				if ( size.x < 1.0f || size.y < 1.0f )
					return;

				s_OrigVkSetWindowSize( pViewport, size );
			}

			static void installVulkanViewportGuards()
			{
				ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
				if ( platformIO.Renderer_CreateWindow != nullptr && platformIO.Renderer_CreateWindow != &GuardedVkCreateWindow )
				{
					s_OrigVkCreateWindow			 = platformIO.Renderer_CreateWindow;
					platformIO.Renderer_CreateWindow = &GuardedVkCreateWindow;
				}
				if ( platformIO.Renderer_SetWindowSize != nullptr && platformIO.Renderer_SetWindowSize != &GuardedVkSetWindowSize )
				{
					s_OrigVkSetWindowSize			  = platformIO.Renderer_SetWindowSize;
					platformIO.Renderer_SetWindowSize = &GuardedVkSetWindowSize;
				}
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "ImGuiVulkan" );

	bool ImGuiVulkanRendererBackend::initialize( class IRHIDevice* pRhiDevice )
	{
		if ( pRhiDevice == nullptr )
			return false;

		RHIVulkanImGuiNative vkNative{};
		if ( pRhiDevice->queryVulkanImGuiNative( vkNative ) == false || vkNative._pDevice == nullptr )
			return false;

		_pRHIDevice = pRhiDevice;
		_pDevice	= static_cast<VkDevice>( vkNative._pDevice );

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
		pool_info.poolSizeCount				 = static_cast<uint32>( IM_ARRAYSIZE( pool_sizes ) );
		pool_info.pPoolSizes				 = pool_sizes;
		if ( vkCreateDescriptorPool( _pDevice, &pool_info, nullptr, &_pImguiDescriptorPool ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create Vulkan descriptor pool for ImGui" );
			return false;
		}

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType		  = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter	  = VK_FILTER_LINEAR;
		samplerInfo.minFilter	  = VK_FILTER_LINEAR;
		samplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.maxLod		  = 1000.0f;
		if ( vkCreateSampler( _pDevice, &samplerInfo, nullptr, &_pSampler ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create Vulkan sampler for ImGui textures" );
			return false;
		}

		const uint32 imageCount = vkNative._imageCount >= 2 ? vkNative._imageCount : 2;
		const uint32 minImages	= vkNative._minImageCount >= 2 ? vkNative._minImageCount : 2;

		ImGui_ImplVulkan_InitInfo init_info	   = {};
		init_info.Instance					   = static_cast<VkInstance>( vkNative._pInstance );
		init_info.PhysicalDevice			   = static_cast<VkPhysicalDevice>( vkNative._pPhysicalDevice );
		init_info.Device					   = _pDevice;
		init_info.QueueFamily				   = vkNative._queueFamily;
		init_info.Queue						   = static_cast<VkQueue>( vkNative._pGraphicsQueue );
		init_info.PipelineCache				   = VK_NULL_HANDLE;
		init_info.DescriptorPool			   = _pImguiDescriptorPool;
		init_info.PipelineInfoMain.RenderPass  = static_cast<VkRenderPass>( vkNative._pRenderPass );
		init_info.PipelineInfoMain.Subpass	   = 0;
		init_info.MinImageCount				   = minImages;
		init_info.ImageCount				   = imageCount;
		init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.Allocator					   = nullptr;
		init_info.CheckVkResultFn			   = nullptr;

		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
#if defined( SW_PLATFORM_WINDOWS )
		platform_io.Platform_CreateVkSurface = []( ImGuiViewport* pVp, ImU64 vk_inst, const void* pVkAllocators, ImU64* pOutVkSurface ) -> int32
		{
			VkWin32SurfaceCreateInfoKHR create_info = {};
			create_info.sType						= VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			create_info.hwnd						= static_cast<HWND>( pVp->PlatformHandleRaw );
			create_info.hinstance					= GetModuleHandle( nullptr );
			VkResult err							= vkCreateWin32SurfaceKHR( reinterpret_cast<VkInstance>( vk_inst ), &create_info, static_cast<const VkAllocationCallbacks*>( pVkAllocators ), reinterpret_cast<VkSurfaceKHR*>( pOutVkSurface ) );
			return err;
		};
#elif defined( SW_PLATFORM_LINUX )
		platform_io.Platform_CreateVkSurface = []( ImGuiViewport* pVp, ImU64 vk_inst, const void* pVkAllocators, ImU64* pOutVkSurface ) -> int32
		{
			const VkInstance			 instance	 = reinterpret_cast<VkInstance>( vk_inst );
			const VkAllocationCallbacks* pAllocators = static_cast<const VkAllocationCallbacks*>( pVkAllocators );
			Display*					 pDpy		 = static_cast<Display*>( pVp->PlatformHandle );
			const xcb_window_t			 window		 = static_cast<xcb_window_t>( reinterpret_cast<uintptr_t>( pVp->PlatformHandleRaw ) );

			PFN_vkCreateXlibSurfaceKHR pCreateXlib = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
				vkGetInstanceProcAddr( instance, "vkCreateXlibSurfaceKHR" ) );
			if ( pCreateXlib != nullptr )
			{
				VkXlibSurfaceCreateInfoKHR create_info = {};
				create_info.sType					   = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
				create_info.dpy						   = pDpy;
				create_info.window					   = static_cast<Window>( window );
				return static_cast<int32>( pCreateXlib( instance, &create_info, pAllocators, reinterpret_cast<VkSurfaceKHR*>( pOutVkSurface ) ) );
			}

			PFN_vkCreateXcbSurfaceKHR pCreateXcb = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
				vkGetInstanceProcAddr( instance, "vkCreateXcbSurfaceKHR" ) );
			if ( pCreateXcb == nullptr || pDpy == nullptr )
				return static_cast<int32>( VK_ERROR_EXTENSION_NOT_PRESENT );

			xcb_connection_t* pConnection = XGetXCBConnection( pDpy );
			if ( pConnection == nullptr )
				return static_cast<int32>( VK_ERROR_INITIALIZATION_FAILED );

			VkXcbSurfaceCreateInfoKHR create_info = {};
			create_info.sType					  = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
			create_info.connection				  = pConnection;
			create_info.window					  = window;
			return static_cast<int32>( pCreateXcb( instance, &create_info, pAllocators, reinterpret_cast<VkSurfaceKHR*>( pOutVkSurface ) ) );
		};
#elif defined( SW_PLATFORM_MACOS )
		platform_io.Platform_CreateVkSurface = []( ImGuiViewport* pVp, ImU64 vk_inst, const void* pVkAllocators, ImU64* pOutVkSurface ) -> int32
		{
			VkMetalSurfaceCreateInfoEXT create_info = {};
			create_info.sType						= VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
			create_info.pLayer						= pVp->PlatformHandleRaw;
			VkResult err							= vkCreateMetalSurfaceEXT( reinterpret_cast<VkInstance>( vk_inst ), &create_info, static_cast<const VkAllocationCallbacks*>( pVkAllocators ), reinterpret_cast<VkSurfaceKHR*>( pOutVkSurface ) );
			return static_cast<int32>( err );
		};
#endif

		if ( ImGui_ImplVulkan_Init( &init_info ) == false )
			return false;

		ImGuiVulkanRendererBackendInternal::installVulkanViewportGuards();
		return true;
	}

	void ImGuiVulkanRendererBackend::shutdown()
	{
		if ( _pDevice != nullptr )
			vkDeviceWaitIdle( _pDevice );

		for ( pair<void* const, RHITextureHandle>& pair : _mapTextureId )
		{
			if ( pair.first != nullptr )
				ImGui_ImplVulkan_RemoveTexture( static_cast<VkDescriptorSet>( pair.first ) );
		}
		_mapTextureId.clear();

		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplVulkan_Shutdown();

		if ( _pSampler != nullptr && _pDevice != nullptr )
		{
			vkDestroySampler( _pDevice, _pSampler, nullptr );
			_pSampler = nullptr;
		}

		if ( _pImguiDescriptorPool != nullptr && _pDevice != nullptr )
		{
			vkDestroyDescriptorPool( _pDevice, _pImguiDescriptorPool, nullptr );
			_pImguiDescriptorPool = nullptr;
		}

		_pDevice	= nullptr;
		_pRHIDevice = nullptr;
	}

	void ImGuiVulkanRendererBackend::newFrame()
	{
		if ( ImGui::GetIO().BackendRendererUserData != nullptr )
			ImGui_ImplVulkan_NewFrame();
	}

	void ImGuiVulkanRendererBackend::processTextureUpdates()
	{
		if ( ImGui::GetIO().BackendRendererUserData == nullptr )
			return;

		// ImGui_ImplVulkan_RenderDrawData 가 draw_data->Textures 를 순회하며 하던 일을 여기(UI 스레드)서 끝낸다.
		// 스냅샷은 Textures 를 공유하지 않으므로 렌더 스레드가 그리기 전에 텍스처를 준비해 둬야 한다.
		for ( ImTextureData* pTexture : ImGui::GetPlatformIO().Textures )
		{
			if ( pTexture != nullptr && pTexture->Status != ImTextureStatus_OK )
				ImGui_ImplVulkan_UpdateTexture( pTexture );
		}
	}

	void ImGuiVulkanRendererBackend::render( class IRHIDevice* pRhiDevice, ImDrawData* pDrawData )
	{
		VkCommandBuffer cmdBuffer = static_cast<VkCommandBuffer>( pRhiDevice->getNativeContext() );
		if ( cmdBuffer != nullptr && pDrawData != nullptr && _pRHIDevice != nullptr )
			ImGui_ImplVulkan_RenderDrawData( pDrawData, cmdBuffer );
	}

	void* ImGuiVulkanRendererBackend::registerTexture( RHITextureHandle texture )
	{
		if ( texture == 0 || _pRHIDevice == nullptr || _pSampler == nullptr )
			return nullptr;

		void* pImageViewPtr{ nullptr };
		if ( _pRHIDevice->queryVulkanTextureView( texture, pImageViewPtr ) == false || pImageViewPtr == nullptr )
		{
			SW_LOG_ERROR( "Failed to resolve VkImageView for RHI handle %#", texture );
			return nullptr;
		}

		VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(
			_pSampler,
			static_cast<VkImageView>( pImageViewPtr ),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );
		if ( set == VK_NULL_HANDLE )
			return nullptr;

		auto textureID			 = set;
		_mapTextureId[textureID] = texture;
		return textureID;
	}

	void ImGuiVulkanRendererBackend::unregisterTexture( void* pTextureID )
	{
		if ( pTextureID == nullptr )
			return;

		auto it = _mapTextureId.find( pTextureID );
		if ( it == _mapTextureId.end() )
			return;

		ImGui_ImplVulkan_RemoveTexture( static_cast<VkDescriptorSet>( pTextureID ) );
		_mapTextureId.erase( it );
	}
} // namespace sw::editor
