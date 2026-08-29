#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include "Core/File/FileUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/FrameResourceRing.h"
#include "Engine/Graphics/RHI/RHIDeferredCommandList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHISwapChain.h"
#include "Engine/Graphics/Shader/ShaderCache.h"

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

namespace
{
	inline VkFormat toVulkanTextureFormat( sw::RHIFormat format )
	{
		switch ( format )
		{
			case sw::RHIFormat::R8G8B8A8_UNORM:
				return VK_FORMAT_R8G8B8A8_UNORM;
			case sw::RHIFormat::B8G8R8A8_UNORM:
				return VK_FORMAT_B8G8R8A8_UNORM;
			case sw::RHIFormat::R16G16B16A16_FLOAT:
				return VK_FORMAT_R16G16B16A16_SFLOAT;
			case sw::RHIFormat::D24_UNORM_S8_UINT:
				return VK_FORMAT_D24_UNORM_S8_UINT;
			case sw::RHIFormat::R32G32B32_FLOAT:
				return VK_FORMAT_R32G32B32_SFLOAT;
			case sw::RHIFormat::R32G32_FLOAT:
				return VK_FORMAT_R32G32_SFLOAT;
			case sw::RHIFormat::R32_FLOAT:
				return VK_FORMAT_R32_SFLOAT;
		}
		return VK_FORMAT_UNDEFINED;
	}
} // namespace

namespace sw
{
	namespace
	{
		struct VulkanRHIDeviceInternal
		{
			static bool hasExtensionVal( const vector<VkExtensionProperties>& availableExts, const utf8* pName )
			{
				for ( const VkExtensionProperties& ext : availableExts )
				{
					if ( StringUtil::strcmp( ext.extensionName, pName ) == 0 )
						return true;
				}
				return false;
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "Vulkan" );

	static const vector<const utf8*> s_listValidationLayers = {
		"VK_LAYER_KHRONOS_validation" };

	static const vector<const utf8*> s_listDeviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	[[maybe_unused]] static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* )
	{
		if ( pCallbackData->pMessage != nullptr )
		{
			if ( strstr( pCallbackData->pMessage, "image has not been acquired" ) != nullptr &&
				 strstr( pCallbackData->pMessage, "performs a layout transition" ) != nullptr )
				return VK_FALSE;
		}

		if ( messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
			SW_LOG_ERROR( "%#", pCallbackData->pMessage );
		else if ( messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
			SW_LOG_WARNING( "%#", pCallbackData->pMessage );
		else
			SW_LOG_INFO( "%#", pCallbackData->pMessage );
		return VK_FALSE;
	}

	[[maybe_unused]] static VkResult CreateDebugUtilsMessengerEXT(
		VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger )
	{
		PFN_vkCreateDebugUtilsMessengerEXT func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>( vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" ) );
		if ( func != nullptr )
			return func( instance, pCreateInfo, pAllocator, pDebugMessenger );
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}

	[[maybe_unused]] static void DestroyDebugUtilsMessengerEXT(
		VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator )
	{
		PFN_vkDestroyDebugUtilsMessengerEXT func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" ) );
		if ( func != nullptr )
			func( instance, debugMessenger, pAllocator );
	}

	VulkanRHIDevice::VulkanRHIDevice()
		: _instance{ nullptr }
		, _debugMessenger{ nullptr }
		, _surface{ nullptr }
		, _physicalDevice{ nullptr }
		, _device{ nullptr }
		, _graphicsQueue{ nullptr }
		, _graphicsQueueFamilyIndex{ 0 }
		, _swapChain{ nullptr }
		, _listSwapChainImage{}
		, _swapChainImageFormat{ 0 }
		, _swapChainExtentWidth{ 0 }
		, _swapChainExtentHeight{ 0 }
		, _listSwapChainImageView{}
		, _listSwapChainFramebuffer{}
		, _renderPass{ nullptr }
		, _offscreenRenderPass{ nullptr }
		, _commandPool{ nullptr }
		, _listCommandBuffer{}
		, _listImageAvailableSemaphore{}
		, _listRenderFinishedSemaphore{}
		, _listInFlightFence{}
		, _listImagesInFlight{}
		, _pHWnd{ nullptr }
		, _pDisplayHandle{ nullptr }
		, _currentFrame{ 0 }
		, _imageIndex{ 0 }
		, _width{ 0 }
		, _height{ 0 }
		, _bFrameStarted{ 0 }
		, _bOffscreenPassActive{ 0 }
		, _bRenderPassActive{ 0 }
#if defined( SW_DEBUG )
		, _bEnableValidationLayers{ 1 }
#else
		, _bEnableValidationLayers{ 0 }
#endif
		, _bMultiDrawIndirect{ 0 }
		, _bDrawIndirectCount{ 0 }
		, _bSwapChainDirty{ 0 }
		, _bDepthHasStencil{ 0 }
		, _linuxWsi{ 0 }
		, _depthFormat{ 0 }
		, _offscreenCommandBuffer{ nullptr }
		, _offscreenFence{ nullptr }
		, _activeOffscreenTarget{ 0 }
		, _activeGraphicsPso{ 0 }
		, _defaultSampler{ nullptr }
		, _pipelineLayout{ nullptr }
		, _descriptorSetLayout{ nullptr }
		, _uavDescriptorSetLayout{ nullptr }
		, _explicitUavDescriptorSetLayout{ nullptr }
		, _descriptorPool{ nullptr }
		, _descriptorSet{ nullptr }
		, _dummyUBO{ nullptr }
		, _dummyUBOMemory{ nullptr }
		, _pipeline{ nullptr }
		, _offscreenPipeline{ nullptr }
		, _vertexBuffer{ nullptr }
		, _bindlessFreeList{}
		, _gpuBuffers{}
		, _mapCbSlotSize{}
		, _boundMeshVb{ 0 }
		, _boundMeshStride{ sizeof( RHIVertex ) }
		, _boundMeshOffset{ 0 }
		, _boundIndexBuffer{ 0 }
		, _boundIndexStride{ 4 }
		, _boundIndexOffset{ 0 }
		, _listRegisteredDescriptorSet{}
		, _listBindlessSourceBuffer{}
		, _listRegisteredUAV{}
		, _listUavSourceBuffer{}
		, _uavFreeList{}
		, _gpuTextures{}
		, _releaseQueue{ 3 }
		, _mapCompositeFramebuffer{}
		, _mapPipelineRenderPass{}
		, _textureDescriptorSetLayout{ nullptr }
		, _listRegisteredTexture{}
		, _textureFreeList{}
		, _bindlessTextureArrayLayout{ nullptr }
		, _bindlessTextureSet{ nullptr }
		, _bindlessDummyImage{ nullptr }
		, _bindlessDummyView{ nullptr }
		, _bindlessDummyMemory{ nullptr }
		, _pipelineStates{}
		, _listRenderPass{}
		, _pipelineCache{ nullptr }
		, _immContext{ nullptr }
		, _deferredContext{ nullptr }
		, _swapChainImpl{ nullptr }
		, _resourceImpl{ nullptr }
	{
		_swapChainImpl = sw::make_unique<VulkanRHISwapChain>( this );
		_resourceImpl  = sw::make_unique<VulkanRHIResource>( this );
	}

	VulkanRHIDevice::~VulkanRHIDevice()
	{
		shutdown();
	}

	IRHISwapChain*		VulkanRHIDevice::getSwapChain() { return _swapChainImpl.get(); }
	IRHIResource*		VulkanRHIDevice::getResource() { return _resourceImpl.get(); }
	IRHICommandContext* VulkanRHIDevice::getImmediateContext() { return _immContext.get(); }
	IRHICommandContext* VulkanRHIDevice::getDeferredCommandContext() { return _deferredContext.get(); }

	bool VulkanRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
	{
		_pHWnd			= desc._pWindowHandle;
		_pDisplayHandle = desc._pWindowDisplay;
		_width			= desc._width;
		_height			= desc._height;

		BLOCK( "Validation Layer Setup" )
		{
#if defined( SW_PLATFORM_WINDOWS )
			if ( _bEnableValidationLayers )
			{
				string execDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
				if ( FileUtil::fileExists( execDir + "/VkLayer_khronos_validation.json" ) )
				{
					SetEnvironmentVariableA( "VK_ADD_LAYER_PATH", execDir.c_str() );
					SetEnvironmentVariableA( "VK_LAYER_PATH", execDir.c_str() );
				}
				else
				{
					const utf8* pVulkanSdkEnv = std::getenv( "VULKAN_SDK" );
					if ( pVulkanSdkEnv != nullptr && StringUtil::strlen( pVulkanSdkEnv ) > 0 )
					{
						string sdkBinPath = string( pVulkanSdkEnv ) + "/Bin";
						SetEnvironmentVariableA( "VK_ADD_LAYER_PATH", sdkBinPath.c_str() );
					}
				}
			}
#endif

			if ( _bEnableValidationLayers && checkValidationLayerSupport() == false )
			{
				SW_LOG_INFO( "Vulkan Validation Layers requested, but VK_LAYER_KHRONOS_validation was not found (Validation Layers: DISABLED)" );
				_bEnableValidationLayers = false;
			}
		}

		BLOCK( "Instance / Surface / Logical Device" )
		{
			if ( createInstance() == false )
				return false;

			setupDebugMessenger();

			if ( createSurface() == false )
				return false;

			if ( pickPhysicalDevice() == false )
				return false;

			if ( selectDepthFormat() == false )
			{
				SW_LOG_ERROR( "No supported depth/stencil format on this GPU." );
				return false;
			}

			if ( createLogicalDevice() == false )
				return false;
		}

		BLOCK( "Swapchain / RenderPass / Framebuffers" )
		{
			if ( createSwapChain() == false )
				return false;

			if ( createImageViews() == false )
				return false;

			if ( createRenderPass() == false )
				return false;

			if ( createFramebuffers() == false )
				return false;
		}

		BLOCK( "Command Pool / Sync Objects" )
		{
			if ( createCommandPool() == false )
				return false;

			if ( createCommandBuffers() == false )
				return false;

			if ( createSyncObjects() == false )
				return false;
		}

		BLOCK( "Descriptor / Pipeline Layout" )
		{
			if ( createDescriptorResources() == false )
			{
				SW_LOG_ERROR( "Failed to create descriptor / pipeline layout resources." );
				return false;
			}
			(void)ensureBindlessTextureArray();
		}

		BLOCK( "Fullscreen Triangle" )
		{
			const RHIVertex arrFullscreenVerts[3] = {
				{{ -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
				{ { 3.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
				{ { -1.0f, 3.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			};
			const RHIBufferHandle vbHandle =
				createVulkanBuffer( static_cast<uint32>( sizeof( arrFullscreenVerts ) ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, arrFullscreenVerts );
			if ( vbHandle != 0 )
			{
				const VulkanBufferRecord* pRec = resolveAllocatedBuffer( vbHandle );
				if ( pRec != nullptr )
					_vertexBuffer = pRec->_buffer;
			}
			else
				SW_LOG_WARNING( "Failed to create fullscreen triangle vertex buffer." );
		}

		initPipelineCache();
		_immContext		 = sw::make_unique<VulkanRHICommandContext>( this );
		_deferredContext = sw::make_unique<VulkanRHICommandContext>( this );

		SW_LOG_INFO( "Vulkan RHI Backend Device Initialized Successfully (Validation Layers: %#)", _bEnableValidationLayers ? "ENABLED" : "DISABLED" );
		return true;
	}

	bool VulkanRHIDevice::createRenderPass()
	{
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format		   = static_cast<VkFormat>( _swapChainImageFormat );
		colorAttachment.samples		   = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp		   = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp		   = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout	   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout	  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint	 = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments	 = &colorAttachmentRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass	 = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass	 = 0;
		dependency.srcStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType		   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments	   = &colorAttachment;
		renderPassInfo.subpassCount	   = 1;
		renderPassInfo.pSubpasses	   = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies   = &dependency;

		if ( vkCreateRenderPass( _device, &renderPassInfo, nullptr, &_renderPass ) != VK_SUCCESS )
			return false;
		return true;
	}

	void VulkanRHIDevice::shutdownInternal()
	{
		if ( _device )
		{
			vkDeviceWaitIdle( _device );
			_releaseQueue.flushAll();
			_immContext.reset();
			_deferredContext.reset();

			if ( _commandPool )
			{
				vkDestroyCommandPool( _device, _commandPool, nullptr );
				_commandPool = VK_NULL_HANDLE;
			}

			_gpuBuffers.forEach( [this]( VulkanBufferRecord& record )
			{
				if ( record._buffer != VK_NULL_HANDLE )
					vkDestroyBuffer( _device, record._buffer, nullptr );
				if ( record._memory != VK_NULL_HANDLE )
					vkFreeMemory( _device, record._memory, nullptr );
			} );
			_gpuBuffers.clear();
			_boundMeshVb	  = 0;
			_boundMeshStride  = sizeof( RHIVertex );
			_boundMeshOffset  = 0;
			_boundIndexBuffer = 0;
			_boundIndexStride = 4;
			_boundIndexOffset = 0;
			_listRegisteredDescriptorSet.clear();

			_gpuTextures.forEach( [this]( VulkanTextureRecord& record )
			{
				destroyOffscreenFramebuffer( record );
				if ( record._imageView != VK_NULL_HANDLE )
					vkDestroyImageView( _device, record._imageView, nullptr );
				if ( record._image != VK_NULL_HANDLE )
					vkDestroyImage( _device, record._image, nullptr );
				if ( record._memory != VK_NULL_HANDLE )
					vkFreeMemory( _device, record._memory, nullptr );
			} );
			_gpuTextures.clear();
			_listBindlessSourceBuffer.clear();
			_listUavSourceBuffer.clear();
			for ( auto& pair : _mapCompositeFramebuffer )
			{
				if ( pair.second._framebuffer != VK_NULL_HANDLE )
					vkDestroyFramebuffer( _device, pair.second._framebuffer, nullptr );
				if ( pair.second._renderPass != VK_NULL_HANDLE )
					vkDestroyRenderPass( _device, pair.second._renderPass, nullptr );
			}
			_mapCompositeFramebuffer.clear();
			for ( auto& pair : _mapPipelineRenderPass )
			{
				if ( pair.second != VK_NULL_HANDLE )
					vkDestroyRenderPass( _device, pair.second, nullptr );
			}
			_mapPipelineRenderPass.clear();
			_listRegisteredTexture.clear();
			_textureFreeList.clear();

			_bindlessTextureSet = VK_NULL_HANDLE; // owned by descriptor pool
			if ( _bindlessTextureArrayLayout )
			{
				vkDestroyDescriptorSetLayout( _device, _bindlessTextureArrayLayout, nullptr );
				_bindlessTextureArrayLayout = nullptr;
			}
			if ( _bindlessDummyView )
			{
				vkDestroyImageView( _device, _bindlessDummyView, nullptr );
				_bindlessDummyView = nullptr;
			}
			if ( _bindlessDummyImage )
			{
				vkDestroyImage( _device, _bindlessDummyImage, nullptr );
				_bindlessDummyImage = nullptr;
			}
			if ( _bindlessDummyMemory )
			{
				vkFreeMemory( _device, _bindlessDummyMemory, nullptr );
				_bindlessDummyMemory = nullptr;
			}

			if ( _defaultSampler )
			{
				vkDestroySampler( _device, _defaultSampler, nullptr );
				_defaultSampler = nullptr;
			}
			if ( _offscreenFence )
			{
				vkDestroyFence( _device, _offscreenFence, nullptr );
				_offscreenFence = nullptr;
			}
			_offscreenCommandBuffer = nullptr; // freed with command pool

			if ( _textureDescriptorSetLayout )
			{
				vkDestroyDescriptorSetLayout( _device, _textureDescriptorSetLayout, nullptr );
				_textureDescriptorSetLayout = nullptr;
			}

			_pipelineStates.forEach( [this]( VulkanPipelineStateRecord& pso )
			{
				if ( pso._pipeline != VK_NULL_HANDLE )
					vkDestroyPipeline( _device, pso._pipeline, nullptr );
			} );
			_pipelineStates.clear();

			// _vertexBuffer is owned by _gpuBuffers (Triangle Resources); do not destroy twice.
			_vertexBuffer = VK_NULL_HANDLE;
			if ( _pipeline )
				vkDestroyPipeline( _device, _pipeline, nullptr );
			if ( _offscreenPipeline )
			{
				vkDestroyPipeline( _device, _offscreenPipeline, nullptr );
				_offscreenPipeline = nullptr;
			}
			if ( _pipelineLayout )
				vkDestroyPipelineLayout( _device, _pipelineLayout, nullptr );
			if ( _dummyUBO )
				vkDestroyBuffer( _device, _dummyUBO, nullptr );
			if ( _dummyUBOMemory )
				vkFreeMemory( _device, _dummyUBOMemory, nullptr );
			if ( _descriptorPool )
				vkDestroyDescriptorPool( _device, _descriptorPool, nullptr );
			if ( _descriptorSetLayout )
				vkDestroyDescriptorSetLayout( _device, _descriptorSetLayout, nullptr );
			// _explicitUavDescriptorSetLayout may alias _uavDescriptorSetLayout.
			if ( _explicitUavDescriptorSetLayout == _uavDescriptorSetLayout )
				_explicitUavDescriptorSetLayout = nullptr;
			if ( _uavDescriptorSetLayout )
			{
				vkDestroyDescriptorSetLayout( _device, _uavDescriptorSetLayout, nullptr );
				_uavDescriptorSetLayout = nullptr;
			}
			if ( _explicitUavDescriptorSetLayout )
			{
				vkDestroyDescriptorSetLayout( _device, _explicitUavDescriptorSetLayout, nullptr );
				_explicitUavDescriptorSetLayout = nullptr;
			}

			cleanupSwapChain();
			destroySyncObjects();

			for ( VulkanRenderPassRecord& rpRecord : _listRenderPass )
			{
				if ( rpRecord._bOwned != 0 && rpRecord._renderPass != VK_NULL_HANDLE &&
					 rpRecord._renderPass != _renderPass )
					vkDestroyRenderPass( _device, rpRecord._renderPass, nullptr );
			}
			_listRenderPass.clear();

			if ( _renderPass )
				vkDestroyRenderPass( _device, _renderPass, nullptr );
			if ( _offscreenRenderPass )
			{
				vkDestroyRenderPass( _device, _offscreenRenderPass, nullptr );
				_offscreenRenderPass = nullptr;
			}
			savePipelineCache();
			vkDestroyDevice( _device, nullptr );
			_device = nullptr;
		}

		if ( _bEnableValidationLayers && _debugMessenger )
		{
			DestroyDebugUtilsMessengerEXT( _instance, _debugMessenger, nullptr );
			_debugMessenger = nullptr;
		}
		if ( _surface )
		{
			vkDestroySurfaceKHR( _instance, _surface, nullptr );
			_surface = nullptr;
		}
		if ( _instance )
		{
			vkDestroyInstance( _instance, nullptr );
			_instance = nullptr;
		}
	}

	void VulkanRHIDevice::waitIdle()
	{
		if ( _device )
			vkDeviceWaitIdle( _device );
		_releaseQueue.flushAll();
	}

	void VulkanRHIDevice::resize( uint32 width, uint32 height )
	{
		if ( _width == width && _height == height )
			return;

		_width	= width;
		_height = height;

		// 임의의 호출 스레드에서 재생성하지 않고 beginFrame까지 미룹니다.
		if ( width != 0 && height != 0 )
			_bSwapChainDirty = 1;
	}

	void VulkanRHIDevice::beginFrame( const float4& clearColor )
	{
		_bFrameStarted = false;
		if ( _width == 0 || _height == 0 )
			return;

		if ( _bSwapChainDirty )
		{
			recreateSwapChain();
			_bSwapChainDirty = 0;
		}

		// 재생성이 실패하면 스왑체인/동기화 객체가 없는 상태이므로 프레임을 건너뜁니다.
		if ( _swapChain == nullptr || _listInFlightFence.empty() || _listImageAvailableSemaphore.empty() )
			return;

		vkWaitForFences( _device, 1, &_listInFlightFence[_currentFrame], VK_TRUE, UINT64_MAX );

		VkResult result = vkAcquireNextImageKHR( _device, _swapChain, UINT64_MAX, _listImageAvailableSemaphore[_currentFrame], VK_NULL_HANDLE, &_imageIndex );
		if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR )
		{
			recreateSwapChain();
			if ( _swapChain == nullptr || _listImageAvailableSemaphore.empty() )
				return;

			result = vkAcquireNextImageKHR( _device, _swapChain, UINT64_MAX, _listImageAvailableSemaphore[_currentFrame], VK_NULL_HANDLE, &_imageIndex );
			if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR )
				return;
		}

		if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR )
			return;

		if ( _listImagesInFlight[_imageIndex] != VK_NULL_HANDLE )
			vkWaitForFences( _device, 1, &_listImagesInFlight[_imageIndex], VK_TRUE, UINT64_MAX );
		_listImagesInFlight[_imageIndex] = _listInFlightFence[_currentFrame];

		vkResetFences( _device, 1, &_listInFlightFence[_currentFrame] );
		vkResetCommandBuffer( _listCommandBuffer[_currentFrame], 0 );

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer( _listCommandBuffer[_currentFrame], &beginInfo );

		_bFrameStarted	   = true;
		_bRenderPassActive = false;

		constexpr float32 kDefaultViewportX		   = 0.0f;
		constexpr float32 kDefaultViewportMinDepth = 0.0f;
		constexpr float32 kDefaultViewportMaxDepth = 1.0f;

		VkViewport viewport{};
		viewport.x = kDefaultViewportX;

		viewport.y		  = static_cast<float32>( _swapChainExtentHeight );
		viewport.width	  = static_cast<float32>( _swapChainExtentWidth );
		viewport.height	  = -static_cast<float32>( _swapChainExtentHeight );
		viewport.minDepth = kDefaultViewportMinDepth;
		viewport.maxDepth = kDefaultViewportMaxDepth;
		vkCmdSetViewport( _listCommandBuffer[_currentFrame], 0, 1, &viewport );

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = { _swapChainExtentWidth, _swapChainExtentHeight };
		vkCmdSetScissor( _listCommandBuffer[_currentFrame], 0, 1, &scissor );

		if ( _renderPass != VK_NULL_HANDLE && _listSwapChainFramebuffer.empty() == false && _imageIndex < _listSwapChainFramebuffer.size() )
		{
			VkRenderPassBeginInfo rpBeginInfo{};
			rpBeginInfo.sType			  = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			rpBeginInfo.renderPass		  = _renderPass;
			rpBeginInfo.framebuffer		  = _listSwapChainFramebuffer[_imageIndex];
			rpBeginInfo.renderArea.offset = { 0, 0 };
			rpBeginInfo.renderArea.extent = { _swapChainExtentWidth, _swapChainExtentHeight };

			VkClearValue clearVal{};
			clearVal.color = {
				{ clearColor._x, clearColor._y, clearColor._z, clearColor._w }
			   };
			rpBeginInfo.clearValueCount = 1;
			rpBeginInfo.pClearValues	= &clearVal;

			vkCmdBeginRenderPass( _listCommandBuffer[_currentFrame], &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE );
			_bRenderPassActive = true;
		}
	}

	void VulkanRHIDevice::endFrame( bool vsync, bool bPresent )
	{
		(void)vsync;
		if ( _bFrameStarted == false )
			return;

		if ( _bRenderPassActive )
		{
			vkCmdEndRenderPass( _listCommandBuffer[_currentFrame] );
			_bRenderPassActive = false;
		}
		vkEndCommandBuffer( _listCommandBuffer[_currentFrame] );

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore			 arrWaitSemaphores[] = { _listImageAvailableSemaphore[_currentFrame] };
		VkPipelineStageFlags arrWaitStages[]	 = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount			 = 1;
		submitInfo.pWaitSemaphores				 = arrWaitSemaphores;
		submitInfo.pWaitDstStageMask			 = arrWaitStages;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers	  = &_listCommandBuffer[_currentFrame];

		VkSemaphore arrSignalSemaphores[] = { _listRenderFinishedSemaphore[_imageIndex] };
		submitInfo.signalSemaphoreCount	  = 1;
		submitInfo.pSignalSemaphores	  = arrSignalSemaphores;

		vkQueueSubmit( _graphicsQueue, 1, &submitInfo, _listInFlightFence[_currentFrame] );

		if ( bPresent )
		{
			VkPresentInfoKHR presentInfo{};
			presentInfo.sType			   = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores	   = arrSignalSemaphores;

			VkSwapchainKHR arrSwapChains[] = { _swapChain };
			presentInfo.swapchainCount	   = 1;
			presentInfo.pSwapchains		   = arrSwapChains;
			presentInfo.pImageIndices	   = &_imageIndex;

			const VkResult presentResult = vkQueuePresentKHR( _graphicsQueue, &presentInfo );
			if ( presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR )
			{
				// 다음 beginFrame에서 스왑체인을 재생성합니다.
				_bSwapChainDirty = 1;
			}
			else if ( presentResult != VK_SUCCESS )
				SW_LOG_ERROR( "vkQueuePresentKHR failed! Error code: %#", static_cast<int32>( presentResult ) );
		}

		_currentFrame  = ( _currentFrame + 1 ) % 2;
		_bFrameStarted = false;
		_releaseQueue.tickFrame();
	}

	VkCommandBuffer VulkanRHIDevice::currentCommandBuffer() const
	{
		if ( _bOffscreenPassActive && _offscreenCommandBuffer != VK_NULL_HANDLE )
			return _offscreenCommandBuffer;
		if ( _bFrameStarted && _listCommandBuffer.empty() == false )
			return _listCommandBuffer[_currentFrame];
		return VK_NULL_HANDLE;
	}

	static VkAttachmentLoadOp toVkLoadOp( RHIRenderPassLoadOp loadOp )
	{
		switch ( loadOp )
		{
			case RHIRenderPassLoadOp::Load:
				return VK_ATTACHMENT_LOAD_OP_LOAD;
			case RHIRenderPassLoadOp::DontCare:
				return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			case RHIRenderPassLoadOp::Clear:
			default:
				return VK_ATTACHMENT_LOAD_OP_CLEAR;
		}
	}

	unique_ptr<IRHICommandList> VulkanRHIDevice::createCommandList( RHICommandListMode mode )
	{
		unique_ptr<RHIDeferredCommandList> list = make_unique<RHIDeferredCommandList>( mode, getCommandContextForMode( mode ) );
		return list;
	}

	void VulkanRHIDevice::executeCommandList( IRHICommandList* pCmdList )
	{
		RHIDeferredCommandList::execute( this, pCmdList );
	}

	bool VulkanRHIDevice::queryVulkanTextureView( RHITextureHandle texture, void*& outImageView ) const
	{
		outImageView					= nullptr;
		const VulkanTextureRecord* pTex = resolveTexture( texture );
		if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE )
			return false;
		outImageView = reinterpret_cast<void*>( pTex->_imageView );
		return true;
	}

	void* VulkanRHIDevice::getNativeTexturePointer( RHITextureHandle texture ) const
	{
		const VulkanTextureRecord* pTex = resolveTexture( texture );
		return ( pTex != nullptr && pTex->_imageView != VK_NULL_HANDLE ) ? reinterpret_cast<void*>( pTex->_imageView ) : nullptr;
	}

	bool VulkanRHIDevice::checkValidationLayerSupport()
	{
		uint32 layerCount{ 0 };
		vkEnumerateInstanceLayerProperties( &layerCount, nullptr );
		vector<VkLayerProperties> availableLayers( layerCount );
		vkEnumerateInstanceLayerProperties( &layerCount, availableLayers.data() );

		for ( const utf8* pLayerName : s_listValidationLayers )
		{
			bool layerFound{ false };
			for ( const VkLayerProperties& layerProperties : availableLayers )
			{
				if ( StringUtil::strcmp( pLayerName, layerProperties.layerName ) == 0 )
				{
					layerFound = true;
					break;
				}
			}
			if ( layerFound == false )
				return false;
		}
		return true;
	}

	bool VulkanRHIDevice::createInstance()
	{
		VkApplicationInfo appInfo{};
		appInfo.sType			   = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName   = "SW App";
		appInfo.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
		appInfo.pEngineName		   = "SW Engine";
		appInfo.engineVersion	   = VK_MAKE_VERSION( 1, 0, 0 );
		appInfo.apiVersion		   = VK_API_VERSION_1_3;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType			= VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		vector<const utf8*> listExtensions;
		listExtensions.push_back( VK_KHR_SURFACE_EXTENSION_NAME );

		uint32 availableExtCount{ 0 };
		vkEnumerateInstanceExtensionProperties( nullptr, &availableExtCount, nullptr );
		vector<VkExtensionProperties> availableExts( availableExtCount );
		if ( availableExtCount > 0 )
			vkEnumerateInstanceExtensionProperties( nullptr, &availableExtCount, availableExts.data() );

#if defined( SW_PLATFORM_WINDOWS )
		if ( VulkanRHIDeviceInternal::hasExtensionVal( availableExts, VK_KHR_WIN32_SURFACE_EXTENSION_NAME ) == false )
		{
			SW_LOG_ERROR( "VK_KHR_win32_surface is not available." );
			return false;
		}
		listExtensions.push_back( VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
#elif defined( SW_PLATFORM_LINUX )
		// WSLg/gfxstream often exposes xcb but not xlib.
		_linuxWsi = 0;
		if ( VulkanRHIDeviceInternal::hasExtensionVal( availableExts, VK_KHR_XLIB_SURFACE_EXTENSION_NAME ) )
		{
			listExtensions.push_back( VK_KHR_XLIB_SURFACE_EXTENSION_NAME );
			_linuxWsi = 1;
			SW_LOG_TRACE( "Vulkan WSI: VK_KHR_xlib_surface" );
		}
		else if ( VulkanRHIDeviceInternal::hasExtensionVal( availableExts, VK_KHR_XCB_SURFACE_EXTENSION_NAME ) )
		{
			listExtensions.push_back( VK_KHR_XCB_SURFACE_EXTENSION_NAME );
			_linuxWsi = 2;
			SW_LOG_TRACE( "Vulkan WSI: VK_KHR_xcb_surface (xlib unavailable)" );
		}
		else
		{
			SW_LOG_ERROR( "No Vulkan X11 WSI extension (VK_KHR_xlib_surface / VK_KHR_xcb_surface). Enumerated %# instance extensions.",
						  availableExtCount );
			for ( const VkExtensionProperties& ext : availableExts )
				SW_LOG_TRACE( "  instance ext: %#", ext.extensionName );
			SW_LOG_ERROR( "Install libxcb1-dev / libx11-xcb-dev, and rebuild vcpkg vulkan-loader with [xcb,xlib]." );
			return false;
		}
#elif defined( SW_PLATFORM_MACOS )
		if ( VulkanRHIDeviceInternal::hasExtensionVal( availableExts, VK_EXT_METAL_SURFACE_EXTENSION_NAME ) == false )
		{
			SW_LOG_ERROR( "VK_EXT_metal_surface is not available." );
			return false;
		}
		listExtensions.push_back( VK_EXT_METAL_SURFACE_EXTENSION_NAME );
#endif
		if ( _bEnableValidationLayers && VulkanRHIDeviceInternal::hasExtensionVal( availableExts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) )
			listExtensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
		else if ( _bEnableValidationLayers )
			_bEnableValidationLayers = false;

		createInfo.enabledExtensionCount   = static_cast<uint32>( listExtensions.size() );
		createInfo.ppEnabledExtensionNames = listExtensions.data();

		if ( _bEnableValidationLayers )
		{
			createInfo.enabledLayerCount   = static_cast<uint32>( s_listValidationLayers.size() );
			createInfo.ppEnabledLayerNames = s_listValidationLayers.data();
		}
		else
			createInfo.enabledLayerCount = 0;

		VkResult result = vkCreateInstance( &createInfo, nullptr, &_instance );
		if ( result != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create Vulkan instance! Error code: %#", static_cast<int32>( result ) );
			return false;
		}
		return true;
	}

	void VulkanRHIDevice::setupDebugMessenger()
	{
		if ( _bEnableValidationLayers == false )
			return;
		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.sType		   = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType	   = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = debugCallback;

		CreateDebugUtilsMessengerEXT( _instance, &createInfo, nullptr, &_debugMessenger );
	}

	bool VulkanRHIDevice::createSurface()
	{
#if defined( SW_PLATFORM_WINDOWS )
		VkWin32SurfaceCreateInfoKHR createInfo{};
		createInfo.sType	 = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createInfo.hwnd		 = static_cast<HWND>( _pHWnd );
		createInfo.hinstance = GetModuleHandle( nullptr );

		if ( vkCreateWin32SurfaceKHR( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create window surface!" );
			return false;
		}
		return true;
#elif defined( SW_PLATFORM_LINUX )
		Display* pDisplay = static_cast<Display*>( _pDisplayHandle );
		Window	 window	  = static_cast<Window>( reinterpret_cast<uintptr_t>( _pHWnd ) );
		if ( pDisplay == nullptr || window == 0 )
		{
			SW_LOG_ERROR( "Invalid X11 display/window for Vulkan surface." );
			return false;
		}

		if ( _linuxWsi == 1 )
		{
			PFN_vkCreateXlibSurfaceKHR pCreateFn = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
				vkGetInstanceProcAddr( _instance, "vkCreateXlibSurfaceKHR" ) );
			if ( pCreateFn == nullptr )
			{
				SW_LOG_ERROR( "vkCreateXlibSurfaceKHR not available from Vulkan loader!" );
				return false;
			}

			VkXlibSurfaceCreateInfoKHR createInfo{};
			createInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
			createInfo.dpy	  = pDisplay;
			createInfo.window = window;
			if ( pCreateFn( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
			{
				SW_LOG_ERROR( "Failed to create Xlib Vulkan surface!" );
				return false;
			}
			return true;
		}

		if ( _linuxWsi == 2 )
		{
			PFN_vkCreateXcbSurfaceKHR pCreateFn = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
				vkGetInstanceProcAddr( _instance, "vkCreateXcbSurfaceKHR" ) );
			if ( pCreateFn == nullptr )
			{
				SW_LOG_ERROR( "vkCreateXcbSurfaceKHR not available from Vulkan loader!" );
				return false;
			}

			xcb_connection_t* pConnection = XGetXCBConnection( pDisplay );
			if ( pConnection == nullptr )
			{
				SW_LOG_ERROR( "XGetXCBConnection failed — cannot create Vulkan xcb surface." );
				return false;
			}

			VkXcbSurfaceCreateInfoKHR createInfo{};
			createInfo.sType	  = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
			createInfo.connection = pConnection;
			createInfo.window	  = static_cast<xcb_window_t>( window );
			if ( pCreateFn( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
			{
				SW_LOG_ERROR( "Failed to create XCB Vulkan surface!" );
				return false;
			}
			return true;
		}

		SW_LOG_ERROR( "No Linux Vulkan WSI selected during instance creation." );
		return false;
#elif defined( SW_PLATFORM_MACOS )
		VkMetalSurfaceCreateInfoEXT createInfo{};
		createInfo.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
		createInfo.pLayer = _pHWnd;

		if ( vkCreateMetalSurfaceEXT( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create Metal window surface!" );
			return false;
		}
		return true;
#else
		return false;
#endif
	}

	bool VulkanRHIDevice::pickPhysicalDevice()
	{
		uint32 deviceCount{ 0 };
		vkEnumeratePhysicalDevices( _instance, &deviceCount, nullptr );
		if ( deviceCount == 0 )
			return false;
		vector<VkPhysicalDevice> devices( deviceCount );
		vkEnumeratePhysicalDevices( _instance, &deviceCount, devices.data() );

		for ( const VkPhysicalDevice& device : devices )
		{
			uint32 queueFamilyCount{ 0 };
			vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, nullptr );
			vector<VkQueueFamilyProperties> queueFamilies( queueFamilyCount );
			vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

			uint32 i{ 0 };
			bool   found{ false };
			for ( const VkQueueFamilyProperties& queueFamily : queueFamilies )
			{
				if ( queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT )
				{
					VkBool32 presentSupport{ false };
					vkGetPhysicalDeviceSurfaceSupportKHR( device, i, _surface, &presentSupport );
					if ( presentSupport )
					{
						_graphicsQueueFamilyIndex = i;
						_physicalDevice			  = device;
						found					  = true;
						break;
					}
				}
				i++;
			}
			if ( found )
				break;
		}
		return _physicalDevice != nullptr;
	}

	bool VulkanRHIDevice::selectDepthFormat()
	{
		if ( _physicalDevice == nullptr )
			return false;

		const VkFormat arrCandidates[] = {
			VK_FORMAT_D24_UNORM_S8_UINT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D32_SFLOAT,
		};
		for ( VkFormat format : arrCandidates )
		{
			VkFormatProperties props{};
			vkGetPhysicalDeviceFormatProperties( _physicalDevice, format, &props );
			if ( ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) == 0 )
				continue;
			_depthFormat	  = static_cast<uint32>( format );
			_bDepthHasStencil = ( format == VK_FORMAT_D32_SFLOAT ) ? 0 : 1;
			SW_LOG_TRACE( "Selected depth format %# (stencil=%#)", static_cast<uint32>( format ),
						  static_cast<uint32>( _bDepthHasStencil ) );
			return true;
		}
		_depthFormat	  = 0;
		_bDepthHasStencil = 0;
		return false;
	}

	uint32 VulkanRHIDevice::depthAspectMask() const
	{
		return _bDepthHasStencil != 0
				 ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT )
				 : VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	bool VulkanRHIDevice::createLogicalDevice()
	{
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType			 = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
		queueCreateInfo.queueCount		 = 1;
		float32 queuePriority{ 1.0f };
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceFeatures availableFeatures{};
		vkGetPhysicalDeviceFeatures( _physicalDevice, &availableFeatures );

		VkPhysicalDeviceFeatures deviceFeatures{};
		deviceFeatures.multiDrawIndirect = availableFeatures.multiDrawIndirect;
		_bMultiDrawIndirect				 = availableFeatures.multiDrawIndirect ? 1 : 0;

		VkPhysicalDeviceVulkan12Features available12{};
		available12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		VkPhysicalDeviceFeatures2 features2{};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &available12;
		vkGetPhysicalDeviceFeatures2( _physicalDevice, &features2 );

		VkPhysicalDeviceVulkan12Features vulkan12Features{};
		vulkan12Features.sType										   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		vulkan12Features.descriptorBindingPartiallyBound			   = available12.descriptorBindingPartiallyBound;
		vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = available12.descriptorBindingStorageBufferUpdateAfterBind;
		vulkan12Features.descriptorBindingSampledImageUpdateAfterBind  = available12.descriptorBindingSampledImageUpdateAfterBind;
		vulkan12Features.shaderStorageBufferArrayNonUniformIndexing	   = available12.shaderStorageBufferArrayNonUniformIndexing;
		vulkan12Features.shaderSampledImageArrayNonUniformIndexing	   = available12.shaderSampledImageArrayNonUniformIndexing;
		vulkan12Features.runtimeDescriptorArray						   = available12.runtimeDescriptorArray;
		vulkan12Features.drawIndirectCount							   = available12.drawIndirectCount;
		_bDrawIndirectCount											   = available12.drawIndirectCount ? 1 : 0;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType				   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pNext				   = &vulkan12Features;
		createInfo.pQueueCreateInfos	   = &queueCreateInfo;
		createInfo.queueCreateInfoCount	   = 1;
		createInfo.pEnabledFeatures		   = &deviceFeatures;
		createInfo.enabledExtensionCount   = static_cast<uint32>( s_listDeviceExtensions.size() );
		createInfo.ppEnabledExtensionNames = s_listDeviceExtensions.data();

		createInfo.enabledLayerCount = 0;

		if ( vkCreateDevice( _physicalDevice, &createInfo, nullptr, &_device ) != VK_SUCCESS )
			return false;

		vkGetDeviceQueue( _device, _graphicsQueueFamilyIndex, 0, &_graphicsQueue );
		return true;
	}

	bool VulkanRHIDevice::createSwapChain()
	{
		VkSurfaceCapabilitiesKHR capabilities;
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR( _physicalDevice, _surface, &capabilities );

		uint32 formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR( _physicalDevice, _surface, &formatCount, nullptr );
		vector<VkSurfaceFormatKHR> formats( formatCount );
		vkGetPhysicalDeviceSurfaceFormatsKHR( _physicalDevice, _surface, &formatCount, formats.data() );

		VkSurfaceFormatKHR surfaceFormat = formats[0];
		for ( const VkSurfaceFormatKHR& availableFormat : formats )
		{
			if ( availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
			{
				surfaceFormat = availableFormat;
				break;
			}
		}

		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

		// 서피스가 크기를 고정한 경우( currentExtent != UINT32_MAX ) 반드시 그 값을 써야 합니다.
		VkExtent2D extent = capabilities.currentExtent;
		if ( capabilities.currentExtent.width == UINT32_MAX || capabilities.currentExtent.height == UINT32_MAX )
		{
			extent = { _width, _height };
			if ( extent.width < capabilities.minImageExtent.width )
				extent.width = capabilities.minImageExtent.width;
			if ( extent.width > capabilities.maxImageExtent.width )
				extent.width = capabilities.maxImageExtent.width;
			if ( extent.height < capabilities.minImageExtent.height )
				extent.height = capabilities.minImageExtent.height;
			if ( extent.height > capabilities.maxImageExtent.height )
				extent.height = capabilities.maxImageExtent.height;
		}

		uint32 imageCount = capabilities.minImageCount + 1;
		if ( capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount )
			imageCount = capabilities.maxImageCount;

		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if ( ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ) == 0 )
		{
			if ( ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR ) != 0 )
				compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
			else if ( ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR ) != 0 )
				compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
			else if ( ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR ) != 0 )
				compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
		}

		VkSurfaceTransformFlagBitsKHR preTransform = capabilities.currentTransform;
		if ( ( capabilities.supportedTransforms & preTransform ) == 0 )
			preTransform = ( ( capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ) != 0 ) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType			= VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface			= _surface;
		createInfo.minImageCount	= imageCount;
		createInfo.imageFormat		= surfaceFormat.format;
		createInfo.imageColorSpace	= surfaceFormat.colorSpace;
		createInfo.imageExtent		= extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage		= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform		= preTransform;
		createInfo.compositeAlpha	= compositeAlpha;
		createInfo.presentMode		= presentMode;
		createInfo.clipped			= VK_TRUE;

		if ( vkCreateSwapchainKHR( _device, &createInfo, nullptr, &_swapChain ) != VK_SUCCESS )
			return false;

		vkGetSwapchainImagesKHR( _device, _swapChain, &imageCount, nullptr );
		_listSwapChainImage.resize( imageCount );
		vkGetSwapchainImagesKHR( _device, _swapChain, &imageCount, _listSwapChainImage.data() );

		_swapChainImageFormat  = static_cast<uint32>( surfaceFormat.format );
		_swapChainExtentWidth  = extent.width;
		_swapChainExtentHeight = extent.height;
		return true;
	}

	bool VulkanRHIDevice::createImageViews()
	{
		_listSwapChainImageView.resize( _listSwapChainImage.size() );
		for ( size_t imageIndex = 0; imageIndex < _listSwapChainImage.size(); imageIndex++ )
		{
			VkImageViewCreateInfo createInfo{};
			createInfo.sType						   = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image						   = _listSwapChainImage[imageIndex];
			createInfo.viewType						   = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.format						   = static_cast<VkFormat>( _swapChainImageFormat );
			createInfo.components.r					   = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.g					   = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.b					   = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.components.a					   = VK_COMPONENT_SWIZZLE_IDENTITY;
			createInfo.subresourceRange.aspectMask	   = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.baseMipLevel   = 0;
			createInfo.subresourceRange.levelCount	   = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount	   = 1;

			if ( vkCreateImageView( _device, &createInfo, nullptr, &_listSwapChainImageView[imageIndex] ) != VK_SUCCESS )
				return false;
		}
		return true;
	}

	bool VulkanRHIDevice::createFramebuffers()
	{
		_listSwapChainFramebuffer.resize( _listSwapChainImageView.size() );
		for ( size_t imageIndex = 0; imageIndex < _listSwapChainImageView.size(); imageIndex++ )
		{
			VkImageView				arrAttachments[] = { _listSwapChainImageView[imageIndex] };
			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType			= VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass		= _renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments	= arrAttachments;
			framebufferInfo.width			= _swapChainExtentWidth;
			framebufferInfo.height			= _swapChainExtentHeight;
			framebufferInfo.layers			= 1;
			if ( vkCreateFramebuffer( _device, &framebufferInfo, nullptr, &_listSwapChainFramebuffer[imageIndex] ) != VK_SUCCESS )
				return false;
		}
		return true;
	}

	bool VulkanRHIDevice::createCommandPool()
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType			  = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags			  = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
		if ( vkCreateCommandPool( _device, &poolInfo, nullptr, &_commandPool ) != VK_SUCCESS )
			return false;
		return true;
	}

	bool VulkanRHIDevice::createCommandBuffers()
	{
		_listCommandBuffer.resize( 2 );
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool		 = _commandPool;
		allocInfo.level				 = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = static_cast<uint32>( _listCommandBuffer.size() );

		if ( vkAllocateCommandBuffers( _device, &allocInfo, _listCommandBuffer.data() ) != VK_SUCCESS )
			return false;

		VkCommandBufferAllocateInfo offscreenAlloc{};
		offscreenAlloc.sType			  = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		offscreenAlloc.commandPool		  = _commandPool;
		offscreenAlloc.level			  = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		offscreenAlloc.commandBufferCount = 1;
		if ( vkAllocateCommandBuffers( _device, &offscreenAlloc, &_offscreenCommandBuffer ) != VK_SUCCESS )
			return false;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if ( vkCreateFence( _device, &fenceInfo, nullptr, &_offscreenFence ) != VK_SUCCESS )
			return false;

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType		  = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter	  = VK_FILTER_LINEAR;
		samplerInfo.minFilter	  = VK_FILTER_LINEAR;
		samplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor	  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.maxLod		  = 1000.0f;
		if ( vkCreateSampler( _device, &samplerInfo, nullptr, &_defaultSampler ) != VK_SUCCESS )
			return false;

		return true;
	}

	bool VulkanRHIDevice::createSyncObjects()
	{
		_listImageAvailableSemaphore.resize( _listSwapChainImage.size() );
		_listRenderFinishedSemaphore.resize( _listSwapChainImage.size() );
		_listInFlightFence.resize( 2 );
		_listImagesInFlight.resize( _listSwapChainImage.size(), VK_NULL_HANDLE );

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for ( size_t syncIndex = 0; syncIndex < _listImageAvailableSemaphore.size(); syncIndex++ )
		{
			if ( vkCreateSemaphore( _device, &semaphoreInfo, nullptr, &_listImageAvailableSemaphore[syncIndex] ) != VK_SUCCESS )
				return false;
		}

		for ( size_t syncIndex = 0; syncIndex < 2; syncIndex++ )
		{
			if ( vkCreateFence( _device, &fenceInfo, nullptr, &_listInFlightFence[syncIndex] ) != VK_SUCCESS )
				return false;
		}

		for ( size_t syncIndex = 0; syncIndex < _listRenderFinishedSemaphore.size(); syncIndex++ )
		{
			if ( vkCreateSemaphore( _device, &semaphoreInfo, nullptr, &_listRenderFinishedSemaphore[syncIndex] ) != VK_SUCCESS )
				return false;
		}
		return true;
	}

	void VulkanRHIDevice::destroySyncObjects()
	{
		if ( _device == nullptr )
			return;

		for ( VkSemaphore semaphore : _listRenderFinishedSemaphore )
		{
			if ( semaphore != VK_NULL_HANDLE )
				vkDestroySemaphore( _device, semaphore, nullptr );
		}
		_listRenderFinishedSemaphore.clear();

		for ( VkSemaphore semaphore : _listImageAvailableSemaphore )
		{
			if ( semaphore != VK_NULL_HANDLE )
				vkDestroySemaphore( _device, semaphore, nullptr );
		}
		_listImageAvailableSemaphore.clear();

		for ( VkFence fence : _listInFlightFence )
		{
			if ( fence != VK_NULL_HANDLE )
				vkDestroyFence( _device, fence, nullptr );
		}
		_listInFlightFence.clear();

		// 스왑체인 이미지가 소유하지 않는 참조 사본이므로 비우기만 합니다.
		_listImagesInFlight.clear();
	}

	void VulkanRHIDevice::recreateSwapChain()
	{
		if ( _device == nullptr || _width == 0 || _height == 0 )
			return;
		vkDeviceWaitIdle( _device );

		// 세마포어/펜스는 스왑체인 이미지 개수로 크기가 정해지므로 함께 재생성해야 합니다.
		destroySyncObjects();
		cleanupSwapChain();

		if ( createSwapChain() == false )
		{
			SW_LOG_ERROR( "Failed to recreate the swapchain!" );
			return;
		}
		if ( createImageViews() == false )
		{
			SW_LOG_ERROR( "Failed to recreate the swapchain image views!" );
			return;
		}
		if ( createFramebuffers() == false )
		{
			SW_LOG_ERROR( "Failed to recreate the swapchain framebuffers!" );
			return;
		}
		if ( createSyncObjects() == false )
		{
			SW_LOG_ERROR( "Failed to recreate the swapchain sync objects!" );
			return;
		}

		_currentFrame = 0;
	}

	void VulkanRHIDevice::cleanupSwapChain()
	{
		if ( _device == nullptr )
			return;

		for ( VkFramebuffer framebuffer : _listSwapChainFramebuffer )
		{
			vkDestroyFramebuffer( _device, framebuffer, nullptr );
		}
		_listSwapChainFramebuffer.clear();

		for ( VkImageView imageView : _listSwapChainImageView )
		{
			vkDestroyImageView( _device, imageView, nullptr );
		}
		_listSwapChainImageView.clear();

		if ( _swapChain )
		{
			vkDestroySwapchainKHR( _device, _swapChain, nullptr );
			_swapChain = nullptr;
		}
		// 스왑체인 소유 이미지이므로 핸들만 버립니다.
		_listSwapChainImage.clear();
	}

	bool VulkanRHIDevice::initPipelineCache()
	{
		if ( _device == VK_NULL_HANDLE )
			return false;

		vector<uint8> listCacheData;
		const string  cachePath = "Saved/ShaderCache/vk_pipeline_cache.bin";
		if ( FileUtil::fileExists( cachePath ) )
			FileUtil::readFile( cachePath, listCacheData );

		VkPipelineCacheCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		if ( listCacheData.empty() == false )
		{
			createInfo.initialDataSize = listCacheData.size();
			createInfo.pInitialData	   = listCacheData.data();
		}

		const VkResult res = vkCreatePipelineCache( _device, &createInfo, nullptr, &_pipelineCache );
		if ( res != VK_SUCCESS )
		{
			SW_LOG_WARNING( "Failed to create pipeline cache with saved data; falling back to empty cache." );
			createInfo.initialDataSize = 0;
			createInfo.pInitialData	   = nullptr;
			vkCreatePipelineCache( _device, &createInfo, nullptr, &_pipelineCache );
		}
		else if ( listCacheData.empty() == false )
		{
			SW_LOG_INFO( "Loaded pipeline cache (%# bytes).", static_cast<uint32>( listCacheData.size() ) );
		}
		return _pipelineCache != VK_NULL_HANDLE;
	}

	void VulkanRHIDevice::savePipelineCache()
	{
		if ( _device == VK_NULL_HANDLE || _pipelineCache == VK_NULL_HANDLE )
			return;

		size_t dataSize{ 0 };
		if ( vkGetPipelineCacheData( _device, _pipelineCache, &dataSize, nullptr ) == VK_SUCCESS && dataSize > 0 )
		{
			vector<uint8> listCacheData( dataSize );
			if ( vkGetPipelineCacheData( _device, _pipelineCache, &dataSize, listCacheData.data() ) == VK_SUCCESS )
			{
				FileUtil::ensureDirectoryExists( "Saved/ShaderCache" );
				FileUtil::writeFile( "Saved/ShaderCache/vk_pipeline_cache.bin", listCacheData.data(), static_cast<uint64>( listCacheData.size() ) );
				SW_LOG_INFO( "Saved pipeline cache (%# bytes).", static_cast<uint32>( dataSize ) );
			}
		}

		vkDestroyPipelineCache( _device, _pipelineCache, nullptr );
		_pipelineCache = VK_NULL_HANDLE;
	}

	bool VulkanRHIDevice::findMemoryType( uint32 typeFilter, uint32 properties, uint32& outIndex )
	{
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties( _physicalDevice, &memProperties );
		for ( uint32 typeIndex = 0; typeIndex < memProperties.memoryTypeCount; typeIndex++ )
		{
			if ( ( typeFilter & ( 1 << typeIndex ) ) && ( memProperties.memoryTypes[typeIndex].propertyFlags & properties ) == properties )
			{
				outIndex = typeIndex;
				return true;
			}
		}
		return false;
	}

	VulkanRHIDevice::VulkanBufferRecord* VulkanRHIDevice::resolveAllocatedBuffer( RHIBufferHandle handle )
	{
		return _gpuBuffers.get( handle );
	}

	const VulkanRHIDevice::VulkanBufferRecord* VulkanRHIDevice::resolveAllocatedBuffer( RHIBufferHandle handle ) const
	{
		return _gpuBuffers.get( handle );
	}

	VulkanRHIDevice::VulkanTextureRecord* VulkanRHIDevice::resolveTexture( RHITextureHandle handle )
	{
		return _gpuTextures.get( handle );
	}

	const VulkanRHIDevice::VulkanTextureRecord* VulkanRHIDevice::resolveTexture( RHITextureHandle handle ) const
	{
		return _gpuTextures.get( handle );
	}

	bool VulkanRHIDevice::createDescriptorResources()
	{
		if ( _device == VK_NULL_HANDLE )
			return false;

		auto createSimpleSetLayout = [this]( VkDescriptorType type, VkShaderStageFlags stages, VkDescriptorSetLayout& outLayout ) -> bool
		{
			VkDescriptorSetLayoutBinding binding{};
			binding.binding			= 0;
			binding.descriptorType	= type;
			binding.descriptorCount = 1;
			binding.stageFlags		= stages;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.bindingCount = 1;
			layoutInfo.pBindings	= &binding;
			return vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &outLayout ) == VK_SUCCESS;
		};

		const VkShaderStageFlags allStages =
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

		if ( createSimpleSetLayout( VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, allStages, _descriptorSetLayout ) == false )
			return false;
		if ( createSimpleSetLayout( VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, allStages, _textureDescriptorSetLayout ) == false )
			return false;
		if ( createSimpleSetLayout( VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, allStages, _uavDescriptorSetLayout ) == false )
			return false;
		_explicitUavDescriptorSetLayout = _uavDescriptorSetLayout;

		// Bindless texture array layout (set 1) — ensureBindlessTextureArray가 세트를 할당.
		{
			VkDescriptorSetLayoutBinding binding{};
			binding.binding			= 0;
			binding.descriptorType	= VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			binding.descriptorCount = kBindlessTextureCount;
			binding.stageFlags		= allStages;

			VkDescriptorBindingFlags bindingFlags =
				VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
			VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
			bindingFlagsInfo.sType		   = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
			bindingFlagsInfo.bindingCount  = 1;
			bindingFlagsInfo.pBindingFlags = &bindingFlags;

			VkDescriptorSetLayoutCreateInfo layoutInfo{};
			layoutInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.pNext		= &bindingFlagsInfo;
			layoutInfo.flags		= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
			layoutInfo.bindingCount = 1;
			layoutInfo.pBindings	= &binding;
			if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_bindlessTextureArrayLayout ) != VK_SUCCESS )
				return false;
		}

		// Descriptor set layout (matches VulkanRHICommandContext binds):
		//   0: Pass/Material UBO
		//   1: Bindless texture array (native sampling)
		//   2..5: Explicit single-texture SRV slots 0..3 (DX11-style emulation)
		//   6..9: Compute UAV / SSBO slots 0..3
		VkDescriptorSetLayout arrSetLayouts[10] = {
			_descriptorSetLayout,
			_bindlessTextureArrayLayout,
			_textureDescriptorSetLayout,
			_textureDescriptorSetLayout,
			_textureDescriptorSetLayout,
			_textureDescriptorSetLayout,
			_uavDescriptorSetLayout,
			_uavDescriptorSetLayout,
			_uavDescriptorSetLayout,
			_uavDescriptorSetLayout,
		};

		VkPushConstantRange pushRange{};
		pushRange.stageFlags = allStages;
		pushRange.offset	 = 0;
		pushRange.size		 = 128;

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType				  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount		  = 10;
		pipelineLayoutInfo.pSetLayouts			  = arrSetLayouts;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges	  = &pushRange;
		if ( vkCreatePipelineLayout( _device, &pipelineLayoutInfo, nullptr, &_pipelineLayout ) != VK_SUCCESS )
			return false;

		VkDescriptorPoolSize arrPoolSizes[] = {
			{		  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16384},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768},
			{		  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16384},
		};
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType		   = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags		   = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
		poolInfo.maxSets	   = 32768;
		poolInfo.poolSizeCount = static_cast<uint32>( sizeof( arrPoolSizes ) / sizeof( arrPoolSizes[0] ) );
		poolInfo.pPoolSizes	   = arrPoolSizes;
		if ( vkCreateDescriptorPool( _device, &poolInfo, nullptr, &_descriptorPool ) != VK_SUCCESS )
			return false;

		return true;
	}

	bool VulkanRHIDevice::ensureBindlessTextureArray()
	{
		if ( _bindlessTextureSet != VK_NULL_HANDLE )
			return true;
		if ( _device == VK_NULL_HANDLE || _descriptorPool == VK_NULL_HANDLE || _defaultSampler == VK_NULL_HANDLE )
			return false;
		if ( _bindlessTextureArrayLayout == VK_NULL_HANDLE )
			return false;

		// 1x1 dummy image so unbound slots are valid.
		VkImageCreateInfo imageInfo{};
		imageInfo.sType			= VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType		= VK_IMAGE_TYPE_2D;
		imageInfo.format		= VK_FORMAT_R8G8B8A8_UNORM;
		imageInfo.extent		= { 1, 1, 1 };
		imageInfo.mipLevels		= 1;
		imageInfo.arrayLayers	= 1;
		imageInfo.samples		= VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling		= VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage			= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if ( vkCreateImage( _device, &imageInfo, nullptr, &_bindlessDummyImage ) != VK_SUCCESS )
			return false;

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements( _device, _bindlessDummyImage, &memReq );
		uint32 memoryTypeIndex{ 0 };
		if ( findMemoryType( memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex ) == false )
		{
			SW_LOG_ERROR( "Failed to find a device local memory type for the bindless dummy image." );
			return false;
		}

		VkMemoryAllocateInfo allocMem{};
		allocMem.sType			 = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocMem.allocationSize	 = memReq.size;
		allocMem.memoryTypeIndex = memoryTypeIndex;
		if ( vkAllocateMemory( _device, &allocMem, nullptr, &_bindlessDummyMemory ) != VK_SUCCESS )
			return false;
		vkBindImageMemory( _device, _bindlessDummyImage, _bindlessDummyMemory, 0 );

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType						 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image						 = _bindlessDummyImage;
		viewInfo.viewType					 = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format						 = VK_FORMAT_R8G8B8A8_UNORM;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;
		if ( vkCreateImageView( _device, &viewInfo, nullptr, &_bindlessDummyView ) != VK_SUCCESS )
			return false;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool	 = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts		 = &_bindlessTextureArrayLayout;

		if ( vkAllocateDescriptorSets( _device, &allocInfo, &_bindlessTextureSet ) != VK_SUCCESS )
			return false;

		vector<VkDescriptorImageInfo> infos( kBindlessTextureCount );
		vector<VkWriteDescriptorSet>  writes( kBindlessTextureCount );
		for ( uint32 slotIndex = 0; slotIndex < kBindlessTextureCount; ++slotIndex )
		{
			infos[slotIndex].sampler		  = _defaultSampler;
			infos[slotIndex].imageView		  = _bindlessDummyView;
			infos[slotIndex].imageLayout	  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			writes[slotIndex].sType			  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[slotIndex].dstSet		  = _bindlessTextureSet;
			writes[slotIndex].dstBinding	  = 0;
			writes[slotIndex].dstArrayElement = slotIndex;
			writes[slotIndex].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[slotIndex].descriptorCount = 1;
			writes[slotIndex].pImageInfo	  = &infos[slotIndex];
		}
		vkUpdateDescriptorSets( _device, kBindlessTextureCount, writes.data(), 0, nullptr );
		SW_LOG_INFO( "Bindless texture array ready (%# slots).", kBindlessTextureCount );
		return true;
	}

	void VulkanRHIDevice::writeBindlessTextureSlot( RHIDescriptorIndex index, VkImageView view )
	{
		if ( _bindlessTextureSet == VK_NULL_HANDLE || view == VK_NULL_HANDLE || index >= kBindlessTextureCount )
			return;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler	  = _defaultSampler;
		imageInfo.imageView	  = view;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType			  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet		  = _bindlessTextureSet;
		write.dstBinding	  = 0;
		write.dstArrayElement = index;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo	  = &imageInfo;
		vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
	}

	bool VulkanRHIDevice::transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayoutU32, uint32 newLayoutU32, uint32 aspectU32 )
	{
		const VkImageLayout		 oldLayout = static_cast<VkImageLayout>( oldLayoutU32 );
		const VkImageLayout		 newLayout = static_cast<VkImageLayout>( newLayoutU32 );
		const VkImageAspectFlags aspect	   = aspectU32;
		if ( cmd == VK_NULL_HANDLE || image == VK_NULL_HANDLE || oldLayout == newLayout )
			return true;

		VkImageMemoryBarrier barrier{};
		barrier.sType						= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout					= oldLayout;
		barrier.newLayout					= newLayout;
		barrier.srcQueueFamilyIndex			= VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex			= VK_QUEUE_FAMILY_IGNORED;
		barrier.image						= image;
		barrier.subresourceRange.aspectMask = aspect;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;

		VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

		if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL )
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			srcStage			  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage			  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		else if ( oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
		{
			barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage			  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dstStage			  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if ( oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL )
		{
			barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			srcStage			  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dstStage			  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		else if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcStage			  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage			  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else
		{
			barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			srcStage			  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			dstStage			  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
		}

		vkCmdPipelineBarrier( cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier );
		return true;
	}

	bool VulkanRHIDevice::ensureOffscreenRenderPass( uint32 vkFormat )
	{
		if ( _offscreenRenderPass != VK_NULL_HANDLE )
			return true;
		if ( _device == nullptr )
			return false;

		// Shared pass is always R8G8B8A8_UNORM (Game View). Other RT formats use a private RP.
		constexpr uint32 sharedFormat = VK_FORMAT_R8G8B8A8_UNORM;
		if ( vkFormat != 0 && vkFormat != sharedFormat )
			return false;

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format		   = static_cast<VkFormat>( sharedFormat );
		colorAttachment.samples		   = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp		   = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp		   = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout	   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout		= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint	 = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments	 = &colorRef;

		// Match swapchain-compatible dependency style so PSO / active RP stay consistent.
		VkSubpassDependency dependency{};
		dependency.srcSubpass	 = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass	 = 0;
		dependency.srcStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType		   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments	   = &colorAttachment;
		rpInfo.subpassCount	   = 1;
		rpInfo.pSubpasses	   = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies   = &dependency;

		return vkCreateRenderPass( _device, &rpInfo, nullptr, &_offscreenRenderPass ) == VK_SUCCESS;
	}

	bool VulkanRHIDevice::createOffscreenFramebuffer( VulkanTextureRecord& record )
	{
		if ( record._imageView == VK_NULL_HANDLE || record._bRenderTarget == 0 )
			return false;

		const bool bUseSharedPass = ( record._format == static_cast<uint32>( VK_FORMAT_R8G8B8A8_UNORM ) );
		if ( bUseSharedPass )
		{
			if ( ensureOffscreenRenderPass( record._format ) == false )
				return false;
			record._renderPass = _offscreenRenderPass;
		}
		else
		{
			VkAttachmentDescription colorAttachment{};
			colorAttachment.format		   = static_cast<VkFormat>( record._format );
			colorAttachment.samples		   = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp		   = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp		   = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachment.finalLayout	   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentReference colorRef{};
			colorRef.attachment = 0;
			colorRef.layout		= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint	 = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments	 = &colorRef;

			VkSubpassDependency dependency{};
			dependency.srcSubpass	 = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass	 = 0;
			dependency.srcStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo rpInfo{};
			rpInfo.sType		   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			rpInfo.attachmentCount = 1;
			rpInfo.pAttachments	   = &colorAttachment;
			rpInfo.subpassCount	   = 1;
			rpInfo.pSubpasses	   = &subpass;
			rpInfo.dependencyCount = 1;
			rpInfo.pDependencies   = &dependency;

			if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &record._renderPass ) != VK_SUCCESS )
				return false;
		}

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType		   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass	   = record._renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments	   = &record._imageView;
		fbInfo.width		   = record._width;
		fbInfo.height		   = record._height;
		fbInfo.layers		   = 1;

		if ( vkCreateFramebuffer( _device, &fbInfo, nullptr, &record._framebuffer ) != VK_SUCCESS )
		{
			if ( record._renderPass != _offscreenRenderPass )
				vkDestroyRenderPass( _device, record._renderPass, nullptr );
			record._renderPass = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	void VulkanRHIDevice::destroyOffscreenFramebuffer( VulkanTextureRecord& record )
	{
		if ( record._framebuffer != VK_NULL_HANDLE )
		{
			vkDestroyFramebuffer( _device, record._framebuffer, nullptr );
			record._framebuffer = VK_NULL_HANDLE;
		}
		// Shared offscreen RP is owned by the device; only destroy private per-texture passes.
		if ( record._renderPass != VK_NULL_HANDLE && record._renderPass != _offscreenRenderPass )
			vkDestroyRenderPass( _device, record._renderPass, nullptr );
		record._renderPass = VK_NULL_HANDLE;
	}

	VkRenderPass VulkanRHIDevice::ensurePipelineRenderPass( const RHIPipelineStateDesc& desc )
	{
		PipelineRpKey key{};
		const bool	  bDepthOnly = ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 );
		key._colorCount			 = bDepthOnly ? 0u : ( desc._numRenderTargets > 0 ? desc._numRenderTargets : 1u );
		if ( key._colorCount > kMaxColorAttachments )
			key._colorCount = kMaxColorAttachments;
		for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
		{
			VkFormat colorFmt = toVulkanTextureFormat( desc._arrRtvFormats[colorIndex] );
			if ( colorFmt == VK_FORMAT_UNDEFINED )
				colorFmt = VK_FORMAT_R8G8B8A8_UNORM;
			key._arrColorFormats[colorIndex] = static_cast<uint32>( colorFmt );
		}
		if ( desc._bEnableDepthTest != 0 )
		{
			// FrameRenderer는 RHI D24를 요청하지만 GPU가 미지원일 수 있음 → 디바이스 선택 포맷 사용
			VkFormat depthFmt = toVulkanTextureFormat( desc._depthStencilFormat );
			if ( desc._depthStencilFormat == sw::RHIFormat::D24_UNORM_S8_UINT ||
				 depthFmt == VK_FORMAT_D24_UNORM_S8_UINT || depthFmt == VK_FORMAT_UNDEFINED )
				depthFmt = static_cast<VkFormat>( _depthFormat );
			key._depthFormat = static_cast<uint32>( depthFmt );
		}

		auto existing = _mapPipelineRenderPass.find( key );
		if ( existing != _mapPipelineRenderPass.end() )
			return existing->second;

		VkAttachmentDescription attachments[kMaxColorAttachments + 1]{};
		VkAttachmentReference	colorRefs[kMaxColorAttachments]{};
		for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
		{
			attachments[colorIndex].format		   = static_cast<VkFormat>( key._arrColorFormats[colorIndex] );
			attachments[colorIndex].samples		   = VK_SAMPLE_COUNT_1_BIT;
			attachments[colorIndex].loadOp		   = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[colorIndex].storeOp		   = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[colorIndex].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[colorIndex].finalLayout	   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorRefs[colorIndex].attachment	   = colorIndex;
			colorRefs[colorIndex].layout		   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		VkAttachmentReference depthRef{};
		const bool			  bHasDepth	  = key._depthFormat != 0;
		uint32				  attachCount = key._colorCount;
		if ( bHasDepth )
		{
			attachments[attachCount].format			= static_cast<VkFormat>( key._depthFormat );
			attachments[attachCount].samples		= VK_SAMPLE_COUNT_1_BIT;
			attachments[attachCount].loadOp			= VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachments[attachCount].storeOp		= VK_ATTACHMENT_STORE_OP_STORE;
			attachments[attachCount].stencilLoadOp	= VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[attachCount].initialLayout	= VK_IMAGE_LAYOUT_UNDEFINED;
			attachments[attachCount].finalLayout	= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depthRef.attachment						= attachCount;
			depthRef.layout							= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			++attachCount;
		}

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint		= VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount	= key._colorCount;
		subpass.pColorAttachments		= key._colorCount > 0 ? colorRefs : nullptr;
		subpass.pDepthStencilAttachment = bHasDepth ? &depthRef : nullptr;

		VkSubpassDependency dependency{};
		dependency.srcSubpass	 = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass	 = 0;
		dependency.srcStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		if ( bHasDepth )
		{
			dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		}

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType		   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = attachCount;
		rpInfo.pAttachments	   = attachments;
		rpInfo.subpassCount	   = 1;
		rpInfo.pSubpasses	   = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies   = &dependency;

		VkRenderPass renderPass = VK_NULL_HANDLE;
		if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &renderPass ) != VK_SUCCESS )
			return VK_NULL_HANDLE;

		_mapPipelineRenderPass.emplace( key, renderPass );
		return renderPass;
	}

	bool VulkanRHIDevice::ensureCompositeFramebuffer( const CompositeFbKey& key, CompositeFbRecord& outRecord )
	{
		auto existing = _mapCompositeFramebuffer.find( key );
		if ( existing != _mapCompositeFramebuffer.end() )
		{
			outRecord = existing->second;
			return outRecord._framebuffer != VK_NULL_HANDLE && outRecord._renderPass != VK_NULL_HANDLE;
		}

		VkImageView colorViews[kMaxColorAttachments]{};
		uint32		colorFormats[kMaxColorAttachments]{};
		uint32		width{ 0 };
		uint32		height{ 0 };
		for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
		{
			VulkanTextureRecord* pTex = resolveTexture( key._arrColors[colorIndex] );
			if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE || pTex->_bDepthStencil != 0 )
				return false;
			colorViews[colorIndex]	 = pTex->_imageView;
			colorFormats[colorIndex] = pTex->_format;
			width					 = pTex->_width;
			height					 = pTex->_height;
		}

		VkImageView depthView = VK_NULL_HANDLE;
		uint32		depthFormat{ 0 };
		if ( key._depth != 0 )
		{
			VulkanTextureRecord* pTex = resolveTexture( key._depth );
			if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE || pTex->_bDepthStencil == 0 )
				return false;
			depthView	= pTex->_imageView;
			depthFormat = pTex->_format;
			if ( width == 0 )
			{
				width  = pTex->_width;
				height = pTex->_height;
			}
		}
		if ( key._colorCount == 0 && depthView == VK_NULL_HANDLE )
			return false;

		VkAttachmentDescription attachments[kMaxColorAttachments + 1]{};
		VkAttachmentReference	colorRefs[kMaxColorAttachments]{};
		VkImageView				fbAttachments[kMaxColorAttachments + 1]{};
		for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
		{
			attachments[colorIndex].format		   = static_cast<VkFormat>( colorFormats[colorIndex] );
			attachments[colorIndex].samples		   = VK_SAMPLE_COUNT_1_BIT;
			attachments[colorIndex].loadOp		   = toVkLoadOp( static_cast<RHIRenderPassLoadOp>( key._arrColorLoadOps[colorIndex] ) );
			attachments[colorIndex].storeOp		   = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			// Match pre-begin transitionImageLayout to COLOR_ATTACHMENT_OPTIMAL.
			attachments[colorIndex].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments[colorIndex].finalLayout	  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorRefs[colorIndex].attachment	  = colorIndex;
			colorRefs[colorIndex].layout		  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			fbAttachments[colorIndex]			  = colorViews[colorIndex];
		}

		VkAttachmentReference depthRef{};
		uint32				  attachCount = key._colorCount;
		const bool			  bHasDepth	  = depthView != VK_NULL_HANDLE;
		if ( bHasDepth )
		{
			attachments[attachCount].format			= static_cast<VkFormat>( depthFormat );
			attachments[attachCount].samples		= VK_SAMPLE_COUNT_1_BIT;
			attachments[attachCount].loadOp			= toVkLoadOp( static_cast<RHIRenderPassLoadOp>( key._depthLoadOp ) );
			attachments[attachCount].storeOp		= VK_ATTACHMENT_STORE_OP_STORE;
			attachments[attachCount].stencilLoadOp	= VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachments[attachCount].initialLayout	= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			attachments[attachCount].finalLayout	= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depthRef.attachment						= attachCount;
			depthRef.layout							= VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			fbAttachments[attachCount]				= depthView;
			++attachCount;
		}

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint		= VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount	= key._colorCount;
		subpass.pColorAttachments		= key._colorCount > 0 ? colorRefs : nullptr;
		subpass.pDepthStencilAttachment = bHasDepth ? &depthRef : nullptr;

		VkSubpassDependency dependency{};
		dependency.srcSubpass	 = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass	 = 0;
		dependency.srcStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType		   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = attachCount;
		rpInfo.pAttachments	   = attachments;
		rpInfo.subpassCount	   = 1;
		rpInfo.pSubpasses	   = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies   = &dependency;

		CompositeFbRecord record{};
		if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &record._renderPass ) != VK_SUCCESS )
			return false;

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType		   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass	   = record._renderPass;
		fbInfo.attachmentCount = attachCount;
		fbInfo.pAttachments	   = fbAttachments;
		fbInfo.width		   = width;
		fbInfo.height		   = height;
		fbInfo.layers		   = 1;

		if ( vkCreateFramebuffer( _device, &fbInfo, nullptr, &record._framebuffer ) != VK_SUCCESS )
		{
			vkDestroyRenderPass( _device, record._renderPass, nullptr );
			return false;
		}
		record._width  = width;
		record._height = height;
		_mapCompositeFramebuffer.emplace( key, record );
		outRecord = record;
		return true;
	}

	void VulkanRHIDevice::destroyCompositeFramebuffersUsing( RHITextureHandle texture )
	{
		if ( texture == 0 || _device == nullptr )
			return;
		for ( auto it = _mapCompositeFramebuffer.begin(); it != _mapCompositeFramebuffer.end(); )
		{
			bool bUses = ( it->first._depth == texture );
			for ( uint32 colorIndex = 0; colorIndex < it->first._colorCount && bUses == false; ++colorIndex )
			{
				bUses = ( it->first._arrColors[colorIndex] == texture );
			}
			if ( bUses )
			{
				if ( it->second._framebuffer != VK_NULL_HANDLE )
					vkDestroyFramebuffer( _device, it->second._framebuffer, nullptr );
				if ( it->second._renderPass != VK_NULL_HANDLE )
					vkDestroyRenderPass( _device, it->second._renderPass, nullptr );
				it = _mapCompositeFramebuffer.erase( it );
			}
			else
				++it;
		}
	}

	RHIBufferHandle VulkanRHIDevice::createVulkanBuffer( uint32 sizeBytes, uint32 usageFlags, const void* pInitialData )
	{
		if ( _device == nullptr || sizeBytes == 0 )
			return 0;

		const uint32 alignedSize = ( sizeBytes + 255u ) & ~255u;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType	   = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size		   = alignedSize;
		bufferInfo.usage	   = usageFlags;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer buffer = VK_NULL_HANDLE;
		if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &buffer ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create VkBuffer (usage=0x%#).", usageFlags );
			return 0;
		}

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements( _device, buffer, &memRequirements );

		uint32 memoryTypeIndex{ 0 };
		if ( findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
		{
			vkDestroyBuffer( _device, buffer, nullptr );
			SW_LOG_ERROR( "Failed to find a host visible memory type for VkBuffer." );
			return 0;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType			  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = memRequirements.size;
		allocInfo.memoryTypeIndex = memoryTypeIndex;

		VkDeviceMemory memory = VK_NULL_HANDLE;
		if ( vkAllocateMemory( _device, &allocInfo, nullptr, &memory ) != VK_SUCCESS )
		{
			vkDestroyBuffer( _device, buffer, nullptr );
			SW_LOG_ERROR( "Failed to allocate memory for VkBuffer." );
			return 0;
		}

		vkBindBufferMemory( _device, buffer, memory, 0 );

		if ( pInitialData != nullptr )
		{
			void* pMapped{ nullptr };
			if ( vkMapMemory( _device, memory, 0, sizeBytes, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr )
			{
				Memory::copy( pMapped, pInitialData, sizeBytes );
				vkUnmapMemory( _device, memory );
			}
		}

		VulkanBufferRecord record{};
		record._buffer = buffer;
		record._memory = memory;
		record._size   = sizeBytes;
		record._usage  = usageFlags;
		record._state  = RHIBufferState::Common;

		return _gpuBuffers.insert( std::move( record ) );
	}

	// ------------------------------------------------------------------------------
	// VulkanRHISwapChain Implementation
	// ------------------------------------------------------------------------------

	// ------------------------------------------------------------------------------
	// VulkanRHIResource Implementation
	// ------------------------------------------------------------------------------

} // namespace sw
