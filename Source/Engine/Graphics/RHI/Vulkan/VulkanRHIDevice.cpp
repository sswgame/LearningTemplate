#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResource.h"
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
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    VulkanRHIDevice::VulkanRHIDevice()
        : _instance{ nullptr }
        , _debugMessenger{ nullptr }
        , _physicalDevice{ nullptr }
        , _device{ nullptr }
        , _graphicsQueue{ nullptr }
        , _graphicsQueueFamilyIndex{ 0 }
        , _swapChain{}
        , _renderPass{ nullptr }
        , _renderPassLoad{ nullptr }
        , _offscreenRenderPass{ nullptr }
        , _commandPool{ nullptr }
        , _listCommandBuffer{}
        , _listInFlightFence{}
        , _listImagesInFlight{}
        , _listRingFrameNumber{}
        , _pHWnd{ nullptr }
        , _pDisplayHandle{ nullptr }
        , _currentFrame{ 0 }
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
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
    {
        _resourceImpl = sw::make_unique<VulkanRHIResource>( this );
    }

    VulkanRHIDevice::~VulkanRHIDevice()
    {
        shutdown();
    }

    IRHIResource*       VulkanRHIDevice::getResource() { return _resourceImpl.get(); }
    IRHICommandContext* VulkanRHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

    bool VulkanRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        _pHWnd          = desc._pWindowHandle;
        _pDisplayHandle = desc._pWindowDisplay;
        _width          = desc._width;
        _height         = desc._height;

        // 백버퍼 포맷/개수는 백엔드 간 계약값이다 — 스왑체인 재생성 때도 같은 요청을 써야 하므로 보관한다.
        _swapChain.setRequested( desc._format, desc._bufferCount );

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

            if ( _swapChain.createSurface( _instance, _pHWnd, _pDisplayHandle, _linuxWsi ) == false )
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
            if ( _swapChain.create( _physicalDevice, _device, _width, _height ) == false )
                return false;

            if ( createRenderPass() == false )
                return false;

            if ( _swapChain.createFramebuffers( _device, _renderPass ) == false )
                return false;
        }

        BLOCK( "Command Pool / Sync Objects" )
        {
            if ( createCommandPool() == false )
                return false;

            if ( createCommandBuffers() == false )
                return false;

            if ( _swapChain.createSemaphores( _device, constant::kMaxFrameCountInFlight ) == false )
                return false;

            if ( createFrameFences() == false )
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
        _frameStreamContext = sw::make_unique<VulkanRHICommandContext>( this );

        SW_LOG_INFO( "Vulkan RHI Backend Device Initialized Successfully (Validation Layers: %#)", _bEnableValidationLayers == SW_TRUE ? "ENABLED" : "DISABLED" );
        return true;
    }

    void VulkanRHIDevice::shutdownInternal()
    {
        if ( _device )
        {
            vkDeviceWaitIdle( _device );
            _releaseQueue.flushAll();
            _frameStreamContext.reset();

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

            _swapChain.destroy( _device );
            _swapChain.destroySemaphores( _device );
            destroyFrameFences();

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
        _swapChain.destroySurface( _instance );
        if ( _instance )
        {
            vkDestroyInstance( _instance, nullptr );
            _instance = nullptr;
        }
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

    RHIBufferHandle VulkanRHIDevice::createVulkanBuffer( uint32 sizeBytes, uint32 usageFlags, const void* pInitialData )
    {
        if ( _device == nullptr || sizeBytes == 0 )
            return 0;

        const uint32 alignedSize = MathUtil::align( sizeBytes, constant::kConstantBufferAlignment );

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
