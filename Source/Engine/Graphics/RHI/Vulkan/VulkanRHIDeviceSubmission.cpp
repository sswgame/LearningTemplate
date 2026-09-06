#include "pch.h"

#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    void VulkanRHIDevice::waitIdle()
    {
        if ( _device )
            vkDeviceWaitIdle( _device );
        _releaseQueue.flushAll();
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
        if ( _swapChain.isValid() == false || _listInFlightFence.empty() )
            return;

        vkWaitForFences( _device, 1, &_listInFlightFence[_currentFrame], VK_TRUE, UINT64_MAX );
        // 이 링 슬롯의 펜스가 신호됐다는 건 그 슬롯에 마지막으로 제출한 세대(_listRingFrameNumber)의
        // GPU 작업이 실제로 끝났다는 뜻이다 — 그 세대 이하로 태그된 리소스 해제를 지금 실행한다.
        _releaseQueue.tickCompleted( _listRingFrameNumber[_currentFrame] );

        VulkanSwapChainStatus status = _swapChain.acquireNextImage( _device, _currentFrame );
        if ( status == VulkanSwapChainStatus::OutOfDate || status == VulkanSwapChainStatus::Suboptimal )
        {
            recreateSwapChain();
            if ( _swapChain.isValid() == false )
                return;

            status = _swapChain.acquireNextImage( _device, _currentFrame );
        }

        // Suboptimal 은 "창과 어긋났지만 이번 프레임은 그릴 수 있다" — 위에서 이미 한 번 다시
        // 만들어 봤으므로 그대로 진행한다.
        if ( status != VulkanSwapChainStatus::Success && status != VulkanSwapChainStatus::Suboptimal )
            return;

        // 이 이미지를 마지막으로 쓴 프레임이 아직 GPU 에 있으면 그 펜스를 기다린 뒤에 덮어쓴다.
        const uint32 imageIndex = _swapChain.getImageIndex();
        if ( imageIndex >= _listImagesInFlight.size() )
            return;
        if ( _listImagesInFlight[imageIndex] != VK_NULL_HANDLE )
            vkWaitForFences( _device, 1, &_listImagesInFlight[imageIndex], VK_TRUE, UINT64_MAX );
        _listImagesInFlight[imageIndex] = _listInFlightFence[_currentFrame];

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
        VkSemaphore          arrWaitSemaphore[] = { _swapChain.getImageAvailableSemaphore( _currentFrame ) };
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

        VkSemaphore arrSignalSemaphore[] = { _swapChain.getRenderFinishedSemaphore() };
        submitInfo.signalSemaphoreCount  = 1;
        submitInfo.pSignalSemaphores     = arrSignalSemaphore;

        // 이 제출에 새 세대 번호를 매긴다 — 이번 프레임 기록 중 등록된 지연 해제(enqueueGpuRelease)는
        // 이 세대가 실제로 끝났다고 확인될 때까지(beginFrame의 tickCompleted) 보류된다.
        _listRingFrameNumber[_currentFrame] = ++_frameFenceCounter;
        vkQueueSubmit( _graphicsQueue, 1, &submitInfo, _listInFlightFence[_currentFrame] );

        if ( bPresent )
        {
            const VulkanSwapChainStatus presentStatus = _swapChain.present( _graphicsQueue );
            if ( presentStatus == VulkanSwapChainStatus::OutOfDate || presentStatus == VulkanSwapChainStatus::Suboptimal )
            {
                // 다음 beginFrame에서 스왑체인을 재생성합니다.
                _bSwapChainDirty = 1;
            }
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
            VkPipelineStageFlags waitStage     = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            flushInfo.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            flushInfo.commandBufferCount       = static_cast<uint32>( _listPendingSubmit.size() );
            flushInfo.pCommandBuffers          = _listPendingSubmit.data();
            const VkSemaphore acquireSemaphore = _swapChain.getImageAvailableSemaphore( _currentFrame );
            if ( _bFrameAcquireWaitPending != 0 && acquireSemaphore != VK_NULL_HANDLE )
            {
                flushInfo.waitSemaphoreCount = 1;
                flushInfo.pWaitSemaphores    = &acquireSemaphore;
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
} // namespace sw
