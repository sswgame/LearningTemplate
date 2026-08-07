/**
 * @file VulkanRHIDevice.cpp
 * @brief Vulkan RHI 디바이스 구현
 */
#include "Core/CoreMinimal.h"

#include "VulkanRHIDevice.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Graphics/Shader/ShaderCache.h"

#include <cstring>
#include <vulkan/vulkan.h>
#if defined( SW_PLATFORM_WINDOWS )
	#include <Windows.h>
	#include <vulkan/vulkan_win32.h>
#elif defined( SW_PLATFORM_LINUX )
	#include <vulkan/vulkan_xlib.h>
#elif defined( SW_PLATFORM_MACOS )
	#include <vulkan/vulkan_metal.h>
#endif
namespace sw
{
	static const std::vector<const utf8*> s_validationLayers = {
		"VK_LAYER_KHRONOS_validation" };

	static const std::vector<const char*> s_deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	[[maybe_unused]] static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT ,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void*  )
	{
		if ( pCallbackData->pMessage != nullptr )
		{

			if ( strstr( pCallbackData->pMessage, "image has not been acquired" ) != nullptr &&
				 strstr( pCallbackData->pMessage, "performs a layout transition" ) != nullptr )
			{
				return VK_FALSE;
			}
		}

		if ( messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
			SW_LOG_ERROR( "[Vulkan Validation Error] %#", pCallbackData->pMessage );
		else if ( messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
			SW_LOG_WARNING( "[Vulkan Validation Warning] %#", pCallbackData->pMessage );
		else
			SW_LOG_INFO( "[Vulkan Validation Info] %#", pCallbackData->pMessage );
		return VK_FALSE;
	}

	[[maybe_unused]] static VkResult CreateDebugUtilsMessengerEXT(
		VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger )
	{
		auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>( vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" ) );
		if ( func != nullptr )
			return func( instance, pCreateInfo, pAllocator, pDebugMessenger );
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}

	[[maybe_unused]] static void DestroyDebugUtilsMessengerEXT(
		VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator )
	{
		auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>( vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" ) );
		if ( func != nullptr )
			func( instance, debugMessenger, pAllocator );
	}

	VulkanRHIDevice::VulkanRHIDevice()
	{
#if defined( SW_DEBUG )
		_bEnableValidationLayers = true;
#else
		_bEnableValidationLayers = false;
#endif
	}

	VulkanRHIDevice::~VulkanRHIDevice() { shutdown(); }

	bool VulkanRHIDevice::checkValidationLayerSupport()
	{
		uint32 layerCount = 0;
		vkEnumerateInstanceLayerProperties( &layerCount, nullptr );
		std::vector<VkLayerProperties> availableLayers( layerCount );
		vkEnumerateInstanceLayerProperties( &layerCount, availableLayers.data() );

		for ( const utf8* layerName : s_validationLayers )
		{
			bool layerFound = false;
			for ( const auto& layerProperties : availableLayers )
			{
				if ( strcmp( layerName, layerProperties.layerName ) == 0 )
				{
					layerFound = true;
					break;
				}
			}
			if ( !layerFound )
				return false;
		}
		return true;
	}

	bool VulkanRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
	{
		_hWnd		   = desc._windowHandle;
		_displayHandle = desc._windowDisplay;
		_width		   = desc._width;
		_height		   = desc._height;

#if defined( SW_PLATFORM_WINDOWS )
		if ( _bEnableValidationLayers )
		{
			std::string execDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
			if ( FileUtil::isFileExist( execDir + "/VkLayer_khronos_validation.json" ) )
			{
				SetEnvironmentVariableA( "VK_ADD_LAYER_PATH", execDir.c_str() );
				SetEnvironmentVariableA( "VK_LAYER_PATH", execDir.c_str() );
			}
			else
			{
				const char* vulkanSdkEnv = std::getenv( "VULKAN_SDK" );
				if ( vulkanSdkEnv != nullptr && std::strlen( vulkanSdkEnv ) > 0 )
				{
					std::string sdkBinPath = std::string( vulkanSdkEnv ) + "/Bin";
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

		if ( createInstance() == false )
			return false;

		setupDebugMessenger();

		if ( createSurface() == false )
			return false;

		if ( pickPhysicalDevice() == false )
			return false;

		if ( createLogicalDevice() == false )
			return false;

		if ( createSwapChain() == false )
			return false;

		if ( createImageViews() == false )
			return false;

		if ( createRenderPass() == false )
			return false;

		if ( createFramebuffers() == false )
			return false;

		if ( createCommandPool() == false )
			return false;

		if ( createCommandBuffers() == false )
			return false;

		if ( createSyncObjects() == false )
			return false;

		if ( createTriangleResources() == false )
			return false;

		SW_LOG_INFO( "Vulkan RHI Backend Device Initialized Successfully (Validation Layers: %#)", _bEnableValidationLayers ? "ENABLED" : "DISABLED" );
		return true;
	}

	bool VulkanRHIDevice::createInstance()
	{
		VkApplicationInfo appInfo{};
		appInfo.sType			   = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName   = "ToyApp";
		appInfo.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
		appInfo.pEngineName		   = "ToyEngine";
		appInfo.engineVersion	   = VK_MAKE_VERSION( 1, 0, 0 );
		appInfo.apiVersion		   = VK_API_VERSION_1_3;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType			= VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		std::vector<const char*> extensions;
		extensions.push_back( VK_KHR_SURFACE_EXTENSION_NAME );
#if defined( SW_PLATFORM_WINDOWS )
		extensions.push_back( VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
#endif
		if ( _bEnableValidationLayers )
			extensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );

		createInfo.enabledExtensionCount   = static_cast<uint32>( extensions.size() );
		createInfo.ppEnabledExtensionNames = extensions.data();

		if ( _bEnableValidationLayers )
		{
			createInfo.enabledLayerCount   = static_cast<uint32>( s_validationLayers.size() );
			createInfo.ppEnabledLayerNames = s_validationLayers.data();
		}
		else
		{
			createInfo.enabledLayerCount = 0;
		}

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
		if ( !_bEnableValidationLayers )
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
		createInfo.hwnd		 = static_cast<HWND>( _hWnd );
		createInfo.hinstance = GetModuleHandle( nullptr );

		if ( vkCreateWin32SurfaceKHR( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create window surface!" );
			return false;
		}
		return true;
#elif defined( SW_PLATFORM_LINUX )
		VkXlibSurfaceCreateInfoKHR createInfo{};
		createInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
		createInfo.dpy	  = (Display*)_displayHandle;
		createInfo.window = (Window)(uintptr_t)_hWnd;

		if ( vkCreateXlibSurfaceKHR( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "Failed to create X11 window surface!" );
			return false;
		}
		return true;
#elif defined( SW_PLATFORM_MACOS )
		VkMetalSurfaceCreateInfoEXT createInfo{};
		createInfo.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
		createInfo.pLayer = _hWnd;

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
		uint32 deviceCount = 0;
		vkEnumeratePhysicalDevices( _instance, &deviceCount, nullptr );
		if ( deviceCount == 0 )
			return false;
		std::vector<VkPhysicalDevice> devices( deviceCount );
		vkEnumeratePhysicalDevices( _instance, &deviceCount, devices.data() );

		for ( const auto& device : devices )
		{
			uint32 queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, nullptr );
			std::vector<VkQueueFamilyProperties> queueFamilies( queueFamilyCount );
			vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

			uint32 i	 = 0;
			bool   found = false;
			for ( const auto& queueFamily : queueFamilies )
			{
				if ( queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT )
				{
					VkBool32 presentSupport = false;
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

	bool VulkanRHIDevice::createLogicalDevice()
	{
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType			 = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
		queueCreateInfo.queueCount		 = 1;
		float queuePriority				 = 1.0f;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceFeatures deviceFeatures{};

		VkPhysicalDeviceVulkan12Features vulkan12Features{};
		vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
		vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
		vulkan12Features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
		vulkan12Features.runtimeDescriptorArray = VK_TRUE;

		VkDeviceCreateInfo		 createInfo{};
		createInfo.sType				   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pNext				   = &vulkan12Features;
		createInfo.pQueueCreateInfos	   = &queueCreateInfo;
		createInfo.queueCreateInfoCount	   = 1;
		createInfo.pEnabledFeatures		   = &deviceFeatures;
		createInfo.enabledExtensionCount   = static_cast<uint32>( s_deviceExtensions.size() );
		createInfo.ppEnabledExtensionNames = s_deviceExtensions.data();

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
		std::vector<VkSurfaceFormatKHR> formats( formatCount );
		vkGetPhysicalDeviceSurfaceFormatsKHR( _physicalDevice, _surface, &formatCount, formats.data() );

		VkSurfaceFormatKHR surfaceFormat = formats[0];
		for ( const auto& availableFormat : formats )
		{
			if ( availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
			{
				surfaceFormat = availableFormat;
				break;
			}
		}

		VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;

		VkExtent2D extent = { _width, _height };

		uint32 imageCount = capabilities.minImageCount + 1;
		if ( capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount )
			imageCount = capabilities.maxImageCount;

		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		if ( !( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR ) )
		{
			if ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR )
				compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
			else if ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR )
				compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
			else if ( capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR )
				compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
		}

		VkSurfaceTransformFlagBitsKHR preTransform = capabilities.currentTransform;
		if ( !( capabilities.supportedTransforms & preTransform ) )
		{
			preTransform = ( capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
		}

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
		_swapChainImages.resize( imageCount );
		vkGetSwapchainImagesKHR( _device, _swapChain, &imageCount, _swapChainImages.data() );

		_swapChainImageFormat  = static_cast<uint32>( surfaceFormat.format );
		_swapChainExtentWidth  = extent.width;
		_swapChainExtentHeight = extent.height;
		return true;
	}

	bool VulkanRHIDevice::createImageViews()
	{
		_swapChainImageViews.resize( _swapChainImages.size() );
		for ( size_t i = 0; i < _swapChainImages.size(); i++ )
		{
			VkImageViewCreateInfo createInfo{};
			createInfo.sType						   = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image						   = _swapChainImages[i];
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

			if ( vkCreateImageView( _device, &createInfo, nullptr, &_swapChainImageViews[i] ) != VK_SUCCESS )
				return false;
		}
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

	bool VulkanRHIDevice::createFramebuffers()
	{
		_swapChainFramebuffers.resize( _swapChainImageViews.size() );
		for ( size_t i = 0; i < _swapChainImageViews.size(); i++ )
		{
			VkImageView				attachments[] = { _swapChainImageViews[i] };
			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType			= VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass		= _renderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments	= attachments;
			framebufferInfo.width			= _swapChainExtentWidth;
			framebufferInfo.height			= _swapChainExtentHeight;
			framebufferInfo.layers			= 1;
			if ( vkCreateFramebuffer( _device, &framebufferInfo, nullptr, &_swapChainFramebuffers[i] ) != VK_SUCCESS )
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
		_commandBuffers.resize( 2 );
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool		 = _commandPool;
		allocInfo.level				 = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = static_cast<uint32>( _commandBuffers.size() );

		if ( vkAllocateCommandBuffers( _device, &allocInfo, _commandBuffers.data() ) != VK_SUCCESS )
			return false;
		return true;
	}

	bool VulkanRHIDevice::createSyncObjects()
	{
		_imageAvailableSemaphores.resize( _swapChainImages.size() );
		_renderFinishedSemaphores.resize( _swapChainImages.size() );
		_inFlightFences.resize( 2 );
		_imagesInFlight.resize( _swapChainImages.size(), VK_NULL_HANDLE );

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for ( size_t i = 0; i < _imageAvailableSemaphores.size(); i++ )
		{
			if ( vkCreateSemaphore( _device, &semaphoreInfo, nullptr, &_imageAvailableSemaphores[i] ) != VK_SUCCESS )
			{
				return false;
			}
		}

		for ( size_t i = 0; i < 2; i++ )
		{
			if ( vkCreateFence( _device, &fenceInfo, nullptr, &_inFlightFences[i] ) != VK_SUCCESS )
			{
				return false;
			}
		}

		for ( size_t i = 0; i < _renderFinishedSemaphores.size(); i++ )
		{
			if ( vkCreateSemaphore( _device, &semaphoreInfo, nullptr, &_renderFinishedSemaphores[i] ) != VK_SUCCESS )
			{
				return false;
			}
		}
		return true;
	}

	bool VulkanRHIDevice::createTriangleResources()
	{
		ShaderCompileDesc vsDesc{};
		vsDesc._filePath			 = "Shaders/BindlessTriangle.hlsl";
		vsDesc._entryPoint			 = "VSMain";
		vsDesc._stage				 = ShaderStage::Vertex;
		vsDesc._targetFormat		 = ShaderTargetFormat::SPIRV_Vulkan;
		ShaderCompileResult vsResult = ShaderCache::getOrCompile( vsDesc );

		ShaderCompileDesc psDesc{};
		psDesc._filePath			 = "Shaders/BindlessTriangle.hlsl";
		psDesc._entryPoint			 = "PSMain";
		psDesc._stage				 = ShaderStage::Pixel;
		psDesc._targetFormat		 = ShaderTargetFormat::SPIRV_Vulkan;
		ShaderCompileResult psResult = ShaderCache::getOrCompile( psDesc );

		if ( vsResult._bSuccess == false || psResult._bSuccess == false )
		{
			SW_LOG_ERROR( "[Vulkan] Failed to compile BindlessTriangle.hlsl for SPIR-V!" );
			return false;
		}

		VkShaderModuleCreateInfo vsInfo{};
		vsInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		vsInfo.codeSize = vsResult._bytecode.size();
		vsInfo.pCode	= reinterpret_cast<const uint32*>( vsResult._bytecode.data() );
		VkShaderModule vertShaderModule;
		if ( vkCreateShaderModule( _device, &vsInfo, nullptr, &vertShaderModule ) != VK_SUCCESS )
			return false;

		VkShaderModuleCreateInfo psInfo{};
		psInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		psInfo.codeSize = psResult._bytecode.size();
		psInfo.pCode	= reinterpret_cast<const uint32*>( psResult._bytecode.data() );
		VkShaderModule fragShaderModule;
		if ( vkCreateShaderModule( _device, &psInfo, nullptr, &fragShaderModule ) != VK_SUCCESS )
			return false;

		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName  = "VSMain";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName  = "PSMain";

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding	 = 0;
		bindingDescription.stride	 = sizeof( float ) * 7;
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription attributeDescriptions[2]{};
		attributeDescriptions[0].binding  = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format	  = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset	  = 0;

		attributeDescriptions[1].binding  = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format	  = VK_FORMAT_R32G32B32A32_SFLOAT;
		attributeDescriptions[1].offset	  = sizeof( float ) * 3;

		vertexInputInfo.vertexBindingDescriptionCount	= 1;
		vertexInputInfo.pVertexBindingDescriptions		= &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount = 2;
		vertexInputInfo.pVertexAttributeDescriptions	= attributeDescriptions;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType					 = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology				 = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType			= VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount	= 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType				   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable		   = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode			   = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth			   = 1.0f;
		rasterizer.cullMode				   = VK_CULL_MODE_NONE;
		rasterizer.frontFace			   = VK_FRONT_FACE_CLOCKWISE;

		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType				   = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable  = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable	= VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType			  = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable	  = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments	  = &colorBlendAttachment;

		std::vector<VkDynamicState>		 dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType			   = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = static_cast<uint32>( dynamicStates.size() );
		dynamicState.pDynamicStates	   = dynamicStates.data();

		VkPushConstantRange pushConstant{};
		pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
		pushConstant.offset		= 0;
		pushConstant.size		= 64;

		VkDescriptorSetLayoutBinding uboLayoutBinding{};
		uboLayoutBinding.binding			= 0;
		uboLayoutBinding.descriptorType		= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboLayoutBinding.descriptorCount	= 1;
		uboLayoutBinding.stageFlags			= VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		uboLayoutBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings	= &uboLayoutBinding;

		if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_descriptorSetLayout ) != VK_SUCCESS )
			return false;

		VkDescriptorSetLayoutBinding uavLayoutBinding{};
		uavLayoutBinding.binding			= 0;
		uavLayoutBinding.descriptorType		= VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		uavLayoutBinding.descriptorCount	= 1024;
		uavLayoutBinding.stageFlags			= VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		uavLayoutBinding.pImmutableSamplers = nullptr;

		VkDescriptorBindingFlags bindlessFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
		VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
		extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		extendedInfo.bindingCount = 1;
		extendedInfo.pBindingFlags = &bindlessFlags;

		VkDescriptorSetLayoutCreateInfo uavLayoutInfo{};
		uavLayoutInfo.sType		   = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		uavLayoutInfo.pNext        = &extendedInfo;
		uavLayoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
		uavLayoutInfo.bindingCount = 1;
		uavLayoutInfo.pBindings	   = &uavLayoutBinding;

		if ( vkCreateDescriptorSetLayout( _device, &uavLayoutInfo, nullptr, &_uavDescriptorSetLayout ) != VK_SUCCESS )
			return false;

		VkDescriptorSetLayoutBinding explicitUavBinding{};
		explicitUavBinding.binding			  = 0;
		explicitUavBinding.descriptorType	  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		explicitUavBinding.descriptorCount	  = 1;
		explicitUavBinding.stageFlags		  = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_ALL_GRAPHICS;
		explicitUavBinding.pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo explicitLayoutInfo{};
		explicitLayoutInfo.sType		= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		explicitLayoutInfo.bindingCount = 1;
		explicitLayoutInfo.pBindings	= &explicitUavBinding;

		if ( vkCreateDescriptorSetLayout( _device, &explicitLayoutInfo, nullptr, &_explicitUavDescriptorSetLayout ) != VK_SUCCESS )
			return false;

		VkBufferCreateInfo uboBufferInfo{};
		uboBufferInfo.sType		  = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		uboBufferInfo.size		  = 16;
		uboBufferInfo.usage		  = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		uboBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if ( vkCreateBuffer( _device, &uboBufferInfo, nullptr, &_dummyUBO ) != VK_SUCCESS )
			return false;

		VkMemoryRequirements uboMemRequirements;
		vkGetBufferMemoryRequirements( _device, _dummyUBO, &uboMemRequirements );

		VkMemoryAllocateInfo uboAllocInfo{};
		uboAllocInfo.sType			= VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		uboAllocInfo.allocationSize = uboMemRequirements.size;

		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties( _physicalDevice, &memProperties );
		uint32 memoryTypeIndex = 0;
		for ( uint32 i = 0; i < memProperties.memoryTypeCount; i++ )
		{
			if ( ( uboMemRequirements.memoryTypeBits & ( 1 << i ) ) && ( memProperties.memoryTypes[i].propertyFlags & ( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) )
			{
				memoryTypeIndex = i;
				break;
			}
		}
		uboAllocInfo.memoryTypeIndex = memoryTypeIndex;
		if ( vkAllocateMemory( _device, &uboAllocInfo, nullptr, &_dummyUBOMemory ) != VK_SUCCESS )
			return false;
		vkBindBufferMemory( _device, _dummyUBO, _dummyUBOMemory, 0 );

		VkDescriptorPoolSize poolSizes[2]{};
		poolSizes[0].type			 = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = 128;
		poolSizes[1].type			 = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSizes[1].descriptorCount = 128;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType		   = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags		   = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.poolSizeCount = 2;
		poolInfo.pPoolSizes	   = poolSizes;
		poolInfo.maxSets	   = 256;
		if ( vkCreateDescriptorPool( _device, &poolInfo, nullptr, &_descriptorPool ) != VK_SUCCESS )
			return false;

		VkDescriptorSetAllocateInfo allocSetInfo{};
		allocSetInfo.sType				= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocSetInfo.descriptorPool		= _descriptorPool;
		allocSetInfo.descriptorSetCount = 1;
		allocSetInfo.pSetLayouts		= &_descriptorSetLayout;
		if ( vkAllocateDescriptorSets( _device, &allocSetInfo, &_descriptorSet ) != VK_SUCCESS )
			return false;

		VkDescriptorBufferInfo dummyBufferInfo{};
		dummyBufferInfo.buffer = _dummyUBO;
		dummyBufferInfo.offset = 0;
		dummyBufferInfo.range  = 16;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet			= _descriptorSet;
		descriptorWrite.dstBinding		= 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType	= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo		= &dummyBufferInfo;

		vkUpdateDescriptorSets( _device, 1, &descriptorWrite, 0, nullptr );

		VkDescriptorSetLayout layouts[6] = {
			_descriptorSetLayout,
			_uavDescriptorSetLayout,
			_explicitUavDescriptorSetLayout,
			_explicitUavDescriptorSetLayout,
			_explicitUavDescriptorSetLayout,
			_explicitUavDescriptorSetLayout
		};

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType				  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges	  = &pushConstant;
		pipelineLayoutInfo.setLayoutCount		  = 6;
		pipelineLayoutInfo.pSetLayouts			  = layouts;

		if ( vkCreatePipelineLayout( _device, &pipelineLayoutInfo, nullptr, &_pipelineLayout ) != VK_SUCCESS )
			return false;

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType				 = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount			 = 2;
		pipelineInfo.pStages			 = shaderStages;
		pipelineInfo.pVertexInputState	 = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState		 = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState	 = &multisampling;
		pipelineInfo.pColorBlendState	 = &colorBlending;
		pipelineInfo.pDynamicState		 = &dynamicState;
		pipelineInfo.layout				 = _pipelineLayout;
		pipelineInfo.renderPass			 = _renderPass;
		pipelineInfo.subpass			 = 0;

		if ( vkCreateGraphicsPipelines( _device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipeline ) != VK_SUCCESS )
			return false;

		vkDestroyShaderModule( _device, fragShaderModule, nullptr );
		vkDestroyShaderModule( _device, vertShaderModule, nullptr );

		struct RHIVertex
		{
			float pos[3];
			float col[4];
		};

		RHIVertex vertices[] = {
			{  { 0.0f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{ { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
			{{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }}
		  };

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType	   = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size		   = sizeof( vertices );
		bufferInfo.usage	   = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &_vertexBuffer ) == VK_SUCCESS )
		{
			VkMemoryRequirements memRequirements;
			vkGetBufferMemoryRequirements( _device, _vertexBuffer, &memRequirements );
			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType			  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize  = memRequirements.size;
			allocInfo.memoryTypeIndex = findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
			if ( vkAllocateMemory( _device, &allocInfo, nullptr, &_vertexBufferMemory ) == VK_SUCCESS )
			{
				vkBindBufferMemory( _device, _vertexBuffer, _vertexBufferMemory, 0 );

				void* data = nullptr;
				if ( vkMapMemory( _device, _vertexBufferMemory, 0, sizeof( vertices ), 0, &data ) == VK_SUCCESS )
				{
					memcpy( data, vertices, sizeof( vertices ) );
					vkUnmapMemory( _device, _vertexBufferMemory );
				}
			}
		}

		return true;
	}

	uint32 VulkanRHIDevice::findMemoryType( uint32 typeFilter, uint32 properties )
	{
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties( _physicalDevice, &memProperties );
		for ( uint32 i = 0; i < memProperties.memoryTypeCount; i++ )
		{
			if ( ( typeFilter & ( 1 << i ) ) && ( memProperties.memoryTypes[i].propertyFlags & properties ) == properties )
			{
				return i;
			}
		}
		return 0;
	}

	void VulkanRHIDevice::cleanupSwapChain()
	{
		if ( _device == nullptr )
			return;

		for ( auto framebuffer : _swapChainFramebuffers )
			vkDestroyFramebuffer( _device, framebuffer, nullptr );
		for ( auto imageView : _swapChainImageViews )
			vkDestroyImageView( _device, imageView, nullptr );
		if ( _swapChain )
			vkDestroySwapchainKHR( _device, _swapChain, nullptr );
	}

	void VulkanRHIDevice::waitIdle()
	{
		if ( _device )
			vkDeviceWaitIdle( _device );
	}

	void VulkanRHIDevice::shutdownInternal()
	{
		if ( _device )
		{
			vkDeviceWaitIdle( _device );

			if ( _commandPool )
			{
				vkDestroyCommandPool( _device, _commandPool, nullptr );
				_commandPool = VK_NULL_HANDLE;
			}

			for ( auto& record : _allocatedBuffers )
			{
				if ( record.buffer != VK_NULL_HANDLE )
					vkDestroyBuffer( _device, record.buffer, nullptr );
				if ( record.memory != VK_NULL_HANDLE )
					vkFreeMemory( _device, record.memory, nullptr );
			}
			_allocatedBuffers.clear();
			_registeredDescriptorSets.clear();

			if ( _vertexBuffer )
				vkDestroyBuffer( _device, _vertexBuffer, nullptr );
			if ( _vertexBufferMemory )
				vkFreeMemory( _device, _vertexBufferMemory, nullptr );
			if ( _pipeline )
				vkDestroyPipeline( _device, _pipeline, nullptr );
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

			cleanupSwapChain();

			for ( size_t i = 0; i < _renderFinishedSemaphores.size(); i++ )
				if ( _renderFinishedSemaphores[i] )
					vkDestroySemaphore( _device, _renderFinishedSemaphores[i], nullptr );
			for ( size_t i = 0; i < _imageAvailableSemaphores.size(); i++ )
				if ( _imageAvailableSemaphores[i] )
					vkDestroySemaphore( _device, _imageAvailableSemaphores[i], nullptr );
			for ( size_t i = 0; i < _inFlightFences.size(); i++ )
				if ( _inFlightFences[i] )
					vkDestroyFence( _device, _inFlightFences[i], nullptr );

			if ( _renderPass )
				vkDestroyRenderPass( _device, _renderPass, nullptr );
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

	void VulkanRHIDevice::recreateSwapChain()
	{
		if ( _width == 0 || _height == 0 )
			return;
		vkDeviceWaitIdle( _device );

		cleanupSwapChain();

		createSwapChain();
		createImageViews();
		createFramebuffers();
	}

	void VulkanRHIDevice::resize( uint32 width, uint32 height )
	{
		_width	= width;
		_height = height;

	}

	void VulkanRHIDevice::beginFrame( float32 clearColor[4] )
	{
		_bFrameStarted = false;
		if ( _width == 0 || _height == 0 )
			return;

		vkWaitForFences( _device, 1, &_inFlightFences[_currentFrame], VK_TRUE, UINT64_MAX );

		VkResult result = vkAcquireNextImageKHR( _device, _swapChain, UINT64_MAX, _imageAvailableSemaphores[_currentFrame], VK_NULL_HANDLE, &_imageIndex );
		if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR )
		{
			recreateSwapChain();
			result = vkAcquireNextImageKHR( _device, _swapChain, UINT64_MAX, _imageAvailableSemaphores[_currentFrame], VK_NULL_HANDLE, &_imageIndex );
			if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR )
				return;
		}

		if ( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR )
			return;

		if ( _imagesInFlight[_imageIndex] != VK_NULL_HANDLE )
		{
			vkWaitForFences( _device, 1, &_imagesInFlight[_imageIndex], VK_TRUE, UINT64_MAX );
		}
		_imagesInFlight[_imageIndex] = _inFlightFences[_currentFrame];

		vkResetFences( _device, 1, &_inFlightFences[_currentFrame] );
		vkResetCommandBuffer( _commandBuffers[_currentFrame], 0 );

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer( _commandBuffers[_currentFrame], &beginInfo );

		_bFrameStarted = true;

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType			 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass		 = _renderPass;
		renderPassInfo.framebuffer		 = _swapChainFramebuffers[_imageIndex];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = { _swapChainExtentWidth, _swapChainExtentHeight };

		VkClearValue clearValue		   = { { { clearColor[0], clearColor[1], clearColor[2], clearColor[3] } } };
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues	   = &clearValue;

		vkCmdBeginRenderPass( _commandBuffers[_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );

		constexpr float32 kDefaultViewportX		   = 0.0f;
		constexpr float32 kDefaultViewportMinDepth = 0.0f;
		constexpr float32 kDefaultViewportMaxDepth = 1.0f;

		VkViewport viewport{};
		viewport.x		  = kDefaultViewportX;

		viewport.y		  = static_cast<float32>( _swapChainExtentHeight );
		viewport.width	  = static_cast<float32>( _swapChainExtentWidth );
		viewport.height	  = -static_cast<float32>( _swapChainExtentHeight );
		viewport.minDepth = kDefaultViewportMinDepth;
		viewport.maxDepth = kDefaultViewportMaxDepth;
		vkCmdSetViewport( _commandBuffers[_currentFrame], 0, 1, &viewport );

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = { _swapChainExtentWidth, _swapChainExtentHeight };
		vkCmdSetScissor( _commandBuffers[_currentFrame], 0, 1, &scissor );
	}

	void VulkanRHIDevice::endFrame( bool vsync )
	{
		(void)vsync;
		if ( !_bFrameStarted )
			return;

		vkCmdEndRenderPass( _commandBuffers[_currentFrame] );
		vkEndCommandBuffer( _commandBuffers[_currentFrame] );

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore			 waitSemaphores[] = { _imageAvailableSemaphores[_currentFrame] };
		VkPipelineStageFlags waitStages[]	  = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount		  = 1;
		submitInfo.pWaitSemaphores			  = waitSemaphores;
		submitInfo.pWaitDstStageMask		  = waitStages;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers	  = &_commandBuffers[_currentFrame];

		VkSemaphore signalSemaphores[]	= { _renderFinishedSemaphores[_imageIndex] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores	= signalSemaphores;

		vkQueueSubmit( _graphicsQueue, 1, &submitInfo, _inFlightFences[_currentFrame] );

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType			   = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores	   = signalSemaphores;

		VkSwapchainKHR swapChains[] = { _swapChain };
		presentInfo.swapchainCount	= 1;
		presentInfo.pSwapchains		= swapChains;
		presentInfo.pImageIndices	= &_imageIndex;

		vkQueuePresentKHR( _graphicsQueue, &presentInfo );

		_currentFrame = ( _currentFrame + 1 ) % 2;
	}

	RHIBufferHandle VulkanRHIDevice::createConstantBuffer( uint32 size )
	{
		if ( _device == nullptr || size == 0 )
			return 0;

		uint32 alignedSize = ( size + 255u ) & ~255u;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType	   = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size		   = alignedSize;
		bufferInfo.usage	   = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer buffer = VK_NULL_HANDLE;
		if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &buffer ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "[Vulkan] Failed to create VkBuffer for Constant Buffer!" );
			return 0;
		}

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements( _device, buffer, &memRequirements );

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType			  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

		VkDeviceMemory memory = VK_NULL_HANDLE;
		if ( vkAllocateMemory( _device, &allocInfo, nullptr, &memory ) != VK_SUCCESS )
		{
			vkDestroyBuffer( _device, buffer, nullptr );
			SW_LOG_ERROR( "[Vulkan] Failed to allocate memory for Constant Buffer!" );
			return 0;
		}

		vkBindBufferMemory( _device, buffer, memory, 0 );

		VulkanBufferRecord record{};
		record.buffer = buffer;
		record.memory = memory;
		record.size	  = size;

		_allocatedBuffers.push_back( record );
		return static_cast<RHIBufferHandle>( _allocatedBuffers.size() );
	}

	RHIBufferHandle VulkanRHIDevice::createStructuredBuffer( uint32 elementSize, uint32 elementCount )
	{
		if ( _device == nullptr || elementSize == 0 || elementCount == 0 )
			return 0;

		uint32 size = elementSize * elementCount;
		uint32 alignedSize = ( size + 255u ) & ~255u;

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType	   = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size		   = alignedSize;
		bufferInfo.usage	   = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer buffer = VK_NULL_HANDLE;
		if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &buffer ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "[Vulkan] Failed to create VkBuffer for Structured Buffer!" );
			return 0;
		}

		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements( _device, buffer, &memRequirements );

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType			  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );

		VkDeviceMemory memory = VK_NULL_HANDLE;
		if ( vkAllocateMemory( _device, &allocInfo, nullptr, &memory ) != VK_SUCCESS )
		{
			vkDestroyBuffer( _device, buffer, nullptr );
			SW_LOG_ERROR( "[Vulkan] Failed to allocate memory for Structured Buffer!" );
			return 0;
		}

		vkBindBufferMemory( _device, buffer, memory, 0 );

		VulkanBufferRecord record{};
		record.buffer = buffer;
		record.memory = memory;
		record.size	  = size;

		_allocatedBuffers.push_back( record );
		return static_cast<RHIBufferHandle>( _allocatedBuffers.size() );
	}

	void VulkanRHIDevice::updateConstantBuffer( RHIBufferHandle buffer, const void* data, uint32 size )
	{
		if ( buffer == 0 || buffer > static_cast<RHIBufferHandle>( _allocatedBuffers.size() ) || data == nullptr || size == 0 )
			return;

		const VulkanBufferRecord& record = _allocatedBuffers[buffer - 1];
		if ( record.memory == VK_NULL_HANDLE )
			return;

		void* mapped = nullptr;
		if ( vkMapMemory( _device, record.memory, 0, size, 0, &mapped ) == VK_SUCCESS )
		{
			memcpy( mapped, data, size );
			vkUnmapMemory( _device, record.memory );
		}
	}

	void VulkanRHIDevice::updateStructuredBuffer( RHIBufferHandle buffer, const void* data, uint32 size )
	{
		updateConstantBuffer( buffer, data, size );
	}

	void VulkanRHIDevice::destroyBuffer( RHIBufferHandle buffer )
	{
		if ( buffer == 0 || buffer > static_cast<RHIBufferHandle>( _allocatedBuffers.size() ) )
			return;

		VulkanBufferRecord& record = _allocatedBuffers[buffer - 1];
		if ( record.buffer != VK_NULL_HANDLE )
		{
			vkDestroyBuffer( _device, record.buffer, nullptr );
			record.buffer = VK_NULL_HANDLE;
		}
		if ( record.memory != VK_NULL_HANDLE )
		{
			vkFreeMemory( _device, record.memory, nullptr );
			record.memory = VK_NULL_HANDLE;
		}
	}

	RHIDescriptorIndex VulkanRHIDevice::registerBindlessResource( RHIBufferHandle buffer )
	{
		if ( buffer == 0 || buffer > static_cast<RHIBufferHandle>( _allocatedBuffers.size() ) || _descriptorPool == VK_NULL_HANDLE || _descriptorSetLayout == VK_NULL_HANDLE )
		{
			return kInvalidDescriptorIndex;
		}

		const VulkanBufferRecord& record = _allocatedBuffers[buffer - 1];

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool	 = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts		 = &_descriptorSetLayout;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		if ( vkAllocateDescriptorSets( _device, &allocInfo, &descriptorSet ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "[Vulkan] Failed to allocate VkDescriptorSet for bindless constant buffer!" );
			return kInvalidDescriptorIndex;
		}

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = record.buffer;
		bufferInfo.offset = 0;
		bufferInfo.range  = record.size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet			= descriptorSet;
		descriptorWrite.dstBinding		= 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType	= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo		= &bufferInfo;

		vkUpdateDescriptorSets( _device, 1, &descriptorWrite, 0, nullptr );

		RHIDescriptorIndex descriptorIndex;
		if ( _bindlessFreeList.empty() == false )
		{
			descriptorIndex = _bindlessFreeList.back();
			_bindlessFreeList.pop_back();
		}
		else
		{
			descriptorIndex = static_cast<RHIDescriptorIndex>( _registeredDescriptorSets.size() );
		}

		if ( descriptorIndex >= _registeredDescriptorSets.size() )
		{
			_registeredDescriptorSets.resize( descriptorIndex + 1 );
		}
		_registeredDescriptorSets[descriptorIndex] = descriptorSet;
		return descriptorIndex;
	}

	void VulkanRHIDevice::unregisterBindlessResource( RHIDescriptorIndex index )
	{
		if ( index < _registeredDescriptorSets.size() )
		{
			VkDescriptorSet set = _registeredDescriptorSets[index];
			if ( set != VK_NULL_HANDLE )
			{
				vkFreeDescriptorSets( _device, _descriptorPool, 1, &set );
				_registeredDescriptorSets[index] = VK_NULL_HANDLE;
			}
			_bindlessFreeList.push_back( index );
		}
	}

	RHIDescriptorIndex VulkanRHIDevice::registerBindlessUAV( RHIBufferHandle buffer )
	{
		if ( buffer == 0 || buffer > static_cast<RHIBufferHandle>( _allocatedBuffers.size() ) || _descriptorPool == VK_NULL_HANDLE || _uavDescriptorSetLayout == VK_NULL_HANDLE )
		{
			return kInvalidDescriptorIndex;
		}

		const VulkanBufferRecord& record = _allocatedBuffers[buffer - 1];

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool	 = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts		 = &_explicitUavDescriptorSetLayout;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		if ( vkAllocateDescriptorSets( _device, &allocInfo, &descriptorSet ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "[Vulkan] Failed to allocate VkDescriptorSet for UAV!" );
			return kInvalidDescriptorIndex;
		}

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = record.buffer;
		bufferInfo.offset = 0;
		bufferInfo.range  = record.size;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet			= descriptorSet;
		descriptorWrite.dstBinding		= 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType	= VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo		= &bufferInfo;

		vkUpdateDescriptorSets( _device, 1, &descriptorWrite, 0, nullptr );

		RHIDescriptorIndex descriptorIndex;
		if ( _uavFreeList.empty() == false )
		{
			descriptorIndex = _uavFreeList.back();
			_uavFreeList.pop_back();
		}
		else
		{
			descriptorIndex = static_cast<RHIDescriptorIndex>( _registeredUAVs.size() );
		}

		if ( descriptorIndex >= _registeredUAVs.size() )
		{
			_registeredUAVs.resize( descriptorIndex + 1 );
		}
		_registeredUAVs[descriptorIndex] = descriptorSet;
		return descriptorIndex;
	}

	void VulkanRHIDevice::unregisterBindlessUAV( RHIDescriptorIndex index )
	{
		if ( index < _registeredUAVs.size() )
		{
			VkDescriptorSet set = _registeredUAVs[index];
			if ( set != VK_NULL_HANDLE )
			{
				vkFreeDescriptorSets( _device, _descriptorPool, 1, &set );
				_registeredUAVs[index] = VK_NULL_HANDLE;
			}
			_uavFreeList.push_back( index );
		}
	}

	void VulkanRHIDevice::bindComputeUAV( RHIDescriptorIndex index, uint32 slot )
	{
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE || _pipelineLayout == VK_NULL_HANDLE )
			return;

		if ( index < static_cast<RHIDescriptorIndex>( _registeredUAVs.size() ) && _registeredUAVs[index] != VK_NULL_HANDLE )
		{
			VkDescriptorSet descSet = _registeredUAVs[index];

			vkCmdBindDescriptorSets( _commandBuffers[_currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 2 + slot, 1, &descSet, 0, nullptr );

		}
	}

	void VulkanRHIDevice::drawTriangle( RHIDescriptorIndex materialDescriptorIndex )
	{
		if ( !_bFrameStarted )
			return;
		if ( _pipeline == VK_NULL_HANDLE || _pipelineLayout == VK_NULL_HANDLE )
			return;

		vkCmdBindPipeline( _commandBuffers[_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline );

		if ( materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _registeredDescriptorSets.size() ) && _registeredDescriptorSets[materialDescriptorIndex] != VK_NULL_HANDLE )
		{
			VkDescriptorSet descSet = _registeredDescriptorSets[materialDescriptorIndex];
			vkCmdBindDescriptorSets( _commandBuffers[_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &descSet, 0, nullptr );
		}
		else if ( _descriptorSet != VK_NULL_HANDLE )
		{
			vkCmdBindDescriptorSets( _commandBuffers[_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr );
		}

		uint32 matIndex = static_cast<uint32>( materialDescriptorIndex );
		vkCmdPushConstants( _commandBuffers[_currentFrame], _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( uint32 ), &matIndex );

		if ( _vertexBuffer != VK_NULL_HANDLE )
		{
			VkBuffer	 vertexBuffers[] = { _vertexBuffer };
			VkDeviceSize offsets[]		 = { 0 };
			vkCmdBindVertexBuffers( _commandBuffers[_currentFrame], 0, 1, vertexBuffers, offsets );
		}

		vkCmdDraw( _commandBuffers[_currentFrame], 3, 1, 0, 0 );
	}

	void VulkanRHIDevice::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE )
			return;
		vkCmdDispatch( _commandBuffers[_currentFrame], threadGroupCountX, threadGroupCountY, threadGroupCountZ );
	}

	void VulkanRHIDevice::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues )
	{
		(void)rootParameterIndex;
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE || _pipelineLayout == VK_NULL_HANDLE )
			return;

		vkCmdPushConstants( _commandBuffers[_currentFrame], _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, destOffsetIn32BitValues * 4, num32BitValues * 4, data );
	}

	void VulkanRHIDevice::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE || argumentBuffer == 0 || argumentBuffer > static_cast<RHIBufferHandle>( _allocatedBuffers.size() ) )
			return;

		const VulkanBufferRecord& record = _allocatedBuffers[argumentBuffer - 1];
		if ( record.buffer != VK_NULL_HANDLE )
		{
			if ( _vertexBuffer != VK_NULL_HANDLE )
			{
				VkBuffer	 vertexBuffers[] = { _vertexBuffer };
				VkDeviceSize offsets[]		 = { 0 };
				vkCmdBindVertexBuffers( _commandBuffers[_currentFrame], 0, 1, vertexBuffers, offsets );
			}
			vkCmdDrawIndirect( _commandBuffers[_currentFrame], record.buffer, argumentBufferOffset, 1, sizeof( VkDrawIndirectCommand ) );
		}
	}

	void VulkanRHIDevice::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE || argumentBuffer == 0 || argumentBuffer > static_cast<RHIBufferHandle>( _allocatedBuffers.size() ) )
			return;

		const VulkanBufferRecord& record = _allocatedBuffers[argumentBuffer - 1];
		if ( record.buffer != VK_NULL_HANDLE )
		{
			vkCmdDispatchIndirect( _commandBuffers[_currentFrame], record.buffer, argumentBufferOffset );
		}
	}
	void VulkanRHIDevice::beginEventMarker( const utf8* ) {}
	void VulkanRHIDevice::endEventMarker() {}

	class VulkanCommandList final : public IRHICommandList
	{
	public:
		VulkanCommandList( VulkanRHIDevice* device ) : _device( device ) {}
		void beginCommandList() override {}
		void endCommandList() override {}
		void setViewport( const RHIViewport& vp ) override { (void)vp; }
		void setPipelineState( RHIPipelineStateHandle pso ) override
		{
			if ( _device )
				_device->setPipelineState( pso );
		}
		void beginRenderPass( const RHIRenderPassBeginInfo& beginInfo ) override
		{
			if ( _device )
				_device->beginRenderPass( beginInfo );
		}
		void endRenderPass() override
		{
			if ( _device )
				_device->endRenderPass();
		}
		void drawTriangle( RHIDescriptorIndex materialDescriptorIndex ) override
		{
			if ( _device )
				_device->drawTriangle( materialDescriptorIndex );
		}
		void dispatchCompute( uint32 x, uint32 y, uint32 z ) override
		{
			if ( _device )
				_device->dispatchCompute( x, y, z );
		}
		void setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues = 0 ) override
		{
			if ( _device )
				_device->setComputeRootConstants( rootParameterIndex, num32BitValues, data, destOffsetIn32BitValues );
		}
		void drawIndirect( RHIBufferHandle argBuf, uint32 offset ) override
		{
			if ( _device )
				_device->drawIndirect( argBuf, offset );
		}
		void dispatchIndirect( RHIBufferHandle argBuf, uint32 offset ) override
		{
			if ( _device )
				_device->dispatchIndirect( argBuf, offset );
		}
		void beginEventMarker( const utf8* name ) override
		{
			if ( _device )
				_device->beginEventMarker( name );
		}
		void endEventMarker() override
		{
			if ( _device )
				_device->endEventMarker();
		}

	private:
		VulkanRHIDevice* _device;
	};

	std::unique_ptr<IRHICommandList> VulkanRHIDevice::createCommandList()
	{
		return std::make_unique<VulkanCommandList>( this );
	}
	void VulkanRHIDevice::executeCommandList( IRHICommandList* cmdList ) { (void)cmdList; }

	RHIPipelineStateHandle VulkanRHIDevice::createPipelineState( const RHIPipelineStateDesc& desc )
	{
		ShaderCompileDesc vsDesc{};
		vsDesc._filePath			 = desc._vertexShaderPath;
		vsDesc._entryPoint			 = desc._vertexEntryPoint;
		vsDesc._stage				 = ShaderStage::Vertex;
		vsDesc._targetFormat		 = ShaderTargetFormat::SPIRV_Vulkan;
		ShaderCompileResult vsResult = ShaderCache::getOrCompile( vsDesc );

		ShaderCompileDesc psDesc{};
		psDesc._filePath			 = desc._pixelShaderPath;
		psDesc._entryPoint			 = desc._pixelEntryPoint;
		psDesc._stage				 = ShaderStage::Pixel;
		psDesc._targetFormat		 = ShaderTargetFormat::SPIRV_Vulkan;
		ShaderCompileResult psResult = ShaderCache::getOrCompile( psDesc );

		if ( !vsResult._bSuccess || !psResult._bSuccess )
		{
			VulkanPipelineStateRecord record{};
			record.pipeline = _pipeline;
			_pipelineStates.push_back( record );
			return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
		}

		VkShaderModuleCreateInfo vsInfo{};
		vsInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		vsInfo.codeSize = vsResult._bytecode.size();
		vsInfo.pCode	= reinterpret_cast<const uint32*>( vsResult._bytecode.data() );
		VkShaderModule vertShaderModule;
		vkCreateShaderModule( _device, &vsInfo, nullptr, &vertShaderModule );

		VkShaderModuleCreateInfo psInfo{};
		psInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		psInfo.codeSize = psResult._bytecode.size();
		psInfo.pCode	= reinterpret_cast<const uint32*>( psResult._bytecode.data() );
		VkShaderModule fragShaderModule;
		vkCreateShaderModule( _device, &psInfo, nullptr, &fragShaderModule );

		VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module = vertShaderModule;
		vertShaderStageInfo.pName  = "VSMain";

		VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module = fragShaderModule;
		fragShaderStageInfo.pName  = "PSMain";

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		vertexInputInfo.vertexBindingDescriptionCount	= 0;
		vertexInputInfo.pVertexBindingDescriptions		= nullptr;
		vertexInputInfo.vertexAttributeDescriptionCount = 0;
		vertexInputInfo.pVertexAttributeDescriptions	= nullptr;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType					 = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology				 = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType			= VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount	= 1;

		VkPipelineRasterizationStateCreateInfo rasterizer{};
		rasterizer.sType				   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable		   = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode			   = (desc._fillMode == RHIFillMode::Wireframe) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth			   = 1.0f;
		rasterizer.cullMode				   = (desc._cullMode == RHICullMode::Front) ? VK_CULL_MODE_FRONT_BIT : ((desc._cullMode == RHICullMode::Back) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);
		rasterizer.frontFace			   = VK_FRONT_FACE_CLOCKWISE;

		VkPipelineMultisampleStateCreateInfo multisampling{};
		multisampling.sType				   = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable  = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable	= VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType			  = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable	  = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments	  = &colorBlendAttachment;

		std::vector<VkDynamicState>		 dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType			   = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = static_cast<uint32>( dynamicStates.size() );
		dynamicState.pDynamicStates	   = dynamicStates.data();

		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType				 = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount			 = 2;
		pipelineInfo.pStages			 = shaderStages;
		pipelineInfo.pVertexInputState	 = &vertexInputInfo;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState		 = &viewportState;
		pipelineInfo.pRasterizationState = &rasterizer;
		pipelineInfo.pMultisampleState	 = &multisampling;
		pipelineInfo.pColorBlendState	 = &colorBlending;
		pipelineInfo.pDynamicState		 = &dynamicState;
		pipelineInfo.layout				 = _pipelineLayout;
		pipelineInfo.renderPass			 = _renderPass;
		pipelineInfo.subpass			 = 0;
		pipelineInfo.basePipelineHandle	 = VK_NULL_HANDLE;

		VkPipeline newPipeline;
		if ( vkCreateGraphicsPipelines( _device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline ) != VK_SUCCESS )
		{
			newPipeline = _pipeline;
		}

		vkDestroyShaderModule( _device, vertShaderModule, nullptr );
		vkDestroyShaderModule( _device, fragShaderModule, nullptr );

		VulkanPipelineStateRecord record{};
		record.pipeline = newPipeline;
		_pipelineStates.push_back( record );
		return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
	}

	RHIPipelineStateHandle VulkanRHIDevice::createComputePipelineState( const std::string& shaderPath, const std::string& entryPoint )
	{
		ShaderCompileDesc csDesc{};
		csDesc._filePath			 = shaderPath;
		csDesc._entryPoint			 = entryPoint;
		csDesc._stage				 = ShaderStage::Compute;
		csDesc._targetFormat		 = ShaderTargetFormat::SPIRV_Vulkan;
		ShaderCompileResult csResult = ShaderCache::getOrCompile( csDesc );

		if ( !csResult._bSuccess )
		{
			VulkanPipelineStateRecord record{};
			_pipelineStates.push_back( record );
			return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
		}

		VkShaderModuleCreateInfo csInfo{};
		csInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		csInfo.codeSize = csResult._bytecode.size();
		csInfo.pCode	= reinterpret_cast<const uint32*>( csResult._bytecode.data() );
		VkShaderModule compShaderModule;
		vkCreateShaderModule( _device, &csInfo, nullptr, &compShaderModule );

		VkPipelineShaderStageCreateInfo compShaderStageInfo{};
		compShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		compShaderStageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
		compShaderStageInfo.module = compShaderModule;
		compShaderStageInfo.pName  = "CSMain";

		VkComputePipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType	= VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = _pipelineLayout;
		pipelineInfo.stage  = compShaderStageInfo;

		VkPipeline newPipeline;
		if ( vkCreateComputePipelines( _device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline ) != VK_SUCCESS )
		{
			newPipeline = VK_NULL_HANDLE;
		}

		vkDestroyShaderModule( _device, compShaderModule, nullptr );

		VulkanPipelineStateRecord record{};
		record.pipeline = newPipeline;
		_pipelineStates.push_back( record );
		return static_cast<RHIPipelineStateHandle>( _pipelineStates.size() );
	}

	void VulkanRHIDevice::destroyPipelineState( RHIPipelineStateHandle pso )
	{
		if ( pso == 0 || pso > _pipelineStates.size() )
			return;
		_pipelineStates[pso - 1].pipeline = VK_NULL_HANDLE;
	}

	void VulkanRHIDevice::setPipelineState( RHIPipelineStateHandle pso )
	{
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE )
			return;

		VkPipeline pipe = _pipeline;
		if ( pso > 0 && pso <= _pipelineStates.size() && _pipelineStates[pso - 1].pipeline != VK_NULL_HANDLE )
		{
			pipe = _pipelineStates[pso - 1].pipeline;
		}

		if ( pipe != VK_NULL_HANDLE )
		{
			vkCmdBindPipeline( _commandBuffers[_currentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe );
		}
	}

	void VulkanRHIDevice::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE )
			return;

		if ( pso > 0 && pso <= _pipelineStates.size() && _pipelineStates[pso - 1].pipeline != VK_NULL_HANDLE )
		{
			vkCmdBindPipeline( _commandBuffers[_currentFrame], VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineStates[pso - 1].pipeline );
		}
	}

	RHIRenderPassHandle VulkanRHIDevice::createRenderPass( const RHIRenderPassDesc& desc )
	{
		(void)desc;
		VulkanRenderPassRecord record{};
		record.renderPass = _renderPass;
		_renderPasses.push_back( record );
		return static_cast<RHIRenderPassHandle>( _renderPasses.size() );
	}

	void VulkanRHIDevice::destroyRenderPass( RHIRenderPassHandle pass )
	{
		(void)pass;
	}

	void VulkanRHIDevice::beginRenderPass( const RHIRenderPassBeginInfo& beginInfo )
	{
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE || _renderPass == VK_NULL_HANDLE || _swapChainFramebuffers.empty() )
			return;

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType			 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass		 = _renderPass;
		renderPassInfo.framebuffer		 = _swapChainFramebuffers[_imageIndex];
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = { _swapChainExtentWidth, _swapChainExtentHeight };

		VkClearValue clearValue		   = { { { beginInfo._clearColor[0], beginInfo._clearColor[1], beginInfo._clearColor[2], beginInfo._clearColor[3] } } };
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues	   = &clearValue;

		vkCmdBeginRenderPass( _commandBuffers[_currentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );
	}

	void VulkanRHIDevice::endRenderPass()
	{
		if ( _commandBuffers.empty() || _commandBuffers[_currentFrame] == VK_NULL_HANDLE )
			return;
		vkCmdEndRenderPass( _commandBuffers[_currentFrame] );
	}
}
