#include "pch.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Graphics/RHI/Support/RHITimestamp.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanOneShotCommands.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDevice.h"
#include "Engine/Graphics/RHI/Vulkan/VulkanRHIDeviceInternal.h"

namespace sw
{
    SW_LOG_CALLER( "Vulkan" );

    namespace
    {
        struct VulkanRHIDeviceSubmissionInternal
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
    } // namespace

    void VulkanRHIDevice::reportQueueResult( int32 result, const utf8* pWhere )
    {
        const VkResult vkResult = static_cast<VkResult>( result );
        if ( vkResult == VK_SUCCESS )
            return;
        // 장치 손실은 처음 본 쪽만 알린다. 그 뒤의 호출은 모두 같은 값을 돌려주므로 프레임마다 로그가 쌓인다.
        const bool bDeviceLost = vkResult == VK_ERROR_DEVICE_LOST;
        if ( bDeviceLost && _transferSerials.markLost() == false )
            return;

        fixed_string<constant::kMaxBuffer64> lastMarker;
        {
            std::scoped_lock<mutex> lock{ _submittedMarkerMutex };
            lastMarker = _lastSubmittedEventMarker;
        }
        SW_LOG_ERROR( "%#: VkResult=%# (%#) — last pass put in the submission order: '%#'", pWhere != nullptr ? pWhere : "<queue>", result,
                      VulkanRHIDeviceSubmissionInternal::getResultName( vkResult ), lastMarker.empty() ? "<none>" : lastMarker.c_str() );
        if ( bDeviceLost == false )
            return;
        SW_LOG_ERROR( "Vulkan device lost - submits, waits and presents stop; frames are skipped until shutdown" );
        reportDeviceFault();
    }

    bool VulkanRHIDevice::debugRemoveDevice()
    {
        if ( _device == VK_NULL_HANDLE )
            return false;
        SW_LOG_WARNING( "Marking the Vulkan device lost on request (rhi.removeDevice) - Vulkan cannot remove a device, so this is the state after the first "
                        "VK_ERROR_DEVICE_LOST: no more submits, waits or presents" );
        _transferSerials.markLost(); // 이미 잃은 장치여도 같은 상태다
        return true;
    }

    void VulkanRHIDevice::noteSubmittedEventMarker( const utf8* pMarker )
    {
        if ( pMarker == nullptr || pMarker[0] == '\0' )
            return;
        std::scoped_lock<mutex> lock{ _submittedMarkerMutex };
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
            SW_LOG_ERROR( "Device lost: VK_EXT_device_fault is not enabled on this device — no fault address or vendor code to report" );
            return;
        }

        VkDeviceFaultCountsEXT counts{};
        counts.sType               = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
        const VkResult countResult = pfnGetFaultInfo( _device, &counts, nullptr );
        if ( countResult != VK_SUCCESS && countResult != VK_INCOMPLETE )
        {
            SW_LOG_ERROR( "Device lost: vkGetDeviceFaultInfoEXT failed to count the fault records (VkResult=%#)", static_cast<int32>( countResult ) );
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
            SW_LOG_ERROR( "Device lost: vkGetDeviceFaultInfoEXT failed (VkResult=%#)", static_cast<int32>( infoResult ) );
            return;
        }

        SW_LOG_ERROR( "Device lost (VK_EXT_device_fault): '%#' — %# address record(s), %# vendor record(s)", info.description, counts.addressInfoCount,
                      counts.vendorInfoCount );
        for ( uint32 index = 0; index < counts.addressInfoCount && index < listAddressInfo.size(); ++index )
        {
            const VkDeviceFaultAddressInfoEXT& address = listAddressInfo[index];
            SW_LOG_ERROR( "  fault address %#: %# at 0x%# (precision %# bytes)", index,
                          VulkanRHIDeviceSubmissionInternal::getFaultAddressTypeName( address.addressType ),
                          Fmt( static_cast<uint64>( address.reportedAddress ), Format( 16, Format::Padding::Zero ).hexUpper() ),
                          static_cast<uint64>( address.addressPrecision ) );
        }
        for ( uint32 index = 0; index < counts.vendorInfoCount && index < listVendorInfo.size(); ++index )
        {
            const VkDeviceFaultVendorInfoEXT& vendor = listVendorInfo[index];
            SW_LOG_ERROR( "  vendor record %#: '%#' code %# data %#", index, vendor.description, static_cast<uint64>( vendor.vendorFaultCode ),
                          static_cast<uint64>( vendor.vendorFaultData ) );
        }
    }

    void VulkanRHIDevice::waitIdleInternal()
    {
        // 잃은 장치는 기다리지 않는다 — 끝나지 않을 일을 기다리게 되거나(TDR 중의 드라이버) 같은 손실만 다시 듣는다.
        if ( _device && isDeviceLost() == false )
        {
            std::scoped_lock<mutex> queueLock{ _queueMutex };
            reportQueueResult( static_cast<int32>( vkDeviceWaitIdle( _device ) ), "waitIdle: vkDeviceWaitIdle" );
        }
        _releaseQueue.flushAll();
    }

    void VulkanRHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _frameFenceCounter + 1 );
    }

    uint32 VulkanRHIDevice::getTimestampSlotCount() const
    {
        return ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampPool != VK_NULL_HANDLE && _timestampPeriod > 0.0f )
                 ? constant::kMaxGPUTimestampSlot
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
        outGPUNanos = RHITimestamp::convertTickToNanos( tick, static_cast<float64>( properties.limits.timestampPeriod ) );
        return true;
    }

    void VulkanRHIDevice::ensureTimestampPool()
    {
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
        poolInfo.queryCount = constant::kMaxGPUTimestampSlot * constant::kMaxFrameCountInFlight;
        if ( vkCreateQueryPool( _device, &poolInfo, nullptr, &_timestampPool ) != VK_SUCCESS )
        {
            _timestampPool = VK_NULL_HANDLE;
            return;
        }
        _timestampPeriod = properties.limits.timestampPeriod;
    }

    void VulkanRHIDevice::collectTimestampsForSlot()
    {
        _timestampFrame._listMicro.clear();
        const bool bNoTimestampsToRead = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _timestampPool == VK_NULL_HANDLE || _timestampPeriod <= 0.0f ||
                                         _arrTimestampSubmitted[_currentFrame] == SW_FALSE;
        if ( bNoTimestampsToRead )
            return;

        // 칸마다 [값][가용 여부] 두 개가 온다. **가용 비트가 반드시 필요하다.** 이번 프레임에 안 쓰인
        // 슬롯은 리셋된 채로 남아 영영 준비되지 않고, 그것 하나 때문에 범위 전체가 VK_NOT_READY 가 된다.
        uint64       arrResult[constant::kMaxGPUTimestampSlot * 2]{};
        const uint32 base = getTimestampBase();
        // **기다리지 않는다**(WAIT 비트 없음). 측정하려던 파이프라인을 멈추면 숫자가 거짓이 된다.
        // 그래서 VK_NOT_READY 도 정상 응답으로 받는다(쓰인 칸의 값은 이미 채워져 있다).
        const VkResult result =
            vkGetQueryPoolResults( _device, _timestampPool, base, constant::kMaxGPUTimestampSlot, sizeof( arrResult ), arrResult,
                                   sizeof( uint64 ) * 2, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT );
        if ( result == VK_ERROR_DEVICE_LOST )
            reportQueueResult( static_cast<int32>( result ), "collectTimestampsForSlot: vkGetQueryPoolResults" );
        if ( result != VK_SUCCESS && result != VK_NOT_READY )
            return;

        // 가용 비트가 선 칸만 준비된 것이다. 값과 비트를 갈라 공통 규칙에 넘긴다.
        uint64               arrTick[constant::kMaxGPUTimestampSlot]{};
        GPUTimestampSlotMask readyMask{ 0 };
        for ( uint32 index = 0; index < constant::kMaxGPUTimestampSlot; ++index )
        {
            if ( arrResult[index * 2 + 1] == 0 )
                continue;
            arrTick[index] = arrResult[index * 2];
            readyMask |= makeGPUTimestampSlotBit( index );
        }
        RHITimestamp::resolve( arrTick, readyMask, static_cast<float64>( _timestampPeriod ), _timestampFrame );
    }

    // 길이 유지(긴 함수 정리 기준): 펜스 대기 · 이미지 획득 · 기록 시작의 프레임 순서가 곧 의미라 나누지 않는다.
    void VulkanRHIDevice::beginFrame()
    {
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

        // 재생성이 실패하면 스왑체인 · 동기화 객체가 없는 상태이므로 프레임을 건너뛰고 다음 프레임에 다시 시도한다.
        if ( _swapChain.isValid() == false || _listInFlightFence.empty() )
        {
            _bSwapChainDirty = 1;
            return;
        }

        const VkResult fenceResult = vkWaitForFences( _device, 1, &_listInFlightFence[_currentFrame], VK_TRUE, UINT64_MAX );
        reportQueueResult( static_cast<int32>( fenceResult ), "beginFrame: vkWaitForFences" );
        // **여기가 타임스탬프를 읽는 유일한 안전한 자리다.** 이 슬롯의 펜스를 방금 통과했다.
        ensureTimestampPool();
        collectTimestampsForSlot();
        // 이 링 슬롯의 펜스가 신호됐다는 것은 그 슬롯에 마지막으로 제출한 세대(_listRingFrameNumber)의
        // GPU 작업이 실제로 끝났다는 뜻이다. 그 세대 이하로 태그된 리소스 해제를 지금 실행한다.
        _releaseQueue.tickCompleted( _listRingFrameNumber[_currentFrame] );
        // 이 칸의 GPU 사용이 끝났다. 값이 바뀔 때만 쓰는 상수버퍼 가운데 이 칸이 옛 값인 것을 채운다.
        fillConstantBufferSlot();
        // 이 링 슬롯에서 프레임 스트림이 쓴 슬롯 세트들도 GPU 가 다 읽었다. 풀 묶음을 통째로 비워 이번 프레임에 다시 쓴다.
        // (리스트가 쓴 세트는 리스트 쌍의 풀 묶음에 있고, 쌍이 재사용 풀로 돌아온 뒤 beginCommandList 가 비운다.)
        resetDescriptorPoolSet( _arrFrameDescriptorPoolSet[_currentFrame] );

        // 앞 프레임이 present 하지 않았다면 그때 획득한 이미지를 아직 쥐고 있다. Vulkan 에는
        // present 말고 이미지를 돌려주는 길이 없으니, 다시 획득하지 말고 그 이미지에 덮어 그린다.
        // 매 프레임 새로 획득하면 이미지가 하나씩 소진되고, 개수를 넘기는 순간 acquire 가
        // UINT64_MAX 타임아웃으로 영원히 막힌다. endFrame(bPresent=false) 로 도는 경로에서 실제로 걸렸다.
        if ( _bSwapChainImageHeld == SW_FALSE )
        {
            VulkanSwapChainStatus status = _swapChain.acquireNextImage( _device, _currentFrame );
            if ( status == VulkanSwapChainStatus::SurfaceLost )
            {
                // 사양: 서피스를 잃으면 스왑체인만이 아니라 서피스부터 다시 만든다. 잃은 서피스로 다시 acquire 하면 계속 진다.
                noteSurfaceLost( "acquire" );
                if ( recreateSurfaceAndSwapChain() == false || _swapChain.isValid() == false )
                {
                    _bSurfaceLost = SW_TRUE;
                    return;
                }
                status = _swapChain.acquireNextImage( _device, _currentFrame );
            }
            if ( status == VulkanSwapChainStatus::DeviceLost )
            {
                reportQueueResult( static_cast<int32>( VK_ERROR_DEVICE_LOST ), "beginFrame: vkAcquireNextImageKHR" );
                return;
            }
            if ( status == VulkanSwapChainStatus::OutOfDate || status == VulkanSwapChainStatus::Suboptimal )
            {
                if ( recreateSwapChain() == false || _swapChain.isValid() == false )
                {
                    _bSwapChainDirty = 1;
                    return;
                }

                status = _swapChain.acquireNextImage( _device, _currentFrame );
            }

            // Suboptimal 은 "창과 어긋났지만 이번 프레임은 그릴 수 있다" 는 뜻이다. 위에서 이미 한 번 다시
            // 만들어 봤으므로 그대로 진행한다.
            if ( status == VulkanSwapChainStatus::DeviceLost ) // 다시 획득한 쪽에서 잃었음을 본 경우(위의 검사는 첫 획득만 본다)
                reportQueueResult( static_cast<int32>( VK_ERROR_DEVICE_LOST ), "beginFrame: vkAcquireNextImageKHR (retry)" );
            if ( status != VulkanSwapChainStatus::Success && status != VulkanSwapChainStatus::Suboptimal )
                return;

            _bSwapChainImageHeld = SW_TRUE;
            // 방금 획득한 이미지의 가용 세마포어는 이 프레임의 첫 제출이 딱 한 번 기다린다.
            // 이미지를 물려받은 프레임은 기다릴 시그널이 없으므로 이 플래그를 세우지 않는다.
            _bFrameAcquireWaitPending = SW_TRUE;
        }

        // 이 이미지를 마지막으로 쓴 프레임이 아직 GPU 에 있으면 그 펜스를 기다린 뒤에 덮어쓴다.
        const uint32 imageIndex = _swapChain.getImageIndex();
        if ( imageIndex >= _listImagesInFlight.size() )
            return;
        if ( _listImagesInFlight[imageIndex] != VK_NULL_HANDLE )
            reportQueueResult( static_cast<int32>( vkWaitForFences( _device, 1, &_listImagesInFlight[imageIndex], VK_TRUE, UINT64_MAX ) ), "beginFrame: vkWaitForFences (image in flight)" );
        _listImagesInFlight[imageIndex] = _listInFlightFence[_currentFrame];

        vkResetFences( _device, 1, &_listInFlightFence[_currentFrame] );
        vkResetCommandBuffer( _listCommandBuffer[_currentFrame], 0 );

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer( _listCommandBuffer[_currentFrame], &beginInfo );
        // Vulkan 은 **쓰기 전에 반드시 리셋**해야 한다. 안 하면 결과가 정의되지 않는다.
        if ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampPool != VK_NULL_HANDLE )
            vkCmdResetQueryPool( _listCommandBuffer[_currentFrame], _timestampPool, getTimestampBase(), constant::kMaxGPUTimestampSlot );

        // 프레임 스트림의 첫 세그먼트. 리스트가 제출될 때마다 여기서 잘리고 새 세그먼트가 열린다.
        _activeFrameBuffer  = _listCommandBuffer[_currentFrame];
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

    void VulkanRHIDevice::endFrame( bool vsync, bool bPresent )
    {
        (void)vsync;
        if ( _bFrameStarted == SW_FALSE )
        {
            // 건너뛴 프레임이다. 버려진 리스트가 앞서 바꾼 레이아웃 기록을 바로 되돌려야 이 프레임 뒤의 읽기(스크린샷)가 실제 레이아웃을 본다.
            restoreDroppedFrameLayouts();
            return;
        }

        if ( _recordingState._bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( _activeFrameBuffer );
            _recordingState._bRenderPassActive = SW_FALSE;
        }
        vkEndCommandBuffer( _activeFrameBuffer );
        _listPendingSubmit.push_back( _activeFrameBuffer );

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        // acquire 대기는 프레임당 한 번뿐이다. 즉시 모드에서 앞선 제출이 이미 소비했으면 생략한다.
        VkSemaphore          arrWaitSemaphore[] = { _swapChain.getImageAvailableSemaphore( _currentFrame ) };
        VkPipelineStageFlags arrWaitStage[]     = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        if ( _bFrameAcquireWaitPending != SW_FALSE )
        {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores    = arrWaitSemaphore;
            submitInfo.pWaitDstStageMask  = arrWaitStage;
            _bFrameAcquireWaitPending     = SW_FALSE;
        }

        // 프레임 세그먼트와 리스트 버퍼를 기록 순서 그대로 한 번에 제출한다. 같은 큐에 대한
        // 제출 순서가 곧 실행 순서라, 세그먼트 사이의 리소스 의존성이 그대로 지켜진다.
        submitInfo.commandBufferCount = static_cast<uint32>( _listPendingSubmit.size() );
        submitInfo.pCommandBuffers    = _listPendingSubmit.data();

        // renderFinished 를 기다리는 건 present 뿐이다. present 하지 않는 프레임까지 시그널하면
        // 아무도 소비하지 않은 이진 세마포어를 거듭 시그널하게 되어 규약 위반이다.
        VkSemaphore arrSignalSemaphore[] = { _swapChain.getRenderFinishedSemaphore() };
        if ( bPresent )
        {
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores    = arrSignalSemaphore;
        }

        // 이 제출에 새 세대 번호를 매긴다. 이번 프레임 기록 중 등록된 지연 해제(enqueueGPURelease)는
        // 이 세대가 실제로 끝났다고 확인될 때까지(beginFrame 의 tickCompleted) 보류된다.
        _listRingFrameNumber[_currentFrame] = ++_frameFenceCounter;
        if ( _frameStreamContext != nullptr )
            noteSubmittedEventMarker( _frameStreamContext->getLastEventMarker() );
        std::unique_lock<mutex> queueLock{ _queueMutex };
        // 기록하는 사이에 장치를 잃었으면(다른 스레드의 손실 · rhi.removeDevice) 내지 않는다. 버퍼는 위에서 닫았다.
        const bool     bDeviceLost  = isDeviceLost();
        const VkResult submitResult = bDeviceLost ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _graphicsQueue, 1, &submitInfo, _listInFlightFence[_currentFrame] );
        reportQueueResult( static_cast<int32>( submitResult ), "endFrame: vkQueueSubmit" );
        // 이 링 슬롯의 쿼리 구간은 이제 "리셋 + 제출" 을 한 번은 거쳤다. 그 전에 읽으면 미정의다.
        if ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampPool != VK_NULL_HANDLE )
            _arrTimestampSubmitted[_currentFrame] = SW_TRUE;

        if ( bPresent && isDeviceLost() == false )
        {
            const VulkanSwapChainStatus presentStatus = _swapChain.present( _graphicsQueue ); // 큐 락 안(위의 queueLock)
            if ( presentStatus == VulkanSwapChainStatus::DeviceLost )
                reportQueueResult( static_cast<int32>( VK_ERROR_DEVICE_LOST ), "endFrame: vkQueuePresentKHR" );
            else if ( presentStatus == VulkanSwapChainStatus::OutOfDate || presentStatus == VulkanSwapChainStatus::Suboptimal )
            {
                // 다음 beginFrame 에서 스왑체인을 다시 만든다.
                _bSwapChainDirty = 1;
            }
            else if ( presentStatus == VulkanSwapChainStatus::SurfaceLost )
            {
                // 다음 beginFrame 에서 서피스부터 다시 만든다(큐 락 안이라 여기서 만들지 않는다).
                noteSurfaceLost( "present" );
                _bSurfaceLost = SW_TRUE;
            }
            // 성공이든 OutOfDate 든 이미지는 프레젠테이션 엔진으로 넘어갔다. 재생성 경로에서도
            // 이미지 자체가 사라지므로, 어느 쪽이든 더 이상 쥐고 있지 않다.
            _bSwapChainImageHeld = SW_FALSE;
        }
        // 해제 콜백(아래 tickFrame)을 큐 락 안에서 부르지 않는다.
        queueLock.unlock();

        _listPendingSubmit.clear();
        _activeFrameBuffer = VK_NULL_HANDLE;
        _currentFrame      = ( _currentFrame + 1 ) % constant::kMaxFrameCountInFlight;
        _bFrameStarted     = SW_FALSE;
        _releaseQueue.tickFrame();
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
            std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
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
            std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
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
        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        _listLiveCmd.push_back( pCmdList );
    }

    void VulkanRHIDevice::unregisterCommandList( VulkanRHICommandList* pCmdList )
    {
        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
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
            std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
            _listFreeCmdListEntry.push_back( entry );
        };
        _releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, recycleCb ), _frameFenceCounter + 1 );
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

        // 프레임 밖 일회성 제출이라 프레임 펜스에 얹을 수 없다. 자체 제출 후 큐가 비기를 기다려
        // 부르는 쪽이 결과를 바로 쓸 수 있게 한다(업로드 · 스모크 용도라 빈도가 낮다).
        VkSubmitInfo submitInfo{};
        submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &listBuffer;
        noteSubmittedEventMarker( pList->getLastEventMarker() );
        std::scoped_lock<mutex> queueLock{ _queueMutex };
        VkResult                queueResult = isDeviceLost() ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE );
        if ( queueResult == VK_SUCCESS )
            queueResult = vkQueueWaitIdle( _graphicsQueue );
        reportQueueResult( static_cast<int32>( queueResult ), "executeCommandListImmediate" );

        // 제출한 리스트가 바꾼 레이아웃은 실제로 일어났으므로 되돌릴 기록에서 뺀다.
        std::scoped_lock<mutex> layoutLock{ _imageLayoutMutex };
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
        if ( _recordingState._bRenderPassActive == SW_TRUE )
        {
            vkCmdEndRenderPass( _activeFrameBuffer );
            _recordingState._bRenderPassActive = SW_FALSE;
        }
        vkEndCommandBuffer( _activeFrameBuffer );
        _listPendingSubmit.push_back( _activeFrameBuffer );
        _listPendingSubmit.push_back( listBuffer );
        noteSubmittedEventMarker( pList->getLastEventMarker() );

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
            if ( _bFrameAcquireWaitPending != SW_FALSE && acquireSemaphore != VK_NULL_HANDLE )
            {
                flushInfo.waitSemaphoreCount = 1;
                flushInfo.pWaitSemaphores    = &acquireSemaphore;
                flushInfo.pWaitDstStageMask  = &waitStage;
                _bFrameAcquireWaitPending    = SW_FALSE;
            }
            {
                std::scoped_lock<mutex> queueLock{ _queueMutex };
                const VkResult          flushResult = isDeviceLost() ? VK_ERROR_DEVICE_LOST : vkQueueSubmit( _graphicsQueue, 1, &flushInfo, VK_NULL_HANDLE );
                reportQueueResult( static_cast<int32>( flushResult ), "executeCommandList: vkQueueSubmit (immediate)" );
            }
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
