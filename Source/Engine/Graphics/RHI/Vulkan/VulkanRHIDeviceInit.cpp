/**
 * @file VulkanRHIDeviceInit.cpp
 * @brief VulkanRHIDevice 부트스트랩입니다(인스턴스 · 서피스 · 물리/논리 디바이스 · 스왑체인 · 동기화 객체).
 * @details 여기 있는 것들은 모두 "한 번 만들고 창 크기가 바뀔 때 다시 만드는" 자원입니다.
 *          프레임마다 도는 코드(VulkanRHIDeviceSubmission.cpp)와 섞여 있으면 어느 쪽을 고치는지 알기 어렵습니다.
 */
#include "pch.h"

#include "Core/Diagnostics/CrashHandler.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIRequiredVersion.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHISamplerPreset.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/X11Headers.h"

    #include <X11/Xlib-xcb.h>
    #include <vulkan/vulkan_xcb.h>
    #include <vulkan/vulkan_xlib.h>
    #include <xcb/xcb.h>

    #include "Core/Common/X11MacroUndef.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    static_assert( VulkanRHIRequiredVersion::makeAPIVersion( 1, 3 ) == VK_MAKE_API_VERSION( 0, 1, 3, 0 ),
                   "VulkanRHIRequiredVersion::makeAPIVersion must encode like VK_MAKE_API_VERSION" );

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
        // 스왑체인 렌더패스: CLEAR 변종과 LOAD 변종. 한 프레임 안에서 백버퍼 렌더패스를 두 번 이상 여는 경우(그래프가
        // 백버퍼에 그린 뒤 UI 를 얹는 경로)에 CLEAR 변종으로 다시 열면 앞의 내용이 통째로 지워진다. 첨부 포맷 · 개수 ·
        // 샘플 수가 같아 프레임버퍼와 파이프라인은 두 렌더패스 모두와 호환된다(render pass compatibility).
        // LOAD 변종은 앞선 패스의 finalLayout 인 PRESENT_SRC 에서 시작한다.
        VulkanRHIRenderPassCache::RenderPassSpec clearSpec{};
        clearSpec.addColor( _swapChain.getImageFormat(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        _renderPass = createRenderPassFromSpec( clearSpec );
        if ( _renderPass == VK_NULL_HANDLE )
            return false;

        VulkanRHIRenderPassCache::RenderPassSpec loadSpec{};
        loadSpec.addColor( _swapChain.getImageFormat(), VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        _renderPassLoad = createRenderPassFromSpec( loadSpec );
        return _renderPassLoad != VK_NULL_HANDLE;
    }

    bool VulkanRHIDevice::supportsValidationLayer()
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
        appInfo.apiVersion         = VulkanRHIRequiredVersion::kRequiredAPIVersion;

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
        if ( VulkanRHIDeviceInternal::hasExtension( listAvailableExt, VK_KHR_WIN32_SURFACE_EXTENSION_NAME ) == false )
        {
            SW_LOG_ERROR( "VK_KHR_win32_surface is not available." );
            return false;
        }
        listExtension.push_back( VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
#elif defined( SW_PLATFORM_LINUX )
        // WSLg · gfxstream 은 흔히 xcb 만 내놓고 xlib 는 없다.
        _linuxWsi = 0;
        if ( VulkanRHIDeviceInternal::hasExtension( listAvailableExt, VK_KHR_XLIB_SURFACE_EXTENSION_NAME ) )
        {
            listExtension.push_back( VK_KHR_XLIB_SURFACE_EXTENSION_NAME );
            _linuxWsi = 1;
            SW_LOG_TRACE( "Vulkan WSI: VK_KHR_xlib_surface" );
        }
        else if ( VulkanRHIDeviceInternal::hasExtension( listAvailableExt, VK_KHR_XCB_SURFACE_EXTENSION_NAME ) )
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
#endif
        if ( _bEnableValidationLayers == SW_TRUE && VulkanRHIDeviceInternal::hasExtension( listAvailableExt, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) )
            listExtension.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
        else if ( _bEnableValidationLayers == SW_TRUE )
        {
            SW_LOG_WARNING( "Vulkan validation layer found but VK_EXT_debug_utils is not available - running without validation" );
            _bEnableValidationLayers = SW_FALSE;
        }

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

    void VulkanRHIDevice::createDebugMessenger()
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

    bool VulkanRHIDevice::pickPhysicalDevice( bool bSoftwareAdapter )
    {
        uint32 deviceCount{ 0 };
        vkEnumeratePhysicalDevices( _instance, &deviceCount, nullptr );
        if ( deviceCount == 0 )
            return false;
        vector<VkPhysicalDevice> devices( deviceCount );
        vkEnumeratePhysicalDevices( _instance, &deviceCount, devices.data() );

        for ( const VkPhysicalDevice& device : devices )
        {
            // 셰이더는 요구 판(VulkanRHIRequiredVersion) 타깃의 SPIR-V 로 쿠킹한다. 그보다 낮은 디바이스는 그 모듈을 하나도 받지 못한다.
            VkPhysicalDeviceProperties candidateProperties{};
            vkGetPhysicalDeviceProperties( device, &candidateProperties );
            if ( VulkanRHIRequiredVersion::isAPIVersionSupported( candidateProperties.apiVersion ) == false )
            {
                SW_LOG_WARNING( "Skipping Vulkan device '%#': API %#.%# is below the required %#.%# (shaders are SPIR-V for that target)",
                                candidateProperties.deviceName, VK_API_VERSION_MAJOR( candidateProperties.apiVersion ),
                                VK_API_VERSION_MINOR( candidateProperties.apiVersion ), VulkanRHIRequiredVersion::kRequiredMajor,
                                VulkanRHIRequiredVersion::kRequiredMinor );
                continue;
            }
            // 소프트웨어 어댑터를 요청했으면(gv_rhiSoftwareAdapter) CPU 디바이스(lavapipe · SwiftShader)만 후보다.
            if ( bSoftwareAdapter && candidateProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU )
                continue;

            uint32 queueFamilyCount{ 0 };
            vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, nullptr );
            vector<VkQueueFamilyProperties> queueFamilies( queueFamilyCount );
            vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

            uint32 queueFamilyIndex{ 0 };
            bool   bFound{ false };
            for ( const VkQueueFamilyProperties& queueFamily : queueFamilies )
            {
                if ( queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT )
                {
                    VkBool32 presentSupport{ false };
                    vkGetPhysicalDeviceSurfaceSupportKHR( device, queueFamilyIndex, _swapChain.getSurface(), &presentSupport );
                    if ( presentSupport )
                    {
                        _graphicsQueueFamilyIndex = queueFamilyIndex;
                        _physicalDevice           = device;
                        bFound                    = true;
                        break;
                    }
                }
                queueFamilyIndex++;
            }
            if ( bFound )
                break;
        }

        // 크래시 리포트에 어댑터 · 드라이버를 남긴다. "어느 GPU · 어느 드라이버에서만 난다" 는 판단이
        // 이것 없이는 불가능하고, 그게 범위를 좁히는 첫 질문이다.
        if ( _physicalDevice != nullptr )
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties( _physicalDevice, &properties );
            utf8 arrGPU[constant::kMaxBuffer256]{};
            formatstring( arrGPU, constant::kMaxBuffer256, "%# (driver %#, api %#.%#.%#)", properties.deviceName,
                          properties.driverVersion, VK_VERSION_MAJOR( properties.apiVersion ),
                          VK_VERSION_MINOR( properties.apiVersion ), VK_VERSION_PATCH( properties.apiVersion ) );
            CrashHandler::setContextValue( "GPU", arrGPU );
            _bSoftwareAdapter = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        }
        if ( _physicalDevice == nullptr && bSoftwareAdapter )
        {
            // 환경 탓이다(CPU 구현이 설치되지 않았다) — 결함으로 알리지 않는다.
            SW_LOG_WARNING( "No CPU Vulkan device (lavapipe / SwiftShader) for gv_rhiSoftwareAdapter - Vulkan does not start" );
            _initResult = RHIInitResult::DriverUnsupported;
        }
        else if ( _physicalDevice == nullptr )
            SW_LOG_ERROR( "No Vulkan %#.%# device with a graphics queue that can present to this surface", VulkanRHIRequiredVersion::kRequiredMajor,
                          VulkanRHIRequiredVersion::kRequiredMinor );
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
        // 범위 밖 버퍼 읽기가 0 이 되도록 한다. DX11/GL 과 같은 결과를 내고, 잘못된 인스턴스 · 머티리얼 인덱스가 GPU 폴트 대신
        // 검은 픽셀로 드러난다(셰이더 쪽 클램프도 따로 있다: binding.hlsli swLoadInstance).
        deviceFeatures.robustBufferAccess = availableFeatures.robustBufferAccess;
        deviceFeatures.samplerAnisotropy  = availableFeatures.samplerAnisotropy; // 정적 샘플러 세트의 ANISO_WRAP (없으면 createDescriptorResources 가 1.0 으로 만든다)
        // 와이어프레임(VK_POLYGON_MODE_LINE). DX11/DX12/GL 은 별도 기능 플래그가 없어 그냥 되는데
        // Vulkan 만 디바이스 생성 때 켜야 한다. 안 켜면 파이프라인 생성이 검증 오류로 거절되고,
        // 그 PSO 가 0 으로 돌아와 조용히 솔리드로 그려진다.
        deviceFeatures.fillModeNonSolid = availableFeatures.fillModeNonSolid;
        // RW 텍스처 배열(RWTexture2D<float4>[], 포맷 미지정 스토리지 이미지)의 읽기 · 쓰기.
        deviceFeatures.shaderStorageImageWriteWithoutFormat   = availableFeatures.shaderStorageImageWriteWithoutFormat;
        deviceFeatures.shaderStorageImageReadWithoutFormat    = availableFeatures.shaderStorageImageReadWithoutFormat;
        deviceFeatures.shaderStorageImageArrayDynamicIndexing = availableFeatures.shaderStorageImageArrayDynamicIndexing;
        // 네이티브 bindless: 배열을 푸시 상수 · CB 값으로 인덱싱한다(동적 인덱싱 코어 기능).
        deviceFeatures.shaderUniformBufferArrayDynamicIndexing = availableFeatures.shaderUniformBufferArrayDynamicIndexing;
        deviceFeatures.shaderStorageBufferArrayDynamicIndexing = availableFeatures.shaderStorageBufferArrayDynamicIndexing;
        deviceFeatures.shaderSampledImageArrayDynamicIndexing  = availableFeatures.shaderSampledImageArrayDynamicIndexing;
        _bMultiDrawIndirect                                    = availableFeatures.multiDrawIndirect ? 1 : 0;
        _bSamplerAnisotropy                                    = availableFeatures.samplerAnisotropy ? 1 : 0;
        _bFillModeNonSolid                                     = availableFeatures.fillModeNonSolid ? 1 : 0;

        // pickPhysicalDevice 가 요구 판(1.3) 미만 디바이스를 건너뛰므로 1.3 기능 구조체는 늘 체인에 넣을 수 있다.
        VkPhysicalDeviceVulkan13Features available13{};
        available13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceVulkan12Features available12{};
        available12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        available12.pNext = &available13;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &available12;
        vkGetPhysicalDeviceFeatures2( _physicalDevice, &features2 );

        VkPhysicalDeviceVulkan12Features vulkan12Features{};
        vulkan12Features.sType                                         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vulkan12Features.descriptorBindingPartiallyBound               = available12.descriptorBindingPartiallyBound;
        vulkan12Features.descriptorBindingUniformBufferUpdateAfterBind = available12.descriptorBindingUniformBufferUpdateAfterBind;
        vulkan12Features.descriptorBindingStorageBufferUpdateAfterBind = available12.descriptorBindingStorageBufferUpdateAfterBind;
        vulkan12Features.descriptorBindingUpdateUnusedWhilePending     = available12.descriptorBindingUpdateUnusedWhilePending;
        vulkan12Features.shaderUniformBufferArrayNonUniformIndexing    = available12.shaderUniformBufferArrayNonUniformIndexing;
        vulkan12Features.descriptorBindingSampledImageUpdateAfterBind  = available12.descriptorBindingSampledImageUpdateAfterBind;
        vulkan12Features.descriptorBindingStorageImageUpdateAfterBind  = available12.descriptorBindingStorageImageUpdateAfterBind;
        vulkan12Features.shaderStorageImageArrayNonUniformIndexing     = available12.shaderStorageImageArrayNonUniformIndexing;
        vulkan12Features.shaderStorageBufferArrayNonUniformIndexing    = available12.shaderStorageBufferArrayNonUniformIndexing;
        vulkan12Features.shaderSampledImageArrayNonUniformIndexing     = available12.shaderSampledImageArrayNonUniformIndexing;
        vulkan12Features.runtimeDescriptorArray                        = available12.runtimeDescriptorArray;
        vulkan12Features.drawIndirectCount                             = available12.drawIndirectCount;
        // 셰이더는 DX 패킹(-fvk-use-dx-layout)으로 쿠킹한다. relaxed block layout(1.1 코어)으로 대부분 충분하지만
        // 스칼라 정렬까지 허용해 두면 어떤 구조체든 DX 와 같은 오프셋을 쓸 수 있다.
        vulkan12Features.scalarBlockLayout = available12.scalarBlockLayout;
        _bDrawIndirectCount                = available12.drawIndirectCount ? 1 : 0;

        // 셰이더는 -fspv-target-env=vulkan1.3 으로 쿠킹하고, 그 타깃에서 DXC 는 HLSL `discard` 를 OpKill 이 아니라
        // OpDemoteToHelperInvocation 으로 낸다(deferredlighting.hlsl · sprite2d.hlsl 의 ALPHA_TEST). 이 기능을 켜지 않으면
        // vkCreateShaderModule 이 검증 오류를 낸다(VUID-VkShaderModuleCreateInfo-pCode-08740). Vulkan 1.3 의 필수 기능이라
        // 1.3 디바이스에는 늘 있다 — 없다고 답하는 드라이버는 스펙을 어긴 것이라 디바이스를 만들지 않는다.
        if ( available13.shaderDemoteToHelperInvocation == VK_FALSE )
        {
            SW_LOG_ERROR( "Vulkan 1.3 device reports no shaderDemoteToHelperInvocation (a required 1.3 feature) - HLSL discard cannot run" );
            return false;
        }
        VkPhysicalDeviceVulkan13Features vulkan13Features{};
        vulkan13Features.sType                          = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;
        vulkan12Features.pNext                          = &vulkan13Features;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext                = &vulkan12Features;
        createInfo.pQueueCreateInfos    = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pEnabledFeatures     = &deviceFeatures;
        // VK_EXT_memory_budget 은 선택이다. 있으면 켜서 드라이버의 사용량 · 예산을 읽고(queryGPUMemoryBudgetInternal), 없으면 그 값은 "모름" 이다.
        vector<const utf8*> listDeviceExtension( s_listDeviceExtensions.begin(), s_listDeviceExtensions.end() );
        {
            uint32 availableExtCount{ 0 };
            vkEnumerateDeviceExtensionProperties( _physicalDevice, nullptr, &availableExtCount, nullptr );
            vector<VkExtensionProperties> listAvailableExt( availableExtCount );
            if ( availableExtCount > 0 )
                vkEnumerateDeviceExtensionProperties( _physicalDevice, nullptr, &availableExtCount, listAvailableExt.data() );
            _bMemoryBudget = VulkanRHIDeviceInternal::hasExtension( listAvailableExt, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME ) ? SW_TRUE : SW_FALSE;
            if ( _bMemoryBudget == SW_TRUE )
                listDeviceExtension.push_back( VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
        }
        createInfo.enabledExtensionCount   = static_cast<uint32>( listDeviceExtension.size() );
        createInfo.ppEnabledExtensionNames = listDeviceExtension.data();

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
        // 일회성 제출 전용 풀. 짧게 살다 버려지는 버퍼라 TRANSIENT 를 준다.
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        if ( vkCreateCommandPool( _device, &poolInfo, nullptr, &_oneShotCommandPool ) != VK_SUCCESS )
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

        // 씬 텍스처의 기본 샘플러다. 지금 원하는 성질은 "선형 + 가장자리 고정" 이므로 그 조리법을
        // 쓴다. 비등방 같은 다른 성질이 필요해지면 여기서 조리법을 바꾸거나 직접 채우면 된다.
        VkSamplerCreateInfo samplerInfo = VulkanRHISamplerPreset::linearClamp();
        if ( vkCreateSampler( _device, &samplerInfo, nullptr, &_defaultSampler ) != VK_SUCCESS )
            return false;

        return true;
    }

    bool VulkanRHIDevice::createFrameFences()
    {
        // 펜스는 인플라이트 슬롯마다 하나다. 스왑체인 이미지 개수와 무관하다.
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

        // 위 펜스를 가리키는 사본이므로 비우기만 한다.
        _listImagesInFlight.clear();
    }

    void VulkanRHIDevice::resizeInternal( uint32 width, uint32 height )
    {
        if ( _width == width && _height == height )
            return;

        _width  = width;
        _height = height;

        // 아무 스레드에서나 재생성하지 않고 beginFrame 까지 미룬다.
        if ( width != 0 && height != 0 )
            _bSwapChainDirty = 1;
    }

    void VulkanRHIDevice::applyVSyncInternal()
    {
        _swapChain.setRequestedVSync( isVSyncEnabled() );
        if ( _width != 0 && _height != 0 )
            _bSwapChainDirty = 1;
    }

    bool VulkanRHIDevice::recreateSwapChain()
    {
        if ( _device == nullptr || _width == 0 || _height == 0 )
            return false;
        {
            std::scoped_lock<mutex> queueLock{ _queueMutex };
            vkDeviceWaitIdle( _device );
        }

        // 아래에서 스왑체인을 통째로 버린다. 쥐고 있던 이미지도 같이 사라지므로 표식을 내린다.
        // 남겨 두면 새 스왑체인에서 첫 acquire 를 건너뛰어 이미지 없이 그리게 된다.
        _bSwapChainImageHeld = SW_FALSE;

        // 세마포어와 이미지별 펜스 표는 스왑체인 이미지 개수로 크기가 정해지므로 함께 다시 만든다.
        destroyFrameFences();
        _swapChain.destroySemaphores( _device );
        _swapChain.destroy( _device );

        // 실패하면 부르는 쪽이 다음 프레임에 다시 시도한다(최소화 · 복원 중에는 서피스 크기가 잠시 0 이라 실패한다). 알림은 연달아
        // 실패하는 동안 한 번이다.
        const utf8* pFailedStep{ nullptr };
        if ( _swapChain.create( _physicalDevice, _device, _width, _height ) == false )
            pFailedStep = "swapchain";
        else if ( _swapChain.createFramebuffers( _device, _renderPass ) == false )
            pFailedStep = "swapchain framebuffers";
        else if ( _swapChain.createSemaphores( _device, constant::kMaxFrameCountInFlight ) == false )
            pFailedStep = "swapchain semaphores";
        else if ( createFrameFences() == false )
            pFailedStep = "frame fences";
        if ( pFailedStep != nullptr )
        {
            if ( _bSwapChainRecreateFailing == SW_FALSE )
                SW_LOG_ERROR( "Failed to recreate the %# (%#x%#) — retrying every frame", pFailedStep, _width, _height );
            _bSwapChainRecreateFailing = SW_TRUE;
            return false;
        }

        _bSwapChainRecreateFailing = SW_FALSE;
        _currentFrame              = 0;
        return true;
    }

    void VulkanRHIDevice::noteSurfaceLost( const utf8* pWhere )
    {
        ++_surfaceLostCount;
        SW_LOG_WARNING( "Vulkan surface lost at %# (%# time(s) on this device, frame %#) - recreating the surface and swapchain", pWhere, _surfaceLostCount,
                        _frameFenceCounter );
    }

    bool VulkanRHIDevice::recreateSurfaceAndSwapChain()
    {
        if ( _device == nullptr || _instance == nullptr )
            return false;
        {
            std::scoped_lock<mutex> queueLock{ _queueMutex };
            vkDeviceWaitIdle( _device );
        }
        // 스왑체인이 서피스 위에 있으므로 스왑체인 → 서피스 순으로 버리고 서피스 → 스왑체인 순으로 만든다.
        _bSwapChainImageHeld = SW_FALSE;
        destroyFrameFences();
        _swapChain.destroySemaphores( _device );
        _swapChain.destroy( _device );
        _swapChain.destroySurface( _instance );
        if ( _swapChain.createSurface( _instance, _pHWnd, _pDisplayHandle, _linuxWsi ) == false )
            return false;
        // 새 서피스를 이 큐가 프레젠트할 수 있는지 — 고를 때(pickPhysicalDevice) 본 것은 옛 서피스다.
        VkBool32 bPresentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR( _physicalDevice, _graphicsQueueFamilyIndex, _swapChain.getSurface(), &bPresentSupported );
        if ( bPresentSupported == VK_FALSE )
        {
            SW_LOG_ERROR( "The recreated Vulkan surface cannot be presented by queue family %#", _graphicsQueueFamilyIndex );
            return false;
        }
        return recreateSwapChain();
    }

    bool VulkanRHIDevice::initializePipelineCache()
    {
        if ( _device == VK_NULL_HANDLE )
            return false;

        vector<uint8> listCacheData;
        const string  cachePath = "Saved/ShaderCache/vk_pipeline_cache.bin";
        // 못 읽으면 빈 캐시로 시작한다 — 파이프라인을 다시 만들 뿐이다.
        if ( FileUtil::exists( cachePath ) )
            (void)FileUtil::readFile( cachePath, listCacheData ); // 못 읽으면 빈 캐시로 시작한다(위 설명)

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
                if ( FileUtil::writeFile( "Saved/ShaderCache/vk_pipeline_cache.bin", listCacheData.data(), static_cast<uint64>( listCacheData.size() ) ) )
                    SW_LOG_INFO( "Saved pipeline cache (%# bytes).", static_cast<uint32>( dataSize ) );
                else
                    SW_LOG_WARNING( "Could not save the pipeline cache - the next run builds pipelines from scratch" );
            }
        }

        vkDestroyPipelineCache( _device, _pipelineCache, nullptr );
        _pipelineCache = VK_NULL_HANDLE;
    }

} // namespace sw
