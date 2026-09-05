#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"
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

namespace sw
{
    namespace
    {
        struct VulkanRHIDeviceInternal
        {
            static inline VkFormat toVulkanTextureFormat( RHIFormat format )
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
                    default:
                        break;
                }
                return VK_FORMAT_UNDEFINED;
            }

            static bool hasExtensionVal( const vector<VkExtensionProperties>& listAvailableExt, const utf8* pName )
            {
                for ( const VkExtensionProperties& ext : listAvailableExt )
                {
                    if ( StringUtil::equals( ext.extensionName, pName ) )
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
        , _renderPassLoad{ nullptr }
        , _offscreenRenderPass{ nullptr }
        , _commandPool{ nullptr }
        , _listCommandBuffer{}
        , _listImageAvailableSemaphore{}
        , _listRenderFinishedSemaphore{}
        , _listInFlightFence{}
        , _listImagesInFlight{}
        , _listRingFrameNumber{}
        , _pHWnd{ nullptr }
        , _pDisplayHandle{ nullptr }
        , _currentFrame{ 0 }
        , _imageIndex{ 0 }
        , _frameFenceCounter{ 0 }
        , _width{ 0 }
        , _height{ 0 }
        , _depthFormat{ 0 }
        , _bFrameStarted{ SW_FALSE }
#if defined( SW_DEBUG )
        , _bEnableValidationLayers{ SW_TRUE }
#else
        , _bEnableValidationLayers{ SW_FALSE }
#endif
        , _bMultiDrawIndirect{ SW_FALSE }
        , _bDrawIndirectCount{ SW_FALSE }
        , _bSwapChainDirty{ SW_FALSE }
        , _bDepthHasStencil{ SW_FALSE }
        , _linuxWsi{ 0 }
        , _reservedVulkan{ 0 }
        , _defaultSampler{ nullptr }
        , _pipelineLayout{ nullptr }
        , _descriptorSetLayout{ nullptr }
        , _uavDescriptorSetLayout{ nullptr }
        , _descriptorPool{ nullptr }
        , _descriptorSet{ nullptr }
        , _staticSamplerLinearWrap{ nullptr }
        , _samplerSetLayout{ nullptr }
        , _staticSamplerSet{ nullptr }
        , _dummyUBO{ nullptr }
        , _dummyUBOMemory{ nullptr }
        , _pipeline{ nullptr }
        , _offscreenPipeline{ nullptr }
        , _vertexBuffer{ nullptr }
        , _listBindlessFree{}
        , _gpuBuffers{}
        , _mapCbSlotSize{}
        , _listRegisteredDescriptorSet{}
        , _listRegisteredCbSetRing{}
        , _listBindlessSourceBuffer{}
        , _listRegisteredUAV{}
        , _listUavSourceBuffer{}
        , _listUavFree{}
        , _gpuTextures{}
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _mapCompositeFramebuffer{}
        , _mapPipelineRenderPass{}
        , _textureDescriptorSetLayout{ nullptr }
        , _listRegisteredTexture{}
        , _listTextureFree{}
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

    IRHISwapChain*      VulkanRHIDevice::getSwapChain() { return _swapChainImpl.get(); }
    IRHIResource*       VulkanRHIDevice::getResource() { return _resourceImpl.get(); }
    IRHICommandContext* VulkanRHIDevice::getImmediateContext() { return _immContext.get(); }
    IRHICommandContext* VulkanRHIDevice::getDeferredCommandContext() { return _deferredContext.get(); }

    bool VulkanRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        _pHWnd          = desc._pWindowHandle;
        _pDisplayHandle = desc._pWindowDisplay;
        _width          = desc._width;
        _height         = desc._height;

        BLOCK( "Validation Layer Setup" )
        {
#if defined( SW_PLATFORM_WINDOWS )
            if ( _bEnableValidationLayers == SW_TRUE )
            {
                string execDir = FileUtil::getDirectoryPart( FileUtil::getExecutablePath() );
                if ( FileUtil::fileExists( FileUtil::joinPath( execDir, "VkLayer_khronos_validation.json" ) ) )
                {
                    SetEnvironmentVariableA( "VK_ADD_LAYER_PATH", execDir.c_str() );
                    SetEnvironmentVariableA( "VK_LAYER_PATH", execDir.c_str() );
                }
                else
                {
                    const utf8* pVulkanSdkEnv = std::getenv( "VULKAN_SDK" );
                    if ( StringUtil::isNullOrEmpty( pVulkanSdkEnv ) == false )
                    {
                        string sdkBinPath = FileUtil::joinPath( pVulkanSdkEnv, "Bin" );
                        SetEnvironmentVariableA( "VK_ADD_LAYER_PATH", sdkBinPath.c_str() );
                    }
                }
            }
#endif

            if ( _bEnableValidationLayers == SW_TRUE && checkValidationLayerSupport() == false )
            {
                SW_LOG_INFO( "Vulkan Validation Layers requested, but VK_LAYER_KHRONOS_validation was not found (Validation Layers: DISABLED)" );
                _bEnableValidationLayers = SW_FALSE;
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
            (void)ensureDefaultDescriptorSet();
        }

        BLOCK( "Fullscreen Triangle" )
        {
            const RHIVertex arrFullscreenVert[3] = {
                {{ -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
                { { 3.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
                { { -1.0f, 3.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            };
            const RHIBufferHandle vbHandle =
                createVulkanBuffer( static_cast<uint32>( sizeof( arrFullscreenVert ) ), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, arrFullscreenVert );
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
        _immContext      = sw::make_unique<VulkanRHICommandContext>( this );
        _deferredContext = sw::make_unique<VulkanRHICommandContext>( this );

        SW_LOG_INFO( "Vulkan RHI Backend Device Initialized Successfully (Validation Layers: %#)", _bEnableValidationLayers == SW_TRUE ? "ENABLED" : "DISABLED" );
        return true;
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
            _recordingState._boundMeshVb      = 0;
            _recordingState._boundMeshStride  = sizeof( RHIVertex );
            _recordingState._boundMeshOffset  = 0;
            _recordingState._boundIndexBuffer = 0;
            _recordingState._boundIndexStride = 4;
            _recordingState._boundIndexOffset = 0;
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
            _listTextureFree.clear();

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
            {
                std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
                for ( const VulkanCommandListEntry& entry : _listFreeCmdListEntry )
                {
                    if ( entry._pool != VK_NULL_HANDLE )
                        vkDestroyCommandPool( _device, entry._pool, nullptr );
                }
                _listFreeCmdListEntry.clear();
            }

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
            if ( _uavDescriptorSetLayout )
            {
                vkDestroyDescriptorSetLayout( _device, _uavDescriptorSetLayout, nullptr );
                _uavDescriptorSetLayout = nullptr;
            }
            if ( _samplerSetLayout )
            {
                vkDestroyDescriptorSetLayout( _device, _samplerSetLayout, nullptr );
                _samplerSetLayout = nullptr;
            }
            if ( _staticSamplerLinearWrap )
            {
                vkDestroySampler( _device, _staticSamplerLinearWrap, nullptr );
                _staticSamplerLinearWrap = nullptr;
            }
            _staticSamplerSet = nullptr; // 풀 파괴로 함께 해제됨

            cleanupSwapChain();
            destroySyncObjects();

            for ( VulkanRenderPassRecord& rpRecord : _listRenderPass )
            {
                if ( rpRecord._bOwned != 0 && rpRecord._renderPass != VK_NULL_HANDLE &&
                     rpRecord._renderPass != _renderPass )
                    vkDestroyRenderPass( _device, rpRecord._renderPass, nullptr );
            }
            _listRenderPass.clear();

            if ( _renderPassLoad )
                vkDestroyRenderPass( _device, _renderPassLoad, nullptr );
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

        if ( _bEnableValidationLayers == SW_TRUE && _debugMessenger )
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

        _width  = width;
        _height = height;

        // 임의의 호출 스레드에서 재생성하지 않고 beginFrame까지 미룹니다.
        if ( width != 0 && height != 0 )
            _bSwapChainDirty = 1;
    }

    void VulkanRHIDevice::beginFrame( const float4& clearColor )
    {
        _bFrameStarted = SW_FALSE;
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
        // 이 링 슬롯의 펜스가 신호됐다는 건 그 슬롯에 마지막으로 제출한 세대(_listRingFrameNumber)의
        // GPU 작업이 실제로 끝났다는 뜻이다 — 그 세대 이하로 태그된 리소스 해제를 지금 실행한다.
        _releaseQueue.tickCompleted( _listRingFrameNumber[_currentFrame] );

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

        // 프레임 스트림의 첫 세그먼트. 리스트가 제출될 때마다 여기서 잘리고 새 세그먼트가 열린다.
        _activeFrameBuffer  = _listCommandBuffer[_currentFrame];
        _frameSegmentCursor = 0;
        _listPendingSubmit.clear();

        // 새 커맨드버퍼엔 아직 아무 디스크립터셋도 안 걸림 — bindGraphicsMaterialSets 캐시 무효화.
        _recordingState._lastBoundGraphicsSet0    = nullptr;
        _recordingState._bStaticGraphicsSetsBound = false;

        _bFrameStarted                     = SW_TRUE;
        _recordingState._bRenderPassActive = SW_FALSE;

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        VkViewport viewport{};
        viewport.x = kDefaultViewportX;

        viewport.y        = static_cast<float32>( _swapChainExtentHeight );
        viewport.width    = static_cast<float32>( _swapChainExtentWidth );
        viewport.height   = -static_cast<float32>( _swapChainExtentHeight );
        viewport.minDepth = kDefaultViewportMinDepth;
        viewport.maxDepth = kDefaultViewportMaxDepth;
        vkCmdSetViewport( _activeFrameBuffer, 0, 1, &viewport );

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { _swapChainExtentWidth, _swapChainExtentHeight };
        vkCmdSetScissor( _activeFrameBuffer, 0, 1, &scissor );

        // 백버퍼 렌더패스는 여기서 열지 않는다 — beginFrame 은 프레임 수명주기 전용이고, 백버퍼
        // 타깃팅은 beginRenderPass(핸들 0) 가 명시적으로 한다(docs/05_RHI_FrameContract.md S2).
        // 클리어도 그 렌더패스의 loadOp 이 담당한다.
        (void)clearColor;
    }

    void VulkanRHIDevice::endFrame( bool vsync, bool bPresent )
    {
        (void)vsync;
        if ( _bFrameStarted == SW_FALSE )
            return;

        if ( _recordingState._bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( _activeFrameBuffer );
            _recordingState._bRenderPassActive = SW_FALSE;
        }
        vkEndCommandBuffer( _activeFrameBuffer );
        _listPendingSubmit.push_back( _activeFrameBuffer );

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore          arrWaitSemaphore[] = { _listImageAvailableSemaphore[_currentFrame] };
        VkPipelineStageFlags arrWaitStage[]     = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        submitInfo.waitSemaphoreCount           = 1;
        submitInfo.pWaitSemaphores              = arrWaitSemaphore;
        submitInfo.pWaitDstStageMask            = arrWaitStage;

        // 프레임 세그먼트와 리스트 버퍼를 기록 순서 그대로 한 번에 제출한다 — 같은 큐에 대한
        // 제출 순서가 곧 실행 순서라, 세그먼트 사이의 리소스 의존성이 그대로 지켜진다.
        submitInfo.commandBufferCount = static_cast<uint32>( _listPendingSubmit.size() );
        submitInfo.pCommandBuffers    = _listPendingSubmit.data();

        VkSemaphore arrSignalSemaphore[] = { _listRenderFinishedSemaphore[_imageIndex] };
        submitInfo.signalSemaphoreCount  = 1;
        submitInfo.pSignalSemaphores     = arrSignalSemaphore;

        // 이 제출에 새 세대 번호를 매긴다 — 이번 프레임 기록 중 등록된 지연 해제(enqueueGpuRelease)는
        // 이 세대가 실제로 끝났다고 확인될 때까지(beginFrame의 tickCompleted) 보류된다.
        _listRingFrameNumber[_currentFrame] = ++_frameFenceCounter;
        vkQueueSubmit( _graphicsQueue, 1, &submitInfo, _listInFlightFence[_currentFrame] );

        if ( bPresent )
        {
            VkPresentInfoKHR presentInfo{};
            presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores    = arrSignalSemaphore;

            VkSwapchainKHR arrSwapChain[] = { _swapChain };
            presentInfo.swapchainCount    = 1;
            presentInfo.pSwapchains       = arrSwapChain;
            presentInfo.pImageIndices     = &_imageIndex;

            const VkResult presentResult = vkQueuePresentKHR( _graphicsQueue, &presentInfo );
            if ( presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR )
            {
                // 다음 beginFrame에서 스왑체인을 재생성합니다.
                _bSwapChainDirty = 1;
            }
            else if ( presentResult != VK_SUCCESS )
                SW_LOG_ERROR( "vkQueuePresentKHR failed! Error code: %#", static_cast<int32>( presentResult ) );
        }

        _listPendingSubmit.clear();
        _activeFrameBuffer = VK_NULL_HANDLE;
        _currentFrame      = ( _currentFrame + 1 ) % constant::kMaxFrameCountInFlight;
        _bFrameStarted     = SW_FALSE;
        _releaseQueue.tickFrame();
    }

    size_t VulkanRHIDevice::registeredDescriptorSetCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        return _listRegisteredDescriptorSet.size();
    }

    VkDescriptorSet VulkanRHIDevice::registeredDescriptorSetAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredDescriptorSet.size() )
            return VK_NULL_HANDLE;

        // 링 상수버퍼는 프레임 슬롯마다 전용 셋을 갖는다. 예전엔 셋 하나를 매 프레임 새 슬롯으로
        // 다시 기록했는데, 그 셋은 직전 프레임 커맨드버퍼가 아직 쓰고 있어서 in-use 위반이었다
        // (오프스크린 블로킹 제출이 사라지기 전까지는 그 스톨이 가려주고 있었다).
        const size_t ringBase = static_cast<size_t>( index ) * constant::kMaxFrameCountInFlight;
        if ( ringBase + _currentFrame < _listRegisteredCbSetRing.size() )
        {
            const VkDescriptorSet ringSet = _listRegisteredCbSetRing[ringBase + _currentFrame];
            if ( ringSet != VK_NULL_HANDLE )
                return ringSet;
        }
        return _listRegisteredDescriptorSet[index];
    }

    VkDescriptorSet VulkanRHIDevice::registeredUavSetAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredUAV.size() )
            return VK_NULL_HANDLE;
        return _listRegisteredUAV[index];
    }

    VkDescriptorSet VulkanRHIDevice::registeredTextureSetAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredTexture.size() )
            return VK_NULL_HANDLE;
        return _listRegisteredTexture[index];
    }

    VkCommandBuffer VulkanRHIDevice::currentCommandBuffer() const
    {
        // 스트림은 하나지만 리스트가 제출될 때마다 세그먼트로 잘린다 — 지금 열려 있는 세그먼트를
        // 돌려준다. 예전엔 오프스크린 전용 버퍼로 갈라졌고 그쪽은 매 프레임 자체 제출 + 펜스
        // 블로킹을 했다(S3 에서 사라졌다).
        if ( _bFrameStarted == SW_TRUE )
            return _activeFrameBuffer;
        return VK_NULL_HANDLE;
    }

    VulkanCommandListEntry VulkanRHIDevice::acquireCommandListEntry()
    {
        {
            std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
            if ( _listFreeCmdListEntry.empty() == false )
            {
                VulkanCommandListEntry entry = _listFreeCmdListEntry.back();
                _listFreeCmdListEntry.pop_back();
                return entry;
            }
        }

        VulkanCommandListEntry entry{};
        if ( _device == nullptr )
            return entry;

        // 풀은 리스트마다 전용이어야 한다 — VkCommandPool 은 외부 동기화 대상이라 두 스레드가 같은
        // 풀에서 동시에 기록하면 정의되지 않은 동작이다.
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
        if ( vkCreateCommandPool( _device, &poolInfo, nullptr, &entry._pool ) != VK_SUCCESS )
            return VulkanCommandListEntry{};

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = entry._pool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        if ( vkAllocateCommandBuffers( _device, &allocInfo, &entry._buffer ) != VK_SUCCESS )
        {
            vkDestroyCommandPool( _device, entry._pool, nullptr );
            return VulkanCommandListEntry{};
        }
        return entry;
    }

    void VulkanRHIDevice::recycleCommandListEntryDeferred( VulkanCommandListEntry entry )
    {
        if ( entry._pool == VK_NULL_HANDLE || entry._buffer == VK_NULL_HANDLE )
            return;

        // 제출 직후 리스트 객체가 사라져도 GPU 는 아직 이 버퍼를 읽고 있다 — 이번 프레임 세대가
        // 끝났다고 확인된 뒤에야 재사용 풀로 돌려보낸다(대기 없음).
        auto recycleCb = [this, entry]()
        {
            std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
            _listFreeCmdListEntry.push_back( entry );
        };
        _releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, recycleCb ), _frameFenceCounter + 1 );
    }

    VkCommandBuffer VulkanRHIDevice::beginNextFrameSegment()
    {
        if ( _device == nullptr || _currentFrame >= constant::kMaxFrameCountInFlight )
            return VK_NULL_HANDLE;

        vector<VkCommandBuffer>& listSegment = _arrFrameSegment[_currentFrame];
        if ( _frameSegmentCursor >= listSegment.size() )
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = _commandPool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            VkCommandBuffer created{ VK_NULL_HANDLE };
            if ( vkAllocateCommandBuffers( _device, &allocInfo, &created ) != VK_SUCCESS )
                return VK_NULL_HANDLE;
            listSegment.push_back( created );
        }

        VkCommandBuffer segment = listSegment[_frameSegmentCursor];
        ++_frameSegmentCursor;

        // 이 프레임 슬롯의 펜스를 beginFrame 에서 이미 기다렸으므로 곧바로 Reset 해도 안전하다.
        vkResetCommandBuffer( segment, 0 );
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer( segment, &beginInfo );
        return segment;
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
        (void)mode; // Vulkan은 소프트웨어 Cmd-vector 없이 즉시 VulkanRHICommandContext를 호출한다.
        return make_unique<VulkanRHICommandList>( this );
    }

    void VulkanRHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        VulkanRHICommandList* pList = static_cast<VulkanRHICommandList*>( pCmdList );
        if ( pList == nullptr || _bFrameStarted == SW_FALSE || _activeFrameBuffer == VK_NULL_HANDLE )
            return;

        const VkCommandBuffer listBuffer = pList->nativeCommandBuffer();
        if ( listBuffer == VK_NULL_HANDLE )
            return;

        // 지금까지의 프레임 세그먼트를 닫아 제출 순서에 넣고, 그 뒤에 이 리스트의 버퍼를 잇는다.
        if ( _recordingState._bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( _activeFrameBuffer );
            _recordingState._bRenderPassActive = SW_FALSE;
        }
        vkEndCommandBuffer( _activeFrameBuffer );
        _listPendingSubmit.push_back( _activeFrameBuffer );
        _listPendingSubmit.push_back( listBuffer );

        // 이어서 기록할 새 세그먼트를 연다. 새 버퍼이므로 바인딩 캐시는 무효다.
        VkCommandBuffer nextSegment = beginNextFrameSegment();
        if ( nextSegment == VK_NULL_HANDLE )
        {
            _activeFrameBuffer = VK_NULL_HANDLE;
            _bFrameStarted     = SW_FALSE;
            return;
        }
        _activeFrameBuffer = nextSegment;
        _recordingState    = VulkanRecordingState{};

        VkViewport viewport{};
        viewport.x        = 0.0f;
        viewport.y        = static_cast<float32>( _swapChainExtentHeight );
        viewport.width    = static_cast<float32>( _swapChainExtentWidth );
        viewport.height   = -static_cast<float32>( _swapChainExtentHeight );
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport( _activeFrameBuffer, 0, 1, &viewport );

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { _swapChainExtentWidth, _swapChainExtentHeight };
        vkCmdSetScissor( _activeFrameBuffer, 0, 1, &scissor );
    }

    bool VulkanRHIDevice::queryVulkanTextureView( RHITextureHandle texture, void*& pOutImageView ) const
    {
        pOutImageView                   = nullptr;
        const VulkanTextureRecord* pTex = resolveTexture( texture );
        if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE )
            return false;
        pOutImageView = reinterpret_cast<void*>( pTex->_imageView );
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

        // 백버퍼 포맷은 백엔드 간에 같아야 한다. 파이프라인은 desc._arrRtvFormat 으로 렌더패스를 만들고
        // Vulkan 은 그 렌더패스와 실제 렌더패스의 첨부 포맷이 다르면 드로우를 거부하는데, 파이프라인
        // 리소스는 DX12 스왑체인에 맞춰 R8G8B8A8_UNORM 을 쓴다. 여기서 B8G8R8A8 을 고르면 백버퍼에
        // 직접 그리는 패스(에디터 없이 실행하는 경로)가 통째로 렌더패스 비호환이 된다.
        // R8G8B8A8_UNORM → B8G8R8A8_UNORM → 첫 번째 순으로 고른다.
        VkSurfaceFormatKHR surfaceFormat   = formats[0];
        constexpr VkFormat arrPreferred[]  = { VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM };
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
        _listImageAvailableSemaphore.resize( _listSwapChainImage.size() );
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
            binding.binding         = 0;
            binding.descriptorType  = type;
            binding.descriptorCount = 1;
            binding.stageFlags      = stages;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings    = &binding;
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

        // set 4 = 정적 샘플러(g_SwSamplerLinearWrap). binding.hlsli 는 standalone SamplerState 로 선언하므로
        // (COMBINED_IMAGE_SAMPLER 가 아니라) VK_DESCRIPTOR_TYPE_SAMPLER 전용 레이아웃이 필요하다.
        // immutable sampler 로 굽기 때문에 vkUpdateDescriptorSets 없이 매 드로우 바인딩만 하면 된다.
        {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter     = VK_FILTER_LINEAR;
            samplerInfo.minFilter     = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.maxAnisotropy = 1.0f;
            samplerInfo.borderColor   = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerInfo.maxLod        = 1000.0f;
            if ( vkCreateSampler( _device, &samplerInfo, nullptr, &_staticSamplerLinearWrap ) != VK_SUCCESS )
                return false;

            VkDescriptorSetLayoutBinding binding{};
            binding.binding            = 0;
            binding.descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
            binding.descriptorCount    = 1;
            binding.stageFlags         = allStages;
            binding.pImmutableSamplers = &_staticSamplerLinearWrap;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings    = &binding;
            if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_samplerSetLayout ) != VK_SUCCESS )
                return false;
        }

        // Bindless texture array layout (set 1) — ensureBindlessTextureArray가 세트를 할당.
        {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding         = 0;
            binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding.descriptorCount = kBindlessTextureCount;
            binding.stageFlags      = allStages;

            VkDescriptorBindingFlags bindingFlags =
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
            bindingFlagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount  = 1;
            bindingFlagsInfo.pBindingFlags = &bindingFlags;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext        = &bindingFlagsInfo;
            layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings    = &binding;
            if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_bindlessTextureArrayLayout ) != VK_SUCCESS )
                return false;
        }

        // Descriptor set layout (matches VulkanRHICommandContext binds):
        //   0: Pass/Material UBO
        //   1: Bindless texture array (native sampling)
        //   2,3,5: Explicit single-texture SRV slots (DX11-style emulation, 텍스처 슬롯 0/1/3)
        //   4: 정적 샘플러(g_SwSamplerLinearWrap, immutable) — 텍스처 슬롯 2 는 이 set 을 쓰지 않는다
        //      (bindShaderResource 는 native bindless 에서 아예 호출 안 됨 — Vulkan 은 항상 native).
        //   6..9: 컴퓨트 읽기전용 구조버퍼(t0..t3, bindComputeShaderResource) / GPUScene 인스턴스 구조버퍼
        //   7..9: 위 4개 set 중 뒤쪽 3개는 컴퓨트 UAV(u0..u2, bindComputeUAV) 와도 공유한다 —
        //         한 디스패치에서 t 슬롯과 u 슬롯을 동시에 쓸 때는 서로 다른 인덱스를 사용해야 한다.
        VkDescriptorSetLayout arrSetLayout[10] = {
            _descriptorSetLayout,
            _bindlessTextureArrayLayout,
            _textureDescriptorSetLayout,
            _textureDescriptorSetLayout,
            _samplerSetLayout,
            _textureDescriptorSetLayout,
            _uavDescriptorSetLayout,
            _uavDescriptorSetLayout,
            _uavDescriptorSetLayout,
            _uavDescriptorSetLayout,
        };

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = allStages;
        pushRange.offset     = 0;
        pushRange.size       = kMaxComputeRootConstantDwords * sizeof( uint32 );

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount         = 10;
        pipelineLayoutInfo.pSetLayouts            = arrSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges    = &pushRange;
        if ( vkCreatePipelineLayout( _device, &pipelineLayoutInfo, nullptr, &_pipelineLayout ) != VK_SUCCESS )
            return false;

        VkDescriptorPoolSize arrPoolSize[] = {
            {        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16384},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768},
            {        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16384},
            {               VK_DESCRIPTOR_TYPE_SAMPLER,     4},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets       = 32768;
        poolInfo.poolSizeCount = static_cast<uint32>( sizeof( arrPoolSize ) / sizeof( arrPoolSize[0] ) );
        poolInfo.pPoolSizes    = arrPoolSize;
        if ( vkCreateDescriptorPool( _device, &poolInfo, nullptr, &_descriptorPool ) != VK_SUCCESS )
            return false;

        // 정적 샘플러 set 4 를 한 번 할당해 둔다 (immutable sampler라 vkUpdateDescriptorSets 불필요).
        {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool     = _descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts        = &_samplerSetLayout;
            if ( vkAllocateDescriptorSets( _device, &allocInfo, &_staticSamplerSet ) != VK_SUCCESS )
                return false;
        }

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
        imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType     = VK_IMAGE_TYPE_2D;
        imageInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent        = { 1, 1, 1 };
        imageInfo.mipLevels     = 1;
        imageInfo.arrayLayers   = 1;
        imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
        allocMem.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocMem.allocationSize  = memReq.size;
        allocMem.memoryTypeIndex = memoryTypeIndex;
        if ( vkAllocateMemory( _device, &allocMem, nullptr, &_bindlessDummyMemory ) != VK_SUCCESS )
            return false;
        vkBindImageMemory( _device, _bindlessDummyImage, _bindlessDummyMemory, 0 );

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                       = _bindlessDummyImage;
        viewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                      = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if ( vkCreateImageView( _device, &viewInfo, nullptr, &_bindlessDummyView ) != VK_SUCCESS )
            return false;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = _descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &_bindlessTextureArrayLayout;

        if ( vkAllocateDescriptorSets( _device, &allocInfo, &_bindlessTextureSet ) != VK_SUCCESS )
            return false;

        vector<VkDescriptorImageInfo> infos( kBindlessTextureCount );
        vector<VkWriteDescriptorSet>  writes( kBindlessTextureCount );
        for ( uint32 slotIndex = 0; slotIndex < kBindlessTextureCount; ++slotIndex )
        {
            infos[slotIndex].sampler          = _defaultSampler;
            infos[slotIndex].imageView        = _bindlessDummyView;
            infos[slotIndex].imageLayout      = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[slotIndex].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[slotIndex].dstSet          = _bindlessTextureSet;
            writes[slotIndex].dstBinding      = 0;
            writes[slotIndex].dstArrayElement = slotIndex;
            writes[slotIndex].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[slotIndex].descriptorCount = 1;
            writes[slotIndex].pImageInfo      = &infos[slotIndex];
        }
        vkUpdateDescriptorSets( _device, kBindlessTextureCount, writes.data(), 0, nullptr );
        SW_LOG_INFO( "Bindless texture array ready (%# slots).", kBindlessTextureCount );
        return true;
    }

    bool VulkanRHIDevice::ensureDefaultDescriptorSet()
    {
        if ( _descriptorSet != VK_NULL_HANDLE )
            return true;
        if ( _device == VK_NULL_HANDLE || _descriptorPool == VK_NULL_HANDLE || _descriptorSetLayout == VK_NULL_HANDLE )
            return false;

        // set 0(Pass/Material UBO)에 바인딩할 게 없을 때 쓰는 기본 세트.
        // 그래픽스 셰이더는 set 0 을 정적으로 참조하므로 Vulkan 검증상 모든 draw 에서
        // set 0 이 바인딩돼 있어야 한다(머티리얼 CBV 가 없는 드로우·인다이렉트 드로우 포함).
        constexpr VkDeviceSize kDummyUboSize = 256;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = kDummyUboSize;
        bufferInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &_dummyUBO ) != VK_SUCCESS )
            return false;

        // 실패 지점마다 여기까지 만든 것을 되돌린다. (핸들을 남기면 재시도 시 그대로 누수된다)
        const auto destroyDummyUbo = [this]()
        {
            if ( _dummyUBOMemory != VK_NULL_HANDLE )
            {
                vkFreeMemory( _device, _dummyUBOMemory, nullptr );
                _dummyUBOMemory = VK_NULL_HANDLE;
            }
            if ( _dummyUBO != VK_NULL_HANDLE )
            {
                vkDestroyBuffer( _device, _dummyUBO, nullptr );
                _dummyUBO = VK_NULL_HANDLE;
            }
        };

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements( _device, _dummyUBO, &memReq );
        uint32 memoryTypeIndex{ 0 };
        if ( findMemoryType( memReq.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             memoryTypeIndex ) == false )
        {
            destroyDummyUbo();
            return false;
        }

        VkMemoryAllocateInfo allocMem{};
        allocMem.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocMem.allocationSize  = memReq.size;
        allocMem.memoryTypeIndex = memoryTypeIndex;
        if ( vkAllocateMemory( _device, &allocMem, nullptr, &_dummyUBOMemory ) != VK_SUCCESS )
        {
            _dummyUBOMemory = VK_NULL_HANDLE;
            destroyDummyUbo();
            return false;
        }
        vkBindBufferMemory( _device, _dummyUBO, _dummyUBOMemory, 0 );

        void* pMapped{ nullptr };
        if ( vkMapMemory( _device, _dummyUBOMemory, 0, kDummyUboSize, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr )
        {
            Memory::set( pMapped, 0, static_cast<size_t>( kDummyUboSize ) );
            vkUnmapMemory( _device, _dummyUBOMemory );
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool     = _descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts        = &_descriptorSetLayout;
        if ( vkAllocateDescriptorSets( _device, &allocInfo, &_descriptorSet ) != VK_SUCCESS )
        {
            _descriptorSet = VK_NULL_HANDLE;
            destroyDummyUbo();
            SW_LOG_ERROR( "Failed to allocate the default (set 0) descriptor set." );
            return false;
        }

        VkDescriptorBufferInfo dbInfo{};
        dbInfo.buffer = _dummyUBO;
        dbInfo.offset = 0;
        dbInfo.range  = kDummyUboSize;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _descriptorSet;
        write.dstBinding      = 0;
        write.dstArrayElement = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo     = &dbInfo;
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
        return true;
    }

    void VulkanRHIDevice::writeBindlessTextureSlot( RHIDescriptorIndex index, VkImageView view )
    {
        if ( _bindlessTextureSet == VK_NULL_HANDLE || view == VK_NULL_HANDLE || index >= kBindlessTextureCount )
            return;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler     = _defaultSampler;
        imageInfo.imageView   = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _bindlessTextureSet;
        write.dstBinding      = 0;
        write.dstArrayElement = index;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imageInfo;
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
    }

    bool VulkanRHIDevice::transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayoutU32, uint32 newLayoutU32, uint32 aspectU32 )
    {
        const VkImageLayout      oldLayout = static_cast<VkImageLayout>( oldLayoutU32 );
        const VkImageLayout      newLayout = static_cast<VkImageLayout>( newLayoutU32 );
        const VkImageAspectFlags aspect    = aspectU32;
        if ( cmd == VK_NULL_HANDLE || image == VK_NULL_HANDLE || oldLayout == newLayout )
            return true;

        VkImageMemoryBarrier barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                   = oldLayout;
        barrier.newLayout                   = newLayout;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL )
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage              = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
        {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL )
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage              = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL )
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstStage              = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
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
        colorAttachment.format         = static_cast<VkFormat>( sharedFormat );
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        // Match swapchain-compatible dependency style so PSO / active RP stay consistent.
        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments    = &colorAttachment;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
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
            colorAttachment.format         = static_cast<VkFormat>( record._format );
            colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
            colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments    = &colorRef;

            VkSubpassDependency dependency{};
            dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass    = 0;
            dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo rpInfo{};
            rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments    = &colorAttachment;
            rpInfo.subpassCount    = 1;
            rpInfo.pSubpasses      = &subpass;
            rpInfo.dependencyCount = 1;
            rpInfo.pDependencies   = &dependency;

            if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &record._renderPass ) != VK_SUCCESS )
                return false;
        }

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = record._renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &record._imageView;
        fbInfo.width           = record._width;
        fbInfo.height          = record._height;
        fbInfo.layers          = 1;

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
        // 즉시 파괴하면 안 된다 - 아직 실행 중인 프레임의 커맨드버퍼가 이 프레임버퍼를 참조할 수
        // 있다(게임뷰 리사이즈가 대표적인 경로다). 예전엔 오프스크린 경로가 매 프레임 블로킹
        // 제출을 해서 우연히 안전했을 뿐이고, 그 스톨을 걷어내자 곧바로 in-use 위반이 드러났다.
        enqueueFramebufferRelease( record._framebuffer,
                                   ( record._renderPass != _offscreenRenderPass ) ? record._renderPass : VK_NULL_HANDLE );
        record._framebuffer = VK_NULL_HANDLE;
        record._renderPass  = VK_NULL_HANDLE;
    }

    void VulkanRHIDevice::enqueueFramebufferRelease( VkFramebuffer framebuffer, VkRenderPass ownedRenderPass )
    {
        if ( _device == nullptr || ( framebuffer == VK_NULL_HANDLE && ownedRenderPass == VK_NULL_HANDLE ) )
            return;

        VkDevice dev = _device;
        _releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, framebuffer, ownedRenderPass]()
        {
            if ( framebuffer != VK_NULL_HANDLE )
                vkDestroyFramebuffer( dev, framebuffer, nullptr );
            if ( ownedRenderPass != VK_NULL_HANDLE )
                vkDestroyRenderPass( dev, ownedRenderPass, nullptr );
        } ),
                                         _frameFenceCounter + 1 );
    }

    VkRenderPass VulkanRHIDevice::ensurePipelineRenderPass( const RHIPipelineStateDesc& desc )
    {
        PipelineRpKey key{};
        const bool    bDepthOnly = ( desc._numRenderTargets == 0 && desc._bEnableDepthTest != 0 );
        key._colorCount          = bDepthOnly ? 0u : ( desc._numRenderTargets > 0 ? desc._numRenderTargets : 1u );
        if ( key._colorCount > kMaxColorAttachments )
            key._colorCount = kMaxColorAttachments;
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            VkFormat colorFmt = VulkanRHIDeviceInternal::toVulkanTextureFormat( desc._arrRtvFormat[colorIndex] );
            if ( colorFmt == VK_FORMAT_UNDEFINED )
                colorFmt = VK_FORMAT_R8G8B8A8_UNORM;
            key._arrColorFormat[colorIndex] = static_cast<uint32>( colorFmt );
        }
        if ( desc._bEnableDepthTest != 0 )
        {
            // FrameRenderer는 RHI D24를 요청하지만 GPU가 미지원일 수 있음 → 디바이스 선택 포맷 사용
            VkFormat depthFmt = VulkanRHIDeviceInternal::toVulkanTextureFormat( desc._depthStencilFormat );
            if ( desc._depthStencilFormat == sw::RHIFormat::D24_UNORM_S8_UINT ||
                 depthFmt == VK_FORMAT_D24_UNORM_S8_UINT || depthFmt == VK_FORMAT_UNDEFINED )
                depthFmt = static_cast<VkFormat>( _depthFormat );
            key._depthFormat = static_cast<uint32>( depthFmt );
        }

        auto existing = _mapPipelineRenderPass.find( key );
        if ( existing != _mapPipelineRenderPass.end() )
            return existing->second;

        VkAttachmentDescription attachments[kMaxColorAttachments + 1]{};
        VkAttachmentReference   colorRefs[kMaxColorAttachments]{};
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            attachments[colorIndex].format         = static_cast<VkFormat>( key._arrColorFormat[colorIndex] );
            attachments[colorIndex].samples        = VK_SAMPLE_COUNT_1_BIT;
            attachments[colorIndex].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[colorIndex].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[colorIndex].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[colorIndex].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorRefs[colorIndex].attachment       = colorIndex;
            colorRefs[colorIndex].layout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        VkAttachmentReference depthRef{};
        const bool            bHasDepth   = key._depthFormat != 0;
        uint32                attachCount = key._colorCount;
        if ( bHasDepth )
        {
            attachments[attachCount].format         = static_cast<VkFormat>( key._depthFormat );
            attachments[attachCount].samples        = VK_SAMPLE_COUNT_1_BIT;
            attachments[attachCount].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[attachCount].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[attachCount].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[attachCount].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[attachCount].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthRef.attachment                     = attachCount;
            depthRef.layout                         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ++attachCount;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = key._colorCount;
        subpass.pColorAttachments       = key._colorCount > 0 ? colorRefs : nullptr;
        subpass.pDepthStencilAttachment = bHasDepth ? &depthRef : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        if ( bHasDepth )
        {
            dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = attachCount;
        rpInfo.pAttachments    = attachments;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
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
        std::scoped_lock<mutex> lock{ _compositeFbMutex };
        auto                    existing = _mapCompositeFramebuffer.find( key );
        if ( existing != _mapCompositeFramebuffer.end() )
        {
            outRecord = existing->second;
            return outRecord._framebuffer != VK_NULL_HANDLE && outRecord._renderPass != VK_NULL_HANDLE;
        }

        VkImageView arrColorView[kMaxColorAttachments]{};
        uint32      arrColorFormat[kMaxColorAttachments]{};
        uint32      width{ 0 };
        uint32      height{ 0 };
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            VulkanTextureRecord* pTex = resolveTexture( key._arrColor[colorIndex] );
            if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE || pTex->_bDepthStencil != 0 )
                return false;
            arrColorView[colorIndex]   = pTex->_imageView;
            arrColorFormat[colorIndex] = pTex->_format;
            width                      = pTex->_width;
            height                     = pTex->_height;
        }

        VkImageView depthView = VK_NULL_HANDLE;
        uint32      depthFormat{ 0 };
        if ( key._depth != 0 )
        {
            VulkanTextureRecord* pTex = resolveTexture( key._depth );
            if ( pTex == nullptr || pTex->_imageView == VK_NULL_HANDLE || pTex->_bDepthStencil == 0 )
                return false;
            depthView   = pTex->_imageView;
            depthFormat = pTex->_format;
            if ( width == 0 )
            {
                width  = pTex->_width;
                height = pTex->_height;
            }
        }
        if ( key._colorCount == 0 && depthView == VK_NULL_HANDLE )
            return false;

        VkAttachmentDescription arrAttachment[kMaxColorAttachments + 1]{};
        VkAttachmentReference   arrColorRef[kMaxColorAttachments]{};
        VkImageView             arrFbAttachment[kMaxColorAttachments + 1]{};
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            arrAttachment[colorIndex].format         = static_cast<VkFormat>( arrColorFormat[colorIndex] );
            arrAttachment[colorIndex].samples        = VK_SAMPLE_COUNT_1_BIT;
            arrAttachment[colorIndex].loadOp         = toVkLoadOp( static_cast<RHIRenderPassLoadOp>( key._arrColorLoadOp[colorIndex] ) );
            arrAttachment[colorIndex].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            arrAttachment[colorIndex].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            arrAttachment[colorIndex].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            // Match pre-begin transitionImageLayout to COLOR_ATTACHMENT_OPTIMAL.
            arrAttachment[colorIndex].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            arrAttachment[colorIndex].finalLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            arrColorRef[colorIndex].attachment      = colorIndex;
            arrColorRef[colorIndex].layout          = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            arrFbAttachment[colorIndex]             = arrColorView[colorIndex];
        }

        VkAttachmentReference depthRef{};
        uint32                attachCount = key._colorCount;
        const bool            bHasDepth   = depthView != VK_NULL_HANDLE;
        if ( bHasDepth )
        {
            arrAttachment[attachCount].format         = static_cast<VkFormat>( depthFormat );
            arrAttachment[attachCount].samples        = VK_SAMPLE_COUNT_1_BIT;
            arrAttachment[attachCount].loadOp         = toVkLoadOp( static_cast<RHIRenderPassLoadOp>( key._depthLoadOp ) );
            arrAttachment[attachCount].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            arrAttachment[attachCount].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            arrAttachment[attachCount].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            arrAttachment[attachCount].initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            arrAttachment[attachCount].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthRef.attachment                       = attachCount;
            depthRef.layout                           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            arrFbAttachment[attachCount]              = depthView;
            ++attachCount;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = key._colorCount;
        subpass.pColorAttachments       = key._colorCount > 0 ? arrColorRef : nullptr;
        subpass.pDepthStencilAttachment = bHasDepth ? &depthRef : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = attachCount;
        rpInfo.pAttachments    = arrAttachment;
        rpInfo.subpassCount    = 1;
        rpInfo.pSubpasses      = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies   = &dependency;

        CompositeFbRecord record{};
        if ( vkCreateRenderPass( _device, &rpInfo, nullptr, &record._renderPass ) != VK_SUCCESS )
            return false;

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = record._renderPass;
        fbInfo.attachmentCount = attachCount;
        fbInfo.pAttachments    = arrFbAttachment;
        fbInfo.width           = width;
        fbInfo.height          = height;
        fbInfo.layers          = 1;

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
        std::scoped_lock<mutex> lock{ _compositeFbMutex };
        for ( auto it = _mapCompositeFramebuffer.begin(); it != _mapCompositeFramebuffer.end(); )
        {
            bool bUses = ( it->first._depth == texture );
            for ( uint32 colorIndex = 0; colorIndex < it->first._colorCount && bUses == false; ++colorIndex )
            {
                bUses = ( it->first._arrColor[colorIndex] == texture );
            }
            if ( bUses )
            {
                // 실행 중인 커맨드버퍼가 아직 참조할 수 있으므로 펜스 통과 후에 파괴한다.
                enqueueFramebufferRelease( it->second._framebuffer, it->second._renderPass );
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

        const uint32 alignedSize = MathUtil::align( sizeBytes, 256u );

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = alignedSize;
        bufferInfo.usage       = usageFlags;
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
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
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
