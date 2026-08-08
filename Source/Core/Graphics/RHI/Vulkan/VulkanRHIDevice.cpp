/**
 * @file VulkanRHIDevice.cpp
 * @brief Vulkan RHI 디바이스 구현
 */
#include "Core/CoreMinimal.h"

#include "VulkanRHIDevice.h"
#include "Core/Graphics/RHI/RHIDeferredCommandList.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Graphics/Shader/ShaderCache.h"

#include <vulkan/vulkan.h>
#if defined( SW_PLATFORM_WINDOWS )
	#include <vulkan/vulkan_win32.h>
#elif defined( SW_PLATFORM_LINUX )
	#include <xcb/xcb.h>
	#include <X11/Xlib-xcb.h>
	#include <vulkan/vulkan_xlib.h>
	#include <vulkan/vulkan_xcb.h>
#elif defined( SW_PLATFORM_MACOS )
	#include <vulkan/vulkan_metal.h>
#endif
#include <cstring>
namespace sw
{
	static const std::vector<const utf8*> s_validationLayers = {
		"VK_LAYER_KHRONOS_validation" };

	static const std::vector<const char*> s_deviceExtensions = {
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
		: _bFrameStarted{ 0 }
		, _bOffscreenPassActive{ 0 }
		, _linuxWsi{ 0 }
		, _reservedFlags{ 0 }
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

		BLOCK( "Validation Layer Setup" )
		{
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

		BLOCK( "Triangle Resources" )
		{
			if ( createTriangleResources() == false )
				return false;
		}

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

		uint32 availableExtCount = 0;
		vkEnumerateInstanceExtensionProperties( nullptr, &availableExtCount, nullptr );
		std::vector<VkExtensionProperties> availableExts( availableExtCount );
		if ( availableExtCount > 0 )
			vkEnumerateInstanceExtensionProperties( nullptr, &availableExtCount, availableExts.data() );

		auto hasExtension = [&availableExts]( const char* name ) -> bool
		{
			for ( const VkExtensionProperties& ext : availableExts )
			{
				if ( std::strcmp( ext.extensionName, name ) == 0 )
					return true;
			}
			return false;
		};

#if defined( SW_PLATFORM_WINDOWS )
		if ( hasExtension( VK_KHR_WIN32_SURFACE_EXTENSION_NAME ) == false )
		{
			SW_LOG_ERROR( "VK_KHR_win32_surface is not available." );
			return false;
		}
		extensions.push_back( VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
#elif defined( SW_PLATFORM_LINUX )
		// WSLg/gfxstream often exposes xcb but not xlib.
		_linuxWsi = 0;
		if ( hasExtension( VK_KHR_XLIB_SURFACE_EXTENSION_NAME ) )
		{
			extensions.push_back( VK_KHR_XLIB_SURFACE_EXTENSION_NAME );
			_linuxWsi = 1;
			SW_LOG_INFO( "Vulkan WSI: VK_KHR_xlib_surface" );
		}
		else if ( hasExtension( VK_KHR_XCB_SURFACE_EXTENSION_NAME ) )
		{
			extensions.push_back( VK_KHR_XCB_SURFACE_EXTENSION_NAME );
			_linuxWsi = 2;
			SW_LOG_INFO( "Vulkan WSI: VK_KHR_xcb_surface (xlib unavailable)" );
		}
		else
		{
			SW_LOG_ERROR( "No Vulkan X11 WSI extension (VK_KHR_xlib_surface / VK_KHR_xcb_surface)." );
			return false;
		}
#elif defined( SW_PLATFORM_MACOS )
		if ( hasExtension( VK_EXT_METAL_SURFACE_EXTENSION_NAME ) == false )
		{
			SW_LOG_ERROR( "VK_EXT_metal_surface is not available." );
			return false;
		}
		extensions.push_back( VK_EXT_METAL_SURFACE_EXTENSION_NAME );
#endif
		if ( _bEnableValidationLayers && hasExtension( VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) )
			extensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
		else if ( _bEnableValidationLayers )
			_bEnableValidationLayers = false;

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
		Display* display = static_cast<Display*>( _displayHandle );
		Window	 window	 = static_cast<Window>( reinterpret_cast<uintptr_t>( _hWnd ) );
		if ( display == nullptr || window == 0 )
		{
			SW_LOG_ERROR( "Invalid X11 display/window for Vulkan surface." );
			return false;
		}

		if ( _linuxWsi == 1 )
		{
			auto* createFn = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
				vkGetInstanceProcAddr( _instance, "vkCreateXlibSurfaceKHR" ) );
			if ( createFn == nullptr )
			{
				SW_LOG_ERROR( "vkCreateXlibSurfaceKHR not available from Vulkan loader!" );
				return false;
			}

			VkXlibSurfaceCreateInfoKHR createInfo{};
			createInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
			createInfo.dpy	  = display;
			createInfo.window = window;
			if ( createFn( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
			{
				SW_LOG_ERROR( "Failed to create Xlib Vulkan surface!" );
				return false;
			}
			return true;
		}

		if ( _linuxWsi == 2 )
		{
			auto* createFn = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
				vkGetInstanceProcAddr( _instance, "vkCreateXcbSurfaceKHR" ) );
			if ( createFn == nullptr )
			{
				SW_LOG_ERROR( "vkCreateXcbSurfaceKHR not available from Vulkan loader!" );
				return false;
			}

			xcb_connection_t* connection = XGetXCBConnection( display );
			if ( connection == nullptr )
			{
				SW_LOG_ERROR( "XGetXCBConnection failed — cannot create Vulkan xcb surface." );
				return false;
			}

			VkXcbSurfaceCreateInfoKHR createInfo{};
			createInfo.sType	  = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
			createInfo.connection = connection;
			createInfo.window	  = static_cast<xcb_window_t>( window );
			if ( createFn( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
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

		VkPhysicalDeviceVulkan12Features available12{};
		available12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		VkPhysicalDeviceFeatures2 features2{};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &available12;
		vkGetPhysicalDeviceFeatures2( _physicalDevice, &features2 );

		VkPhysicalDeviceVulkan12Features vulkan12Features{};
		vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		vulkan12Features.descriptorBindingPartiallyBound			   = available12.descriptorBindingPartiallyBound;
		vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = available12.descriptorBindingStorageBufferUpdateAfterBind;
		vulkan12Features.descriptorBindingSampledImageUpdateAfterBind  = available12.descriptorBindingSampledImageUpdateAfterBind;
		vulkan12Features.shaderStorageBufferArrayNonUniformIndexing	   = available12.shaderStorageBufferArrayNonUniformIndexing;
		vulkan12Features.shaderSampledImageArrayNonUniformIndexing	   = available12.shaderSampledImageArrayNonUniformIndexing;
		vulkan12Features.runtimeDescriptorArray						   = available12.runtimeDescriptorArray;

		VkDeviceCreateInfo createInfo{};
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
		ShaderCompileResult vsResult{};
		ShaderCompileResult psResult{};
		VkShaderModule		vertShaderModule = VK_NULL_HANDLE;
		VkShaderModule		fragShaderModule = VK_NULL_HANDLE;

		BLOCK( "Compile HLSL → SPIR-V / Create Shader Modules" )
		{
			ShaderCompileDesc vsDesc{};
			vsDesc._filePath	 = "Shaders/BindlessTriangle.hlsl";
			vsDesc._entryPoint	 = "VSMain";
			vsDesc._stage		 = ShaderStage::Vertex;
			vsDesc._targetFormat = ShaderTargetFormat::SPIRV_Vulkan;
			vsResult			 = ShaderCache::getOrCompile( vsDesc );

			ShaderCompileDesc psDesc{};
			psDesc._filePath	 = "Shaders/BindlessTriangle.hlsl";
			psDesc._entryPoint	 = "PSMain";
			psDesc._stage		 = ShaderStage::Pixel;
			psDesc._targetFormat = ShaderTargetFormat::SPIRV_Vulkan;
			psResult			 = ShaderCache::getOrCompile( psDesc );

			if ( vsResult._bSuccess == false || psResult._bSuccess == false )
			{
				SW_LOG_ERROR( "[Vulkan] Failed to compile BindlessTriangle.hlsl for SPIR-V!" );
				return false;
			}

			VkShaderModuleCreateInfo vsInfo{};
			vsInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			vsInfo.codeSize = vsResult._bytecode.size();
			vsInfo.pCode	= reinterpret_cast<const uint32*>( vsResult._bytecode.data() );
			if ( vkCreateShaderModule( _device, &vsInfo, nullptr, &vertShaderModule ) != VK_SUCCESS )
				return false;

			VkShaderModuleCreateInfo psInfo{};
			psInfo.sType	= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			psInfo.codeSize = psResult._bytecode.size();
			psInfo.pCode	= reinterpret_cast<const uint32*>( psResult._bytecode.data() );
			if ( vkCreateShaderModule( _device, &psInfo, nullptr, &fragShaderModule ) != VK_SUCCESS )
				return false;
		}

		BLOCK( "Graphics Pipeline / Descriptor Sets" )
		{
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

			VkDescriptorBindingFlags					bindlessFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
			VkDescriptorSetLayoutBindingFlagsCreateInfo extendedInfo{};
			extendedInfo.sType		   = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
			extendedInfo.bindingCount  = 1;
			extendedInfo.pBindingFlags = &bindlessFlags;

			VkDescriptorSetLayoutCreateInfo uavLayoutInfo{};
			uavLayoutInfo.sType		   = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			uavLayoutInfo.pNext		   = &extendedInfo;
			uavLayoutInfo.flags		   = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
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

			VkDescriptorSetLayoutBinding textureLayoutBinding{};
			textureLayoutBinding.binding			 = 0;
			textureLayoutBinding.descriptorType		 = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			textureLayoutBinding.descriptorCount	 = 1;
			textureLayoutBinding.stageFlags			 = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;
			textureLayoutBinding.pImmutableSamplers = nullptr;

			VkDescriptorSetLayoutCreateInfo textureLayoutInfo{};
			textureLayoutInfo.sType		   = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			textureLayoutInfo.bindingCount = 1;
			textureLayoutInfo.pBindings	   = &textureLayoutBinding;
			if ( vkCreateDescriptorSetLayout( _device, &textureLayoutInfo, nullptr, &_textureDescriptorSetLayout ) != VK_SUCCESS )
				return false;

			VkDescriptorPoolSize poolSizes[3]{};
			poolSizes[0].type			 = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			poolSizes[0].descriptorCount = 256;
			poolSizes[1].type			 = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			poolSizes[1].descriptorCount = 256;
			poolSizes[2].type			 = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			poolSizes[2].descriptorCount = 256;

			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType		   = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.flags		   = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			poolInfo.poolSizeCount = 3;
			poolInfo.pPoolSizes	   = poolSizes;
			poolInfo.maxSets	   = 512;
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
				_explicitUavDescriptorSetLayout };

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

			for ( auto& pair : _textures )
			{
				VulkanTextureRecord& record = pair.second;
				destroyOffscreenFramebuffer( record );
				if ( record.imageView != VK_NULL_HANDLE )
					vkDestroyImageView( _device, record.imageView, nullptr );
				if ( record.image != VK_NULL_HANDLE )
					vkDestroyImage( _device, record.image, nullptr );
				if ( record.memory != VK_NULL_HANDLE )
					vkFreeMemory( _device, record.memory, nullptr );
			}
			_textures.clear();
			_registeredTextures.clear();
			_textureFreeList.clear();

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

			for ( auto& pso : _pipelineStates )
			{
				if ( pso.pipeline != VK_NULL_HANDLE )
					vkDestroyPipeline( _device, pso.pipeline, nullptr );
			}
			_pipelineStates.clear();

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
		viewport.x = kDefaultViewportX;

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

		uint32 size		   = elementSize * elementCount;
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

	namespace
	{
		VkFormat toVulkanTextureFormat( RHIFormat format )
		{
			switch ( format )
			{
				case RHIFormat::R8G8B8A8_UNORM:
					return VK_FORMAT_R8G8B8A8_UNORM;
				case RHIFormat::B8G8R8A8_UNORM:
					return VK_FORMAT_B8G8R8A8_UNORM;
				case RHIFormat::R16G16B16A16_FLOAT:
					return VK_FORMAT_R16G16B16A16_SFLOAT;
				case RHIFormat::D24_UNORM_S8_UINT:
					return VK_FORMAT_D24_UNORM_S8_UINT;
				case RHIFormat::R32G32B32_FLOAT:
					return VK_FORMAT_R32G32B32_SFLOAT;
				case RHIFormat::R32G32_FLOAT:
					return VK_FORMAT_R32G32_SFLOAT;
				case RHIFormat::R32_FLOAT:
					return VK_FORMAT_R32_SFLOAT;
			}
			return VK_FORMAT_R8G8B8A8_UNORM;
		}
	} // namespace

	VkCommandBuffer VulkanRHIDevice::currentCommandBuffer() const
	{
		if ( _bOffscreenPassActive && _offscreenCommandBuffer != VK_NULL_HANDLE )
			return _offscreenCommandBuffer;
		if ( _bFrameStarted && _commandBuffers.empty() == false )
			return _commandBuffers[_currentFrame];
		return VK_NULL_HANDLE;
	}

	bool VulkanRHIDevice::transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayoutU32, uint32 newLayoutU32, uint32 aspectU32 )
	{
		const VkImageLayout		 oldLayout = static_cast<VkImageLayout>( oldLayoutU32 );
		const VkImageLayout		 newLayout = static_cast<VkImageLayout>( newLayoutU32 );
		const VkImageAspectFlags aspect	   = static_cast<VkImageAspectFlags>( aspectU32 );
		if ( cmd == VK_NULL_HANDLE || image == VK_NULL_HANDLE || oldLayout == newLayout )
			return true;

		VkImageMemoryBarrier barrier{};
		barrier.sType						 = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout					 = oldLayout;
		barrier.newLayout					 = newLayout;
		barrier.srcQueueFamilyIndex			 = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex			 = VK_QUEUE_FAMILY_IGNORED;
		barrier.image						 = image;
		barrier.subresourceRange.aspectMask	 = aspect;
		barrier.subresourceRange.levelCount	 = 1;
		barrier.subresourceRange.layerCount	 = 1;

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

	bool VulkanRHIDevice::createOffscreenFramebuffer( VulkanTextureRecord& record )
	{
		if ( record.imageView == VK_NULL_HANDLE || record.bRenderTarget == 0 )
			return false;

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format		   = static_cast<VkFormat>( record.format );
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
		dependency.srcStageMask	 = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependency.dstStageMask	 = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType		   = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments	   = &colorAttachment;
		rpInfo.subpassCount	   = 1;
		rpInfo.pSubpasses	   = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies   = &dependency;

		if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &record.renderPass ) != VK_SUCCESS )
			return false;

		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType		   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass	   = record.renderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments	   = &record.imageView;
		fbInfo.width		   = record.width;
		fbInfo.height		   = record.height;
		fbInfo.layers		   = 1;

		if ( vkCreateFramebuffer( _device, &fbInfo, nullptr, &record.framebuffer ) != VK_SUCCESS )
		{
			vkDestroyRenderPass( _device, record.renderPass, nullptr );
			record.renderPass = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	void VulkanRHIDevice::destroyOffscreenFramebuffer( VulkanTextureRecord& record )
	{
		if ( record.framebuffer != VK_NULL_HANDLE )
		{
			vkDestroyFramebuffer( _device, record.framebuffer, nullptr );
			record.framebuffer = VK_NULL_HANDLE;
		}
		if ( record.renderPass != VK_NULL_HANDLE )
		{
			vkDestroyRenderPass( _device, record.renderPass, nullptr );
			record.renderPass = VK_NULL_HANDLE;
		}
	}

	bool VulkanRHIDevice::queryVulkanTextureView( RHITextureHandle texture, void*& outImageView ) const
	{
		outImageView = nullptr;
		auto it		 = _textures.find( texture );
		if ( it == _textures.end() || it->second.imageView == VK_NULL_HANDLE )
			return false;
		outImageView = reinterpret_cast<void*>( it->second.imageView );
		return true;
	}

	RHITextureHandle VulkanRHIDevice::createTexture2D( const RHITextureDesc& desc )
	{
		if ( _device == nullptr || desc._width == 0 || desc._height == 0 )
			return 0;

		VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if ( desc._bIsRenderTarget )
			usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		if ( desc._bIsDepthStencil )
			usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if ( desc._bIsUnorderedAccess )
			usage |= VK_IMAGE_USAGE_STORAGE_BIT;

		const VkFormat format = toVulkanTextureFormat( desc._format );

		VkImageCreateInfo imageInfo{};
		imageInfo.sType			= VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType		= VK_IMAGE_TYPE_2D;
		imageInfo.extent.width	= desc._width;
		imageInfo.extent.height = desc._height;
		imageInfo.extent.depth	= 1;
		imageInfo.mipLevels		= desc._mipLevels > 0 ? desc._mipLevels : 1;
		imageInfo.arrayLayers	= 1;
		imageInfo.format		= format;
		imageInfo.tiling		= VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage			= usage;
		imageInfo.samples		= VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode	= VK_SHARING_MODE_EXCLUSIVE;

		VulkanTextureRecord record{};
		record.width		 = desc._width;
		record.height		 = desc._height;
		record.format		 = static_cast<uint32>( format );
		record.layout		 = static_cast<uint32>( VK_IMAGE_LAYOUT_UNDEFINED );
		record.bRenderTarget = desc._bIsRenderTarget ? 1 : 0;
		record.bDepthStencil = desc._bIsDepthStencil ? 1 : 0;
		record.bindlessIndex = kInvalidDescriptorIndex;

		if ( vkCreateImage( _device, &imageInfo, nullptr, &record.image ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "[Vulkan] Failed to create VkImage for Texture2D." );
			return 0;
		}

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements( _device, record.image, &memRequirements );

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType			  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize  = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

		if ( vkAllocateMemory( _device, &allocInfo, nullptr, &record.memory ) != VK_SUCCESS )
		{
			vkDestroyImage( _device, record.image, nullptr );
			SW_LOG_ERROR( "[Vulkan] Failed to allocate memory for Texture2D." );
			return 0;
		}

		vkBindImageMemory( _device, record.image, record.memory, 0 );

		VkImageAspectFlags aspect = desc._bIsDepthStencil ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT ) : VK_IMAGE_ASPECT_COLOR_BIT;
		if ( desc._bIsDepthStencil && format != VK_FORMAT_D24_UNORM_S8_UINT && format != VK_FORMAT_D32_SFLOAT_S8_UINT )
			aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType							 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image							 = record.image;
		viewInfo.viewType						 = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format							 = format;
		viewInfo.subresourceRange.aspectMask	 = aspect;
		viewInfo.subresourceRange.baseMipLevel	 = 0;
		viewInfo.subresourceRange.levelCount	 = imageInfo.mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount	 = 1;

		if ( vkCreateImageView( _device, &viewInfo, nullptr, &record.imageView ) != VK_SUCCESS )
		{
			vkDestroyImage( _device, record.image, nullptr );
			vkFreeMemory( _device, record.memory, nullptr );
			SW_LOG_ERROR( "[Vulkan] Failed to create VkImageView for Texture2D." );
			return 0;
		}

		if ( record.bRenderTarget && createOffscreenFramebuffer( record ) == false )
		{
			SW_LOG_WARNING( "[Vulkan] createTexture2D: framebuffer creation failed — texture kept without offscreen pass." );
		}

		const RHITextureHandle handle = static_cast<RHITextureHandle>( _nextTextureId++ );
		_textures.emplace( handle, record );
		return handle;
	}

	void VulkanRHIDevice::destroyTexture( RHITextureHandle texture )
	{
		if ( texture == 0 )
			return;

		auto it = _textures.find( texture );
		if ( it == _textures.end() )
			return;

		VulkanTextureRecord& record = it->second;
		if ( record.bindlessIndex != kInvalidDescriptorIndex )
		{
			const RHIDescriptorIndex index = record.bindlessIndex;
			if ( index < _registeredTextures.size() && _registeredTextures[index] != VK_NULL_HANDLE )
			{
				vkFreeDescriptorSets( _device, _descriptorPool, 1, &_registeredTextures[index] );
				_registeredTextures[index] = VK_NULL_HANDLE;
				_textureFreeList.push_back( index );
			}
			record.bindlessIndex = kInvalidDescriptorIndex;
		}

		destroyOffscreenFramebuffer( record );
		if ( record.imageView != VK_NULL_HANDLE )
			vkDestroyImageView( _device, record.imageView, nullptr );
		if ( record.image != VK_NULL_HANDLE )
			vkDestroyImage( _device, record.image, nullptr );
		if ( record.memory != VK_NULL_HANDLE )
			vkFreeMemory( _device, record.memory, nullptr );
		_textures.erase( it );
	}

	RHIDescriptorIndex VulkanRHIDevice::registerBindlessTexture( RHITextureHandle texture )
	{
		if ( texture == 0 || _descriptorPool == VK_NULL_HANDLE || _textureDescriptorSetLayout == VK_NULL_HANDLE || _defaultSampler == VK_NULL_HANDLE )
			return kInvalidDescriptorIndex;

		auto it = _textures.find( texture );
		if ( it == _textures.end() || it->second.imageView == VK_NULL_HANDLE )
			return kInvalidDescriptorIndex;

		VulkanTextureRecord& record = it->second;
		if ( record.bindlessIndex != kInvalidDescriptorIndex )
			return record.bindlessIndex;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType				 = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool	 = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts		 = &_textureDescriptorSetLayout;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		if ( vkAllocateDescriptorSets( _device, &allocInfo, &descriptorSet ) != VK_SUCCESS )
		{
			SW_LOG_ERROR( "[Vulkan] Failed to allocate VkDescriptorSet for bindless texture!" );
			return kInvalidDescriptorIndex;
		}

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler	  = _defaultSampler;
		imageInfo.imageView	  = record.imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType			  = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet		  = descriptorSet;
		write.dstBinding	  = 0;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo	  = &imageInfo;
		vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );

		RHIDescriptorIndex descriptorIndex;
		if ( _textureFreeList.empty() == false )
		{
			descriptorIndex = _textureFreeList.back();
			_textureFreeList.pop_back();
		}
		else
		{
			descriptorIndex = static_cast<RHIDescriptorIndex>( _registeredTextures.size() );
		}

		if ( descriptorIndex >= _registeredTextures.size() )
			_registeredTextures.resize( descriptorIndex + 1 );
		_registeredTextures[descriptorIndex] = descriptorSet;
		record.bindlessIndex				 = descriptorIndex;
		return descriptorIndex;
	}

	void VulkanRHIDevice::beginOffscreenPass( RHITextureHandle colorTarget, float32 clearColor[4] )
	{
		if ( colorTarget == 0 )
		{
			beginFrame( clearColor );
			return;
		}

		auto it = _textures.find( colorTarget );
		if ( it == _textures.end() || _offscreenCommandBuffer == VK_NULL_HANDLE )
			return;

		VulkanTextureRecord& record = it->second;
		if ( record.framebuffer == VK_NULL_HANDLE || record.renderPass == VK_NULL_HANDLE )
		{
			SW_LOG_ERROR( "[Vulkan] beginOffscreenPass: texture has no framebuffer." );
			return;
		}

		vkWaitForFences( _device, 1, &_offscreenFence, VK_TRUE, UINT64_MAX );
		vkResetFences( _device, 1, &_offscreenFence );
		vkResetCommandBuffer( _offscreenCommandBuffer, 0 );

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer( _offscreenCommandBuffer, &beginInfo );

		transitionImageLayout( _offscreenCommandBuffer, record.image, record.layout,
							   static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ),
							   static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT ) );
		record.layout = static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );

		VkClearValue clearValue{};
		clearValue.color = { { clearColor[0], clearColor[1], clearColor[2], clearColor[3] } };

		VkRenderPassBeginInfo rpBegin{};
		rpBegin.sType			  = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpBegin.renderPass		  = record.renderPass;
		rpBegin.framebuffer		  = record.framebuffer;
		rpBegin.renderArea.extent = { record.width, record.height };
		rpBegin.clearValueCount	  = 1;
		rpBegin.pClearValues	  = &clearValue;
		vkCmdBeginRenderPass( _offscreenCommandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );

		VkViewport viewport{};
		viewport.x		  = 0.0f;
		viewport.y		  = static_cast<float32>( record.height );
		viewport.width	  = static_cast<float32>( record.width );
		viewport.height	  = -static_cast<float32>( record.height );
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( _offscreenCommandBuffer, 0, 1, &viewport );

		VkRect2D scissor{};
		scissor.extent = { record.width, record.height };
		vkCmdSetScissor( _offscreenCommandBuffer, 0, 1, &scissor );

		_bOffscreenPassActive = 1;
		_bFrameStarted		  = 1; // allow drawTriangle / setPipelineState during offscreen
	}

	void VulkanRHIDevice::endOffscreenPass( RHITextureHandle colorTarget )
	{
		if ( colorTarget == 0 || _bOffscreenPassActive == 0 || _offscreenCommandBuffer == VK_NULL_HANDLE )
			return;

		auto it = _textures.find( colorTarget );
		if ( it == _textures.end() )
			return;

		VulkanTextureRecord& record = it->second;
		vkCmdEndRenderPass( _offscreenCommandBuffer );
		transitionImageLayout( _offscreenCommandBuffer, record.image,
							   static_cast<uint32>( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ),
							   static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ),
							   static_cast<uint32>( VK_IMAGE_ASPECT_COLOR_BIT ) );
		record.layout = static_cast<uint32>( VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL );

		vkEndCommandBuffer( _offscreenCommandBuffer );

		VkSubmitInfo submitInfo{};
		submitInfo.sType			  = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers	  = &_offscreenCommandBuffer;
		vkQueueSubmit( _graphicsQueue, 1, &submitInfo, _offscreenFence );
		vkWaitForFences( _device, 1, &_offscreenFence, VK_TRUE, UINT64_MAX );

		_bOffscreenPassActive = 0;
		_bFrameStarted		  = 0;
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
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || _pipelineLayout == VK_NULL_HANDLE )
			return;

		if ( index < static_cast<RHIDescriptorIndex>( _registeredUAVs.size() ) && _registeredUAVs[index] != VK_NULL_HANDLE )
		{
			VkDescriptorSet descSet = _registeredUAVs[index];
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineLayout, 2 + slot, 1, &descSet, 0, nullptr );
		}
	}

	void VulkanRHIDevice::drawTriangle( RHIDescriptorIndex materialDescriptorIndex )
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || _pipeline == VK_NULL_HANDLE || _pipelineLayout == VK_NULL_HANDLE )
			return;

		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline );

		if ( materialDescriptorIndex < static_cast<RHIDescriptorIndex>( _registeredDescriptorSets.size() ) && _registeredDescriptorSets[materialDescriptorIndex] != VK_NULL_HANDLE )
		{
			VkDescriptorSet descSet = _registeredDescriptorSets[materialDescriptorIndex];
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &descSet, 0, nullptr );
		}
		else if ( _descriptorSet != VK_NULL_HANDLE )
		{
			vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr );
		}

		uint32 matIndex = static_cast<uint32>( materialDescriptorIndex );
		vkCmdPushConstants( cmd, _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( uint32 ), &matIndex );

		if ( _vertexBuffer != VK_NULL_HANDLE )
		{
			VkBuffer	 vertexBuffers[] = { _vertexBuffer };
			VkDeviceSize offsets[]		 = { 0 };
			vkCmdBindVertexBuffers( cmd, 0, 1, vertexBuffers, offsets );
		}

		vkCmdDraw( cmd, 3, 1, 0, 0 );
	}

	void VulkanRHIDevice::dispatchCompute( uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ )
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;
		vkCmdDispatch( cmd, threadGroupCountX, threadGroupCountY, threadGroupCountZ );
	}

	void VulkanRHIDevice::setViewport( const RHIViewport& viewport )
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;

		VkViewport vkViewport{};
		vkViewport.x		= viewport._x;
		vkViewport.y		= viewport._y;
		vkViewport.width	= viewport._width;
		vkViewport.height	= viewport._height;
		vkViewport.minDepth = viewport._minDepth;
		vkViewport.maxDepth = viewport._maxDepth;
		vkCmdSetViewport( cmd, 0, 1, &vkViewport );
	}

	void VulkanRHIDevice::setComputeRootConstants( uint32 rootParameterIndex, uint32 num32BitValues, const void* data, uint32 destOffsetIn32BitValues )
	{
		(void)rootParameterIndex;
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || _pipelineLayout == VK_NULL_HANDLE || data == nullptr || num32BitValues == 0 )
			return;

		vkCmdPushConstants( cmd, _pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, destOffsetIn32BitValues * 4, num32BitValues * 4, data );
	}

	void VulkanRHIDevice::drawIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || argumentBuffer == 0 || argumentBuffer > static_cast<RHIBufferHandle>( _allocatedBuffers.size() ) )
			return;

		const VulkanBufferRecord& record = _allocatedBuffers[argumentBuffer - 1];
		if ( record.buffer != VK_NULL_HANDLE )
		{
			if ( _vertexBuffer != VK_NULL_HANDLE )
			{
				VkBuffer	 vertexBuffers[] = { _vertexBuffer };
				VkDeviceSize offsets[]		 = { 0 };
				vkCmdBindVertexBuffers( cmd, 0, 1, vertexBuffers, offsets );
			}
			vkCmdDrawIndirect( cmd, record.buffer, argumentBufferOffset, 1, sizeof( VkDrawIndirectCommand ) );
		}
	}

	void VulkanRHIDevice::dispatchIndirect( RHIBufferHandle argumentBuffer, uint32 argumentBufferOffset )
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || argumentBuffer == 0 || argumentBuffer > static_cast<RHIBufferHandle>( _allocatedBuffers.size() ) )
			return;

		const VulkanBufferRecord& record = _allocatedBuffers[argumentBuffer - 1];
		if ( record.buffer != VK_NULL_HANDLE )
			vkCmdDispatchIndirect( cmd, record.buffer, argumentBufferOffset );
	}
	void VulkanRHIDevice::beginEventMarker( const utf8* name )
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || name == nullptr || _instance == VK_NULL_HANDLE )
			return;

		auto fn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
			vkGetInstanceProcAddr( _instance, "vkCmdBeginDebugUtilsLabelEXT" ) );
		if ( fn == nullptr )
			return;

		VkDebugUtilsLabelEXT label{};
		label.sType		 = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		label.pLabelName = name;
		fn( cmd, &label );
	}
	void VulkanRHIDevice::endEventMarker()
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE || _instance == VK_NULL_HANDLE )
			return;

		auto fn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
			vkGetInstanceProcAddr( _instance, "vkCmdEndDebugUtilsLabelEXT" ) );
		if ( fn )
			fn( cmd );
	}

	std::unique_ptr<IRHICommandList> VulkanRHIDevice::createCommandList()
	{
		return std::make_unique<RHIDeferredCommandList>();
	}

	void VulkanRHIDevice::executeCommandList( IRHICommandList* cmdList )
	{
		executeDeferredCommandList( this, cmdList );
	}

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
		rasterizer.polygonMode			   = ( desc._fillMode == RHIFillMode::Wireframe ) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth			   = 1.0f;
		rasterizer.cullMode				   = ( desc._cullMode == RHICullMode::Front ) ? VK_CULL_MODE_FRONT_BIT : ( ( desc._cullMode == RHICullMode::Back ) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE );
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
		pipelineInfo.stage	= compShaderStageInfo;

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
		VkPipeline& pipe = _pipelineStates[pso - 1].pipeline;
		if ( pipe != VK_NULL_HANDLE )
		{
			vkDestroyPipeline( _device, pipe, nullptr );
			pipe = VK_NULL_HANDLE;
		}
	}

	void VulkanRHIDevice::setPipelineState( RHIPipelineStateHandle pso )
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;

		VkPipeline pipe = _pipeline;
		if ( pso > 0 && pso <= _pipelineStates.size() && _pipelineStates[pso - 1].pipeline != VK_NULL_HANDLE )
			pipe = _pipelineStates[pso - 1].pipeline;

		if ( pipe != VK_NULL_HANDLE )
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe );
	}

	void VulkanRHIDevice::setComputePipelineState( RHIPipelineStateHandle pso )
	{
		VkCommandBuffer cmd = currentCommandBuffer();
		if ( cmd == VK_NULL_HANDLE )
			return;

		if ( pso > 0 && pso <= _pipelineStates.size() && _pipelineStates[pso - 1].pipeline != VK_NULL_HANDLE )
			vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _pipelineStates[pso - 1].pipeline );
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
} // namespace sw
