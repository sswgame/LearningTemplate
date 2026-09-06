/**
 * @file VulkanRHIDeviceInit.cpp
 * @brief VulkanRHIDevice 부트스트랩 — 인스턴스·서피스·물리/논리 디바이스·스왑체인·동기화 객체
 * @details 여기 있는 것들은 전부 "한 번 만들고 창 크기가 바뀔 때 다시 만드는" 자원이다.
 *          프레임마다 도는 코드(VulkanRHIDevice.cpp)와 섞여 있으면 어느 쪽을 고치는지 알기 어렵다.
 */
#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

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

    bool VulkanRHIDevice::createRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = static_cast<VkFormat>( _swapChainImageFormat );
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments    = &colorAttachment;
        renderPassInfo.subpassCount    = 1;
        renderPassInfo.pSubpasses      = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies   = &dependency;

        if ( vkCreateRenderPass( _device, &renderPassInfo, nullptr, &_renderPass ) != VK_SUCCESS )
            return false;

        // LOAD 변종. 한 프레임 안에서 백버퍼 렌더패스를 두 번 이상 여는 경우(그래프가 백버퍼에 그린
        // 뒤 UI 를 얹는 경로)에 CLEAR 변종으로 다시 열면 앞의 내용이 통째로 지워진다. 첨부 포맷/개수/
        // 샘플수가 같아 프레임버퍼와 파이프라인은 두 렌더패스 모두와 호환된다(render pass compatibility).
        // 이 시점의 스왑체인 이미지는 앞선 패스의 finalLayout 인 PRESENT_SRC 상태다.
        colorAttachment.loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        if ( vkCreateRenderPass( _device, &renderPassInfo, nullptr, &_renderPassLoad ) != VK_SUCCESS )
            return false;
        return true;
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
                if ( StringUtil::equals( pLayerName, layerProperties.layerName ) )
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
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "SW App";
        appInfo.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
        appInfo.pEngineName        = "SW Engine";
        appInfo.engineVersion      = VK_MAKE_VERSION( 1, 0, 0 );
        appInfo.apiVersion         = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        vector<const utf8*> listExtension;
        listExtension.push_back( VK_KHR_SURFACE_EXTENSION_NAME );

        uint32 availableExtCount{ 0 };
        vkEnumerateInstanceExtensionProperties( nullptr, &availableExtCount, nullptr );
        vector<VkExtensionProperties> listAvailableExt( availableExtCount );
        if ( availableExtCount > 0 )
            vkEnumerateInstanceExtensionProperties( nullptr, &availableExtCount, listAvailableExt.data() );

#if defined( SW_PLATFORM_WINDOWS )
        if ( VulkanRHIDeviceInternal::hasExtensionVal( listAvailableExt, VK_KHR_WIN32_SURFACE_EXTENSION_NAME ) == false )
        {
            SW_LOG_ERROR( "VK_KHR_win32_surface is not available." );
            return false;
        }
        listExtension.push_back( VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
#elif defined( SW_PLATFORM_LINUX )
        // WSLg/gfxstream often exposes xcb but not xlib.
        _linuxWsi = 0;
        if ( VulkanRHIDeviceInternal::hasExtensionVal( listAvailableExt, VK_KHR_XLIB_SURFACE_EXTENSION_NAME ) )
        {
            listExtension.push_back( VK_KHR_XLIB_SURFACE_EXTENSION_NAME );
            _linuxWsi = 1;
            SW_LOG_TRACE( "Vulkan WSI: VK_KHR_xlib_surface" );
        }
        else if ( VulkanRHIDeviceInternal::hasExtensionVal( listAvailableExt, VK_KHR_XCB_SURFACE_EXTENSION_NAME ) )
        {
            listExtension.push_back( VK_KHR_XCB_SURFACE_EXTENSION_NAME );
            _linuxWsi = 2;
            SW_LOG_TRACE( "Vulkan WSI: VK_KHR_xcb_surface (xlib unavailable)" );
        }
        else
        {
            SW_LOG_ERROR( "No Vulkan X11 WSI extension (VK_KHR_xlib_surface / VK_KHR_xcb_surface). Enumerated %# instance extensions.",
                          availableExtCount );
            for ( const VkExtensionProperties& ext : listAvailableExt )
            {
                (void)ext;
                SW_LOG_TRACE( "  instance ext: %#", ext.extensionName );
            }
            SW_LOG_ERROR( "Install libxcb1-dev / libx11-xcb-dev, and rebuild vcpkg vulkan-loader with [xcb,xlib]." );
            return false;
        }
#elif defined( SW_PLATFORM_MACOS )
        if ( VulkanRHIDeviceInternal::hasExtensionVal( listAvailableExt, VK_EXT_METAL_SURFACE_EXTENSION_NAME ) == false )
        {
            SW_LOG_ERROR( "VK_EXT_metal_surface is not available." );
            return false;
        }
        listExtension.push_back( VK_EXT_METAL_SURFACE_EXTENSION_NAME );
#endif
        if ( _bEnableValidationLayers == SW_TRUE && VulkanRHIDeviceInternal::hasExtensionVal( listAvailableExt, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) )
            listExtension.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
        else if ( _bEnableValidationLayers == SW_TRUE )
            _bEnableValidationLayers = SW_FALSE;

        createInfo.enabledExtensionCount   = static_cast<uint32>( listExtension.size() );
        createInfo.ppEnabledExtensionNames = listExtension.data();

        if ( _bEnableValidationLayers == SW_TRUE )
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
        if ( _bEnableValidationLayers == SW_FALSE )
            return;
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;

        CreateDebugUtilsMessengerEXT( _instance, &createInfo, nullptr, &_debugMessenger );
    }

    bool VulkanRHIDevice::createSurface()
    {
#if defined( SW_PLATFORM_WINDOWS )
        VkWin32SurfaceCreateInfoKHR createInfo{};
        createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd      = static_cast<HWND>( _pHWnd );
        createInfo.hinstance = GetModuleHandle( nullptr );

        if ( vkCreateWin32SurfaceKHR( _instance, &createInfo, nullptr, &_surface ) != VK_SUCCESS )
        {
            SW_LOG_ERROR( "Failed to create window surface!" );
            return false;
        }
        return true;
#elif defined( SW_PLATFORM_LINUX )
        Display* pDisplay = static_cast<Display*>( _pDisplayHandle );
        Window   window   = static_cast<Window>( reinterpret_cast<uintptr_t>( _pHWnd ) );
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
            createInfo.dpy    = pDisplay;
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
            createInfo.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
            createInfo.connection = pConnection;
            createInfo.window     = static_cast<xcb_window_t>( window );
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
                        _physicalDevice           = device;
                        found                     = true;
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

        const VkFormat arrCandidate[] = {
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D32_SFLOAT,
        };
        for ( VkFormat format : arrCandidate )
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties( _physicalDevice, format, &props );
            if ( ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) == 0 )
                continue;
            _depthFormat      = static_cast<uint32>( format );
            _bDepthHasStencil = ( format == VK_FORMAT_D32_SFLOAT ) ? 0 : 1;
            SW_LOG_TRACE( "Selected depth format %# (stencil=%#)", static_cast<uint32>( format ),
                          static_cast<uint32>( _bDepthHasStencil ) );
            return true;
        }
        _depthFormat      = 0;
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
        queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
        queueCreateInfo.queueCount       = 1;
        float32 queuePriority{ 1.0f };
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures availableFeatures{};
        vkGetPhysicalDeviceFeatures( _physicalDevice, &availableFeatures );

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.multiDrawIndirect = availableFeatures.multiDrawIndirect;
        _bMultiDrawIndirect              = availableFeatures.multiDrawIndirect ? 1 : 0;

        VkPhysicalDeviceVulkan12Features available12{};
        available12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &available12;
        vkGetPhysicalDeviceFeatures2( _physicalDevice, &features2 );

        VkPhysicalDeviceVulkan12Features vulkan12Features{};
        vulkan12Features.sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkan12Features.descriptorBindingPartiallyBound               = available12.descriptorBindingPartiallyBound;
        vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = available12.descriptorBindingStorageBufferUpdateAfterBind;
        vulkan12Features.descriptorBindingSampledImageUpdateAfterBind  = available12.descriptorBindingSampledImageUpdateAfterBind;
        vulkan12Features.shaderStorageBufferArrayNonUniformIndexing    = available12.shaderStorageBufferArrayNonUniformIndexing;
        vulkan12Features.shaderSampledImageArrayNonUniformIndexing     = available12.shaderSampledImageArrayNonUniformIndexing;
        vulkan12Features.runtimeDescriptorArray                        = available12.runtimeDescriptorArray;
        vulkan12Features.drawIndirectCount                             = available12.drawIndirectCount;
        _bDrawIndirectCount                                            = available12.drawIndirectCount ? 1 : 0;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext                   = &vulkan12Features;
        createInfo.pQueueCreateInfos       = &queueCreateInfo;
        createInfo.queueCreateInfoCount    = 1;
        createInfo.pEnabledFeatures        = &deviceFeatures;
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

        // 백버퍼 포맷은 백엔드 간 계약이다(constant::kBackBufferFormat). 파이프라인이 그 포맷으로
        // 렌더패스를 만들기 때문에, 여기서 다른 걸 고르면 백버퍼에 직접 그리는 패스가 통째로
        // 렌더패스 비호환이 된다. 요청 포맷 → 대체 → 첫 번째 순으로 고르고, 요청을 못 맞추면
        // getBackBufferFormat() 으로 실제 값을 보고한다(조용히 어긋나게 두지 않는다).
        const VkFormat requestedFormat = VulkanRHIDeviceInternal::toVulkanTextureFormat( _requestedBackBufferFormat );
        const VkFormat arrPreferred[]  = { requestedFormat, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };

        VkSurfaceFormatKHR surfaceFormat   = formats[0];
        bool               bFoundPreferred = false;
        for ( const VkFormat preferred : arrPreferred )
        {
            for ( const VkSurfaceFormatKHR& availableFormat : formats )
            {
                if ( availableFormat.format == preferred && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
                {
                    surfaceFormat   = availableFormat;
                    bFoundPreferred = true;
                    break;
                }
            }
            if ( bFoundPreferred )
                break;
        }
        if ( surfaceFormat.format != requestedFormat )
        {
            SW_LOG_ERROR( "스왑체인이 요청 포맷(%#)을 지원하지 않아 %# 로 대체됐습니다 — 백버퍼를 직접 "
                          "타깃으로 하는 파이프라인이 렌더패스 비호환이 될 수 있습니다.",
                          static_cast<uint32>( requestedFormat ), static_cast<uint32>( surfaceFormat.format ) );
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

        // 창이 최소화되거나 파괴되는 중이면 서피스가 0 크기를 보고한다. 그대로 넘기면
        // vkCreateSwapchainKHR / vkCreateFramebuffer / vkCmdBeginRenderPass 가 줄줄이 0 크기로
        // 불려 검증 오류가 쏟아진다(종료 경로에서 실제로 그랬다). 크기가 생기면 _bSwapChainDirty
        // 경로가 다시 부르므로 여기서는 조용히 물러난다.
        if ( extent.width == 0 || extent.height == 0 )
        {
            SW_LOG_TRACE( "createSwapChain: 서피스 크기가 0 (%#x%#) — 창이 최소화/종료 중입니다. 스왑체인 생성을 건너뜁니다.",
                          extent.width, extent.height );
            return false;
        }

        // 백버퍼 개수도 백엔드 간 계약이다 — 예전엔 이 값을 무시하고 minImageCount + 1 을 썼다.
        // 요청값을 존중하되 서피스 능력으로 클램프한다.
        uint32 imageCount = ( _requestedBufferCount > 0 ) ? _requestedBufferCount : ( capabilities.minImageCount + 1 );
        if ( imageCount < capabilities.minImageCount )
            imageCount = capabilities.minImageCount;
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
        createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface          = _surface;
        createInfo.minImageCount    = imageCount;
        createInfo.imageFormat      = surfaceFormat.format;
        createInfo.imageColorSpace  = surfaceFormat.colorSpace;
        createInfo.imageExtent      = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform     = preTransform;
        createInfo.compositeAlpha   = compositeAlpha;
        createInfo.presentMode      = presentMode;
        createInfo.clipped          = VK_TRUE;

        if ( vkCreateSwapchainKHR( _device, &createInfo, nullptr, &_swapChain ) != VK_SUCCESS )
            return false;

        vkGetSwapchainImagesKHR( _device, _swapChain, &imageCount, nullptr );
        _listSwapChainImage.resize( imageCount );
        vkGetSwapchainImagesKHR( _device, _swapChain, &imageCount, _listSwapChainImage.data() );

        _swapChainImageFormat   = static_cast<uint32>( surfaceFormat.format );
        _actualBackBufferFormat = ( surfaceFormat.format == requestedFormat )
                                    ? _requestedBackBufferFormat
                                    : ( ( surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM ) ? RHIFormat::B8G8R8A8_UNORM
                                                                                             : RHIFormat::R8G8B8A8_UNORM );
        _swapChainExtentWidth   = extent.width;
        _swapChainExtentHeight  = extent.height;
        return true;
    }

    bool VulkanRHIDevice::createImageViews()
    {
        _listSwapChainImageView.resize( _listSwapChainImage.size() );
        for ( size_t imageIndex = 0; imageIndex < _listSwapChainImage.size(); imageIndex++ )
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image                           = _listSwapChainImage[imageIndex];
            createInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format                          = static_cast<VkFormat>( _swapChainImageFormat );
            createInfo.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel   = 0;
            createInfo.subresourceRange.levelCount     = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount     = 1;

            if ( vkCreateImageView( _device, &createInfo, nullptr, &_listSwapChainImageView[imageIndex] ) != VK_SUCCESS )
            {
                for ( size_t cleanupIndex = 0; cleanupIndex < imageIndex; ++cleanupIndex )
                {
                    if ( _listSwapChainImageView[cleanupIndex] != VK_NULL_HANDLE )
                        vkDestroyImageView( _device, _listSwapChainImageView[cleanupIndex], nullptr );
                }
                _listSwapChainImageView.clear();
                return false;
            }
        }
        return true;
    }

    bool VulkanRHIDevice::createFramebuffers()
    {
        _listSwapChainFramebuffer.resize( _listSwapChainImageView.size(), VK_NULL_HANDLE );
        for ( size_t imageIndex = 0; imageIndex < _listSwapChainImageView.size(); imageIndex++ )
        {
            VkImageView             arrAttachment[] = { _listSwapChainImageView[imageIndex] };
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass      = _renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments    = arrAttachment;
            framebufferInfo.width           = _swapChainExtentWidth;
            framebufferInfo.height          = _swapChainExtentHeight;
            framebufferInfo.layers          = 1;
            if ( vkCreateFramebuffer( _device, &framebufferInfo, nullptr, &_listSwapChainFramebuffer[imageIndex] ) != VK_SUCCESS )
            {
                for ( size_t cleanupIndex = 0; cleanupIndex < imageIndex; ++cleanupIndex )
                {
                    if ( _listSwapChainFramebuffer[cleanupIndex] != VK_NULL_HANDLE )
                        vkDestroyFramebuffer( _device, _listSwapChainFramebuffer[cleanupIndex], nullptr );
                }
                _listSwapChainFramebuffer.clear();
                return false;
            }
        }
        return true;
    }

    bool VulkanRHIDevice::createCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
        if ( vkCreateCommandPool( _device, &poolInfo, nullptr, &_commandPool ) != VK_SUCCESS )
            return false;
        return true;
    }

    bool VulkanRHIDevice::createCommandBuffers()
    {
        _listCommandBuffer.resize( constant::kMaxFrameCountInFlight );
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = _commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32>( _listCommandBuffer.size() );

        if ( vkAllocateCommandBuffers( _device, &allocInfo, _listCommandBuffer.data() ) != VK_SUCCESS )
            return false;

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter     = VK_FILTER_LINEAR;
        samplerInfo.minFilter     = VK_FILTER_LINEAR;
        samplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor   = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.maxLod        = 1000.0f;
        if ( vkCreateSampler( _device, &samplerInfo, nullptr, &_defaultSampler ) != VK_SUCCESS )
            return false;

        return true;
    }

    bool VulkanRHIDevice::createSyncObjects()
    {
        // acquire 세마포어는 _currentFrame 으로, present 세마포어는 _imageIndex 로 인덱싱한다 —
        // 두 개수는 다를 수 있으므로 각자 맞는 크기로 잡는다. 예전엔 둘 다 이미지 수로 잡아서,
        // 드라이버가 인플라이트 프레임 수보다 적은 이미지를 주면 범위 밖 접근이 될 수 있었다.
        _listImageAvailableSemaphore.resize( constant::kMaxFrameCountInFlight );
        _listRenderFinishedSemaphore.resize( _listSwapChainImage.size() );
        _listInFlightFence.resize( constant::kMaxFrameCountInFlight );
        _listImagesInFlight.resize( _listSwapChainImage.size(), VK_NULL_HANDLE );
        _listRingFrameNumber.resize( constant::kMaxFrameCountInFlight, 0 );

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

        for ( size_t syncIndex = 0; syncIndex < constant::kMaxFrameCountInFlight; syncIndex++ )
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
        _listRingFrameNumber.clear();

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
            createInfo.pInitialData    = listCacheData.data();
        }

        const VkResult res = vkCreatePipelineCache( _device, &createInfo, nullptr, &_pipelineCache );
        if ( res != VK_SUCCESS )
        {
            SW_LOG_WARNING( "Failed to create pipeline cache with saved data; falling back to empty cache." );
            createInfo.initialDataSize = 0;
            createInfo.pInitialData    = nullptr;
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

} // namespace sw
