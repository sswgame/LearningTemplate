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
        colorAttachment.format         = static_cast<VkFormat>( _swapChain.getImageFormat() );
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
                    vkGetPhysicalDeviceSurfaceSupportKHR( device, i, _swapChain.getSurface(), &presentSupport );
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

    bool VulkanRHIDevice::createFrameFences()
    {
        // 펜스는 인플라이트 슬롯당 하나 — 스왑체인 이미지 개수와 무관하다.
        // 이미지별 세마포어는 스왑체인이 만든다(VulkanRHISwapChain::createSemaphores).
        _listInFlightFence.resize( constant::kMaxFrameCountInFlight );
        _listRingFrameNumber.resize( constant::kMaxFrameCountInFlight, 0 );
        // "이 이미지를 마지막으로 쓴 펜스" 표는 이미지 개수만큼 필요하다.
        _listImagesInFlight.resize( _swapChain.getImageCount(), VK_NULL_HANDLE );

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for ( VkFence& fence : _listInFlightFence )
        {
            if ( vkCreateFence( _device, &fenceInfo, nullptr, &fence ) != VK_SUCCESS )
                return false;
        }
        return true;
    }

    void VulkanRHIDevice::destroyFrameFences()
    {
        if ( _device == nullptr )
            return;

        for ( VkFence fence : _listInFlightFence )
        {
            if ( fence != VK_NULL_HANDLE )
                vkDestroyFence( _device, fence, nullptr );
        }
        _listInFlightFence.clear();
        _listRingFrameNumber.clear();

        // 위 펜스를 가리키는 사본이므로 비우기만 합니다.
        _listImagesInFlight.clear();
    }

    void VulkanRHIDevice::resize( uint32 width, uint32 height )
    {
        if ( _width == width && _height == height )
            return;

        _width  = width;
        _height = height;

        // 임의의 호출 스레드에서 재생성하지 않고 beginFrame까지 미룹니다.
        if ( width != 0 && height != 0 )
            _bSwapChainDirty = 1;
    }

    void VulkanRHIDevice::recreateSwapChain()
    {
        if ( _device == nullptr || _width == 0 || _height == 0 )
            return;
        vkDeviceWaitIdle( _device );

        // 세마포어와 이미지별 펜스 표는 스왑체인 이미지 개수로 크기가 정해지므로 함께 재생성합니다.
        destroyFrameFences();
        _swapChain.destroySemaphores( _device );
        _swapChain.destroy( _device );

        if ( _swapChain.create( _physicalDevice, _device, _width, _height ) == false )
        {
            SW_LOG_ERROR( "Failed to recreate the swapchain!" );
            return;
        }
        if ( _swapChain.createFramebuffers( _device, _renderPass ) == false )
        {
            SW_LOG_ERROR( "Failed to recreate the swapchain framebuffers!" );
            return;
        }
        if ( _swapChain.createSemaphores( _device, constant::kMaxFrameCountInFlight ) == false )
        {
            SW_LOG_ERROR( "Failed to recreate the swapchain semaphores!" );
            return;
        }
        if ( createFrameFences() == false )
        {
            SW_LOG_ERROR( "Failed to recreate the frame fences!" );
            return;
        }

        _currentFrame = 0;
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
