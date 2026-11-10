#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/ProcessEnvironment.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Backend/RHILiveCommandListUtil.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandContext.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHICommandList.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"

#include <vulkan/vulkan.h>

#if defined( SW_PLATFORM_WINDOWS )
    #include <vulkan/vulkan_win32.h>
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/Foundation/X11Headers.h"

    #include <X11/Xlib-xcb.h>
    #include <vulkan/vulkan_xcb.h>
    #include <vulkan/vulkan_xlib.h>
    #include <xcb/xcb.h>

    #include "Core/Foundation/X11MacroUndef.h"
#elif defined( SW_PLATFORM_ANDROID )
    #include <vulkan/vulkan_android.h>
#else
    #error "Unsupported platform"
#endif
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/Foundation/BuildInfo.h"
#include "Engine/Graphics/Debug/GPUBreadcrumbs.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestampFrame.h"
#include "Engine/Graphics/RHI/Transfer/RHIStagingRingUtil.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanOneShotCommands.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIRequiredVersion.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHISamplerPreset.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIUtil.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    VulkanRHIDevice::VulkanRHIDevice( uint32 deviceGeneration )
        : _instance{ nullptr }
        , _debugMessenger{ nullptr }
        , _physicalDevice{ nullptr }
        , _device{ nullptr }
        , _graphicsQueue{ nullptr }
        , _graphicsQueueFamilyIndex{ 0 }
        , _asyncComputeQueue{ nullptr }
        , _frameFenceRing{}
        , _swapChain{}
        , _renderPass{ nullptr }
        , _renderPassLoad{ nullptr }
        , _offscreenRenderPass{ nullptr }
        , _commandPool{ nullptr }
        , _oneShotCommandPool{ nullptr }
        , _oneShotMutex{}
        , _queueMutex{}
        , _liveCmdListMutex{}
        , _listLiveCmd{}
        , _frameSegmentCursor{ 0 }
        , _timestampPeriod{ 0.0f }
        , _activeFrameBuffer{ nullptr }
        , _timestampPool{ nullptr }
        , _clockQueryPool{ nullptr }
        , _timestampFrame{}
        , _listCommandBuffer{}
        , _listInFlightFence{}
        , _windowSwapChains{}
        , _listPendingAcquireWait{}
        , _listPendingComputeWait{}
        , _listSubmitWaitSemaphore{}
        , _listSubmitWaitStage{}
        , _listSubmitSignalSemaphore{}
        , _listAsyncSignal{}
        , _listAsyncSignalWaited{}
        , _listAsyncSemaphorePool{}
        , _asyncSemaphoreCursor{ 0 }
        , _pHWnd{ nullptr }
        , _pDisplayHandle{ nullptr }
        , _surfaceKind{ NativeSurfaceKind::Win32 }
        , _wsiKind{ VulkanWsiKind::None }
        , _width{ 0 }
        , _height{ 0 }
        , _depthFormat{ 0 }
        , _surfaceLostCount{ 0 }
        , _bFrameStarted{ SW_FALSE }
#if defined( SW_VULKAN_VALIDATION )
        , _bEnableValidationLayers{ SW_TRUE }
#else
        , _bEnableValidationLayers{ SW_FALSE }
#endif
        , _bMultiDrawIndirect{ SW_FALSE }
        , _bDrawIndirectCount{ SW_FALSE }
        , _bSamplerAnisotropy{ SW_FALSE }
        , _bFillModeNonSolid{ SW_FALSE }
        , _bDepthClamp{ SW_FALSE }
        , _bGeometryShader{ 0 }
        , _bTessellationShader{ 0 }
        , _bSwapChainDirty{ SW_FALSE }
        , _bDepthHasStencil{ SW_FALSE }
        , _reservedSwapChainBit{ 0 }
        , _bMemoryBudget{ SW_FALSE }
        , _bSurfaceLost{ SW_FALSE }
        , _bDeviceFault{ SW_FALSE }
        , _bFragmentStores{ SW_FALSE }
        , _bSwapChainRecreateFailing{ SW_FALSE }
        , _breadcrumbMode{ VulkanBreadcrumbMode::None }
        , _pCmdSetCheckpoint{ nullptr }
        , _pGetCheckpointData{ nullptr }
        , _pCmdWriteBufferMarker{ nullptr }
        , _pCmdDrawIndirectCount{ nullptr }
        , _pCmdDrawIndexedIndirectCount{ nullptr }
        , _breadcrumbBuffer{ VK_NULL_HANDLE }
        , _breadcrumbMemory{ VK_NULL_HANDLE }
        , _pBreadcrumbMapped{ nullptr }
        , _bTimestampEnabled{ SW_FALSE }
        , _timestampRing{}
        , _defaultSampler{ nullptr }
        , _pipelineLayout{ nullptr }
        , _descriptorPool{ nullptr }
        , _arrStaticSampler{}
        , _gpuBuffers{}
        , _constantBufferMirror{}
        , _bufferRegistry{ kBindlessBufferCapacity }
        , _uavRegistry{ kBindlessBufferCapacity }
        , _arrBufferReadSlot{ makeFixedCountArray<atomic<uint64>, kBindlessBufferCapacity>() }
        , _arrUAVBufferReadSlot{ makeFixedCountArray<atomic<uint64>, kBindlessBufferCapacity>() }
        , _gpuTextures{}
        , _releaseQueue{}
        , _renderPassCache{}
        , _textureRegistry{ kBindlessTextureCount }
        , _slotSetLayout{ nullptr }
        , _textureSetLayout{ nullptr }
        , _textureSet{ nullptr }
        , _arrFrameDescriptorPoolSet{}
        , _listCmdListDescriptorPoolSet{}
        , _bindlessDummyImage{ nullptr }
        , _bindlessDummyView{ nullptr }
        , _bindlessDummyMemory{ nullptr }
        , _dummyUBO{ nullptr }
        , _dummyUBOMemory{ nullptr }
        , _pipelineStates{}
        , _pipelineCache{ nullptr }
        , _transferSerials{ deviceGeneration }
        , _readbackTable{}
        , _transferFenceQueue{ _transferSerials }
        , _transferReleaseQueue{}
        , _uploadRing{}
        , _readbackRing{}
        , _transferMutex{}
        , _transferCommandPool{ nullptr }
        , _transferJobMutex{}
        , _listFreeTransferJob{}
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
        , _timestampProvider{ *this }
        , _lastSubmittedEventMarker{}
        , _submittedMarkerMutex{}
    {
        _resourceImpl       = sw::make_unique<VulkanRHIResourceFactory>( this );
        _pTimestampProvider = &_timestampProvider;
    }

    VulkanRHIDevice::~VulkanRHIDevice()
    {
        shutdown();
    }

    VulkanRHIDevice::VulkanHeapMemory::~VulkanHeapMemory()
    {
        if ( _device != VK_NULL_HANDLE && _memory != VK_NULL_HANDLE )
            vkFreeMemory( _device, _memory, nullptr );
    }

    IRHIResourceFactory* VulkanRHIDevice::getResourceFactory()
    {
        return _resourceImpl.get();
    }
    IRHICommandContext* VulkanRHIDevice::getFrameStreamContext()
    {
        return _frameStreamContext.get();
    }

    // 길이 유지(긴 함수 정리 기준): 인스턴스 · 물리 디바이스 · 논리 디바이스 · 스왑체인 생성의 순서가 곧 의미라 나누지 않는다.
    bool VulkanRHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
#if defined( SW_PLATFORM_WINDOWS )
        // Windows 의 표면은 Win32 `HWND` 다 — 핸들이 Win32 표면의 것이 아니면 HWND 로 읽지 않고 오류로 멈춘다.
        if ( desc.tryGetWin32Handle( _pHWnd ) == false )
        {
            SW_LOG_ERROR_ID( "Vulkan.VulkanWindowsNeedsWin32", "Vulkan on Windows needs a Win32 surface" );
            return false;
        }
#elif defined( SW_PLATFORM_POSIX )
        _pHWnd = desc._pWindowHandle; // 종류별 읽기는 `createSurface` 의 `_wsiKind` 가 고른다(`_surfaceKind` 에서 정한다)
#else
    #error "Unsupported platform"
#endif
        _pDisplayHandle = desc._pWindowDisplay;
        _surfaceKind    = desc._surfaceKind;
        _width          = desc._width;
        _height         = desc._height;

        // 백버퍼 포맷 · 개수는 백엔드 간 계약값이다. 스왑체인 재생성 때도 같은 요청을 써야 하므로 보관한다.
        _swapChain.setRequested( desc._format, desc._bufferCount, desc._bVSync );

        BLOCK( "Validation Layer Setup" )
        {
#if defined( SW_PLATFORM_WINDOWS )
            if ( _bEnableValidationLayers == SW_TRUE )
            {
                string execDir       = ProcessRoots::get()._binary.str();
                bool   bLayerPathSet = true;
                if ( FileUtil::exists( FileUtil::joinPath( execDir, "VkLayer_khronos_validation.json" ) ) )
                {
                    bLayerPathSet = ProcessEnvironment::trySet( "VK_ADD_LAYER_PATH", execDir ) && ProcessEnvironment::trySet( "VK_LAYER_PATH", execDir );
                }
                else
                {
                    string vulkanSdkEnv;
                    if ( ProcessEnvironment::tryReadNonEmpty( "VULKAN_SDK", vulkanSdkEnv ) )
                        bLayerPathSet = ProcessEnvironment::trySet( "VK_ADD_LAYER_PATH", FileUtil::joinPath( vulkanSdkEnv, "Bin" ) );
                }
                if ( bLayerPathSet == false )
                    SW_LOG_WARNING_ID( "Vulkan.CouldNotPointVulkan", "Could not point the Vulkan loader at the validation layer - the layer check below decides" );
            }
#endif

            if ( _bEnableValidationLayers == SW_TRUE && supportsValidationLayer() == false )
            {
                // 리눅스는 시스템 패키지(vulkan-validationlayers)의 레이어를 찾는다 — vcpkg 포트는 Windows 만 짓는다(Bin 옆 VK_LAYER_PATH).
                SW_LOG_WARNING_ID( "Vulkan.VulkanValidationLayersRequested", "Vulkan Validation Layers requested, but VK_LAYER_KHRONOS_validation was not found - running without validation "
                                                                             "(Linux: install the vulkan-validationlayers package)" );
                _bEnableValidationLayers = SW_FALSE;
            }
        }
        markInitPhase( "validation" );

        BLOCK( "Instance / Surface / Logical Device" )
        {
            if ( createInstance() == false )
                return false;

            createDebugMessenger();
            markInitPhase( "instance" );

            if ( _swapChain.createSurface( _instance, _pHWnd, _pDisplayHandle, _wsiKind ) == false )
                return false;
            markInitPhase( "surface" );

            if ( pickPhysicalDevice( desc._bSoftwareAdapter, desc._bPreferMinimumPower ) == false )
                return false;
            markInitPhase( "adapter" );

            if ( selectDepthFormat() == false )
            {
                SW_LOG_ERROR_ID( "Vulkan.NoSupportedDepthStencil", "No supported depth/stencil format on this GPU." );
                return false;
            }

            if ( verifyRequiredColorAttachmentFormats() == false )
                return false;

            if ( createLogicalDevice() == false )
                return false;
            markInitPhase( "device" );
        }

        BLOCK( "Swapchain / RenderPass / Framebuffers" )
        {
            if ( _swapChain.create( _physicalDevice, _device, _width, _height ) == false )
                return false;

            if ( createRenderPass() == false )
                return false;

            if ( _swapChain.createFramebuffers( _device, _renderPass ) == false )
                return false;
            markInitPhase( "swapchain" );
        }

        BLOCK( "Command Pool / Sync Objects" )
        {
            if ( createCommandPool() == false )
                return false;

            if ( createCommandBuffers() == false )
                return false;

            if ( _swapChain.createSemaphores( _device, constant::render::kMaxFrameCountInFlight ) == false )
                return false;

            if ( createFrameFences() == false )
                return false;
            markInitPhase( "commands" );
        }

        BLOCK( "Transfer Pool / Staging Rings" )
        {
            if ( initializeTransfers( desc ) == false )
                return false;
            markInitPhase( "transfers" );
        }

        BLOCK( "Descriptor / Pipeline Layout" )
        {
            if ( createDescriptorResources() == false )
            {
                SW_LOG_ERROR_ID( "Vulkan.FailedCreateDescriptorPipeline", "Failed to create descriptor / pipeline layout resources." );
                return false;
            }
            if ( ensureTextureSet() == false )
            {
                SW_LOG_ERROR_ID( "Vulkan.FailedCreateBindlessTexture", "Failed to create the bindless texture set." );
                return false;
            }
            markInitPhase( "descriptors" );
        }

        (void)initializePipelineCache(); // 캐시가 없으면 파이프라인을 처음부터 만들 뿐이다 — 실패는 initializePipelineCache 가 경고로 남긴다
        markInitPhase( "pipelineCache" );
        _frameStreamContext = sw::make_unique<VulkanRHICommandContext>( this );

        SW_LOG_INFO_ID( "Vulkan.VulkanRHIBackendDevice", "Vulkan RHI Backend Device Initialized Successfully (Validation Layers: %#)", _bEnableValidationLayers == SW_TRUE ? "ENABLED" : "DISABLED" );
        return true;
    }

    void VulkanRHIDevice::writeConstantBufferSlot( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size )
    {
        VulkanBufferRecord* pRecord                     = resolveAllocatedBuffer( buffer );
        const bool          bCannotUpdateConstantBuffer = pRecord == nullptr || pRecord->_cbSlotSize == 0 || pRecord->_memory == VK_NULL_HANDLE || pData == nullptr || size == 0;
        if ( bCannotUpdateConstantBuffer )
            return;
        const uint32 slotSize  = pRecord->_cbSlotSize;
        const uint32 copyBytes = MathUtil::min( size, slotSize );
        void*        pMapped{ nullptr };
        // Map 실패는 오류로 남긴다 — 조용히 건너뛰면 옛 상수로 그려 원인이 화면에서만 보인다.
        if ( isVkSuccess( vkMapMemory( _device, pRecord->_memory, static_cast<VkDeviceSize>( slot ) * slotSize, copyBytes, 0, &pMapped ), "vkMapMemory (constant buffer slot)" ) == false || pMapped == nullptr )
            return;
        Memory::copy( pMapped, pData, copyBytes );
        vkUnmapMemory( _device, pRecord->_memory );
    }

    void VulkanRHIDevice::fillConstantBufferSlot()
    {
        // 칸 크기는 버퍼 레코드에서 락 없이 읽는다(`VulkanBufferRecord::_cbSlotSize`). 그림자 표는 제 뮤텍스가 지킨다.
        _constantBufferMirror.fillSlot( getFrameSlot() % constant::render::kMaxFrameCountInFlight,
                                        [this]( RHIBufferHandle buffer, uint32 slot, const void* pData, uint32 size )
        { writeConstantBufferSlot( buffer, slot, pData, size ); } );
    }

    void VulkanRHIDevice::detachCommandRecordingInternal()
    {
        // 뗀 리스트는 제 커맨드 버퍼 쌍을 부순다. 안 그러면 그쪽 소멸자가 죽은 디바이스에 반납하려 들고, 풀은 새어 검증 레이어가 잡는다.
        _frameStreamContext.reset();
        RHILiveCommandListUtil::detachAll( _liveCmdListMutex, _listLiveCmd );
    }

    // 길이 유지(긴 함수 정리 기준): 생성의 역순으로 놓는 해제 목록(GPU 유휴 대기 뒤)이라 나누지 않는다.
    void VulkanRHIDevice::shutdownInternal()
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        if ( _device )
        {
            // 전송을 기다리던 해제는 프레임 해제 큐로 넘어가므로 아래의 `_releaseQueue.flushAll` 보다 먼저 놓는다.
            shutdownTransfers();
            for ( vector<VkSemaphore>& listPool : _listAsyncSemaphorePool )
            {
                for ( VkSemaphore semaphore : listPool )
                {
                    vkDestroySemaphore( _device, semaphore, nullptr );
                }
                listPool.clear();
            }
            _asyncComputeQueue = VK_NULL_HANDLE;
            if ( _commandPool )
            {
                vkDestroyCommandPool( _device, _commandPool, nullptr );
                _commandPool = VK_NULL_HANDLE;
            }
            if ( _oneShotCommandPool )
            {
                vkDestroyCommandPool( _device, _oneShotCommandPool, nullptr );
                _oneShotCommandPool = VK_NULL_HANDLE;
            }

            _gpuBuffers.forEach( [this]( VulkanBufferRecord& record )
            {
                if ( record._buffer != VK_NULL_HANDLE )
                    vkDestroyBuffer( _device, record._buffer, nullptr );
                if ( record._memory != VK_NULL_HANDLE )
                    vkFreeMemory( _device, record._memory, nullptr );
            } );
            _gpuBuffers.clear();
            _constantBufferMirror.clear();
            for ( StructuredUploadSlot& slot : _arrStructuredUploadSlot )
            {
                if ( slot._pMapped != nullptr && slot._memory != VK_NULL_HANDLE )
                    vkUnmapMemory( _device, slot._memory );
                if ( slot._buffer != VK_NULL_HANDLE )
                    vkDestroyBuffer( _device, slot._buffer, nullptr );
                if ( slot._memory != VK_NULL_HANDLE )
                    vkFreeMemory( _device, slot._memory, nullptr );
                slot = StructuredUploadSlot{};
            }
            _recordingState._geometry = RHIGeometryBindings{};

            _gpuTextures.forEach( [this]( VulkanTextureRecord& record )
            {
                destroyOffscreenFramebuffer( record );
                for ( VkImageView sliceView : record._listSliceView )
                {
                    vkDestroyImageView( _device, sliceView, nullptr );
                }
                record._listSliceView.clear();
                // 뷰는 이미지보다 **반드시 먼저** 없앤다. 깊이 텍스처의 `_sampleView` 도 포함한다(빠뜨리면 살아 있는 뷰를
                // 두고 vkDestroyImage 를 부른다). destroyTexture 경로도 둘 다 지운다.
                if ( record._imageView != VK_NULL_HANDLE )
                    vkDestroyImageView( _device, record._imageView, nullptr );
                if ( record._sampleView != VK_NULL_HANDLE )
                    vkDestroyImageView( _device, record._sampleView, nullptr );
                if ( record._image != VK_NULL_HANDLE )
                    vkDestroyImage( _device, record._image, nullptr );
                if ( record._memory != VK_NULL_HANDLE )
                    vkFreeMemory( _device, record._memory, nullptr );
            } );
            // 힙 핸들 표를 텍스처 표보다 먼저 비운다 — 이미지는 위에서 모두 지웠고, 남은 참조(레코드 · 해제 대리자)가 놓일 때 장치가 아직 살아 있다.
            _mapTransientHeap.clear();
            _gpuTextures.clear();
            // 오프스크린 프레임버퍼는 해제 큐로 놓인다(`enqueueFramebufferRelease`). GPU 는 2 단계에서 이미 쉬고 있으니 디바이스가 살아 있는 지금 부른다 —
            // 남기면 해제 큐의 소멸자가 부순 VkDevice 로 vkDestroyFramebuffer 를 부른다(`IRHIDevice::shutdown` 이 단언한다).
            _releaseQueue.flushAll();
            _bufferRegistry.clear(); // 빈 목록도 함께 — 남기면 다음 장치가 표 밖 번호를 낸다
            _uavRegistry.clear();
            for ( uint32 index = 0; index < kBindlessBufferCapacity; ++index )
            {
                publishBufferReadSlot( index, false, 0 );
                publishBufferReadSlot( index, true, 0 );
            }
            // 합성 프레임버퍼 · PSO 호환 RP · desc RP 는 캐시가 한 번에 놓는다. 스왑체인 RP(_renderPass)는 아래에서 따로.
            _renderPassCache.destroyAll( _device, _renderPass );
            _textureRegistry.clear();

            _textureSet = VK_NULL_HANDLE; // 디스크립터 풀이 소유한다
            if ( _textureSetLayout )
            {
                vkDestroyDescriptorSetLayout( _device, _textureSetLayout, nullptr );
                _textureSetLayout = nullptr;
            }
            if ( _slotSetLayout )
            {
                vkDestroyDescriptorSetLayout( _device, _slotSetLayout, nullptr );
                _slotSetLayout = nullptr;
            }
            for ( VulkanDescriptorPoolSet& poolSet : _arrFrameDescriptorPoolSet )
            {
                destroyDescriptorPoolSet( poolSet );
            }
            // 목록은 잠금 아래에서 꺼내고 장치 호출(풀 부수기)은 잠금 밖에서 한다(잠금은 잎 잠금, 결정 5-3).
            vector<unique_ptr<VulkanDescriptorPoolSet>> listPoolSet;
            {
                ScopedLock<mutex> lock{ _cmdListPoolMutex };
                listPoolSet.swap( _listCmdListDescriptorPoolSet );
            }
            for ( unique_ptr<VulkanDescriptorPoolSet>& pPoolSet : listPoolSet )
            {
                if ( pPoolSet != nullptr )
                    destroyDescriptorPoolSet( *pPoolSet );
            }
            if ( _dummyUBO )
            {
                vkDestroyBuffer( _device, _dummyUBO, nullptr );
                _dummyUBO = nullptr;
            }
            if ( _dummyUBOMemory )
            {
                vkFreeMemory( _device, _dummyUBOMemory, nullptr );
                _dummyUBOMemory = nullptr;
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

            // 타임스탬프 쿼리 풀은 계측을 켠 적이 있으면 있다. 안 부수면 검증 레이어가 종료 때 새는 객체로 잡는다.
            if ( _timestampPool != VK_NULL_HANDLE )
            {
                vkDestroyQueryPool( _device, _timestampPool, nullptr );
                _timestampPool = VK_NULL_HANDLE;
            }
            if ( _clockQueryPool != VK_NULL_HANDLE )
            {
                vkDestroyQueryPool( _device, _clockQueryPool, nullptr );
                _clockQueryPool = VK_NULL_HANDLE;
            }

            if ( _defaultSampler )
            {
                vkDestroySampler( _device, _defaultSampler, nullptr );
                _defaultSampler = nullptr;
            }
            {
                ScopedLock<mutex> lock{ _cmdListPoolMutex };
                for ( const VulkanCommandListEntry& entry : _listFreeCmdListEntry )
                {
                    if ( entry._pool != VK_NULL_HANDLE )
                        vkDestroyCommandPool( _device, entry._pool, nullptr );
                }
                _listFreeCmdListEntry.clear();
            }

            _pipelineStates.forEach( [this]( VulkanPipelineStateRecord& pso )
            {
                if ( pso._pipeline != VK_NULL_HANDLE )
                    vkDestroyPipeline( _device, pso._pipeline, nullptr );
            } );
            _pipelineStates.clear();

            if ( _pipelineLayout )
                vkDestroyPipelineLayout( _device, _pipelineLayout, nullptr );
            if ( _descriptorPool )
            {
                getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( _descriptorPool ) );
                vkDestroyDescriptorPool( _device, _descriptorPool, nullptr );
            }
            for ( VkSampler& sampler : _arrStaticSampler )
            {
                if ( sampler != VK_NULL_HANDLE )
                    vkDestroySampler( _device, sampler, nullptr );
                sampler = VK_NULL_HANDLE;
            }

            for ( const unique_ptr<VulkanRHISwapChain>& pWindow : _windowSwapChains.getOwnedList() )
            {
                pWindow->destroy( _device );
                pWindow->destroySemaphores( _device );
                pWindow->destroySurface( _instance );
            }
            _windowSwapChains.clear();
            _swapChain.destroy( _device );
            _swapChain.destroySemaphores( _device );
            destroyFrameFences();

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
            destroyBreadcrumbBuffer();
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

    bool VulkanRHIDevice::queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget )
    {
        if ( _bMemoryBudget == SW_FALSE || _physicalDevice == VK_NULL_HANDLE )
            return false;

        VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProperties{};
        budgetProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
        VkPhysicalDeviceMemoryProperties2 memoryProperties{};
        memoryProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        memoryProperties.pNext = &budgetProperties;
        vkGetPhysicalDeviceMemoryProperties2( _physicalDevice, &memoryProperties );

        // 사용량은 모든 힙(로컬 + 호스트 가시)의 합이다 — 추적기가 호스트 가시 버퍼도 센다. 예산 · 남은 양은 로컬 힙만 본다.
        uint64 usageBytes{ 0 };
        uint64 localUsageBytes{ 0 };
        uint64 localBudgetBytes{ 0 };
        for ( uint32 heapIndex = 0; heapIndex < memoryProperties.memoryProperties.memoryHeapCount; ++heapIndex )
        {
            usageBytes += budgetProperties.heapUsage[heapIndex];
            const bool bDeviceLocal = ( memoryProperties.memoryProperties.memoryHeaps[heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) != 0;
            if ( bDeviceLocal == false )
                continue;
            localUsageBytes += budgetProperties.heapUsage[heapIndex];
            localBudgetBytes += budgetProperties.heapBudget[heapIndex];
        }
        outBudget._usageBytes      = usageBytes;
        outBudget._budgetBytes     = localBudgetBytes;
        outBudget._availableBytes  = localBudgetBytes > localUsageBytes ? localBudgetBytes - localUsageBytes : 0;
        outBudget._scope           = RHIMemoryScope::Process;
        outBudget._bUsageKnown     = SW_TRUE;
        outBudget._bBudgetKnown    = SW_TRUE;
        outBudget._bAvailableKnown = SW_TRUE;
        return true;
    }

    RHIBufferHandle VulkanRHIDevice::createVulkanBuffer( uint32 sizeBytes, uint32 usageFlags, const void* pInitialData, uint32 cbSlotSize )
    {
        if ( _device == nullptr || sizeBytes == 0 )
            return 0;

        const uint32 alignedSize = MathUtil::align( sizeBytes, constant::render::kConstantBufferAlignment );

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = alignedSize;
        bufferInfo.usage       = usageFlags;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buffer = VK_NULL_HANDLE;
        if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &buffer ) != VK_SUCCESS )
        {
            SW_LOG_ERROR_ID( "Vulkan.FailedCreateVkBufferUsage", "Failed to create VkBuffer (usage=0x%#).", usageFlags );
            return 0;
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements( _device, buffer, &memRequirements );

        uint32 memoryTypeIndex{ 0 };
        if ( findMemoryType( memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
        {
            vkDestroyBuffer( _device, buffer, nullptr );
            SW_LOG_ERROR_ID( "Vulkan.FailedFindHostVisible", "Failed to find a host visible memory type for VkBuffer." );
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
            SW_LOG_ERROR_ID( "Vulkan.FailedAllocateMemoryVkBuffer", "Failed to allocate memory for VkBuffer." );
            return 0;
        }

        // 메모리를 붙이지 못한 버퍼 · 초기 값을 쓰지 못한 버퍼는 돌려주지 않는다 — 빈 버퍼로 그리면 조용히 0 을 읽는다.
        bool bReady = isVkSuccess( vkBindBufferMemory( _device, buffer, memory, 0 ), "vkBindBufferMemory" );
        if ( bReady && pInitialData != nullptr )
        {
            void* pMapped{ nullptr };
            bReady = isVkSuccess( vkMapMemory( _device, memory, 0, sizeBytes, 0, &pMapped ), "vkMapMemory (initial data)" ) && pMapped != nullptr;
            if ( bReady )
            {
                Memory::copy( pMapped, pInitialData, sizeBytes );
                vkUnmapMemory( _device, memory );
            }
        }
        if ( bReady == false )
        {
            vkDestroyBuffer( _device, buffer, nullptr );
            vkFreeMemory( _device, memory, nullptr );
            return 0;
        }

        VulkanBufferRecord record{};
        record._buffer     = buffer;
        record._memory     = memory;
        record._sizeBytes  = sizeBytes;
        record._usage      = usageFlags;
        record._cbSlotSize = cbSlotSize;
        record._state      = RHIBufferState::Common;

        // GPU 메모리 추적기의 Buffer 줄에 올리는 유일한 자리다. 크기는 드라이버가 요구한 할당 크기(정렬 포함)다.
        const RHIBufferHandle handle = _gpuBuffers.insert( record );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeBuffer( handle ), RHIMemoryKind::Buffer, memRequirements.size );
        return handle;
    }

    namespace
    {
        /**
         * @brief 파이프라인 캐시 파일(`<Bin>/Saved/ShaderCache/vk_pipeline_cache.bin`)입니다.
         * @details 작업 폴더가 아니라 실행 파일 폴더 기준이다 — 병렬 실행(ctest -j · 시나리오 행렬)도 같은 `Bin` 을 쓰고, 이 캐시는 같은 빌드의 실행끼리 나누는 것이 목적이다(쓰기는 원자적 바꿔치기라 겹쳐도 깨지지 않는다).
         */
        string getVulkanPipelineCachePath()
        {
            return FileUtil::joinPath( ProcessRoots::get()._saved.str(), "ShaderCache/vk_pipeline_cache.bin" );
        }

        struct VulkanRHIDeviceInternal
        {
            /** @brief 큐 호출이 돌려주는 실패 값의 이름입니다. 모르는 값은 "unknown" 이고 숫자는 로그에 같이 적힌다. */
            static const utf8* getResultName( VkResult result )
            {
                switch ( result )
                {
                    case VK_ERROR_DEVICE_LOST:
                        return "VK_ERROR_DEVICE_LOST";
                    case VK_ERROR_OUT_OF_HOST_MEMORY:
                        return "VK_ERROR_OUT_OF_HOST_MEMORY";
                    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                    case VK_TIMEOUT:
                        return "VK_TIMEOUT";
                    default:
                        return "unknown";
                }
            }

            /** @brief VK_EXT_device_fault 의 주소 종류 이름입니다. */
            static const utf8* getFaultAddressTypeName( VkDeviceFaultAddressTypeEXT type )
            {
                switch ( type )
                {
                    case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:
                        return "read-invalid";
                    case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:
                        return "write-invalid";
                    case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:
                        return "execute-invalid";
                    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:
                        return "instruction-pointer-unknown";
                    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:
                        return "instruction-pointer-invalid";
                    case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:
                        return "instruction-pointer-fault";
                    default:
                        return "none";
                }
            }
        };

        constexpr VkDeviceSize kDummyUboSize = 256;
    } // namespace

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
        // 정점 셰이더가 내보내고 픽셀 셰이더가 읽지 않는 출력은 명세상 허용된다(보간 슬롯 하나가 노는 것뿐). 셰이더 하나를 여러 픽셀 셰이더와
        // 짝지우면 늘 생기므로 언리얼 VulkanRHI 도 이 메시지를 거른다.
        if ( pCallbackData->pMessageIdName != nullptr && strstr( pCallbackData->pMessageIdName, "OutputNotConsumed" ) != nullptr )
            return VK_FALSE;

        if ( messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
            SW_LOG_ERROR_ID( "Vulkan.ValidationError", "%#", pCallbackData->pMessage );
        else if ( messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
            SW_LOG_WARNING_ID( "Vulkan.ValidationWarning", "%#", pCallbackData->pMessage );
        else
            SW_LOG_INFO_ID( "Vulkan.ValidationInfo", "%#", pCallbackData->pMessage );
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
        vector<VkLayerProperties> listAvailableLayer;
        if ( enumerateVk( listAvailableLayer, "vkEnumerateInstanceLayerProperties", []( uint32* pCount, VkLayerProperties* pData )
        { return vkEnumerateInstanceLayerProperties( pCount, pData ); } ) == false )
            return false;

        for ( const utf8* pLayerName : s_listValidationLayers )
        {
            bool layerFound{ false };
            for ( const VkLayerProperties& layerProperties : listAvailableLayer )
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

    // 길이를 나누지 않는다: 확장 · 레이어를 확인한 뒤 생성하는 백엔드 API 호출 순서가 곧 뜻이다(창 시스템 길 분기는 짧다).
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

        vector<VkExtensionProperties> listAvailableExt;
        if ( enumerateVk( listAvailableExt, "vkEnumerateInstanceExtensionProperties", []( uint32* pCount, VkExtensionProperties* pData )
        { return vkEnumerateInstanceExtensionProperties( nullptr, pCount, pData ); } ) == false )
            return false;

#if defined( SW_PLATFORM_WINDOWS )
        if ( VulkanRHIUtil::hasExtension( listAvailableExt, VK_KHR_WIN32_SURFACE_EXTENSION_NAME ) == false )
        {
            SW_LOG_ERROR_ID( "Vulkan.VKKHRWin32Surface", "VK_KHR_win32_surface is not available." );
            return false;
        }
        listExtension.push_back( VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
        _wsiKind = VulkanWsiKind::Win32;
#elif defined( SW_PLATFORM_LINUX )
        // WSLg · gfxstream 은 흔히 xcb 만 내놓고 xlib 는 없다.
        _wsiKind = VulkanWsiKind::None;
        if ( _surfaceKind == NativeSurfaceKind::Win32 )
        {
            // 아래 X11 길로 떨어지면 HWND 를 창 ID 로 읽게 된다 — 대신하지 않고 멈춘다.
            SW_LOG_ERROR_ID( "Vulkan.VulkanLinuxNeedsX11", "Vulkan on Linux needs an X11 or Wayland surface, but the window is a Win32 surface" );
            return false;
        }
        if ( _surfaceKind == NativeSurfaceKind::Wayland )
        {
            // Wayland 창(`WaylandWindow`) — 표면은 wl_surface 라 X11 확장으로는 만들 수 없다. 대신할 것 없이 이 확장만 받는다.
            if ( VulkanRHIUtil::hasExtension( listAvailableExt, "VK_KHR_wayland_surface" ) == false )
            {
                SW_LOG_ERROR_ID( "Vulkan.WindowWaylandSurfaceBut", "The window is a Wayland surface but the Vulkan loader has no VK_KHR_wayland_surface (rebuild vulkan-loader with [wayland])" );
                return false;
            }
            listExtension.push_back( "VK_KHR_wayland_surface" );
            _wsiKind = VulkanWsiKind::Wayland;
            SW_LOG_TRACE_ID( "Vulkan.VulkanWSIVKKHR", "Vulkan WSI: VK_KHR_wayland_surface" );
        }
        else if ( VulkanRHIUtil::hasExtension( listAvailableExt, VK_KHR_XLIB_SURFACE_EXTENSION_NAME ) )
        {
            listExtension.push_back( VK_KHR_XLIB_SURFACE_EXTENSION_NAME );
            _wsiKind = VulkanWsiKind::Xlib;
            SW_LOG_TRACE_ID( "Vulkan.VulkanWSIVKKHR2", "Vulkan WSI: VK_KHR_xlib_surface" );
        }
        else if ( VulkanRHIUtil::hasExtension( listAvailableExt, VK_KHR_XCB_SURFACE_EXTENSION_NAME ) )
        {
            listExtension.push_back( VK_KHR_XCB_SURFACE_EXTENSION_NAME );
            _wsiKind = VulkanWsiKind::Xcb;
            SW_LOG_TRACE_ID( "Vulkan.VulkanWSIVKKHR3", "Vulkan WSI: VK_KHR_xcb_surface (xlib unavailable)" );
        }
        else
        {
            SW_LOG_ERROR_ID( "Vulkan.NoVulkanX11WSI", "No Vulkan X11 WSI extension (VK_KHR_xlib_surface / VK_KHR_xcb_surface). Enumerated %# instance extensions.",
                             listAvailableExt.size() );
            for ( const VkExtensionProperties& ext : listAvailableExt )
            {
                (void)ext;
                SW_LOG_TRACE_ID( "Vulkan.InstanceExt", "  instance ext: %#", ext.extensionName );
            }
            SW_LOG_ERROR_ID( "Vulkan.InstallLibxcb1DevLibx11", "Install libxcb1-dev / libx11-xcb-dev, and rebuild vcpkg vulkan-loader with [xcb,xlib]." );
            return false;
        }
#elif defined( SW_PLATFORM_ANDROID )
        // NDK 의 libvulkan.so 가 로더다 — 표면 확장은 Android 1.0 부터 늘 있다(그래도 없으면 장치가 Vulkan 을 주지 않는 것이다).
        if ( VulkanRHIUtil::hasExtension( listAvailableExt, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME ) == false )
        {
            SW_LOG_ERROR_ID( "Vulkan.NoAndroidSurfaceExtension", "VK_KHR_android_surface is not available." );
            _initResult = RHIInitResult::DriverUnsupported;
            return false;
        }
        listExtension.push_back( VK_KHR_ANDROID_SURFACE_EXTENSION_NAME );
        _wsiKind = VulkanWsiKind::Android;
#else
    #error "Unsupported platform"
#endif
        if ( _bEnableValidationLayers == SW_TRUE && VulkanRHIUtil::hasExtension( listAvailableExt, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) )
            listExtension.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
        else if ( _bEnableValidationLayers == SW_TRUE )
        {
            SW_LOG_WARNING_ID( "Vulkan.VulkanValidationLayerFound", "Vulkan validation layer found but VK_EXT_debug_utils is not available - running without validation" );
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
            SW_LOG_ERROR_ID( "Vulkan.FailedCreateVulkanInstance", "Failed to create Vulkan instance! Error code: %#", static_cast<int32>( result ) );
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

        // 검증 메시지 수신은 진단용이라 실패해도 장치는 이어 간다 — 오류로 남기고 핸들을 비운다(파괴 쪽이 빈 핸들을 건너뛴다).
        if ( isVkSuccess( CreateDebugUtilsMessengerEXT( _instance, &createInfo, nullptr, &_debugMessenger ), "vkCreateDebugUtilsMessengerEXT" ) == false )
            _debugMessenger = VK_NULL_HANDLE;
    }

    bool VulkanRHIDevice::pickPhysicalDevice( bool bSoftwareAdapter, bool bPreferMinimumPower )
    {
        vector<VkPhysicalDevice> listDevice;
        const bool               bEnumerated = enumerateVk( listDevice, "vkEnumeratePhysicalDevices", [this]( uint32* pCount, VkPhysicalDevice* pData )
                      { return vkEnumeratePhysicalDevices( _instance, pCount, pData ); } );
        if ( bEnumerated == false || listDevice.empty() )
            return false;
        // 외장 GPU(저전력 선호면 내장 GPU)를 먼저 본다 — 열거 순서는 드라이버 마음이라 하이브리드 노트북에서 내장 GPU 가 먼저 나온다(DX 의 OS 선호 순서 · RHIDxgiAdapter.h 와 같은 규칙, 제조사와 무관).
        const VkPhysicalDeviceType preferredType = bPreferMinimumPower ? VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU : VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        std::stable_partition( listDevice.begin(), listDevice.end(), [preferredType]( VkPhysicalDevice device )
        {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties( device, &properties );
            return properties.deviceType == preferredType;
        } );

        for ( const VkPhysicalDevice& device : listDevice )
        {
            // 셰이더는 요구 판(VulkanRHIRequiredVersion) 타깃의 SPIR-V 로 쿠킹한다. 그보다 낮은 디바이스는 그 모듈을 하나도 받지 못한다.
            VkPhysicalDeviceProperties candidateProperties{};
            vkGetPhysicalDeviceProperties( device, &candidateProperties );
            if ( VulkanRHIRequiredVersion::isAPIVersionSupported( candidateProperties.apiVersion ) == false )
            {
                SW_LOG_WARNING_ID( "Vulkan.SkippingVulkanDeviceAPI", "Skipping Vulkan device '%#': API %#.%# is below the required %#.%# (shaders are SPIR-V for that target)",
                                   candidateProperties.deviceName, VK_API_VERSION_MAJOR( candidateProperties.apiVersion ),
                                   VK_API_VERSION_MINOR( candidateProperties.apiVersion ), constant::render::kVulkanRequiredMajor,
                                   constant::render::kVulkanRequiredMinor );
                continue;
            }
            // 소프트웨어 어댑터를 요청했으면(gv_rhiSoftwareAdapter) CPU 디바이스(lavapipe · SwiftShader)만 후보다.
            if ( bSoftwareAdapter && candidateProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU )
                continue;
            // 셰이더는 네이티브 bindless(SW_BINDLESS)로만 쿠킹한다 — 기능 · 한도가 모자란 장치(모바일에 흔하다)는 텍스처 세트를 만들지 못하고 그리지 못한다.
            if ( meetsBindlessRequirements( device, candidateProperties.deviceName ) == false )
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
                    VkBool32   presentSupport{ false };
                    const bool bQueried = isVkSuccess( vkGetPhysicalDeviceSurfaceSupportKHR( device, queueFamilyIndex, _swapChain.getSurface(), &presentSupport ),
                                                       "vkGetPhysicalDeviceSurfaceSupportKHR" );
                    if ( bQueried && presentSupport )
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
            utf8 arrGPU[constant::buffer::kMax256]{};
            formatstring( arrGPU, constant::buffer::kMax256, "%# (driver %#, api %#.%#.%#)", properties.deviceName,
                          properties.driverVersion, VK_VERSION_MAJOR( properties.apiVersion ),
                          VK_VERSION_MINOR( properties.apiVersion ), VK_VERSION_PATCH( properties.apiVersion ) );
            engine::getSubsystem<CrashHandler>().setContextValue( "GPU", arrGPU );
            _bSoftwareAdapter = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        }
        if ( _physicalDevice == nullptr && bSoftwareAdapter )
        {
            // 환경 탓이다(CPU 구현이 설치되지 않았다) — 결함으로 알리지 않는다.
            SW_LOG_WARNING_ID( "Vulkan.NoCPUVulkanDevice", "No CPU Vulkan device (lavapipe / SwiftShader) for gv_rhiSoftwareAdapter - Vulkan does not start" );
            _initResult = RHIInitResult::DriverUnsupported;
        }
        else if ( _physicalDevice == nullptr )
            SW_LOG_ERROR_ID( "Vulkan.NoVulkanDeviceGraphics", "No Vulkan %#.%# device with a graphics queue that can present to this surface", constant::render::kVulkanRequiredMajor,
                             constant::render::kVulkanRequiredMinor );
        return _physicalDevice != nullptr;
    }

    bool VulkanRHIDevice::meetsBindlessRequirements( VkPhysicalDevice device, const utf8* pDeviceName )
    {
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;
        vkGetPhysicalDeviceFeatures2( device, &features2 );
        VkPhysicalDeviceDescriptorIndexingProperties indexing{};
        indexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
        VkPhysicalDeviceProperties2 properties2{};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &indexing;
        vkGetPhysicalDeviceProperties2( device, &properties2 );

        // 텍스처 세트(set 1)의 두 배열이 쓰는 기능 · 한도다(createDescriptorResources). 데스크톱 드라이버는 모두 넘고, 모바일은 장치마다 다르다(docs/PlatformTargets.md).
        const bool bFeatures = features12.runtimeDescriptorArray == VK_TRUE && features12.descriptorBindingPartiallyBound == VK_TRUE &&
                               features12.descriptorBindingSampledImageUpdateAfterBind == VK_TRUE && features12.descriptorBindingStorageImageUpdateAfterBind == VK_TRUE &&
                               features12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
        const bool bLimits = indexing.maxPerStageDescriptorUpdateAfterBindSampledImages >= kBindlessTextureCount &&
                             indexing.maxDescriptorSetUpdateAfterBindSampledImages >= kBindlessTextureCount &&
                             indexing.maxPerStageDescriptorUpdateAfterBindStorageImages >= kBindlessStorageImageCount &&
                             indexing.maxDescriptorSetUpdateAfterBindStorageImages >= kBindlessStorageImageCount;
        if ( bFeatures && bLimits )
            return true;
        SW_LOG_WARNING_ID( "Vulkan.SkippingDeviceBindless", "Skipping Vulkan device '%#': descriptor indexing features %# or update-after-bind limits (sampled %# / %#, storage %# / %#) are below the bindless sets",
                           pDeviceName, bFeatures ? "present" : "missing", indexing.maxPerStageDescriptorUpdateAfterBindSampledImages, kBindlessTextureCount,
                           indexing.maxPerStageDescriptorUpdateAfterBindStorageImages, kBindlessStorageImageCount );
        return false;
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
            SW_LOG_TRACE_ID( "Vulkan.SelectedDepthFormatStencil", "Selected depth format %# (stencil=%#)", static_cast<uint32>( format ),
                             static_cast<uint32>( _bDepthHasStencil ) );
            return true;
        }
        _depthFormat      = 0;
        _bDepthHasStencil = 0;
        return false;
    }

    bool VulkanRHIDevice::verifyRequiredColorAttachmentFormats() const
    {
        // MRT 한도 — G버퍼(판 3)가 여섯 장을 함께 쓴다. Vulkan 의 최소 보장은 4 라 장치마다 확인한다(DX · GL · WebGPU 는 8 을 보장한다).
        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties( _physicalDevice, &deviceProperties );
        if ( deviceProperties.limits.maxColorAttachments < kMaxColorAttachments )
        {
            SW_LOG_ERROR_ID( "Vulkan.GPUColorAttachmentLimit", "This GPU binds %# color attachments at once, the renderer needs %# (G-buffer)", deviceProperties.limits.maxColorAttachments,
                             kMaxColorAttachments );
            return false;
        }
        // 화면 속도 · 왜곡 첨부(R16G16 실수)도 G버퍼 노멀(R16G16 부호 정규화)처럼 샘플 가능한 컬러 첨부여야 한다.
        const VkFormat arrRequired[] = { VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16_SFLOAT };
        for ( const VkFormat format : arrRequired )
        {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties( _physicalDevice, format, &props );
            const VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            if ( ( props.optimalTilingFeatures & kNeeded ) != kNeeded )
            {
                SW_LOG_ERROR_ID( "Vulkan.GPUCannotUseVulkan", "This GPU cannot use Vulkan format %# as a sampled color attachment (G-buffer normal)", static_cast<uint32>( format ) );
                return false;
            }
        }
        return true;
    }

    bool VulkanRHIDevice::supportsTextureFormat( RHIFormat format ) const
    {
        const VkFormat vkFormat = VulkanRHIUtil::toVulkanTextureFormat( format );
        if ( vkFormat == VK_FORMAT_UNDEFINED )
            return false;
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties( _physicalDevice, vkFormat, &props );
        return ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) != 0;
    }

    uint32 VulkanRHIDevice::depthAspectMask() const
    {
        return _bDepthHasStencil != 0
                 ? ( VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT )
                 : VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // 길이 유지(긴 함수 정리 기준): 기능 확인 · 큐 · 확장 · 생성의 순서가 곧 의미라 나누지 않는다.
    bool VulkanRHIDevice::createLogicalDevice()
    {
        // 비동기 컴퓨트는 그래픽스 패밀리의 두 번째 큐다(같은 패밀리라 소유권 이전이 없다). 큐가 하나뿐인 패밀리(일부 AMD · 모바일)는 능력이 거짓이다.
        uint32 familyCount{ 0 };
        vkGetPhysicalDeviceQueueFamilyProperties( _physicalDevice, &familyCount, nullptr );
        vector<VkQueueFamilyProperties> listFamily( familyCount );
        vkGetPhysicalDeviceQueueFamilyProperties( _physicalDevice, &familyCount, listFamily.data() );
        const bool              bFamilyKnown       = _graphicsQueueFamilyIndex < familyCount;
        const bool              bSecondQueue       = bFamilyKnown && listFamily[_graphicsQueueFamilyIndex].queueCount >= 2 && ( listFamily[_graphicsQueueFamilyIndex].queueFlags & VK_QUEUE_COMPUTE_BIT ) != 0;
        const float32           arrQueuePriority[] = { 1.0f, 1.0f };
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
        queueCreateInfo.queueCount       = bSecondQueue ? 2u : 1u;
        queueCreateInfo.pQueuePriorities = arrQueuePriority;

        VkPhysicalDeviceFeatures availableFeatures{};
        vkGetPhysicalDeviceFeatures( _physicalDevice, &availableFeatures );

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.multiDrawIndirect = availableFeatures.multiDrawIndirect;
        // 블록 압축 형식은 기능을 켜야 쓸 수 있다(Vulkan 명세). 장치가 지원하는 것을 켠다 — 실제 고르기는 `supportsTextureFormat` 의 형식 조회가 본다.
        deviceFeatures.textureCompressionBC       = availableFeatures.textureCompressionBC;
        deviceFeatures.textureCompressionETC2     = availableFeatures.textureCompressionETC2;
        deviceFeatures.textureCompressionASTC_LDR = availableFeatures.textureCompressionASTC_LDR;
        // 범위 밖 버퍼 읽기가 0 이 되도록 한다. DX11/GL 과 같은 결과를 내고, 잘못된 인스턴스 · 머티리얼 인덱스가 GPU 폴트 대신
        // 검은 픽셀로 드러난다(셰이더 쪽 클램프도 따로 있다: binding.hlsli InstanceData::load).
        deviceFeatures.robustBufferAccess = availableFeatures.robustBufferAccess;
        deviceFeatures.samplerAnisotropy  = availableFeatures.samplerAnisotropy; // 정적 샘플러 세트의 ANISO_WRAP (없으면 createDescriptorResources 가 1.0 으로 만든다)
        // 와이어프레임(VK_POLYGON_MODE_LINE). DX11/DX12/GL 은 별도 기능 플래그가 없어 그냥 되는데
        // Vulkan 만 디바이스 생성 때 켜야 한다. 안 켜면 파이프라인 생성이 검증 오류로 거절되고,
        // 그 PSO 가 0 으로 돌아와 조용히 솔리드로 그려진다.
        deviceFeatures.fillModeNonSolid = availableFeatures.fillModeNonSolid;
        // 깊이 클립을 끄는 렌더 상태(depthClampEnable)도 마찬가지다.
        deviceFeatures.depthClamp = availableFeatures.depthClamp;
        // 렌더 타깃마다 다른 블렌드 · 쓰기 마스크(RHIRenderState::_arrBlendTarget). 켜지 않으면 첨부 상태가 모두 같아야 한다.
        deviceFeatures.independentBlend = availableFeatures.independentBlend;
        // 프래그먼트 셰이더의 스토리지 버퍼 쓰기 · 원자 연산 — 셰이더 메시지 채널(`SW_GPU_PRINT`)이 픽셀 셰이더에서 쓴다. 없으면 능력이 거짓이고 메시지를 요구한 실행이 멈춘다.
        // https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html — 꺼져 있으면 프래그먼트 단계의 스토리지 자원은 읽기 전용이어야 한다.
        deviceFeatures.fragmentStoresAndAtomics = availableFeatures.fragmentStoresAndAtomics;
        // 파이프라인 XML 이 고르는 선택 스테이지다. 없는 장치에서 그런 패스의 PSO 는 오류로 만들어지지 않는다.
        deviceFeatures.geometryShader     = availableFeatures.geometryShader;
        deviceFeatures.tessellationShader = availableFeatures.tessellationShader;
        _bGeometryShader                  = availableFeatures.geometryShader;
        _bTessellationShader              = availableFeatures.tessellationShader;
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
        _bDepthClamp                                           = availableFeatures.depthClamp ? 1 : 0;
        _bFragmentStores                                       = availableFeatures.fragmentStoresAndAtomics ? 1 : 0;

        // pickPhysicalDevice 가 요구 판(1.2) 미만 디바이스를 건너뛰므로 1.2 기능 구조체는 늘 체인에 넣을 수 있다.
        VkPhysicalDeviceVulkan12Features available12{};
        available12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &available12;
        vkGetPhysicalDeviceFeatures2( _physicalDevice, &features2 );

        VkPhysicalDeviceVulkan12Features vulkan12Features{};
        vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        // 묶음 비트(descriptorIndexing)도 켠다 — 개별 비트를 대신하지 않는 "descriptor indexing 을 쓴다" 선언이다(VkPhysicalDeviceVulkan12Features 명세:
        // "does not imply the other minimum descriptor indexing features are also enabled"). dzn(Mesa 26 Vulkan-on-D3D12)은 이 비트나 그 확장을 보고서야
        // bindless 디스크립터 경로로 셰이더를 내린다 — 개별 비트만 켜면 크기 없는 텍스처 배열(g_SwBindless*)을 SRV 로 선언하지 못해 컴퓨트 PSO 컴파일에서 죽었다.
        vulkan12Features.descriptorIndexing                            = available12.descriptorIndexing;
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

        // 셰이더는 -fspv-target-env=vulkan1.2 로 쿠킹하고, 그 타깃에서 DXC 는 HLSL `discard` 를 OpKill 로 낸다 — 기능을 켤 것이 없다.
        // 셰이더가 미분 읽기를 다 마친 뒤 버리므로(binding.hlsli 8-1 절) 1.3 의 shaderDemoteToHelperInvocation 없이 그림이 같다.

        VkDeviceCreateInfo createInfo{};
        createInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pNext                = &vulkan12Features;
        createInfo.pQueueCreateInfos    = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pEnabledFeatures     = &deviceFeatures;
        // VK_EXT_memory_budget 은 선택이다. 있으면 켜서 드라이버의 사용량 · 예산을 읽고(queryGPUMemoryBudgetInternal), 없으면 그 값은 "모름" 이다.
        vector<const utf8*>           listDeviceExtension( s_listDeviceExtensions.begin(), s_listDeviceExtensions.end() );
        vector<VkExtensionProperties> listAvailableExt;
        {
            if ( enumerateVk( listAvailableExt, "vkEnumerateDeviceExtensionProperties", [this]( uint32* pCount, VkExtensionProperties* pData )
            { return vkEnumerateDeviceExtensionProperties( _physicalDevice, nullptr, pCount, pData ); } ) == false )
                return false;
            _bMemoryBudget = VulkanRHIUtil::hasExtension( listAvailableExt, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME ) ? SW_TRUE : SW_FALSE;
            if ( _bMemoryBudget == SW_TRUE )
                listDeviceExtension.push_back( VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
        }
        // VK_EXT_device_fault(진단 전용): 디바이스를 잃었을 때 결함 주소 · 공급사 코드를 읽는다(reportDeviceFault). 기능 비트까지 있어야 켠다.
        VkPhysicalDeviceFaultFeaturesEXT faultFeatures{};
        faultFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
        if ( build::kShipping == false && VulkanRHIUtil::hasExtension( listAvailableExt, VK_EXT_DEVICE_FAULT_EXTENSION_NAME ) )
        {
            VkPhysicalDeviceFeatures2 faultQuery{};
            faultQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            faultQuery.pNext = &faultFeatures;
            vkGetPhysicalDeviceFeatures2( _physicalDevice, &faultQuery );
            faultFeatures.pNext = nullptr;
            if ( faultFeatures.deviceFault == VK_TRUE )
            {
                faultFeatures.deviceFaultVendorBinary = VK_FALSE; // 공급사 바이너리 덤프는 읽지 않는다
                vulkan12Features.pNext                = &faultFeatures;
                listDeviceExtension.push_back( VK_EXT_DEVICE_FAULT_EXTENSION_NAME );
                _bDeviceFault = SW_TRUE;
            }
        }
        // VK_NV_device_diagnostic_checkpoints(진단 전용): GPU 브레드크럼 - 패스마다 `vkCmdSetCheckpointNV` 로 id 를 남기고 소실 뒤 큐에서 읽는다(`readLastBreadcrumbs`).
        // 그래픽스 큐 패밀리가 체크포인트를 지원하는 단계(checkpointExecutionStageMask)가 있어야 쓴다. 없으면 켜지 않는다 - 이 능력이 없는 드라이버는 CPU 가 마지막으로 기록한 패스만 보고한다.
        if ( GPUBreadcrumbs::isEnabled() && VulkanRHIUtil::hasExtension( listAvailableExt, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME ) )
        {
            uint32 familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties2( _physicalDevice, &familyCount, nullptr );
            vector<VkQueueFamilyCheckpointPropertiesNV> listCheckpointProperties( familyCount );
            vector<VkQueueFamilyProperties2>            listFamilyProperties( familyCount );
            for ( uint32 familyIndex = 0; familyIndex < familyCount; ++familyIndex )
            {
                listCheckpointProperties[familyIndex].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_CHECKPOINT_PROPERTIES_NV;
                listFamilyProperties[familyIndex].sType     = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
                listFamilyProperties[familyIndex].pNext     = &listCheckpointProperties[familyIndex];
            }
            vkGetPhysicalDeviceQueueFamilyProperties2( _physicalDevice, &familyCount, listFamilyProperties.data() );
            const bool bFamilySupports = _graphicsQueueFamilyIndex < familyCount && listCheckpointProperties[_graphicsQueueFamilyIndex].checkpointExecutionStageMask != 0;
            if ( bFamilySupports )
            {
                listDeviceExtension.push_back( VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME );
                _breadcrumbMode = VulkanBreadcrumbMode::NVCheckpoints;
            }
        }
        // VK_AMD_buffer_marker(진단 전용): NV 체크포인트가 없는 드라이버(AMD · 인텔 Mesa)의 GPU 브레드크럼 — 패스 id 를 호스트 보임 버퍼에 쓰고 소실 뒤 매핑으로 읽는다.
        // https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdWriteBufferMarkerAMD.html (dstOffset 은 4 의 배수, 버퍼는 TRANSFER_DST 용도)
        if ( GPUBreadcrumbs::isEnabled() && _breadcrumbMode == VulkanBreadcrumbMode::None && VulkanRHIUtil::hasExtension( listAvailableExt, VK_AMD_BUFFER_MARKER_EXTENSION_NAME ) )
        {
            listDeviceExtension.push_back( VK_AMD_BUFFER_MARKER_EXTENSION_NAME );
            _breadcrumbMode = VulkanBreadcrumbMode::AMDBufferMarker;
        }
        createInfo.enabledExtensionCount   = static_cast<uint32>( listDeviceExtension.size() );
        createInfo.ppEnabledExtensionNames = listDeviceExtension.data();

        createInfo.enabledLayerCount = 0;

        if ( vkCreateDevice( _physicalDevice, &createInfo, nullptr, &_device ) != VK_SUCCESS )
            return false;

        vkGetDeviceQueue( _device, _graphicsQueueFamilyIndex, 0, &_graphicsQueue );
        if ( _bDrawIndirectCount != 0 )
        {
            _pCmdDrawIndirectCount        = reinterpret_cast<void*>( vkGetDeviceProcAddr( _device, "vkCmdDrawIndirectCount" ) );
            _pCmdDrawIndexedIndirectCount = reinterpret_cast<void*>( vkGetDeviceProcAddr( _device, "vkCmdDrawIndexedIndirectCount" ) );
            if ( _pCmdDrawIndirectCount == nullptr || _pCmdDrawIndexedIndirectCount == nullptr )
            {
                SW_LOG_WARNING_ID( "Vulkan.DrawIndirectCountUnavailable", "drawIndirectCount is reported but the device gives no vkCmdDraw*IndirectCount - GPU draw counts are off" );
                _bDrawIndirectCount = 0;
            }
        }
        _asyncComputeQueue = VK_NULL_HANDLE;
        if ( bSecondQueue )
            vkGetDeviceQueue( _device, _graphicsQueueFamilyIndex, 1, &_asyncComputeQueue );
        else
            SW_LOG_INFO_ID( "Vulkan.AsyncComputeQueueUnavailable", "The graphics queue family has one queue - AsyncCompute passes run on the graphics queue" );
        _listAsyncSemaphorePool.assign( constant::render::kMaxFrameCountInFlight, vector<VkSemaphore>{} );
        initBreadcrumbs();
        return true;
    }

    void VulkanRHIDevice::initBreadcrumbs()
    {
        switch ( _breadcrumbMode )
        {
            case VulkanBreadcrumbMode::None:
            {
                return;
            }
            case VulkanBreadcrumbMode::NVCheckpoints:
            {
                _pCmdSetCheckpoint  = reinterpret_cast<void*>( vkGetDeviceProcAddr( _device, "vkCmdSetCheckpointNV" ) );
                _pGetCheckpointData = reinterpret_cast<void*>( vkGetDeviceProcAddr( _device, "vkGetQueueCheckpointDataNV" ) );
                if ( _pCmdSetCheckpoint == nullptr || _pGetCheckpointData == nullptr )
                {
                    SW_LOG_WARNING_ID( "Vulkan.VKNVDeviceDiagnostic", "VK_NV_device_diagnostic_checkpoints is enabled but its functions did not load - GPU breadcrumbs are off" );
                    _breadcrumbMode = VulkanBreadcrumbMode::None;
                    return;
                }
                SW_LOG_INFO_ID( "Vulkan.VulkanGPUBreadcrumbsEnabled", "Vulkan GPU breadcrumbs enabled (VK_NV_device_diagnostic_checkpoints)" );
                return;
            }
            case VulkanBreadcrumbMode::AMDBufferMarker:
            {
                _pCmdWriteBufferMarker = reinterpret_cast<void*>( vkGetDeviceProcAddr( _device, "vkCmdWriteBufferMarkerAMD" ) );
                if ( _pCmdWriteBufferMarker == nullptr || createBreadcrumbBuffer() == false )
                {
                    SW_LOG_WARNING_ID( "Vulkan.AMDBufferMarkerUnavailable", "VK_AMD_buffer_marker is enabled but its function or marker buffer is missing - GPU breadcrumbs are off" );
                    destroyBreadcrumbBuffer();
                    _breadcrumbMode = VulkanBreadcrumbMode::None;
                    return;
                }
                SW_LOG_INFO_ID( "Vulkan.VulkanGPUBreadcrumbsAMD", "Vulkan GPU breadcrumbs enabled (VK_AMD_buffer_marker)" );
                return;
            }
        }
    }

    bool VulkanRHIDevice::createBreadcrumbBuffer()
    {
        constexpr VkDeviceSize kSize = kBreadcrumbSlotCount * sizeof( uint32 );
        VkBufferCreateInfo     bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = kSize;
        bufferInfo.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT; // vkCmdWriteBufferMarkerAMD 의 도착지 조건
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &_breadcrumbBuffer ) != VK_SUCCESS )
            return false;
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements( _device, _breadcrumbBuffer, &requirements );
        uint32 memoryTypeIndex{ 0 };
        // 소실 뒤에도 CPU 가 매핑으로 읽어야 하므로 호스트 일관 메모리다(flush · invalidate 없이 GPU 의 마지막 쓰기가 보인다).
        if ( findMemoryType( requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
            return false;
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = requirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;
        if ( vkAllocateMemory( _device, &allocInfo, nullptr, &_breadcrumbMemory ) != VK_SUCCESS )
            return false;
        if ( isVkSuccess( vkBindBufferMemory( _device, _breadcrumbBuffer, _breadcrumbMemory, 0 ), "vkBindBufferMemory (breadcrumbs)" ) == false )
            return false;
        void*      pMapped{ nullptr };
        const bool bMapped = isVkSuccess( vkMapMemory( _device, _breadcrumbMemory, 0, kSize, 0, &pMapped ), "vkMapMemory (breadcrumbs)" ) && pMapped != nullptr;
        if ( bMapped == false )
            return false;
        // 0 은 표지가 아니다 — 아직 쓰지 않은 칸을 소실 보고에 싣지 않는다.
        uint32* const pSlot = static_cast<uint32*>( pMapped );
        for ( uint32 slot = 0; slot < kBreadcrumbSlotCount; ++slot )
        {
            pSlot[slot] = 0;
        }
        _pBreadcrumbMapped = pSlot;
        return true;
    }

    void VulkanRHIDevice::destroyBreadcrumbBuffer()
    {
        if ( _pBreadcrumbMapped != nullptr )
            vkUnmapMemory( _device, _breadcrumbMemory );
        _pBreadcrumbMapped = nullptr;
        if ( _breadcrumbBuffer != VK_NULL_HANDLE )
            vkDestroyBuffer( _device, _breadcrumbBuffer, nullptr );
        if ( _breadcrumbMemory != VK_NULL_HANDLE )
            vkFreeMemory( _device, _breadcrumbMemory, nullptr );
        _breadcrumbBuffer = VK_NULL_HANDLE;
        _breadcrumbMemory = VK_NULL_HANDLE;
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
        _listCommandBuffer.resize( constant::render::kMaxFrameCountInFlight );
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
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        // 펜스는 인플라이트 슬롯마다 하나다. 스왑체인 이미지 개수와 무관하다.
        // 이미지별 세마포어는 스왑체인이 만든다(VulkanRHISwapChain::createSemaphores).
        _listInFlightFence.resize( constant::render::kMaxFrameCountInFlight );

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
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        if ( _device == nullptr )
            return;

        for ( VkFence fence : _listInFlightFence )
        {
            if ( fence != VK_NULL_HANDLE )
                vkDestroyFence( _device, fence, nullptr );
        }
        _listInFlightFence.clear();

        // 스왑체인의 이미지별 펜스 표는 위 펜스의 사본이므로 비우기만 한다.
        _swapChain.removeImageFences();
        for ( const unique_ptr<VulkanRHISwapChain>& pWindow : _windowSwapChains.getOwnedList() )
        {
            pWindow->removeImageFences();
        }
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

    IRHISwapChain* VulkanRHIDevice::createSwapChainInternal( const RHISwapChainDesc& desc )
    {
        // 서피스 확장은 인스턴스를 만들 때 메인 창의 창 시스템으로 골랐다 — 다른 창 시스템의 핸들은 그 확장으로 읽지 못한다.
        const bool bSameWindowSystem = ( desc._surfaceKind == NativeSurfaceKind::Win32 ) == ( _wsiKind == VulkanWsiKind::Win32 ) &&
                                       ( desc._surfaceKind == NativeSurfaceKind::Wayland ) == ( _wsiKind == VulkanWsiKind::Wayland );
        if ( bSameWindowSystem == false || _device == nullptr || isDeviceLost() )
            return nullptr;

        unique_ptr<VulkanRHISwapChain> pSwapChain = make_unique<VulkanRHISwapChain>();
        pSwapChain->setRequested( _swapChain.getActualBackBufferFormat(), 0, isVSyncEnabled() );
        if ( pSwapChain->createSurface( _instance, desc._pWindowHandle, desc._pWindowDisplay, _wsiKind ) == false )
            return nullptr;
        VkBool32   bPresentSupported = VK_FALSE;
        const bool bQueried          = isVkSuccess( vkGetPhysicalDeviceSurfaceSupportKHR( _physicalDevice, _graphicsQueueFamilyIndex, pSwapChain->getSurface(), &bPresentSupported ),
                                                    "vkGetPhysicalDeviceSurfaceSupportKHR (window)" );
        // 메인 창의 렌더 패스 · 프레임버퍼 규칙을 그대로 쓰므로 이미지 포맷이 같아야 한다 — 다르면 대신할 포맷을 고르지 않고 거절한다.
        const bool bCreated = bQueried && bPresentSupported != VK_FALSE && rebuildWindowSwapChain( *pSwapChain, desc._width, desc._height ) &&
                              pSwapChain->getImageFormat() == _swapChain.getImageFormat();
        if ( bCreated == false )
        {
            pSwapChain->destroy( _device );
            pSwapChain->destroySemaphores( _device );
            pSwapChain->destroySurface( _instance );
            return nullptr;
        }
        return _windowSwapChains.add( std::move( pSwapChain ) );
    }

    void VulkanRHIDevice::resizeSwapChainInternal( IRHISwapChain& swapChain, uint32 width, uint32 height )
    {
        (void)rebuildWindowSwapChain( static_cast<VulkanRHISwapChain&>( swapChain ), width, height ); // 실패 알림은 안에서 남긴다. 다음 프레임의 획득이 다시 시도한다
    }

    void VulkanRHIDevice::destroySwapChainInternal( IRHISwapChain& swapChain )
    {
        VulkanRHISwapChain& window = static_cast<VulkanRHISwapChain&>( swapChain ); // 이 디바이스가 만든 것만 들어온다(`createSwapChain`)
        window.destroy( _device );
        window.destroySemaphores( _device );
        window.destroySurface( _instance );
        _windowSwapChains.remove( window );
    }

    void VulkanRHIDevice::rebuildPendingWindowSwapChains()
    {
        if ( _windowSwapChains.getRebuildList().empty() )
            return;
        {
            ScopedLock<mutex> queueLock{ _queueMutex };
            reportQueueResult( static_cast<int32>( vkDeviceWaitIdle( _device ) ), "rebuildPendingWindowSwapChains: vkDeviceWaitIdle" );
        }
        if ( isDeviceLost() == false )
        {
            _frameFenceRing.markAllCompleted(); // GPU 가 쉰다
            for ( VulkanRHISwapChain* pWindow : _windowSwapChains.getRebuildList() )
            {
                (void)rebuildWindowSwapChain( *pWindow, pWindow->getExtentWidth(), pWindow->getExtentHeight() ); // 실패 알림은 안에서 남긴다. 다음 획득이 다시 본다
            }
        }
        _windowSwapChains.clearRebuild();
    }

    bool VulkanRHIDevice::rebuildWindowSwapChain( VulkanRHISwapChain& swapChain, uint32 width, uint32 height )
    {
        swapChain.destroySemaphores( _device );
        swapChain.destroy( _device );
        if ( swapChain.create( _physicalDevice, _device, width, height ) == false )
            return false; // 서피스 크기 0(최소화)은 오류가 아니다 — 다음 획득이 다시 시도한다
        const bool bBuilt = swapChain.createFramebuffers( _device, _renderPass ) && swapChain.createSemaphores( _device, constant::render::kMaxFrameCountInFlight );
        if ( bBuilt == false )
            SW_LOG_ERROR_ONCE_ID( "Vulkan.WindowSwapChainBuildFailed", "Failed to build the framebuffers or semaphores of a window swap chain (%#x%#)", width, height );
        return bBuilt;
    }

    VulkanRHISwapChain* VulkanRHIDevice::useSwapChain( IRHISwapChain* pSwapChain )
    {
        _renderThread.assumeHeldByJoinedTask(); // 렌더 패스를 여는 기록(렌더 스레드와 그 병렬 기록 태스크)이 부른다 — 다른 창은 프레임 스트림만 고른다
        if ( pSwapChain == nullptr || pSwapChain == &_swapChain )
            return &_swapChain;
        VulkanRHISwapChain& window = static_cast<VulkanRHISwapChain&>( *pSwapChain ); // 이 디바이스가 만든 것만 들어온다(`createSwapChain`)
        if ( _windowSwapChains.isUsedThisFrame( window ) )
            return &window;
        if ( _bFrameStarted == SW_FALSE || isDeviceLost() )
            return nullptr;

        VulkanSwapChainStatus status = window.isValid() ? window.acquireNextImage( _device, getFrameSlot() ) : VulkanSwapChainStatus::OutOfDate;
        if ( status == VulkanSwapChainStatus::OutOfDate || status == VulkanSwapChainStatus::SurfaceLost )
        {
            // 창 크기가 바뀌었거나 표면이 낡았다 — 프레임 중간에 GPU 를 비우지 않고 이번 프레임은 이 창을 건너뛴다.
            // 다음 beginFrame 이 메인 창 재생성과 같은 자리에서 다시 만든다(언리얼 FVulkanViewport 도 재생성을 프레임 경계의 플러시에서 한다 — 기억에 의존한 비교).
            _windowSwapChains.markRebuild( window );
            return nullptr;
        }
        if ( status == VulkanSwapChainStatus::DeviceLost )
            reportQueueResult( static_cast<int32>( VK_ERROR_DEVICE_LOST ), "useSwapChain: vkAcquireNextImageKHR" );
        if ( status != VulkanSwapChainStatus::Success && status != VulkanSwapChainStatus::Suboptimal )
            return nullptr;

        const VkSemaphore acquireWait = window.takeAcquireWait();
        if ( acquireWait != VK_NULL_HANDLE )
            _listPendingAcquireWait.push_back( acquireWait );
        reportQueueResult( window.waitForImageReuse( _device, _listInFlightFence[getFrameSlot()] ), "useSwapChain: vkWaitForFences (image in flight)" );
        (void)_windowSwapChains.markUsedThisFrame( window );
        return &window;
    }

    void VulkanRHIDevice::applyVSyncInternal()
    {
        _swapChain.setRequestedVSync( isVSyncEnabled() );
        if ( _width != 0 && _height != 0 )
            _bSwapChainDirty = 1;
    }

    void VulkanRHIDevice::releaseSurfaceInternal()
    {
        if ( _device == nullptr )
            return;
        {
            ScopedLock<mutex> queueLock{ _queueMutex };
            reportQueueResult( static_cast<int32>( vkDeviceWaitIdle( _device ) ), "releaseSurface: vkDeviceWaitIdle" );
        }
        if ( isDeviceLost() == false )
            _frameFenceRing.markAllCompleted();
        // 네이티브 창(ANativeWindow)은 이 함수가 돌아온 뒤 사라진다 — 스왑체인 · 표면을 지금 놓는다. 장치 · 자원은 그대로다(돌아오면 표면만 다시 짓는다).
        destroyFrameFences();
        _swapChain.destroySemaphores( _device );
        _swapChain.destroy( _device );
        _swapChain.destroySurface( _instance );
    }

    bool VulkanRHIDevice::restoreSurfaceInternal()
    {
        if ( _device == nullptr || _pSurface == nullptr )
            return false;
        _pHWnd          = _pSurface->getSurfaceHandle(); // 새 ANativeWindow 다(같은 자리의 다른 창)
        _pDisplayHandle = _pSurface->getSurfaceDisplay();
        _width          = _pSurface->getSurfaceWidth();
        _height         = _pSurface->getSurfaceHeight();
        if ( _swapChain.createSurface( _instance, _pHWnd, _pDisplayHandle, _wsiKind ) == false )
            return false;
        // 큐 패밀리는 그대로 쓴다 — 같은 장치의 새 창이라 표시 지원이 같다(Android 는 그래픽 큐가 늘 표시한다, 기억). 스왑체인은 다음 프레임이 다시 짓는다.
        _bSwapChainDirty = 1;
        return true;
    }

    bool VulkanRHIDevice::recreateSwapChain()
    {
        const bool bCannotRecreateSwapChain = _device == nullptr || _width == 0 || _height == 0 || isDeviceLost();
        if ( bCannotRecreateSwapChain )
            return false;
        {
            ScopedLock<mutex> queueLock{ _queueMutex };
            reportQueueResult( static_cast<int32>( vkDeviceWaitIdle( _device ) ), "recreateSwapChain: vkDeviceWaitIdle" );
        }
        // 대기에서 잃었음을 봤으면 다시 만들지 않는다 — 잃은 장치의 스왑체인 생성은 같은 손실만 다시 듣는다.
        if ( isDeviceLost() )
            return false;
        _frameFenceRing.markAllCompleted(); // GPU 가 쉰다 — 새 펜스는 신호된 채 만든다

        // 세마포어와 이미지별 펜스 표는 스왑체인 이미지 개수로 크기가 정해지므로 함께 다시 만든다.
        destroyFrameFences();
        _swapChain.destroySemaphores( _device );
        _swapChain.destroy( _device );

        // 실패하면 부르는 쪽이 다음 프레임에 다시 시도한다(최소화 · 복원 중에는 서피스 크기가 잠시 0 이라 실패한다). 알림은 연달아
        // 실패하는 동안 한 번이다.
        const utf8* pFailedStep{ nullptr };
        if ( _swapChain.create( _physicalDevice, _device, _width, _height ) == false )
        {
            // 서피스 크기 0 은 최소화 중이라는 정상 상태다. 오류 없이 다음 프레임에 다시 본다.
            if ( _swapChain.isSurfaceEmpty() )
                return false;
            pFailedStep = "swapchain";
        }
        else if ( _swapChain.createFramebuffers( _device, _renderPass ) == false )
            pFailedStep = "swapchain framebuffers";
        else if ( _swapChain.createSemaphores( _device, constant::render::kMaxFrameCountInFlight ) == false )
            pFailedStep = "swapchain semaphores";
        else if ( createFrameFences() == false )
            pFailedStep = "frame fences";
        if ( pFailedStep != nullptr )
        {
            if ( _bSwapChainRecreateFailing == SW_FALSE )
                SW_LOG_ERROR_ID( "Vulkan.FailedRecreateXRetrying", "Failed to recreate the %# (%#x%#) — retrying every frame", pFailedStep, _width, _height );
            _bSwapChainRecreateFailing = SW_TRUE;
            return false;
        }

        _bSwapChainRecreateFailing = SW_FALSE;
        return true;
    }

    void VulkanRHIDevice::recordSurfaceLost( const utf8* pWhere )
    {
        ++_surfaceLostCount;
        SW_LOG_WARNING_ID( "Vulkan.VulkanSurfaceLostTime", "Vulkan surface lost at %# (%# time(s) on this device, frame %#) - recreating the surface and swapchain", pWhere, _surfaceLostCount,
                           _frameFenceRing.getLastSubmittedSerial() );
    }

    bool VulkanRHIDevice::recreateSurfaceAndSwapChain()
    {
        const bool bCannotRecreateSurface = _device == nullptr || _instance == nullptr || isDeviceLost();
        if ( bCannotRecreateSurface )
            return false;
        {
            ScopedLock<mutex> queueLock{ _queueMutex };
            reportQueueResult( static_cast<int32>( vkDeviceWaitIdle( _device ) ), "recreateSurfaceAndSwapChain: vkDeviceWaitIdle" );
        }
        if ( isDeviceLost() )
            return false;
        _frameFenceRing.markAllCompleted(); // GPU 가 쉰다 — 새 펜스는 신호된 채 만든다
        // 스왑체인이 서피스 위에 있으므로 스왑체인 → 서피스 순으로 버리고 서피스 → 스왑체인 순으로 만든다.
        destroyFrameFences();
        _swapChain.destroySemaphores( _device );
        _swapChain.destroy( _device );
        _swapChain.destroySurface( _instance );
        if ( _swapChain.createSurface( _instance, _pHWnd, _pDisplayHandle, _wsiKind ) == false )
            return false;
        // 새 서피스를 이 큐가 프레젠트할 수 있는지 — 고를 때(pickPhysicalDevice) 본 것은 옛 서피스다.
        VkBool32   bPresentSupported = VK_FALSE;
        const bool bQueried          = isVkSuccess( vkGetPhysicalDeviceSurfaceSupportKHR( _physicalDevice, _graphicsQueueFamilyIndex, _swapChain.getSurface(), &bPresentSupported ),
                                                    "vkGetPhysicalDeviceSurfaceSupportKHR (recreated surface)" );
        if ( bQueried == false || bPresentSupported == VK_FALSE )
        {
            SW_LOG_ERROR_ID( "Vulkan.RecreatedVulkanSurfaceCannot", "The recreated Vulkan surface cannot be presented by queue family %#", _graphicsQueueFamilyIndex );
            return false;
        }
        return recreateSwapChain();
    }

    bool VulkanRHIDevice::initializePipelineCache()
    {
        if ( _device == VK_NULL_HANDLE )
            return false;

        vector<uint8> listCacheData;
        const string  cachePath = getVulkanPipelineCachePath();
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
            SW_LOG_WARNING_ID( "Vulkan.FailedCreatePipelineCache", "Failed to create pipeline cache with saved data; falling back to empty cache." );
            createInfo.initialDataSize = 0;
            createInfo.pInitialData    = nullptr;
            if ( isVkSuccess( vkCreatePipelineCache( _device, &createInfo, nullptr, &_pipelineCache ), "vkCreatePipelineCache (empty)" ) == false )
                _pipelineCache = VK_NULL_HANDLE;
        }
        else if ( listCacheData.empty() == false )
        {
            SW_LOG_INFO_ID( "Vulkan.LoadedPipelineCacheBytes", "Loaded pipeline cache (%# bytes).", static_cast<uint32>( listCacheData.size() ) );
        }
        return _pipelineCache != VK_NULL_HANDLE;
    }

    void VulkanRHIDevice::savePipelineCache()
    {
        if ( _device == VK_NULL_HANDLE || _pipelineCache == VK_NULL_HANDLE )
            return;

        size_t dataSize{ 0 };
        // 캐시를 읽지 못하면 오류로 남기고 저장만 건너뛴다(다음 실행이 파이프라인을 새로 만들 뿐이다).
        if ( isVkSuccess( vkGetPipelineCacheData( _device, _pipelineCache, &dataSize, nullptr ), "vkGetPipelineCacheData (size)" ) && dataSize > 0 )
        {
            vector<uint8> listCacheData( dataSize );
            if ( isVkSuccess( vkGetPipelineCacheData( _device, _pipelineCache, &dataSize, listCacheData.data() ), "vkGetPipelineCacheData (data)" ) )
            {
                const string cachePath = getVulkanPipelineCachePath();
                // 새 파이프라인이 없던 실행은 같은 바이트다 — 다시 쓰지 않는다. 드라이버가 같은 내용에 다른 바이트를 내면 지금처럼 쓴다.
                vector<uint8> listSavedData;
                const bool    bUnchanged = FileUtil::exists( cachePath ) && FileUtil::readFile( cachePath, listSavedData ) && listSavedData.size() == dataSize &&
                                        Memory::compare( listSavedData.data(), listCacheData.data(), dataSize ) == 0;
                if ( bUnchanged )
                    SW_LOG_INFO_ID( "Vulkan.PipelineCacheUnchanged", "Pipeline cache unchanged (%# bytes) - not saved.", static_cast<uint32>( dataSize ) );
                else if ( FileUtil::ensureParentDirectoryExists( cachePath ) &&
                          FileUtil::writeFile( cachePath, listCacheData.data(), static_cast<uint64>( listCacheData.size() ) ) )
                    SW_LOG_INFO_ID( "Vulkan.SavedPipelineCacheBytes", "Saved pipeline cache (%# bytes).", static_cast<uint32>( dataSize ) );
                else
                    SW_LOG_WARNING_ID( "Vulkan.CouldNotSavePipeline", "Could not save the pipeline cache - the next run builds pipelines from scratch" );
            }
        }

        vkDestroyPipelineCache( _device, _pipelineCache, nullptr );
        _pipelineCache = VK_NULL_HANDLE;
    }

    void VulkanRHIDevice::reportQueueResult( int32 result, const utf8* pWhere )
    {
        const VkResult vkResult = static_cast<VkResult>( result );
        if ( vkResult == VK_SUCCESS )
            return;
        // 장치 손실은 처음 본 쪽만 알린다. 그 뒤의 호출은 모두 같은 값을 돌려주므로 프레임마다 로그가 쌓인다.
        const bool bDeviceLost = vkResult == VK_ERROR_DEVICE_LOST;
        if ( bDeviceLost && _transferSerials.markLost() == false )
            return;

        fixed_string<constant::buffer::kMax64> lastMarker;
        {
            ScopedLock<mutex> lock{ _submittedMarkerMutex };
            lastMarker = _lastSubmittedEventMarker;
        }
        SW_LOG_ERROR_ID( "Vulkan.VkResultLastPassPut", "%#: VkResult=%# (%#) — last pass put in the submission order: '%#'", pWhere != nullptr ? pWhere : "<queue>", result,
                         VulkanRHIDeviceInternal::getResultName( vkResult ), lastMarker.empty() ? "<none>" : lastMarker.c_str() );
        if ( bDeviceLost == false )
            return;
        SW_LOG_ERROR_ID( "Vulkan.VulkanDeviceLostSubmits", "Vulkan device lost - submits, waits and presents stop; frames are skipped until shutdown" );
        // 마지막으로 GPU 가 닿은 패스 - 체크포인트를 이름으로 풀어 로그 · 크래시 문맥 · 빵부스러기에 싣는다(없으면 CPU 가 마지막으로 기록한 패스만).
        uint32       arrBreadcrumb[GPUBreadcrumbs::kMaxReported]{};
        const uint32 breadcrumbCount = readLastBreadcrumbs( arrBreadcrumb, GPUBreadcrumbs::kMaxReported );
        GPUBreadcrumbs::reportDeviceLost( "Vulkan", arrBreadcrumb, breadcrumbCount );
        reportDeviceFault();
    }

    bool VulkanRHIDevice::debugRemoveDevice()
    {
        if ( _device == VK_NULL_HANDLE )
            return false;
        SW_LOG_WARNING_ID( "Vulkan.MarkingVulkanDeviceLost", "Marking the Vulkan device lost on request (rhi.removeDevice) - Vulkan cannot remove a device, so this is the state after the first "
                                                             "VK_ERROR_DEVICE_LOST: no more submits, waits or presents" );
        _transferSerials.markLost(); // 이미 잃은 장치여도 같은 상태다
        return true;
    }

    uint32 VulkanRHIDevice::readLastBreadcrumbs( uint32* pOutID, uint32 capacity )
    {
        if ( pOutID == nullptr || capacity == 0 )
            return 0;
        if ( _breadcrumbMode == VulkanBreadcrumbMode::AMDBufferMarker )
        {
            // 큐마다 [시작 · 끝] — 그래픽스 다음 컴퓨트. 0 인 칸(아직 쓰지 않음)은 싣지 않는다(DX12 와 같은 모양, 푸는 일은 GPUBreadcrumbs::parse).
            uint32 filledMarker = 0;
            for ( uint32 slot = 0; slot < kBreadcrumbSlotCount && filledMarker < capacity && _pBreadcrumbMapped != nullptr; ++slot )
            {
                const uint32 marker = _pBreadcrumbMapped[slot];
                if ( marker != 0 )
                    pOutID[filledMarker++] = marker;
            }
            return filledMarker;
        }
        if ( _breadcrumbMode != VulkanBreadcrumbMode::NVCheckpoints || _graphicsQueue == VK_NULL_HANDLE )
            return 0;
        uint32                               dataCount            = 0;
        const PFN_vkGetQueueCheckpointDataNV pfnGetCheckpointData = reinterpret_cast<PFN_vkGetQueueCheckpointDataNV>( _pGetCheckpointData );
        pfnGetCheckpointData( _graphicsQueue, &dataCount, nullptr );
        if ( dataCount == 0 )
            return 0;
        vector<VkCheckpointDataNV> listData( dataCount );
        for ( VkCheckpointDataNV& data : listData )
        {
            data.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV;
        }
        pfnGetCheckpointData( _graphicsQueue, &dataCount, listData.data() );
        // 파이프라인 단계마다 그 단계가 마지막으로 지난 체크포인트가 온다 - 같은 id 는 한 번만 싣는다.
        uint32 filled = 0;
        for ( uint32 index = 0; index < dataCount && filled < capacity; ++index )
        {
            const uint32 markerID = static_cast<uint32>( reinterpret_cast<uintptr_t>( listData[index].pCheckpointMarker ) );
            bool         bKnown   = markerID == 0;
            for ( uint32 knownIndex = 0; knownIndex < filled; ++knownIndex )
            {
                if ( pOutID[knownIndex] == markerID )
                    bKnown = true;
            }
            if ( bKnown == false )
                pOutID[filled++] = markerID;
        }
        return filled;
    }

    void VulkanRHIDevice::recordSubmittedEventMarker( const utf8* pMarker )
    {
        if ( pMarker == nullptr || pMarker[0] == '\0' )
            return;
        ScopedLock<mutex> lock{ _submittedMarkerMutex };
        _lastSubmittedEventMarker = pMarker;
    }

    void VulkanRHIDevice::reportDeviceFault()
    {
        PFN_vkGetDeviceFaultInfoEXT pfnGetFaultInfo =
            ( _bDeviceFault != SW_FALSE && _device != VK_NULL_HANDLE )
                ? reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>( vkGetDeviceProcAddr( _device, "vkGetDeviceFaultInfoEXT" ) )
                : nullptr;
        if ( pfnGetFaultInfo == nullptr )
        {
            SW_LOG_ERROR_ID( "Vulkan.DeviceLostVKEXT", "Device lost: VK_EXT_device_fault is not enabled on this device — no fault address or vendor code to report" );
            return;
        }

        VkDeviceFaultCountsEXT counts{};
        counts.sType               = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
        const VkResult countResult = pfnGetFaultInfo( _device, &counts, nullptr );
        if ( countResult != VK_SUCCESS && countResult != VK_INCOMPLETE )
        {
            SW_LOG_ERROR_ID( "Vulkan.DeviceLostVkGetDeviceFaultInfoEXTFailed", "Device lost: vkGetDeviceFaultInfoEXT failed to count the fault records (VkResult=%#)", static_cast<int32>( countResult ) );
            return;
        }

        vector<VkDeviceFaultAddressInfoEXT> listAddressInfo( counts.addressInfoCount );
        vector<VkDeviceFaultVendorInfoEXT>  listVendorInfo( counts.vendorInfoCount );
        VkDeviceFaultInfoEXT                info{};
        info.sType                = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
        info.pAddressInfos        = listAddressInfo.empty() ? nullptr : listAddressInfo.data();
        info.pVendorInfos         = listVendorInfo.empty() ? nullptr : listVendorInfo.data();
        info.pVendorBinaryData    = nullptr;
        counts.vendorBinarySize   = 0; // 공급사 바이너리 덤프는 읽지 않는다(도구 없이는 읽을 수 없다)
        const VkResult infoResult = pfnGetFaultInfo( _device, &counts, &info );
        if ( infoResult != VK_SUCCESS && infoResult != VK_INCOMPLETE )
        {
            SW_LOG_ERROR_ID( "Vulkan.DeviceLostVkGetDeviceFaultInfoEXTFailed2", "Device lost: vkGetDeviceFaultInfoEXT failed (VkResult=%#)", static_cast<int32>( infoResult ) );
            return;
        }

        SW_LOG_ERROR_ID( "Vulkan.DeviceLostVKEXT2", "Device lost (VK_EXT_device_fault): '%#' — %# address record(s), %# vendor record(s)", info.description, counts.addressInfoCount,
                         counts.vendorInfoCount );
        for ( uint32 index = 0; index < counts.addressInfoCount && index < listAddressInfo.size(); ++index )
        {
            const VkDeviceFaultAddressInfoEXT& address = listAddressInfo[index];
            SW_LOG_ERROR_ID( "Vulkan.FaultAddressXPrecision", "  fault address %#: %# at 0x%# (precision %# bytes)", index,
                             VulkanRHIDeviceInternal::getFaultAddressTypeName( address.addressType ),
                             Fmt( static_cast<uint64>( address.reportedAddress ), Format( 16, Format::Padding::Zero ).hexUpper() ),
                             static_cast<uint64>( address.addressPrecision ) );
        }
        for ( uint32 index = 0; index < counts.vendorInfoCount && index < listVendorInfo.size(); ++index )
        {
            const VkDeviceFaultVendorInfoEXT& vendor = listVendorInfo[index];
            SW_LOG_ERROR_ID( "Vulkan.VendorRecordCodeData", "  vendor record %#: '%#' code %# data %#", index, vendor.description, static_cast<uint64>( vendor.vendorFaultCode ),
                             static_cast<uint64>( vendor.vendorFaultData ) );
        }
    }

    void VulkanRHIDevice::waitIdleInternal()
    {
        // 다른 스레드가 렌더 스레드를 `waitIdle` 로 세운 채 부른다(`Engine::onMainWindowResize`) — 그동안 프레임 진행 상태는 이 스레드가 빌려 만진다.
        ScopedThreadRoleBorrow borrow{ _renderThread };
        // 잃은 장치는 기다리지 않는다 — 끝나지 않을 일을 기다리게 되거나(TDR 중의 드라이버) 같은 손실만 다시 듣는다.
        if ( _device && isDeviceLost() == false )
        {
            {
                ScopedLock<mutex> queueLock{ _queueMutex };
                reportQueueResult( static_cast<int32>( vkDeviceWaitIdle( _device ) ), "waitIdle: vkDeviceWaitIdle" );
            }
            // 큐가 비었으니 낸 전송 · 프레임은 모두 끝났다. 잃었으면 워터마크를 올리지 않는다 — 끝나지 않은 티켓이 Complete 로 보이면 안 된다.
            if ( isDeviceLost() == false )
            {
                _frameFenceRing.markAllCompleted();
                _transferFenceQueue.retireAll( [this]( VulkanTransferJob& job )
                { recycleTransferJob( job ); } );
            }
        }
        // 전송을 기다리던 해제가 먼저 프레임 해제 큐로 넘어가야 아래에서 함께 놓인다.
        _transferReleaseQueue.flushAll();
        _releaseQueue.flushAll();
    }

    void VulkanRHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _frameFenceRing.getRecordingSerial() );
    }

    uint32 VulkanRHIDevice::getTimestampSlotCount() const
    {
        _renderThread.assumeHeldByJoinedTask(); // 패스 실행(병렬 기록 태스크 포함)이 부른다 — 렌더 스레드가 합류를 기다린다
        return ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampPool != VK_NULL_HANDLE && _timestampPeriod > 0.0f )
                 ? constant::render::kMaxGPUTimestampSlot
                 : 0u;
    }

    bool VulkanRHIDevice::readTimestamps( RHITimestampFrame& outFrame )
    {
        outFrame = _timestampFrame;
        return outFrame._listMicro.empty() == false;
    }

    bool VulkanRHIDevice::readGPUClockNanos( int64& outGPUNanos )
    {
        outGPUNanos                            = 0;
        const bool bTimestampDeviceUnavailable = _device == VK_NULL_HANDLE || _physicalDevice == VK_NULL_HANDLE || _graphicsQueue == VK_NULL_HANDLE ||
                                                 _oneShotCommandPool == VK_NULL_HANDLE || isDeviceLost();
        if ( bTimestampDeviceUnavailable )
            return false;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties( _physicalDevice, &properties );
        if ( properties.limits.timestampPeriod <= 0.0f || properties.limits.timestampComputeAndGraphics == VK_FALSE )
            return false;
        if ( _clockQueryPool == VK_NULL_HANDLE )
        {
            VkQueryPoolCreateInfo poolInfo{};
            poolInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            poolInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
            poolInfo.queryCount = 1;
            if ( vkCreateQueryPool( _device, &poolInfo, nullptr, &_clockQueryPool ) != VK_SUCCESS )
            {
                _clockQueryPool = VK_NULL_HANDLE;
                return false;
            }
        }

        // 보정 확장(VK_EXT_calibrated_timestamps) 없이 "지금 GPU 시계" 를 얻는 길: 타임스탬프 하나를 일회성 버퍼로 내고 큐가 빌 때까지
        // 기다린다. 돌아오는 순간의 값이 곧 지금이다(관찰 지연만큼 이르다). 큐를 비우므로 컨텍스트를 열 때 한 번만 부른다.
        {
            VulkanOneShotCommands oneShot{ _device, _oneShotCommandPool, _oneShotMutex, _graphicsQueue, _queueMutex };
            if ( oneShot.isValid() == false )
                return false;
            vkCmdResetQueryPool( oneShot.get(), _clockQueryPool, 0, 1 );
            vkCmdWriteTimestamp( oneShot.get(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _clockQueryPool, 0 );
            if ( oneShot.endSubmitAndWait() == false )
            {
                reportQueueResult( static_cast<int32>( oneShot.getLastResult() ), "readGPUClockNanos: one-shot submit" );
                return false;
            }
        }
        uint64         tick{ 0 };
        const VkResult clockResult = vkGetQueryPoolResults( _device, _clockQueryPool, 0, 1, sizeof( tick ), &tick, sizeof( tick ), VK_QUERY_RESULT_64_BIT );
        if ( clockResult == VK_ERROR_DEVICE_LOST )
            reportQueueResult( static_cast<int32>( clockResult ), "readGPUClockNanos: vkGetQueryPoolResults" );
        if ( clockResult != VK_SUCCESS )
            return false;
        outGPUNanos = RHITimestampFrame::convertTickToNanos( tick, static_cast<float64>( properties.limits.timestampPeriod ) );
        return true;
    }

    void VulkanRHIDevice::ensureTimestampPool()
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        const bool bTimestampPoolNotNeeded = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _timestampPool != VK_NULL_HANDLE || _device == VK_NULL_HANDLE ||
                                             _physicalDevice == VK_NULL_HANDLE;
        if ( bTimestampPoolNotNeeded )
            return;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties( _physicalDevice, &properties );
        // 주기가 0 이면 이 디바이스는 타임스탬프를 세지 않는다. 큐가 유효 비트를 안 주는 경우도 같다.
        if ( properties.limits.timestampPeriod <= 0.0f || properties.limits.timestampComputeAndGraphics == VK_FALSE )
            return;

        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = constant::render::kMaxGPUTimestampSlot * constant::render::kMaxFrameCountInFlight;
        if ( vkCreateQueryPool( _device, &poolInfo, nullptr, &_timestampPool ) != VK_SUCCESS )
        {
            _timestampPool = VK_NULL_HANDLE;
            return;
        }
        _timestampPeriod = properties.limits.timestampPeriod;
    }

    void VulkanRHIDevice::collectTimestampsForSlot()
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        _timestampFrame._listMicro.clear();
        const bool bNoTimestampsToRead = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _timestampPool == VK_NULL_HANDLE || _timestampPeriod <= 0.0f ||
                                         _timestampRing.getSlotMask( getFrameSlot() ).any() == false;
        if ( bNoTimestampsToRead )
            return;

        // 칸마다 [값][가용 여부] 두 개가 온다. **가용 비트가 반드시 필요하다.** 이번 프레임에 안 쓰인
        // 슬롯은 리셋된 채로 남아 영영 준비되지 않고, 그것 하나 때문에 범위 전체가 VK_NOT_READY 가 된다.
        uint64       arrResult[constant::render::kMaxGPUTimestampSlot * 2]{};
        const uint32 base = getTimestampBase();
        // **기다리지 않는다**(WAIT 비트 없음). 측정하려던 파이프라인을 멈추면 숫자가 거짓이 된다.
        // 그래서 VK_NOT_READY 도 정상 응답으로 받는다(쓰인 칸의 값은 이미 채워져 있다).
        const VkResult result =
            vkGetQueryPoolResults( _device, _timestampPool, base, constant::render::kMaxGPUTimestampSlot, sizeof( arrResult ), arrResult,
                                   sizeof( uint64 ) * 2, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT );
        if ( result == VK_ERROR_DEVICE_LOST )
            reportQueueResult( static_cast<int32>( result ), "collectTimestampsForSlot: vkGetQueryPoolResults" );
        if ( result != VK_SUCCESS && result != VK_NOT_READY )
            return;

        // 가용 비트가 선 칸만 준비된 것이다. 값과 비트를 갈라 공통 규칙에 넘긴다.
        uint64               arrTick[constant::render::kMaxGPUTimestampSlot]{};
        GPUTimestampSlotMask readyMask{};
        for ( uint32 index = 0; index < constant::render::kMaxGPUTimestampSlot; ++index )
        {
            if ( arrResult[index * 2 + 1] == 0 )
                continue;
            arrTick[index] = arrResult[index * 2];
            readyMask.set( index );
        }
        RHITimestampFrame::resolve( arrTick, readyMask, static_cast<float64>( _timestampPeriod ), _timestampFrame );
    }

    // 길이 유지(긴 함수 정리 기준): 펜스 대기 · 이미지 획득 · 기록 시작의 프레임 순서가 곧 의미라 나누지 않는다.
    void VulkanRHIDevice::beginFrameInternal()
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        // 앞 프레임이 시작되지 못해 버려진 리스트가 있었으면 그 리스트가 앞서 바꾼 레이아웃 기록부터 되돌린다.
        restoreDroppedFrameLayouts();
        _bFrameStarted = SW_FALSE;
        // 장치를 잃었으면 프레임을 시작하지 않는다(펜스 · 이미지 대기도, 스왑체인 다시 만들기도 하지 않는다). endFrame 은 시작하지 않은 프레임을 건너뛴다.
        const bool bCannotStartFrame = _width == 0 || _height == 0 || isDeviceLost();
        if ( bCannotStartFrame )
            return;

        // 재생성이 성공할 때만 표시를 내린다. 실패해도 내리면 복원 도중 한 번 실패한 뒤 스왑체인 없이 프레임을 **영영** 건너뛴다
        // (창이 검은 채로 멈춤).
        // 서피스를 잃은 채면(지난 present · 재생성 실패) 서피스부터 다시 만든다. 실패하면 이 프레임은 건너뛰고 다음 프레임에 다시 시도한다.
        if ( _bSurfaceLost == SW_TRUE )
        {
            if ( recreateSurfaceAndSwapChain() == false )
                return;
            _bSurfaceLost    = SW_FALSE;
            _bSwapChainDirty = 0;
        }
        if ( _bSwapChainDirty && recreateSwapChain() )
            _bSwapChainDirty = 0;
        rebuildPendingWindowSwapChains();

        // 재생성이 실패하면 스왑체인 · 동기화 객체가 없는 상태이므로 프레임을 건너뛰고 다음 프레임에 다시 시도한다.
        if ( _swapChain.isValid() == false || _listInFlightFence.empty() )
        {
            _bSwapChainDirty = 1;
            return;
        }

        // 앞서가기 상한: 이 칸을 앞 프레임이 아직 쓰면 그 펜스를 기다린다(`RHIFrameFenceRing`). 칸이 비었으면 펜스는 이미 신호돼 있다.
        (void)_frameFenceRing.waitForRecordingSlot( [this]( uint32 slot, uint64 busySerial )
        {
            (void)busySerial;
            assumeRenderThreadRole(); // 람다 본문은 분석이 둘러싼 함수의 역할을 잇지 못한다 — 같은 렌더 스레드에서 바로 불린다
            const VkResult fenceResult = vkWaitForFences( _device, 1, &_listInFlightFence[slot], VK_TRUE, UINT64_MAX );
            reportQueueResult( static_cast<int32>( fenceResult ), "beginFrame: vkWaitForFences" );
            return true; // 잃었으면 아래에서 프레임을 건너뛴다 — 잃은 장치는 끝나지 않을 일을 기다리지 않는다
        } );
        // 펜스 대기에서 잃었음을 봤으면 이 프레임을 바로 건너뛴다 — 획득 · 기록으로 이어가면 잃은 장치에 대기와 제출을 더 낸다.
        if ( isDeviceLost() )
            return;
        // **여기가 타임스탬프를 읽는 유일한 안전한 자리다.** 이 슬롯의 펜스를 방금 통과했다.
        ensureTimestampPool();
        collectTimestampsForSlot();
        _timestampRing.beginFrame( getFrameSlot() );
        // 이 칸의 앞 프레임까지 GPU 가 끝냈다. 그 시리얼 이하로 태그된 리소스 해제를 지금 실행한다.
        _releaseQueue.tickCompleted( _frameFenceRing.getCompletedSerial() );
        // 끝난 텍스처 전송도 걷는다 — 아무도 폴링하지 않는 전송(구간 갱신)의 링 구간과, 전송을 기다리던 텍스처 해제가 프레임마다 진행한다.
        retireFinishedTransfers();
        // 이 칸의 GPU 사용이 끝났다. 값이 바뀔 때만 쓰는 상수버퍼 가운데 이 칸이 옛 값인 것을 채운다.
        fillConstantBufferSlot();
        // 이 링 슬롯에서 프레임 스트림이 쓴 슬롯 세트들도 GPU 가 다 읽었다. 풀 묶음을 통째로 비워 이번 프레임에 다시 쓴다.
        // (리스트가 쓴 세트는 리스트 쌍의 풀 묶음에 있고, 쌍이 재사용 풀로 돌아온 뒤 beginCommandList 가 비운다.)
        resetDescriptorPoolSet( _arrFrameDescriptorPoolSet[getFrameSlot()] );

        // 앞 프레임이 present 하지 않았다면 그때 획득한 이미지를 스왑체인이 아직 쥐고 있고, acquireNextImage 는 그 이미지를 돌려준다(VulkanRHISwapChain 머리말).
        _listPendingAcquireWait.clear();
        {
            VulkanSwapChainStatus status = _swapChain.acquireNextImage( _device, getFrameSlot() );
            if ( status == VulkanSwapChainStatus::SurfaceLost )
            {
                // 사양: 서피스를 잃으면 스왑체인만이 아니라 서피스부터 다시 만든다. 잃은 서피스로 다시 acquire 하면 계속 진다.
                recordSurfaceLost( "acquire" );
                if ( recreateSurfaceAndSwapChain() == false || _swapChain.isValid() == false )
                {
                    _bSurfaceLost = SW_TRUE;
                    return;
                }
                status = _swapChain.acquireNextImage( _device, getFrameSlot() );
            }
            if ( status == VulkanSwapChainStatus::DeviceLost )
            {
                reportQueueResult( static_cast<int32>( VK_ERROR_DEVICE_LOST ), "beginFrame: vkAcquireNextImageKHR" );
                return;
            }
            // Suboptimal 은 서피스가 정말 바뀌었을 때만 다시 만든다 — 회전한 Android 기기는 IDENTITY 스왑체인에 표시마다 Suboptimal 을 준다(`hasSurfaceChanged`).
            const bool bRecreate = status == VulkanSwapChainStatus::OutOfDate || ( status == VulkanSwapChainStatus::Suboptimal && _swapChain.hasSurfaceChanged( _physicalDevice ) );
            if ( bRecreate )
            {
                if ( recreateSwapChain() == false || _swapChain.isValid() == false )
                {
                    _bSwapChainDirty = 1;
                    return;
                }

                status = _swapChain.acquireNextImage( _device, getFrameSlot() );
            }

            // Suboptimal 은 "창과 어긋났지만 이번 프레임은 그릴 수 있다" 는 뜻이다. 위에서 이미 한 번 다시
            // 만들어 봤으므로 그대로 진행한다.
            if ( status == VulkanSwapChainStatus::DeviceLost ) // 다시 획득한 쪽에서 잃었음을 본 경우(위의 검사는 첫 획득만 본다)
                reportQueueResult( static_cast<int32>( VK_ERROR_DEVICE_LOST ), "beginFrame: vkAcquireNextImageKHR (retry)" );
            if ( status != VulkanSwapChainStatus::Success && status != VulkanSwapChainStatus::Suboptimal )
                return;
        }
        // 방금 획득한 이미지의 가용 세마포어는 이 프레임의 첫 제출이 딱 한 번 기다린다(물려받은 이미지는 기다릴 신호가 없다).
        const VkSemaphore mainAcquireWait = _swapChain.takeAcquireWait();
        if ( mainAcquireWait != VK_NULL_HANDLE )
            _listPendingAcquireWait.push_back( mainAcquireWait );

        // 이 이미지를 마지막으로 쓴 프레임이 아직 GPU 에 있으면 그 펜스를 기다린 뒤에 덮어쓴다.
        reportQueueResult( _swapChain.waitForImageReuse( _device, _listInFlightFence[getFrameSlot()] ), "beginFrame: vkWaitForFences (image in flight)" );

        // 이 셋은 메모리 부족으로만 실패한다(장치 분실은 돌려주지 않는다). 실패한 버퍼에 기록하면 제출이 검증 오류 · 크래시라 멈춘다.
        requireVk( vkResetFences( _device, 1, &_listInFlightFence[getFrameSlot()] ), "beginFrame: vkResetFences" );
        // 이 슬롯의 지난 프레임이 끝났다 — 그 프레임이 빌린 비동기 컴퓨트 세마포어는 모두 신호되고 기다려졌다(endFrame 이 남은 것을 기다린다).
        _asyncSemaphoreCursor = 0;
        _listAsyncSignal.clear();
        _listAsyncSignalWaited.clear();
        _listPendingComputeWait.clear();
        requireVk( vkResetCommandBuffer( _listCommandBuffer[getFrameSlot()], 0 ), "beginFrame: vkResetCommandBuffer" );

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        requireVk( vkBeginCommandBuffer( _listCommandBuffer[getFrameSlot()], &beginInfo ), "beginFrame: vkBeginCommandBuffer" );
        // Vulkan 은 **쓰기 전에 반드시 리셋**해야 한다. 안 하면 결과가 정의되지 않는다.
        if ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampPool != VK_NULL_HANDLE )
            vkCmdResetQueryPool( _listCommandBuffer[getFrameSlot()], _timestampPool, getTimestampBase(), constant::render::kMaxGPUTimestampSlot );

        // 프레임 스트림의 첫 세그먼트. 리스트가 제출될 때마다 여기서 잘리고 새 세그먼트가 열린다.
        _activeFrameBuffer  = _listCommandBuffer[getFrameSlot()];
        _frameSegmentCursor = 0;
        _listPendingSubmit.clear();

        // 새 커맨드버퍼에는 아직 아무 세트도 안 걸렸다. flushSlotSet 이 슬롯 세트와 텍스처 세트를 다시 건다.
        _recordingState._bTextureSetBound = SW_FALSE;
        _recordingState._arrSlotState[0]  = VulkanSlotState{};
        _recordingState._arrSlotState[1]  = VulkanSlotState{};

        _bFrameStarted                     = SW_TRUE;
        _recordingState._bRenderPassActive = SW_FALSE;

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        VkViewport viewport{};
        viewport.x = kDefaultViewportX;

        viewport.y        = static_cast<float32>( _swapChain.getExtentHeight() );
        viewport.width    = static_cast<float32>( _swapChain.getExtentWidth() );
        viewport.height   = -static_cast<float32>( _swapChain.getExtentHeight() );
        viewport.minDepth = kDefaultViewportMinDepth;
        viewport.maxDepth = kDefaultViewportMaxDepth;
        vkCmdSetViewport( _activeFrameBuffer, 0, 1, &viewport );

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { _swapChain.getExtentWidth(), _swapChain.getExtentHeight() };
        vkCmdSetScissor( _activeFrameBuffer, 0, 1, &scissor );

        // 백버퍼 렌더패스는 여기서 열지 않는다. beginFrame 은 프레임 수명주기 전용이고, 백버퍼
        // 타깃팅은 RenderThread 가 여는 beginRenderPass(핸들 0) 가 명시적으로 한다.
        // 클리어도 그 렌더패스의 loadOp 이 맡는다.
    }

    void VulkanRHIDevice::endFrameInternal( bool vsync, bool bPresent )
    {
        _renderThread.assumeHeld(); // 렌더 스레드의 프레임 진행(그 병렬 기록 태스크 포함)에서, 또는 렌더 스레드를 세운 뒤(초기화 · 종료 · resize) 불린다
        (void)vsync;
        if ( _bFrameStarted == SW_FALSE )
        {
            // 건너뛴 프레임이다. 버려진 리스트가 앞서 바꾼 레이아웃 기록을 바로 되돌려야 이 프레임 뒤의 읽기(스크린샷)가 실제 레이아웃을 본다.
            restoreDroppedFrameLayouts();
            return;
        }

        // 열린 렌더 패스는 공용 `IRHIDevice::endFrame` 이 이미 닫았다.
        requireVk( vkEndCommandBuffer( _activeFrameBuffer ), "endFrame: vkEndCommandBuffer" );
        _listPendingSubmit.push_back( _activeFrameBuffer );

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        // 기다리지 않은 비동기 컴퓨트 신호를 이 마지막 제출이 모두 기다린다 — 이진 세마포어는 신호마다 한 번 기다려야 하고, 프레임 펜스 하나가 두 큐의 끝을 덮는다.
        for ( size_t signalIndex = 0; signalIndex < _listAsyncSignal.size(); ++signalIndex )
        {
            if ( _listAsyncSignalWaited[signalIndex] == SW_FALSE )
                _listPendingComputeWait.push_back( _listAsyncSignal[signalIndex] );
            _listAsyncSignalWaited[signalIndex] = SW_TRUE;
        }
        // acquire 대기는 이미지마다 한 번뿐이다. 즉시 모드에서 앞선 제출이 이미 소비했으면 남은 것만 기다린다.
        gatherSubmitWaits();
        _listPendingComputeWait.clear();
        submitInfo.waitSemaphoreCount = static_cast<uint32>( _listSubmitWaitSemaphore.size() );
        submitInfo.pWaitSemaphores    = _listSubmitWaitSemaphore.data();
        submitInfo.pWaitDstStageMask  = _listSubmitWaitStage.data();

        // 프레임 세그먼트와 리스트 버퍼를 기록 순서 그대로 한 번에 제출한다. 같은 큐에 대한
        // 제출 순서가 곧 실행 순서라, 세그먼트 사이의 리소스 의존성이 그대로 지켜진다.
        submitInfo.commandBufferCount = static_cast<uint32>( _listPendingSubmit.size() );
        submitInfo.pCommandBuffers    = _listPendingSubmit.data();

        // renderFinished 를 기다리는 건 present 뿐이다. present 하지 않는 프레임까지 시그널하면
        // 아무도 소비하지 않은 이진 세마포어를 거듭 시그널하게 되어 규약 위반이다.
        _listSubmitSignalSemaphore.clear();
        if ( bPresent )
        {
            _listSubmitSignalSemaphore.push_back( _swapChain.getRenderFinishedSemaphore() );
            for ( const VulkanRHISwapChain* pWindow : _windowSwapChains.getFrameList() )
            {
                _listSubmitSignalSemaphore.push_back( pWindow->getRenderFinishedSemaphore() );
            }
            submitInfo.signalSemaphoreCount = static_cast<uint32>( _listSubmitSignalSemaphore.size() );
            submitInfo.pSignalSemaphores    = _listSubmitSignalSemaphore.data();
        }

        // 이번 프레임 기록 중 등록된 지연 해제(enqueueGPURelease)는 기록 시리얼(`_frameFenceRing`)이 실제로 끝났다고 확인될 때까지(beginFrame) 보류된다.
        if ( _frameStreamContext != nullptr )
            recordSubmittedEventMarker( _frameStreamContext->getLastEventMarker() );
        UniqueLock<mutex> queueLock{ _queueMutex };
        // 기록하는 사이에 장치를 잃었으면(다른 스레드의 손실 · rhi.removeDevice) 내지 않는다. 버퍼는 위에서 닫았다.
        const bool     bDeviceLost  = isDeviceLost();
        const VkResult submitResult = bDeviceLost ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _graphicsQueue, 1, &submitInfo, _listInFlightFence[getFrameSlot()] );
        reportQueueResult( static_cast<int32>( submitResult ), "endFrame: vkQueueSubmit" );
        _listPendingAcquireWait.clear();
        // 이 링 슬롯의 쿼리 구간은 이제 "리셋 + 제출" 을 한 번은 거쳤다. 그 전에 읽으면 미정의다.
        if ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampPool != VK_NULL_HANDLE )
            (void)_timestampRing.closeFrame( getFrameSlot() );

        if ( bPresent && isDeviceLost() == false )
        {
            const VulkanSwapChainStatus presentStatus = _swapChain.present( _graphicsQueue ); // 큐 락 안(위의 queueLock)
            if ( presentStatus == VulkanSwapChainStatus::DeviceLost )
                reportQueueResult( static_cast<int32>( VK_ERROR_DEVICE_LOST ), "endFrame: vkQueuePresentKHR" );
            else if ( presentStatus == VulkanSwapChainStatus::OutOfDate || ( presentStatus == VulkanSwapChainStatus::Suboptimal && _swapChain.hasSurfaceChanged( _physicalDevice ) ) )
            {
                // 다음 beginFrame 에서 스왑체인을 다시 만든다.
                _bSwapChainDirty = 1;
            }
            else if ( presentStatus == VulkanSwapChainStatus::SurfaceLost )
            {
                // 다음 beginFrame 에서 서피스부터 다시 만든다(큐 락 안이라 여기서 만들지 않는다).
                recordSurfaceLost( "present" );
                _bSurfaceLost = SW_TRUE;
            }
            // 다른 창도 같은 제출 뒤에 표시한다(Godot 의 execute_and_present 자리). 낡았으면 다음에 그 창을 쓸 때(`useSwapChain`) 다시 만든다.
            for ( VulkanRHISwapChain* pWindow : _windowSwapChains.getFrameList() )
            {
                if ( pWindow->present( _graphicsQueue ) == VulkanSwapChainStatus::DeviceLost )
                    reportQueueResult( static_cast<int32>( VK_ERROR_DEVICE_LOST ), "endFrame: vkQueuePresentKHR (window)" );
            }
        }
        _windowSwapChains.clearFrame();
        queueLock.unlock();

        _listPendingSubmit.clear();
        _activeFrameBuffer = VK_NULL_HANDLE;
        (void)_frameFenceRing.markSubmitted(); // 칸 · 해제 키가 다음 프레임으로 넘어간다
        _bFrameStarted = SW_FALSE;
    }

    VkCommandBuffer VulkanRHIDevice::currentCommandBuffer() const
    {
        // 스트림은 하나지만 리스트가 제출될 때마다 세그먼트로 잘린다. 지금 열려 있는 세그먼트를
        // 반환한다. 오프스크린도 같은 스트림이다(별도 버퍼 · 자체 제출 · 펜스 블로킹 없음).
        if ( _bFrameStarted == SW_TRUE )
            return _activeFrameBuffer;
        return VK_NULL_HANDLE;
    }

    VulkanCommandListEntry VulkanRHIDevice::acquireCommandListEntry()
    {
        // 반환 대상은 이 하나다. 이름 있는 반환 객체가 여럿이면 NRVO 가 걸리지 않는다.
        VulkanCommandListEntry entry{};
        {
            ScopedLock<mutex> lock{ _cmdListPoolMutex };
            if ( _listFreeCmdListEntry.empty() == false )
            {
                entry = _listFreeCmdListEntry.back();
                _listFreeCmdListEntry.pop_back();
                return entry;
            }
        }

        if ( _device == nullptr )
            return entry;

        // 풀은 리스트마다 전용이어야 한다. VkCommandPool 은 외부 동기화 대상이라 두 스레드가 같은
        // 풀에서 동시에 기록하면 정의되지 않은 동작이다. 슬롯 세트 풀 묶음도 같은 이유로 쌍마다 전용이다.
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
        if ( vkCreateCommandPool( _device, &poolInfo, nullptr, &entry._pool ) != VK_SUCCESS )
        {
            entry = VulkanCommandListEntry{};
            return entry;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = entry._pool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        if ( vkAllocateCommandBuffers( _device, &allocInfo, &entry._buffer ) != VK_SUCCESS )
        {
            vkDestroyCommandPool( _device, entry._pool, nullptr );
            entry = VulkanCommandListEntry{};
            return entry;
        }

        {
            // 풀 묶음은 디바이스가 소유한다. 쌍은 값으로 복사돼 다니므로(지연 반환 람다 캡처) 빌려 쓰는 포인터만 든다.
            ScopedLock<mutex> lock{ _cmdListPoolMutex };
            _listCmdListDescriptorPoolSet.push_back( make_unique<VulkanDescriptorPoolSet>() );
            entry._pDescriptorPoolSet = _listCmdListDescriptorPoolSet.back().get();
        }
        return entry;
    }

    void VulkanRHIDevice::destroyCommandListEntryImmediate( VulkanCommandListEntry& entry )
    {
        if ( _device != nullptr && entry._pool != VK_NULL_HANDLE )
            vkDestroyCommandPool( _device, entry._pool, nullptr ); // 풀을 부수면 버퍼도 같이 간다
        entry = VulkanCommandListEntry{};
    }

    void VulkanRHIDevice::registerCommandList( VulkanRHICommandList* pCmdList )
    {
        ScopedLock<mutex> lock{ _liveCmdListMutex };
        _listLiveCmd.push_back( pCmdList );
    }

    void VulkanRHIDevice::unregisterCommandList( VulkanRHICommandList* pCmdList )
    {
        ScopedLock<mutex> lock{ _liveCmdListMutex };
        (void)VectorUtil::removeSingleSwap( _listLiveCmd, pCmdList ); // 등록되지 않은 리스트면 뺄 것이 없다
    }

    void VulkanRHIDevice::recycleCommandListEntryDeferred( VulkanCommandListEntry entry )
    {
        if ( entry._pool == VK_NULL_HANDLE || entry._buffer == VK_NULL_HANDLE )
            return;

        // 제출 직후 리스트 객체가 사라져도 GPU 는 아직 이 버퍼를 읽고 있다. 이번 프레임 세대가
        // 끝났다고 확인된 뒤에야 재사용 풀로 돌려보낸다(대기 없음).
        auto recycleCb = [this, entry]()
        {
            ScopedLock<mutex> lock{ _cmdListPoolMutex };
            _listFreeCmdListEntry.push_back( entry );
        };
        _releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, recycleCb ), _frameFenceRing.getRecordingSerial() );
    }

    VkCommandBuffer VulkanRHIDevice::beginNextFrameSegment()
    {
        if ( _device == nullptr || getFrameSlot() >= constant::render::kMaxFrameCountInFlight )
            return VK_NULL_HANDLE;

        vector<VkCommandBuffer>& listSegment = _arrFrameSegment[getFrameSlot()];
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
        requireVk( vkResetCommandBuffer( segment, 0 ), "frame segment: vkResetCommandBuffer" );
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        requireVk( vkBeginCommandBuffer( segment, &beginInfo ), "frame segment: vkBeginCommandBuffer" );
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

        // 프레임 밖 일회성 제출이라 프레임 펜스에 얹을 수 없다. 자체 제출 후 큐가 비기를 기다려
        // 부르는 쪽이 결과를 바로 쓸 수 있게 한다(업로드 · 스모크 용도라 빈도가 낮다).
        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &listBuffer;
        recordSubmittedEventMarker( pList->getLastEventMarker() );
        ScopedLock<mutex> queueLock{ _queueMutex };
        VkResult          queueResult = isDeviceLost() ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE );
        if ( queueResult == VK_SUCCESS )
            queueResult = vkQueueWaitIdle( _graphicsQueue );
        reportQueueResult( static_cast<int32>( queueResult ), "executeCommandListImmediate" );

        // 제출한 리스트가 바꾼 레이아웃은 실제로 일어났으므로 되돌릴 기록에서 뺀다.
        ScopedLock<mutex> layoutLock{ _droppedLayoutMutex };
        for ( size_t index = _listDroppedFrameLayout.size(); index > 0; --index )
        {
            if ( _listDroppedFrameLayout[index - 1]._cmd == listBuffer )
                _listDroppedFrameLayout.erase( _listDroppedFrameLayout.begin() + static_cast<std::ptrdiff_t>( index - 1 ) );
        }
    }

    void VulkanRHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        VulkanRHICommandList* pList                    = static_cast<VulkanRHICommandList*>( pCmdList );
        const bool            bCannotSubmitCommandList = pList == nullptr || _bFrameStarted == SW_FALSE || _activeFrameBuffer == VK_NULL_HANDLE;
        if ( bCannotSubmitCommandList )
            return;

        const VkCommandBuffer listBuffer = pList->nativeCommandBuffer();
        if ( listBuffer == VK_NULL_HANDLE )
            return;

        // 지금까지의 프레임 세그먼트를 닫아 제출 순서에 넣고, 그 뒤에 이 리스트의 버퍼를 잇는다.
        _frameStreamContext->endRenderPass(); // 공용 패스 상태와 함께 닫는다(IRHICommandContext)
        requireVk( vkEndCommandBuffer( _activeFrameBuffer ), "executeCommandList: vkEndCommandBuffer" );
        _listPendingSubmit.push_back( _activeFrameBuffer );
        _listPendingSubmit.push_back( listBuffer );
        recordSubmittedEventMarker( pList->getLastEventMarker() );

        // 즉시 모드: 여기서 바로 내보낸다. acquire 세마포어 대기는 '이 프레임의 첫 제출'에만 걸고,
        // renderFinished 신호와 인플라이트 펜스는 endFrame 의 마지막 제출이 담당한다.
        if ( _bImmediateSubmit )
            submitPendingGraphics( VK_NULL_HANDLE );
        openNextFrameSegment();
    }

    void VulkanRHIDevice::gatherSubmitWaits()
    {
        // acquire 대기는 색 출력 단계에서, 비동기 컴퓨트 대기는 모든 단계에서 기다린다(컴퓨트 결과를 정점 · 간접 인자 단계부터 읽는다).
        _listSubmitWaitSemaphore.assign( _listPendingAcquireWait.begin(), _listPendingAcquireWait.end() );
        _listSubmitWaitStage.assign( _listPendingAcquireWait.size(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT );
        _listSubmitWaitSemaphore.insert( _listSubmitWaitSemaphore.end(), _listPendingComputeWait.begin(), _listPendingComputeWait.end() );
        _listSubmitWaitStage.insert( _listSubmitWaitStage.end(), _listPendingComputeWait.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT );
    }

    void VulkanRHIDevice::submitPendingGraphics( VkSemaphore signal )
    {
        gatherSubmitWaits();
        VkSubmitInfo flushInfo{};
        flushInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        flushInfo.commandBufferCount   = static_cast<uint32>( _listPendingSubmit.size() );
        flushInfo.pCommandBuffers      = _listPendingSubmit.data();
        flushInfo.waitSemaphoreCount   = static_cast<uint32>( _listSubmitWaitSemaphore.size() );
        flushInfo.pWaitSemaphores      = _listSubmitWaitSemaphore.data();
        flushInfo.pWaitDstStageMask    = _listSubmitWaitStage.data();
        flushInfo.signalSemaphoreCount = signal != VK_NULL_HANDLE ? 1u : 0u;
        flushInfo.pSignalSemaphores    = &signal;
        {
            ScopedLock<mutex> queueLock{ _queueMutex };
            const VkResult    flushResult = isDeviceLost() ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _graphicsQueue, 1, &flushInfo, VK_NULL_HANDLE );
            reportQueueResult( static_cast<int32>( flushResult ), "vkQueueSubmit (pending graphics)" );
        }
        _listPendingAcquireWait.clear();
        _listPendingComputeWait.clear();
        _listPendingSubmit.clear();
    }

    void VulkanRHIDevice::submitFrameWorkNow( VkSemaphore signal )
    {
        if ( _bFrameStarted == SW_FALSE || _activeFrameBuffer == VK_NULL_HANDLE )
            return;
        // 세그먼트가 비어 있어도 닫아 넣는다(빈 버퍼 하나의 비용) — 신호는 그 앞의 모든 그래픽스 일을 덮어야 한다.
        _frameStreamContext->endRenderPass();
        requireVk( vkEndCommandBuffer( _activeFrameBuffer ), "submitFrameWorkNow: vkEndCommandBuffer" );
        _listPendingSubmit.push_back( _activeFrameBuffer );
        submitPendingGraphics( signal );
        openNextFrameSegment();
    }

    VkSemaphore VulkanRHIDevice::acquireAsyncSemaphore()
    {
        vector<VkSemaphore>& listPool = _listAsyncSemaphorePool[getFrameSlot()];
        if ( _asyncSemaphoreCursor < listPool.size() )
            return listPool[_asyncSemaphoreCursor++];
        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        requireVk( vkCreateSemaphore( _device, &createInfo, nullptr, &semaphore ), "vkCreateSemaphore (async compute)" );
        listPool.push_back( semaphore );
        ++_asyncSemaphoreCursor;
        return semaphore;
    }

    unique_ptr<IRHICommandList> VulkanRHIDevice::createAsyncComputeCommandList()
    {
        if ( _asyncComputeQueue == VK_NULL_HANDLE )
            return IRHIDevice::createAsyncComputeCommandList(); // 능력이 거짓인 길 — 오류를 남기고 nullptr
        // 같은 패밀리라 일반 리스트(같은 패밀리의 풀 · 같은 바인딩)를 그대로 두 번째 큐에 낸다. 브레드크럼만 제 칸에 쓴다(큐 사이 쓰기 경합이 없게).
        unique_ptr<VulkanRHICommandList> list = make_unique<VulkanRHICommandList>( this );
        list->setBreadcrumbSlotBase( kComputeBreadcrumbSlotBase );
        return list;
    }

    RHIQueueSyncPoint VulkanRHIDevice::submitAsyncCompute( IRHICommandList* pCmdList )
    {
        if ( _asyncComputeQueue == VK_NULL_HANDLE )
            return IRHIDevice::submitAsyncCompute( pCmdList );
        VulkanRHICommandList* pList = static_cast<VulkanRHICommandList*>( pCmdList );
        if ( pList == nullptr || pList->nativeCommandBuffer() == VK_NULL_HANDLE || _bFrameStarted == SW_FALSE )
            return RHIQueueSyncPoint{};
        // 포크: 지금까지의 그래픽스 일을 내며 신호하고, 두 번째 큐가 그 신호를 기다린 뒤 이 리스트를 실행하고 다른 세마포어를 신호한다.
        const VkSemaphore graphicsDone = acquireAsyncSemaphore();
        const VkSemaphore computeDone  = acquireAsyncSemaphore();
        submitFrameWorkNow( graphicsDone );
        const VkCommandBuffer      listBuffer = pList->nativeCommandBuffer();
        const VkPipelineStageFlags waitStage  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo               submitInfo{};
        submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount   = 1;
        submitInfo.pWaitSemaphores      = &graphicsDone;
        submitInfo.pWaitDstStageMask    = &waitStage;
        submitInfo.commandBufferCount   = 1;
        submitInfo.pCommandBuffers      = &listBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &computeDone;
        recordSubmittedEventMarker( pList->getLastEventMarker() );
        {
            ScopedLock<mutex> queueLock{ _queueMutex };
            const VkResult    submitResult = isDeviceLost() ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _asyncComputeQueue, 1, &submitInfo, VK_NULL_HANDLE );
            reportQueueResult( static_cast<int32>( submitResult ), "submitAsyncCompute: vkQueueSubmit" );
        }
        _listAsyncSignal.push_back( computeDone );
        _listAsyncSignalWaited.push_back( SW_FALSE );
        return RHIQueueSyncPoint{ static_cast<uint64>( _listAsyncSignal.size() ) };
    }

    void VulkanRHIDevice::waitForAsyncCompute( RHIQueueSyncPoint point )
    {
        if ( _asyncComputeQueue == VK_NULL_HANDLE )
        {
            IRHIDevice::waitForAsyncCompute( point );
            return;
        }
        const bool bKnown = point.isValid() && point._value <= _listAsyncSignal.size();
        if ( bKnown == false )
            return;
        const size_t signalIndex = static_cast<size_t>( point._value - 1 );
        if ( _listAsyncSignalWaited[signalIndex] != SW_FALSE )
            return;
        // 조인: 그 전 그래픽스 일은 기다리지 않게 먼저 큐에 낸다(포크와 조인 사이가 겹친다). 이 신호는 다음 그래픽스 제출이 기다린다.
        submitFrameWorkNow( VK_NULL_HANDLE );
        _listPendingComputeWait.push_back( _listAsyncSignal[signalIndex] );
        _listAsyncSignalWaited[signalIndex] = SW_TRUE;
    }

    void VulkanRHIDevice::openNextFrameSegment()
    {
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
        viewport.y        = static_cast<float32>( _swapChain.getExtentHeight() );
        viewport.width    = static_cast<float32>( _swapChain.getExtentWidth() );
        viewport.height   = -static_cast<float32>( _swapChain.getExtentHeight() );
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport( _activeFrameBuffer, 0, 1, &viewport );

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { _swapChain.getExtentWidth(), _swapChain.getExtentHeight() };
        vkCmdSetScissor( _activeFrameBuffer, 0, 1, &scissor );
    }

    bool VulkanRHIDevice::initializeTransfers( const RHISwapChainDesc& desc )
    {
        if ( desc._stagingRingBytes == 0 || desc._readbackRingBytes == 0 )
        {
            SW_LOG_ERROR_ID( "Vulkan.VulkanBackendNeedsStaging", "The Vulkan backend needs staging ring capacities (gv_rhiStagingRingMegabytes and gv_rhiReadbackRingMegabytes must be positive)" );
            return false;
        }

        // 작업을 다시 쓸 때 begin 이 버퍼를 리셋하므로 RESET_COMMAND_BUFFER 가 필요하다. 짧게 사는 기록이라 TRANSIENT 도 준다.
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = _graphicsQueueFamilyIndex;
        if ( vkCreateCommandPool( _device, &poolInfo, nullptr, &_transferCommandPool ) != VK_SUCCESS )
        {
            _transferCommandPool = VK_NULL_HANDLE;
            SW_LOG_ERROR_ID( "Vulkan.FailedCreateVulkanTransfer", "Failed to create the Vulkan transfer command pool" );
            return false;
        }
        return createStagingRing( _uploadRing, desc._stagingRingBytes, desc._stagingSoloMaxBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "upload" ) &&
               createStagingRing( _readbackRing, desc._readbackRingBytes, desc._stagingSoloMaxBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "readback" );
    }

    void VulkanRHIDevice::shutdownTransfers()
    {
        // GPU 는 종료 2단계(waitIdle)에서 쉬었다. 잃은 장치면 끝나지 않은 작업이 남아 있을 수 있지만 더 기다리지 않는다(isDeviceLost 설명).
        _transferFenceQueue.retireAll( [this]( VulkanTransferJob& job )
        { recycleTransferJob( job ); } );
        _transferReleaseQueue.flushAll();
        {
            ScopedLock<mutex> jobLock{ _transferJobMutex };
            for ( const VulkanTransferJob& job : _listFreeTransferJob )
            {
                if ( job._fence != VK_NULL_HANDLE )
                    vkDestroyFence( _device, job._fence, nullptr );
            }
            _listFreeTransferJob.clear();
        }
        // 커맨드 버퍼는 풀과 함께 놓인다.
        if ( _transferCommandPool != VK_NULL_HANDLE )
        {
            vkDestroyCommandPool( _device, _transferCommandPool, nullptr );
            _transferCommandPool = VK_NULL_HANDLE;
        }
        // 가져가지 않은 읽기는 링과 함께 사라진다. 그 뒤의 가져가기 · 버리기는 업로드 티켓처럼 아무 일도 하지 않는다.
        _readbackTable.clear();
        destroyStagingRing( _uploadRing );
        destroyStagingRing( _readbackRing );
    }

    bool VulkanRHIDevice::createStagingRing( VulkanStagingRing& ring, uint64 capacityBytes, uint64 soloMaxBytes, uint32 usageFlags, const utf8* pName )
    {
        // 구간 시작 정렬. 복사 오프셋 규칙(텍셀 블록 바이트와 4 의 배수)은 제출 함수가 구간 안에서 맞추고, 여기서는 캐시 줄 경계에서 시작하도록 넉넉히 둔다(GL 링과 같다).
        constexpr uint64 kSpanAlignmentBytes = 256;
        if ( createMappedStagingBuffer( capacityBytes, usageFlags, pName, ring._buffer, ring._memory, ring._pMapped ) == false )
            return false;
        ring._allocator.initialize( capacityBytes, kSpanAlignmentBytes, soloMaxBytes );
        return true;
    }

    void VulkanRHIDevice::destroyStagingRing( VulkanStagingRing& ring )
    {
        {
            ScopedLock<mutex> soloLock{ ring._soloMutex };
            destroyMappedStagingBuffer( ring._soloBuffer, ring._soloMemory, ring._pSoloMapped );
        }
        destroyMappedStagingBuffer( ring._buffer, ring._memory, ring._pMapped );
        ring._allocator.shutdown();
    }

    bool VulkanRHIDevice::createMappedStagingBuffer( uint64 capacityBytes, uint32 usageFlags, const utf8* pName, VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                                                     uint8*& outMapped )
    {
        outBuffer = VK_NULL_HANDLE;
        outMemory = VK_NULL_HANDLE;
        outMapped = nullptr;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size        = capacityBytes;
        bufferInfo.usage       = usageFlags;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &outBuffer ) != VK_SUCCESS )
        {
            outBuffer = VK_NULL_HANDLE;
            SW_LOG_ERROR_ID( "Vulkan.FailedCreateVulkanStaging", "Failed to create the Vulkan %# staging buffer (%# bytes, gv_rhiStagingRingMegabytes · gv_rhiReadbackRingMegabytes · gv_rhiStagingSingleMaxMegabytes)",
                             pName, capacityBytes );
            return false;
        }

        // 호스트 일관 메모리라 제출 전의 memcpy 는 flush 없이 보이고, 되읽기는 복사 뒤의 HOST 배리어와 펜스만으로 읽힌다(invalidate 가 필요 없다).
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements( _device, outBuffer, &requirements );
        uint32 memoryTypeIndex{ 0 };
        void*  pMapped{ nullptr };
        bool   bMemoryReady = findMemoryType( requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex );
        if ( bMemoryReady )
        {
            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize  = requirements.size;
            allocInfo.memoryTypeIndex = memoryTypeIndex;
            bMemoryReady              = vkAllocateMemory( _device, &allocInfo, nullptr, &outMemory ) == VK_SUCCESS &&
                           vkBindBufferMemory( _device, outBuffer, outMemory, 0 ) == VK_SUCCESS &&
                           vkMapMemory( _device, outMemory, 0, VK_WHOLE_SIZE, 0, &pMapped ) == VK_SUCCESS && pMapped != nullptr;
        }
        if ( bMemoryReady == false )
        {
            SW_LOG_ERROR_ID( "Vulkan.VulkanStagingBufferBytes", "The Vulkan %# staging buffer (%# bytes) could not be allocated or mapped in host visible memory", pName, capacityBytes );
            destroyMappedStagingBuffer( outBuffer, outMemory, outMapped );
            return false;
        }

        outMapped = static_cast<uint8*>( pMapped );
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( outMemory ), RHIMemoryKind::Staging, requirements.size );
        return true;
    }

    void VulkanRHIDevice::destroyMappedStagingBuffer( VkBuffer& rBuffer, VkDeviceMemory& rMemory, uint8*& pMapped )
    {
        // 메모리를 놓으면 매핑도 풀린다. 추적기에는 매핑까지 끝난 버퍼만 올렸다.
        if ( pMapped != nullptr )
            getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( rMemory ) );
        if ( rBuffer != VK_NULL_HANDLE )
            vkDestroyBuffer( _device, rBuffer, nullptr );
        if ( rMemory != VK_NULL_HANDLE )
            vkFreeMemory( _device, rMemory, nullptr );
        rBuffer = VK_NULL_HANDLE;
        rMemory = VK_NULL_HANDLE;
        pMapped = nullptr;
    }

    RHISubmitResult VulkanRHIDevice::allocateStagingSpan( VulkanStagingRing& ring, uint64 sizeBytes, uint32 usageFlags, const utf8* pCaller, RHIStagingSpan& outSpan )
    {
        RHIStagingAllocateResult result = ring._allocator.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        if ( result == RHIStagingAllocateResult::Full )
        {
            retireFinishedTransfers();
            result = ring._allocator.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        }
        // 결과 옮기기 · 못 받은 구간 놓기 · 이유 로그는 세 백엔드가 같이 쓰는 한 곳이다(잃은 장치면 Full 도 Rejected, 계약 2).
        const RHISubmitResult submitResult = RHIStagingRingUtil::settleAllocation( ring._allocator, _transferSerials, result, sizeBytes, "Vulkan", pCaller, outSpan );
        if ( submitResult != RHISubmitResult::Accepted || outSpan._bSolo == SW_FALSE )
            return submitResult;
        // 할당기가 단독 구간을 내줬다면 앞선 단독 구간은 이미 돌아왔다 — 남은 버퍼(아직 회수가 지우지 않은 것)를 GPU 가 더 쓰지 않는다.
        ScopedLock<mutex> soloLock{ ring._soloMutex };
        destroyMappedStagingBuffer( ring._soloBuffer, ring._soloMemory, ring._pSoloMapped );
        if ( createMappedStagingBuffer( outSpan._sizeBytes, usageFlags, "single transfer", ring._soloBuffer, ring._soloMemory, ring._pSoloMapped ) )
            return RHISubmitResult::Accepted;
        return RHIStagingRingUtil::rejectSoloBufferFailure( ring._allocator, outSpan, "Vulkan", pCaller );
    }

    void VulkanRHIDevice::destroyIdleSoloBuffer( VulkanStagingRing& ring )
    {
        ScopedLock<mutex> soloLock{ ring._soloMutex };
        if ( ring._soloBuffer == VK_NULL_HANDLE )
            return;
        // 판정은 락 안에서 한다 — 밖에서 보면 그 사이 다른 스레드가 새 단독 구간을 받아 버퍼를 만들 수 있다.
        ring._allocator.freeCompleted( _transferSerials.getWatermark() );
        if ( ring._allocator.hasLiveSolo() )
            return;
        destroyMappedStagingBuffer( ring._soloBuffer, ring._soloMemory, ring._pSoloMapped );
    }

    bool VulkanRHIDevice::beginTransferJob( VulkanTransferJob& outJob )
    {
        outJob = VulkanTransferJob{};
        {
            ScopedLock<mutex> jobLock{ _transferJobMutex };
            if ( _listFreeTransferJob.empty() == false )
            {
                outJob = _listFreeTransferJob.back();
                _listFreeTransferJob.pop_back();
            }
        }

        VkResult result{ VK_SUCCESS };
        if ( outJob._commandBuffer == VK_NULL_HANDLE )
        {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool        = _transferCommandPool;
            allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            result                       = vkAllocateCommandBuffers( _device, &allocInfo, &outJob._commandBuffer );
            if ( result != VK_SUCCESS )
            {
                SW_LOG_ERROR_ID( "Vulkan.FailedAllocateVulkanTransfer", "Failed to allocate a Vulkan transfer command buffer (VkResult=%#)", static_cast<int32>( result ) );
                return false;
            }
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            result          = vkCreateFence( _device, &fenceInfo, nullptr, &outJob._fence );
            if ( result != VK_SUCCESS )
            {
                vkFreeCommandBuffers( _device, _transferCommandPool, 1, &outJob._commandBuffer );
                SW_LOG_ERROR_ID( "Vulkan.FailedCreateVulkanTransfer2", "Failed to create a Vulkan transfer fence (VkResult=%#)", static_cast<int32>( result ) );
                return false;
            }
        }
        else
        {
            // 다시 쓰는 작업의 펜스는 지난 제출에서 신호됐다(제출하지 못하고 돌아온 작업이면 신호되지 않은 채다 — 리셋은 둘 다 받는다).
            result = vkResetFences( _device, 1, &outJob._fence );
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if ( result == VK_SUCCESS )
            result = vkBeginCommandBuffer( outJob._commandBuffer, &beginInfo ); // 풀이 RESET_COMMAND_BUFFER 라 지난 기록을 함께 지운다
        if ( result == VK_SUCCESS )
            return true;
        SW_LOG_ERROR_ID( "Vulkan.FailedBeginVulkanTransfer", "Failed to begin a Vulkan transfer command buffer (VkResult=%#)", static_cast<int32>( result ) );
        recycleTransferJob( outJob );
        return false;
    }

    uint64 VulkanRHIDevice::submitTransferJob( const VulkanTransferJob& job, const utf8* pWhere )
    {
        VkResult result = vkEndCommandBuffer( job._commandBuffer );
        uint64   serial{ 0 };
        if ( result == VK_SUCCESS )
        {
            VkSubmitInfo submitInfo{};
            submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers    = &job._commandBuffer;
            ScopedLock<mutex> queueLock{ _queueMutex };
            // 기록하는 사이에 장치를 잃었으면(다른 스레드의 손실 · rhi.removeDevice) 내지 않는다.
            result = isDeviceLost() ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _graphicsQueue, 1, &submitInfo, job._fence );
            // 시리얼은 큐에 낸 직후 같은 락 안에서 매긴다 — 락 밖에서 매기면 시리얼 순서가 큐 순서와 어긋나 워터마크가 끝나지 않은 제출을 덮는다(계약 5).
            if ( result == VK_SUCCESS )
                serial = _transferFenceQueue.enqueue( job );
        }
        if ( serial != 0 )
            return serial;
        reportQueueResult( static_cast<int32>( result ), pWhere );
        recycleTransferJob( job );
        return 0;
    }

    void VulkanRHIDevice::recycleTransferJob( const VulkanTransferJob& job )
    {
        ScopedLock<mutex> jobLock{ _transferJobMutex };
        _listFreeTransferJob.push_back( job );
    }

    void VulkanRHIDevice::retireFinishedTransfers()
    {
        // 잃은 장치의 펜스는 묻지 않는다(isDeviceLost 설명). 워터마크가 멈추고 남은 티켓은 Failed 다.
        if ( _device != VK_NULL_HANDLE && isDeviceLost() == false )
        {
            _transferFenceQueue.retireDone(
                [this]( const VulkanTransferJob& job )
            {
                const VkResult status = vkGetFenceStatus( _device, job._fence );
                if ( status == VK_ERROR_DEVICE_LOST )
                    reportQueueResult( static_cast<int32>( status ), "retireFinishedTransfers: vkGetFenceStatus" );
                return status == VK_SUCCESS;
            },
                [this]( VulkanTransferJob& job )
            { recycleTransferJob( job ); } );
        }
        // 워터마크가 지난 전송을 기다리던 텍스처 해제를 프레임 해제 큐로 넘긴다(그 다음 프레임 펜스 뒤에 놓인다).
        _transferReleaseQueue.tickCompleted( _transferSerials.getWatermark() );
        // 할당기가 단독 구간을 돌려받았으면 그 일회성 버퍼를 지운다(GL 과 같은 규칙, 호스트 가시 메모리를 링 하나 크기 넘게 쥐고 있지 않는다).
        destroyIdleSoloBuffer( _uploadRing );
        destroyIdleSoloBuffer( _readbackRing );
    }

    RHIBufferHandle VulkanRHIDevice::bindlessSourceBufferAt( RHIDescriptorIndex index ) const
    {
        // 락이 없다 — 표는 크기가 고정이고 등록 · 해제가 배타 락 아래에서 release 로 쓴다(`_arrBufferReadSlot`).
        return index < kBindlessBufferCapacity ? _arrBufferReadSlot.get()[index].load( std::memory_order_acquire ) : 0;
    }

    void VulkanRHIDevice::publishBufferReadSlot( RHIDescriptorIndex index, bool bUAV, RHIBufferHandle buffer )
    {
        if ( index >= kBindlessBufferCapacity )
            SW_FATAL( "Vulkan: bindless buffer index %# is outside the read table (%#)", index, kBindlessBufferCapacity );
        ( bUAV ? _arrUAVBufferReadSlot : _arrBufferReadSlot ).get()[index].store( buffer, std::memory_order_release );
    }

    bool VulkanRHIDevice::isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        if ( space == RHIBindlessIndexSpace::Buffer )
            return _bufferRegistry.isLive( index );
        // 해제 대기 칸은 살아 있지 않다 — 원소가 아직 옛 이미지를 가리키지만 그 이미지는 같은 펜스 뒤에 파괴된다.
        return _textureRegistry.isLive( index );
    }

    RHIBufferHandle VulkanRHIDevice::uavSourceBufferAt( RHIDescriptorIndex index ) const
    {
        return index < kBindlessBufferCapacity ? _arrUAVBufferReadSlot.get()[index].load( std::memory_order_acquire ) : 0;
    }

    uint32 VulkanRHIDevice::getAllShaderStages() const
    {
        return VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT | ( _bGeometryShader != 0 ? VK_SHADER_STAGE_GEOMETRY_BIT : 0u ) |
               ( _bTessellationShader != 0 ? VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT : 0u );
    }

    // 길이 유지(긴 함수 정리 기준): 세트 레이아웃 · 파이프라인 레이아웃 · 풀 · 세트 생성의 순서가 곧 의미라 나누지 않는다.
    bool VulkanRHIDevice::createDescriptorResources()
    {
        if ( _device == VK_NULL_HANDLE )
            return false;

        namespace vk                       = shaderslot::vk;
        namespace bindless                 = shaderslot::bindless;
        const VkShaderStageFlags allStages = getAllShaderStages();

        // 1) 정적 샘플러 세트 s0..s7 (binding_slots.hlsli 4). set 1 binding SW_VK_SAMPLER_BINDING 의 immutable sampler 배열(0..6)
        //    + binding SW_VK_SHADOW_SAMPLER_BINDING 의 비교 샘플러(7). DX12 의 정적 샘플러와 같은 표.
        {
            struct SamplerSpec
            {
                VkFilter             _filter;
                VkSamplerAddressMode _address;
                VkBool32             _bCompare;
                float32              _anisotropy;
            };
            const SamplerSpec arrSpec[shaderslot::kStaticSamplerCount] = {
                { VK_FILTER_LINEAR,          VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_FALSE, 1.0f}, // LINEAR_WRAP
                { VK_FILTER_LINEAR,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FALSE, 1.0f}, // LINEAR_CLAMP
                {VK_FILTER_NEAREST,          VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_FALSE, 1.0f}, // POINT_WRAP
                {VK_FILTER_NEAREST,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FALSE, 1.0f}, // POINT_CLAMP
                { VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, VK_FALSE, 1.0f}, // LINEAR_MIRROR
                { VK_FILTER_LINEAR,          VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_FALSE, 8.0f}, // ANISO_WRAP
                {VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_FALSE, 1.0f}, // POINT_BORDER
                { VK_FILTER_LINEAR,   VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,  VK_TRUE, 1.0f}, // SHADOW_CMP
            };
            for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerCount; ++samplerIndex )
            {
                VkSamplerCreateInfo samplerInfo{};
                samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter    = arrSpec[samplerIndex]._filter;
                samplerInfo.minFilter    = arrSpec[samplerIndex]._filter;
                samplerInfo.mipmapMode   = arrSpec[samplerIndex]._filter == VK_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
                samplerInfo.addressModeU = arrSpec[samplerIndex]._address;
                samplerInfo.addressModeV = arrSpec[samplerIndex]._address;
                samplerInfo.addressModeW = arrSpec[samplerIndex]._address;
                // 기능이 꺼져 있으면(samplerAnisotropy 미지원) 이방성만 끈다. 세트 생성 자체가 실패하면 아무것도 못 그린다.
                const bool bAniso            = arrSpec[samplerIndex]._anisotropy > 1.0f && _bSamplerAnisotropy != 0;
                samplerInfo.anisotropyEnable = bAniso ? VK_TRUE : VK_FALSE;
                samplerInfo.maxAnisotropy    = bAniso ? arrSpec[samplerIndex]._anisotropy : 1.0f;
                samplerInfo.compareEnable    = arrSpec[samplerIndex]._bCompare;
                samplerInfo.compareOp        = VK_COMPARE_OP_LESS_OR_EQUAL;
                samplerInfo.borderColor      = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                samplerInfo.maxLod           = 1000.0f;
                if ( vkCreateSampler( _device, &samplerInfo, nullptr, &_arrStaticSampler[samplerIndex] ) != VK_SUCCESS )
                    return false;
            }
        }

        // 2) set 0 = 슬롯 세트. binding 0..15 = b# (UBO), 16..31 = t# (SSBO), 32..47 = u# (SSBO). 모두 partially-bound 라
        //    드로우마다 실제로 쓰인 슬롯만 쓴다(셰이더가 정적으로 참조하는 슬롯은 반드시 걸려 있어야 한다: b0/b1 은 더미로 채운다).
        {
            VkDescriptorSetLayoutBinding arrBinding[vk::kSlotBindingCount]{};
            VkDescriptorBindingFlags     arrFlags[vk::kSlotBindingCount]{};
            for ( uint32 bindingIndex = 0; bindingIndex < vk::kSlotBindingCount; ++bindingIndex )
            {
                arrBinding[bindingIndex].binding         = bindingIndex;
                arrBinding[bindingIndex].descriptorType  = ( bindingIndex < vk::kTShift ) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                arrBinding[bindingIndex].descriptorCount = 1;
                arrBinding[bindingIndex].stageFlags      = allStages;
                arrFlags[bindingIndex]                   = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
            }
            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
            bindingFlagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount  = vk::kSlotBindingCount;
            bindingFlagsInfo.pBindingFlags = arrFlags;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext        = &bindingFlagsInfo;
            layoutInfo.bindingCount = vk::kSlotBindingCount;
            layoutInfo.pBindings    = arrBinding;
            if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_slotSetLayout ) != VK_SUCCESS )
            {
                SW_LOG_ERROR_ID( "Vulkan.FailedCreateSlotSet", "Failed to create the slot set layout (descriptor indexing / partially-bound unsupported?)." );
                return false;
            }
        }

        // 3) set 1 = 텍스처 배열(binding 0, update-after-bind) + immutable sampler 배열(binding 1) + 비교 샘플러(binding 2)
        //    + RW 텍스처 배열(binding 3, STORAGE_IMAGE, update-after-bind).
        {
            constexpr uint32             kBindingCount = 4;
            VkDescriptorSetLayoutBinding arrBinding[kBindingCount]{};
            arrBinding[0].binding            = bindless::kVkTextureBinding;
            arrBinding[0].descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            arrBinding[0].descriptorCount    = kBindlessTextureCount;
            arrBinding[0].stageFlags         = allStages;
            arrBinding[1].binding            = bindless::kVkSamplerBinding;
            arrBinding[1].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
            arrBinding[1].descriptorCount    = shaderslot::kStaticSamplerArrayCount;
            arrBinding[1].stageFlags         = allStages;
            arrBinding[1].pImmutableSamplers = _arrStaticSampler;
            arrBinding[2].binding            = bindless::kVkShadowSamplerBinding;
            arrBinding[2].descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLER;
            arrBinding[2].descriptorCount    = 1;
            arrBinding[2].stageFlags         = allStages;
            arrBinding[2].pImmutableSamplers = &_arrStaticSampler[shaderslot::kSamplerShadowCmp];
            arrBinding[3].binding            = bindless::kVkRwTextureBinding;
            arrBinding[3].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            arrBinding[3].descriptorCount    = kBindlessStorageImageCount;
            arrBinding[3].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorBindingFlags arrFlags[kBindingCount] = {
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, 0, 0,
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT };
            VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
            bindingFlagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
            bindingFlagsInfo.bindingCount  = kBindingCount;
            bindingFlagsInfo.pBindingFlags = arrFlags;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.pNext        = &bindingFlagsInfo;
            layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
            layoutInfo.bindingCount = kBindingCount;
            layoutInfo.pBindings    = arrBinding;
            if ( vkCreateDescriptorSetLayout( _device, &layoutInfo, nullptr, &_textureSetLayout ) != VK_SUCCESS )
            {
                SW_LOG_ERROR_ID( "Vulkan.FailedCreateTextureArray", "Failed to create the texture array set layout (update-after-bind unsupported?)." );
                return false;
            }
        }

        // 4) 파이프라인 레이아웃: set 0 슬롯, set 1 텍스처 배열, 푸시 상수(setComputeRootConstants, 16 dword).
        {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = allStages;
            pushRange.offset     = 0;
            pushRange.size       = shaderslot::kRootConstantDwords * sizeof( uint32 );

            VkDescriptorSetLayout arrSetLayout[2] = { _slotSetLayout, _textureSetLayout };

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount         = 2;
            pipelineLayoutInfo.pSetLayouts            = arrSetLayout;
            pipelineLayoutInfo.pushConstantRangeCount = 1;
            pipelineLayoutInfo.pPushConstantRanges    = &pushRange;
            if ( vkCreatePipelineLayout( _device, &pipelineLayoutInfo, nullptr, &_pipelineLayout ) != VK_SUCCESS )
                return false;
        }

        // 5) 풀: 텍스처 세트용 하나(update-after-bind), 슬롯 세트용은 프레임 링마다 하나(beginFrame 이 통째로 리셋).
        {
            VkDescriptorPoolSize arrPoolSize[] = {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,           kBindlessTextureCount},
                {               VK_DESCRIPTOR_TYPE_SAMPLER, shaderslot::kStaticSamplerCount},
                {         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,      kBindlessStorageImageCount},
            };
            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            poolInfo.maxSets       = 1;
            poolInfo.poolSizeCount = static_cast<uint32>( sizeof( arrPoolSize ) / sizeof( arrPoolSize[0] ) );
            poolInfo.pPoolSizes    = arrPoolSize;
            if ( vkCreateDescriptorPool( _device, &poolInfo, nullptr, &_descriptorPool ) != VK_SUCCESS )
                return false;
            // 디스크립터 풀의 크기는 Vulkan 이 알려 주지 않는다. 0 으로 숨기지 않고 "크기 모름" 칸에서 센다.
            getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( _descriptorPool ), RHIMemoryKind::Descriptor, kRHIMemoryUnknownBytes );
        }
        for ( VulkanDescriptorPoolSet& poolSet : _arrFrameDescriptorPoolSet )
        {
            const VkDescriptorPool pool = createSlotPool();
            if ( pool == VK_NULL_HANDLE )
                return false;
            poolSet._listPool.push_back( pool );
            poolSet._cursor = 0;
        }

        // 6) 셰이더가 정적으로 참조하지만 엔진이 안 건 b# 슬롯(픽스처의 MaterialCB 등)이 가리킬 0 채운 더미 UBO.
        {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size        = kDummyUboSize;
            bufferInfo.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if ( vkCreateBuffer( _device, &bufferInfo, nullptr, &_dummyUBO ) != VK_SUCCESS )
                return false;
            VkMemoryRequirements memoryRequirements{};
            vkGetBufferMemoryRequirements( _device, _dummyUBO, &memoryRequirements );
            uint32 memoryTypeIndex{ 0 };
            if ( findMemoryType( memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryTypeIndex ) == false )
                return false;
            VkMemoryAllocateInfo allocMem{};
            allocMem.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocMem.allocationSize  = memoryRequirements.size;
            allocMem.memoryTypeIndex = memoryTypeIndex;
            if ( vkAllocateMemory( _device, &allocMem, nullptr, &_dummyUBOMemory ) != VK_SUCCESS )
                return false;
            if ( isVkSuccess( vkBindBufferMemory( _device, _dummyUBO, _dummyUBOMemory, 0 ), "vkBindBufferMemory (dummy UBO)" ) == false )
                return false;
            // 빈 슬롯이 읽는 값이다 — 0 으로 채우지 못하면 쓰레기를 읽으므로 초기화를 실패시킨다.
            void* pMapped{ nullptr };
            if ( isVkSuccess( vkMapMemory( _device, _dummyUBOMemory, 0, kDummyUboSize, 0, &pMapped ), "vkMapMemory (dummy UBO)" ) == false || pMapped == nullptr )
                return false;
            Memory::set( pMapped, 0, static_cast<size_t>( kDummyUboSize ) );
            vkUnmapMemory( _device, _dummyUBOMemory );
        }

        return true;
    }

    bool VulkanRHIDevice::ensureTextureSet()
    {
        if ( _textureSet != VK_NULL_HANDLE )
            return true;
        const bool bDescriptorObjectsMissing = _device == VK_NULL_HANDLE || _descriptorPool == VK_NULL_HANDLE || _defaultSampler == VK_NULL_HANDLE || _textureSetLayout == VK_NULL_HANDLE;
        if ( bDescriptorObjectsMissing )
            return false;

        // 1x1 더미 이미지. 안 쓰는 텍스처 원소가 유효하도록 한다.
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

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements( _device, _bindlessDummyImage, &memoryRequirements );
        uint32 memoryTypeIndex{ 0 };
        if ( findMemoryType( memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex ) == false )
        {
            SW_LOG_ERROR_ID( "Vulkan.FailedFindDeviceLocal", "Failed to find a device local memory type for the bindless dummy image." );
            return false;
        }

        VkMemoryAllocateInfo allocMem{};
        allocMem.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocMem.allocationSize  = memoryRequirements.size;
        allocMem.memoryTypeIndex = memoryTypeIndex;
        if ( vkAllocateMemory( _device, &allocMem, nullptr, &_bindlessDummyMemory ) != VK_SUCCESS )
            return false;
        if ( isVkSuccess( vkBindImageMemory( _device, _bindlessDummyImage, _bindlessDummyMemory, 0 ), "vkBindImageMemory (bindless dummy image)" ) == false )
            return false;

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
        allocInfo.pSetLayouts        = &_textureSetLayout;
        if ( vkAllocateDescriptorSets( _device, &allocInfo, &_textureSet ) != VK_SUCCESS )
        {
            SW_LOG_ERROR_ID( "Vulkan.FailedAllocateTextureArray", "Failed to allocate the texture array set." );
            return false;
        }

        vector<VkDescriptorImageInfo> listImage( kBindlessTextureCount );
        for ( VkDescriptorImageInfo& info : listImage )
        {
            info.sampler     = _defaultSampler;
            info.imageView   = _bindlessDummyView;
            info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _textureSet;
        write.dstBinding      = shaderslot::bindless::kVkTextureBinding;
        write.dstArrayElement = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = kBindlessTextureCount;
        write.pImageInfo      = listImage.data();
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
        SW_LOG_INFO_ID( "Vulkan.BindlessTextureArrayReady", "Bindless texture array ready (%# slots).", kBindlessTextureCount );
        return true;
    }

    void VulkanRHIDevice::writeBindlessTextureSlot( RHIDescriptorIndex index, VkImageView view, uint32 imageLayout )
    {
        const bool bInvalidTextureSlotWrite = _textureSet == VK_NULL_HANDLE || view == VK_NULL_HANDLE || index >= kBindlessTextureCount;
        if ( bInvalidTextureSlotWrite )
            return;

        // 레이아웃은 샘플 시점에 이미지가 실제로 있을 레이아웃과 같아야 한다. 컬러는 SHADER_READ_ONLY,
        // 깊이는 prepareTextureForShaderRead 가 옮기는 DEPTH_STENCIL_READ_ONLY (부르는 쪽이 고른다).
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler     = _defaultSampler;
        imageInfo.imageView   = view;
        imageInfo.imageLayout = static_cast<VkImageLayout>( imageLayout );

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _textureSet;
        write.dstBinding      = shaderslot::bindless::kVkTextureBinding;
        write.dstArrayElement = index;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo      = &imageInfo;
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
    }

    void VulkanRHIDevice::writeBindlessStorageImageSlot( RHIDescriptorIndex index, VkImageView view )
    {
        const bool bInvalidStorageImageSlotWrite = _textureSet == VK_NULL_HANDLE || view == VK_NULL_HANDLE || index >= kBindlessStorageImageCount;
        if ( bInvalidStorageImageSlotWrite )
            return;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView   = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // 스토리지 이미지는 GENERAL. prepareTextureForUnorderedAccess 가 맞춘다

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = _textureSet;
        write.dstBinding      = shaderslot::bindless::kVkRwTextureBinding;
        write.dstArrayElement = index;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        write.descriptorCount = 1;
        write.pImageInfo      = &imageInfo;
        vkUpdateDescriptorSets( _device, 1, &write, 0, nullptr );
    }

    VkDescriptorPool VulkanRHIDevice::createSlotPool()
    {
        // 세트 하나가 b/t/u 슬롯을 모두 걸 수 있게 잡는다. 종류별로 덜 잡으면 컴퓨트 패스가 많을 때 maxSets 전에 바닥난다.
        VkDescriptorPoolSize arrPoolSize[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,                             kSlotSetsPerPool * shaderslot::kConstantBufferSlotCount},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSlotSetsPerPool * ( shaderslot::kSRVSlotCount + shaderslot::kComputeUAVSlotCount )},
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets       = kSlotSetsPerPool;
        poolInfo.poolSizeCount = static_cast<uint32>( sizeof( arrPoolSize ) / sizeof( arrPoolSize[0] ) );
        poolInfo.pPoolSizes    = arrPoolSize;
        VkDescriptorPool pool{ VK_NULL_HANDLE };
        if ( vkCreateDescriptorPool( _device, &poolInfo, nullptr, &pool ) != VK_SUCCESS )
            return VK_NULL_HANDLE;
        // 디스크립터 풀의 크기는 Vulkan 이 알려 주지 않는다. 0 으로 숨기지 않고 "크기 모름" 칸에서 센다. 내리는 자리는 destroyDescriptorPoolSet 하나다.
        getMemoryTracker().recordAllocation( RHIMemoryKey::makeDeviceObject( pool ), RHIMemoryKind::Descriptor, kRHIMemoryUnknownBytes );
        return pool;
    }

    VkDescriptorSet VulkanRHIDevice::allocateSlotSet( VulkanDescriptorPoolSet& poolSet )
    {
        // 락이 없다. 풀 묶음은 커맨드 버퍼 하나의 것이고, 그 버퍼는 한 스레드만 기록한다(VkDescriptorPool 은 외부 동기화 대상).
        if ( _slotSetLayout == VK_NULL_HANDLE )
            return VK_NULL_HANDLE;

        vector<VkDescriptorPool>& listPool = poolSet._listPool;
        uint32&                   cursor   = poolSet._cursor;
        while ( true )
        {
            if ( cursor >= listPool.size() )
            {
                // 언리얼처럼 풀이 차면 하나 더 만든다 — 상한을 두지 않는다. 묶음은 커맨드 버퍼 하나가 한 번 기록하는 동안 쓴 세트 수만큼 자라고
                // (바인딩이 바뀌는 드로우 · 디스패치마다 하나), 만든 풀은 묶음이 사는 동안 유지된다(리셋만 한다). 상한을 두면 넘친 드로우가
                // 앞 세트로 조용히 그려진다. 풀을 더 만들지 못하는 것은 장치 메모리 부족이라 그릴 길이 없어 멈춘다.
                const VkDescriptorPool pool = createSlotPool();
                if ( pool == VK_NULL_HANDLE )
                    SW_FATAL( "Could not create another slot set descriptor pool (%# pools x %# sets in use) - out of device memory", static_cast<uint32>( listPool.size() ), kSlotSetsPerPool );
                listPool.push_back( pool );
            }

            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool     = listPool[cursor];
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts        = &_slotSetLayout;
            VkDescriptorSet set{ VK_NULL_HANDLE };
            const VkResult  result = vkAllocateDescriptorSets( _device, &allocInfo, &set );
            if ( result == VK_SUCCESS )
                return set;
            // 풀이 찬 것(다음 풀로)만 정상 응답이다. 그 밖의 실패(장치 메모리 부족)는 세트 없이 그리면 앞 세트로 그려지므로 멈춘다.
            if ( result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL )
                requireVk( result, "vkAllocateDescriptorSets (slot set)" );
            ++cursor; // 이 풀은 찼다. 다음 풀로
        }
    }

    void VulkanRHIDevice::resetDescriptorPoolSet( VulkanDescriptorPoolSet& poolSet )
    {
        if ( _device == VK_NULL_HANDLE )
            return;
        for ( VkDescriptorPool pool : poolSet._listPool )
        {
            if ( pool != VK_NULL_HANDLE )
                requireVk( vkResetDescriptorPool( _device, pool, 0 ), "vkResetDescriptorPool" ); // 사양상 늘 VK_SUCCESS — 어기면 드라이버 결함이라 멈춘다
        }
        poolSet._cursor = 0;
    }

    void VulkanRHIDevice::destroyDescriptorPoolSet( VulkanDescriptorPoolSet& poolSet )
    {
        if ( _device != VK_NULL_HANDLE )
        {
            for ( VkDescriptorPool pool : poolSet._listPool )
            {
                if ( pool == VK_NULL_HANDLE )
                    continue;
                getMemoryTracker().recordFree( RHIMemoryKey::makeDeviceObject( pool ) );
                vkDestroyDescriptorPool( _device, pool, nullptr );
            }
        }
        poolSet._listPool.clear();
        poolSet._cursor = 0;
    }

    void VulkanRHIDevice::transitionTextureLayout( VkCommandBuffer cmd, VulkanTextureRecord& record,
                                                   uint32 targetLayout, uint32 aspect )
    {
        if ( cmd == VK_NULL_HANDLE || record._image == VK_NULL_HANDLE )
            return;

        // 확인과 기록이 비교 교환 하나다 — 같은 레벨의 두 워커가 같은 이미지를 옮기면 이긴 쪽 하나만 배리어를 쏜다(`RHITrackedState`).
        uint32 layoutBefore{ 0 };
        if ( record._layout.transitionTo( targetLayout, layoutBefore ) == false )
            return;

        reportBarrierDuringRecording( "transitionTextureLayout" );

        transitionImageLayout( cmd, record._image, layoutBefore, targetLayout, aspect );
        // 프레임 밖에서 기록한 리스트는 제출되지 않을 수 있다(시작되지 못한 프레임). 리스트마다 처음 바꾸는 이미지의 옛 레이아웃을 적어 두어 다음 프레임이 되돌린다.
        if ( _bFrameStarted == SW_FALSE )
        {
            ScopedLock<mutex> lock{ _droppedLayoutMutex };
            bool              bKnown{ false };
            for ( const VulkanPendingLayout& entry : _listDroppedFrameLayout )
            {
                bKnown = bKnown || ( entry._cmd == cmd && entry._image == record._image );
            }
            if ( bKnown == false )
                _listDroppedFrameLayout.push_back( VulkanPendingLayout{ cmd, record._image, layoutBefore } );
        }
    }

    void VulkanRHIDevice::beginImageAlias( VkCommandBuffer cmd, VulkanTextureRecord& record, uint32 aspect )
    {
        if ( cmd == VK_NULL_HANDLE || record._image == VK_NULL_HANDLE )
            return;
        // 앞 앨리어스가 어느 단계에서 썼는지 이 자리는 모른다 — 모든 단계의 쓰기를 기다린다(첨부를 바꾸는 패스 경계에서 프레임에 몇 번뿐이다).
        VkMemoryBarrier memoryBarrier{};
        memoryBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        memoryBarrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

        const uint32         layout = record._layout.load();
        VkImageMemoryBarrier imageBarrier{};
        uint32               imageBarrierCount{ 0 };
        if ( layout != static_cast<uint32>( VK_IMAGE_LAYOUT_UNDEFINED ) )
        {
            imageBarrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier.srcAccessMask               = VK_ACCESS_MEMORY_WRITE_BIT;
            imageBarrier.dstAccessMask               = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            imageBarrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout                   = static_cast<VkImageLayout>( layout );
            imageBarrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image                       = record._image;
            imageBarrier.subresourceRange.aspectMask = aspect;
            imageBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
            imageBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
            imageBarrierCount                        = 1;
        }
        vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, imageBarrierCount, &imageBarrier );
    }

    void VulkanRHIDevice::restoreDroppedFrameLayouts()
    {
        ScopedLock<mutex> lock{ _droppedLayoutMutex };
        if ( _listDroppedFrameLayout.empty() )
            return;
        // 그 사이 지워진 텍스처는 찾지 못하고 넘어간다.
        _gpuTextures.forEach( [this]( VulkanTextureRecord& record )
        {
            // 같은 이미지를 여러 리스트가 바꿨으면 가장 먼저 기록된 옛 값이 원래 값이다.
            for ( const VulkanPendingLayout& entry : _listDroppedFrameLayout )
            {
                if ( entry._image == record._image )
                {
                    record._layout.store( entry._oldLayout );
                    break;
                }
            }
        } );
        _listDroppedFrameLayout.clear();
    }

    bool VulkanRHIDevice::transitionImageLayout( VkCommandBuffer cmd, VkImage image, uint32 oldLayoutU32, uint32 newLayoutU32, uint32 aspectU32 )
    {
        const VkImageLayout      oldLayout           = static_cast<VkImageLayout>( oldLayoutU32 );
        const VkImageLayout      newLayout           = static_cast<VkImageLayout>( newLayoutU32 );
        const VkImageAspectFlags aspect              = aspectU32;
        const bool               bNoLayoutTransition = cmd == VK_NULL_HANDLE || image == VK_NULL_HANDLE || oldLayout == newLayout;
        if ( bNoLayoutTransition )
            return true;

        VkImageMemoryBarrier barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                   = oldLayout;
        barrier.newLayout                   = newLayout;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = image;
        barrier.subresourceRange.aspectMask = aspect;
        // 레이아웃은 텍스처 단위로 추적하므로 모든 밉 · 면을 함께 옮긴다(면 하나만 옮기면 나머지 면이 옛 레이아웃으로 남는다).
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

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
        else if ( oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL )
        {
            // 그림자맵: 깊이 쓰기(late fragment tests) 가 끝난 뒤 프래그먼트 셰이더가 샘플한다.
            barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage              = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dstStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if ( oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL )
        {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            srcStage              = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dstStage              = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
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

    VkRenderPass VulkanRHIDevice::createRenderPassFromSpec( const VulkanRHIRenderPassCache::RenderPassSpec& spec ) const
    {
        return VulkanRHIRenderPassCache::createFromSpec( _device, spec );
    }

    bool VulkanRHIDevice::ensureOffscreenRenderPass( uint32 vkFormat )
    {
        if ( _offscreenRenderPass != VK_NULL_HANDLE )
            return true;
        if ( _device == nullptr )
            return false;

        // 공용 오프스크린 렌더패스는 계약 포맷일 때만 재사용한다. 다른 포맷은 전용 RP 를 만든다.
        const uint32 sharedFormat = static_cast<uint32>( VulkanRHIUtil::toVulkanTextureFormat( constant::render::kOffscreenColorFormat ) );
        if ( vkFormat != 0 && vkFormat != sharedFormat )
            return false;

        VulkanRHIRenderPassCache::RenderPassSpec spec{};
        spec.addColor( sharedFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        _offscreenRenderPass = createRenderPassFromSpec( spec );
        return _offscreenRenderPass != VK_NULL_HANDLE;
    }

    bool VulkanRHIDevice::createOffscreenFramebuffer( VulkanTextureRecord& record )
    {
        if ( record._imageView == VK_NULL_HANDLE || record._bRenderTarget == SW_FALSE )
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
            // 포맷별 전용 RP. beginRenderPass 가 전이를 먼저 걸어 두므로 COLOR_ATTACHMENT_OPTIMAL 에서 시작한다.
            VulkanRHIRenderPassCache::RenderPassSpec spec{};
            spec.addColor( record._format, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            record._renderPass = createRenderPassFromSpec( spec );
            if ( record._renderPass == VK_NULL_HANDLE )
                return false;
        }

        // 면마다 프레임버퍼 하나(면 뷰가 첨부). 면이 하나면 `_framebuffer` 하나다.
        const uint32 sliceCount = record._listSliceView.empty() ? 1u : static_cast<uint32>( record._listSliceView.size() );
        for ( uint32 slice = 0; slice < sliceCount; ++slice )
        {
            const VkImageView       attachment = record.findAttachmentView( slice );
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass      = record._renderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments    = &attachment;
            fbInfo.width           = record._width;
            fbInfo.height          = record._height;
            fbInfo.layers          = 1;
            VkFramebuffer framebuffer{ VK_NULL_HANDLE };
            if ( vkCreateFramebuffer( _device, &fbInfo, nullptr, &framebuffer ) != VK_SUCCESS )
            {
                destroyOffscreenFramebuffer( record );
                return false;
            }
            if ( slice == 0 )
                record._framebuffer = framebuffer;
            if ( record._listSliceView.empty() == false )
                record._listSliceFramebuffer.push_back( framebuffer );
        }
        return true;
    }

    void VulkanRHIDevice::destroyOffscreenFramebuffer( VulkanTextureRecord& record )
    {
        // 즉시 파괴하면 안 된다. 아직 실행 중인 프레임의 커맨드버퍼가 이 프레임버퍼를 참조할 수
        // 있다(게임뷰 리사이즈가 대표적인 경로다). 즉시 파괴하면 검증 레이어가 in-use 위반을 낸다.
        // 면별 프레임버퍼의 0 번은 `_framebuffer` 와 같다 — 한 번만 놓는다.
        for ( size_t slice = 1; slice < record._listSliceFramebuffer.size(); ++slice )
        {
            enqueueFramebufferRelease( record._listSliceFramebuffer[slice], VK_NULL_HANDLE );
        }
        record._listSliceFramebuffer.clear();
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
        _releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [dev, framebuffer, ownedRenderPass]()
        {
            if ( framebuffer != VK_NULL_HANDLE )
                vkDestroyFramebuffer( dev, framebuffer, nullptr );
            if ( ownedRenderPass != VK_NULL_HANDLE )
                vkDestroyRenderPass( dev, ownedRenderPass, nullptr );
        } ),
                                         _frameFenceRing.getRecordingSerial() );
    }

    VkRenderPass VulkanRHIDevice::ensurePipelineRenderPass( const RHIPipelineStateDesc& desc )
    {
        VulkanRHIRenderPassCache::PipelineKey key{};
        const bool                            bDepthOnly = ( desc._numRenderTargets == 0 && desc._renderState._bEnableDepthTest != 0 );
        key._colorCount                                  = bDepthOnly ? 0u : ( desc._numRenderTargets > 0 ? desc._numRenderTargets : 1u );
        if ( key._colorCount > kMaxColorAttachments )
            key._colorCount = kMaxColorAttachments;
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            VkFormat colorFmt = VulkanRHIUtil::toVulkanTextureFormat( desc._arrRTVFormat[colorIndex] );
            if ( colorFmt == VK_FORMAT_UNDEFINED )
                colorFmt = VK_FORMAT_R8G8B8A8_UNORM;
            key._arrColorFormat[colorIndex] = static_cast<uint32>( colorFmt );
        }
        if ( desc._renderState._bEnableDepthTest != 0 )
        {
            // FrameRenderer 는 RHI D24 를 요청하지만 GPU 가 지원하지 않을 수 있다 → 디바이스가 고른 포맷을 쓴다.
            VkFormat   depthFmt              = VulkanRHIUtil::toVulkanTextureFormat( desc._depthStencilFormat );
            const bool bUseDeviceDepthFormat = desc._depthStencilFormat == sw::RHIFormat::D24_UNORM_S8_UINT ||
                                               depthFmt == VK_FORMAT_D24_UNORM_S8_UINT || depthFmt == VK_FORMAT_UNDEFINED;
            if ( bUseDeviceDepthFormat )
                depthFmt = static_cast<VkFormat>( _depthFormat );
            key._depthFormat = static_cast<uint32>( depthFmt );
        }

        // PSO 호환용. 파이프라인은 이 RP 와 "호환되는" RP 어디에서든 쓰인다(포맷 · 개수 · 샘플 수만 같으면 된다).
        VulkanRHIRenderPassCache::RenderPassSpec spec{};
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            spec.addColor( key._arrColorFormat[colorIndex], VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
        }
        if ( key._depthFormat != 0 )
            spec.setDepth( key._depthFormat, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );

        return _renderPassCache.ensurePipelineRenderPass( _device, key, spec );
    }

    bool VulkanRHIDevice::ensureCompositeFramebuffer( const VulkanRHIRenderPassCache::CompositeKey& key,
                                                      VulkanRHIRenderPassCache::CompositeRecord&    outRecord )
    {
        // 첨부 뷰와 서술은 여기서 풀고, 조회 · 생성은 캐시가 한 임계구역에서 한다.
        VkImageView arrFbAttachment[kMaxColorAttachments + 1]{};
        uint32      width{ 0 };
        uint32      height{ 0 };
        // beginRenderPass 가 첨부를 미리 COLOR_ATTACHMENT / DEPTH_STENCIL_ATTACHMENT 로 전이해 두므로 그 레이아웃에서 시작·종료한다.
        VulkanRHIRenderPassCache::RenderPassSpec spec{};
        for ( uint32 colorIndex = 0; colorIndex < key._colorCount; ++colorIndex )
        {
            VulkanTextureRecord* pTex                    = resolveTexture( key._arrColor[colorIndex] );
            const bool           bInvalidColorAttachment = pTex == nullptr || pTex->findAttachmentView( key._arrColorSlice[colorIndex] ) == VK_NULL_HANDLE || pTex->_bDepthStencil != SW_FALSE;
            if ( bInvalidColorAttachment )
                return false;
            arrFbAttachment[colorIndex] = pTex->findAttachmentView( key._arrColorSlice[colorIndex] );
            width                       = pTex->_width;
            height                      = pTex->_height;
            spec.addColor( pTex->_format, VulkanRHIUtil::toVulkanLoadOp( static_cast<RHIRenderPassLoadOp>( key._arrColorLoadOp[colorIndex] ) ),
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
            spec._arrColorStoreOp[colorIndex] = VulkanRHIUtil::toVulkanStoreOp( static_cast<RHIRenderPassStoreOp>( key._arrColorStoreOp[colorIndex] ) );
        }

        uint32 attachCount = key._colorCount;
        if ( key._depth != 0 )
        {
            VulkanTextureRecord* pTex                    = resolveTexture( key._depth );
            const bool           bInvalidDepthAttachment = pTex == nullptr || pTex->findAttachmentView( key._depthSlice ) == VK_NULL_HANDLE || pTex->_bDepthStencil == SW_FALSE;
            if ( bInvalidDepthAttachment )
                return false;
            arrFbAttachment[attachCount] = pTex->findAttachmentView( key._depthSlice );
            ++attachCount;
            if ( width == 0 )
            {
                width  = pTex->_width;
                height = pTex->_height;
            }
            spec.setDepth( pTex->_format, VulkanRHIUtil::toVulkanLoadOp( static_cast<RHIRenderPassLoadOp>( key._depthLoadOp ) ),
                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
            spec._depthStoreOp = VulkanRHIUtil::toVulkanStoreOp( static_cast<RHIRenderPassStoreOp>( key._depthStoreOp ) );
        }
        if ( attachCount == 0 )
            return false;

        return _renderPassCache.ensureComposite( _device, key, spec, arrFbAttachment, attachCount, width, height, outRecord );
    }

    void VulkanRHIDevice::destroyCompositeFramebuffersUsing( RHITextureHandle texture )
    {
        if ( texture == 0 || _device == nullptr )
            return;
        vector<VulkanRHIRenderPassCache::CompositeRecord> listDetached;
        _renderPassCache.detachCompositesUsing( texture, listDetached );
        for ( const VulkanRHIRenderPassCache::CompositeRecord& record : listDetached )
        {
            // 실행 중인 커맨드버퍼가 아직 참조할 수 있으므로 펜스 통과 후에 파괴한다.
            enqueueFramebufferRelease( record._framebuffer, record._renderPass );
        }
    }
} // namespace sw
