#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/Support/FrameResourceRing.h"
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
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

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
        , _frameStreamContext{ nullptr }
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
    IRHICommandContext* VulkanRHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

    bool VulkanRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        _pHWnd          = desc._pWindowHandle;
        _pDisplayHandle = desc._pWindowDisplay;
        _width          = desc._width;
        _height         = desc._height;

        // 백버퍼 포맷/개수는 백엔드 간 계약값이다 — 스왑체인 재생성 때도 같은 요청을 써야 하므로 보관한다.
        _requestedBackBufferFormat = desc._format;
        _requestedBufferCount      = desc._bufferCount;

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
        _activeFrameBuffer        = _listCommandBuffer[_currentFrame];
        _frameSegmentCursor       = 0;
        _bFrameAcquireWaitPending = 1;
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

        // acquire 대기는 프레임당 한 번만 — 즉시 모드에서 앞선 제출이 이미 소비했으면 생략한다.
        VkSemaphore          arrWaitSemaphore[] = { _listImageAvailableSemaphore[_currentFrame] };
        VkPipelineStageFlags arrWaitStage[]     = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        if ( _bFrameAcquireWaitPending != 0 )
        {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores    = arrWaitSemaphore;
            submitInfo.pWaitDstStageMask  = arrWaitStage;
            _bFrameAcquireWaitPending     = 0;
        }

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

    unique_ptr<IRHICommandList> VulkanRHIDevice::createCommandList()
    {
        return make_unique<VulkanRHICommandList>( this );
    }

    void VulkanRHIDevice::executeCommandListImmediate( IRHICommandList* pCmdList )
    {
        VulkanRHICommandList* pList = static_cast<VulkanRHICommandList*>( pCmdList );
        if ( pList == nullptr || _graphicsQueue == VK_NULL_HANDLE )
            return;

        const VkCommandBuffer listBuffer = pList->nativeCommandBuffer();
        if ( listBuffer == VK_NULL_HANDLE )
            return;

        // 프레임 밖 일회성 제출이라 프레임 펜스에 얹을 수 없다 — 자체 제출 후 큐가 비기를 기다려
        // 호출자가 결과를 바로 쓸 수 있게 한다(업로드/스모크 용도라 빈도가 낮다).
        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &listBuffer;
        if ( vkQueueSubmit( _graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE ) == VK_SUCCESS )
            vkQueueWaitIdle( _graphicsQueue );
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

        // 즉시 모드: 여기서 바로 내보낸다. acquire 세마포어 대기는 '이 프레임의 첫 제출'에만 걸고,
        // renderFinished 신호와 인플라이트 펜스는 endFrame 의 마지막 제출이 담당한다.
        if ( _bImmediateSubmit )
        {
            VkSubmitInfo         flushInfo{};
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            flushInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            flushInfo.commandBufferCount   = static_cast<uint32>( _listPendingSubmit.size() );
            flushInfo.pCommandBuffers      = _listPendingSubmit.data();
            if ( _bFrameAcquireWaitPending != 0 && _listImageAvailableSemaphore.empty() == false )
            {
                flushInfo.waitSemaphoreCount = 1;
                flushInfo.pWaitSemaphores    = &_listImageAvailableSemaphore[_currentFrame];
                flushInfo.pWaitDstStageMask  = &waitStage;
                _bFrameAcquireWaitPending    = 0;
            }
            vkQueueSubmit( _graphicsQueue, 1, &flushInfo, VK_NULL_HANDLE );
            _listPendingSubmit.clear();
        }

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
