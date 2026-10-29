#include "pch.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Log/LogOnce.h"

#include "Engine/Graphics/RHI/DX/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX/DX12/D3D12RHIResourceFactory.h"
#include "Engine/Graphics/RHI/Diagnostics/RHITimestamp.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Foundation/EnginePlatformHeaders.h"
    #include "Engine/Foundation/EngineServices.h"
    #include "Engine/Config/EngineDefaultAssets.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"
    #include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    void D3D12RHIDevice::waitIdleInternal()
    {
        // 다른 스레드가 렌더 스레드를 `waitIdle` 로 세운 채 부른다(`Engine::onMainWindowResize`) — 그동안 프레임 진행 상태는 이 스레드가 빌려 만진다.
        ScopedThreadRoleBorrow borrow{ _swapChain.getRenderThreadRole() };
        waitForPreviousFrame();
        // 전송은 같은 큐에서 프레임 펜스보다 먼저 끝났다 — 워터마크를 올려야 전송을 기다리던 텍스처 해제가 `_releaseQueue` 로 넘어와 아래 비우기에 든다.
        retireCompletedTransfers();
        _releaseQueue.flushAll();
    }

    void D3D12RHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _fenceValue );
    }

    unique_ptr<IRHICommandList> D3D12RHIDevice::createCommandList()
    {
        unique_ptr<D3D12RHICommandList> list = make_unique<D3D12RHICommandList>( this );
        if ( list->isValid() == false )
        {
            if ( _bDeviceRemovedLogged.load( std::memory_order_relaxed ) == SW_FALSE )
                SW_LOG_ERROR_ONCE( "D3D12RHIDevice::createCommandList: failed to create the native command list." );
            return nullptr;
        }
        return list;
    }

    void D3D12RHIDevice::registerCommandList( D3D12RHICommandList* pCmdList )
    {
        ScopedLock<mutex> lock{ _liveCmdListMutex };
        _listLiveCmd.push_back( pCmdList );
    }

    void D3D12RHIDevice::unregisterCommandList( D3D12RHICommandList* pCmdList )
    {
        ScopedLock<mutex> lock{ _liveCmdListMutex };
        (void)VectorUtil::removeSingleSwap( _listLiveCmd, pCmdList ); // 등록되지 않은 리스트면 뺄 것이 없다
    }

    ID3D12GraphicsCommandList* D3D12RHIDevice::beginNextFrameSegment()
    {
        D3D12CommandListEntry entry = acquireCommandListEntry();
        if ( entry._list == nullptr || entry._allocator == nullptr )
            return nullptr;

        // 풀에서 나온 쌍은 이미 펜스를 통과했으므로 곧바로 Reset 해도 된다.
        if ( isHrSuccess( entry._allocator->Reset(), "Reset" ) == false || isHrSuccess( entry._list->Reset( entry._allocator.Get(), nullptr ), "Reset" ) == false )
            return nullptr;

        bindBindlessRootState( entry._list.Get() );

        ID3D12GraphicsCommandList* pList = entry._list.Get();
        _listFrameSegment.push_back( std::move( entry ) );
        return pList;
    }

    void D3D12RHIDevice::executeCommandListImmediate( IRHICommandList* pCmdList )
    {
        auto* pNative = static_cast<D3D12RHICommandList*>( pCmdList );
        if ( pNative == nullptr || _commandQueue == nullptr )
            return;

        ID3D12GraphicsCommandList* pList = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;

        // 이 리스트가 방금 올린 버퍼를 읽을 수 있다. 열어 둔 업로드 복사를 먼저 내보낸다.
        {
            ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
            flushPendingUploads( true );
        }
        ID3D12CommandList* arr[] = { pList };
        _commandQueue->ExecuteCommandLists( 1, arr );
    }

    void D3D12RHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        auto*      pNative                  = static_cast<D3D12RHICommandList*>( pCmdList );
        const bool bCannotSubmitCommandList = pNative == nullptr || _commandQueue == nullptr || _pActiveFrameList == nullptr;
        if ( bCannotSubmitCommandList )
            return;

        ID3D12GraphicsCommandList* pList = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;

        // 프레임 스트림을 이 지점에서 자르고 [지금까지의 조각][이 리스트] 순서로 모아 endFrame 에서 한 번에
        // 제출한다(Vulkan 과 같다). 같은 큐의 제출 순서가 곧 실행 순서다. 주의: 여기서 곧바로 ExecuteCommandLists 를
        // 부르면 프레임 스트림은 endFrame 에 제출되므로, 그래프보다 **먼저** 기록한 것(오프스크린 게임 RT 클리어 등)이
        // 그래프 뒤에 실행된다.
        //
        // **빈 조각은 자르지 않는다.** 이 조각에 아무것도 기록되지 않았으면(레벨 배리어는 패스 리스트에 들어가므로
        // 레벨 사이가 그렇다) 패스 리스트만 넣고 조각은 열어 둔 채 다음 기록을 받는다. 그 조각은 뒤에 기록될
        // 것만 담으므로 뒤에 제출돼도 순서가 맞다. 잘라 내보내면 큐에 빈 리스트가 나가고 제출이 리스트당 ~7 us 다.
        const bool bSegmentEmpty = ( _frameStreamState._bRecordedAny == SW_FALSE );
        if ( bSegmentEmpty )
        {
            _listPendingSubmit.push_back( pList );
            submitPendingIfImmediate();
            return;
        }

        // Close 실패는 기록 중 잘못된 명령이 있었다는 뜻이다 — 제출하면 장치가 제거되므로 이유와 함께 멈춘다(아래 Close 도 같다).
        requireHr( _pActiveFrameList->Close(), "ID3D12GraphicsCommandList::Close (frame segment)" );
        releaseOnlineBlocksDeferred( _frameStreamState );
        _listPendingSubmit.push_back( _pActiveFrameList );
        _listPendingSubmit.push_back( pList );
        submitPendingIfImmediate();

        ID3D12GraphicsCommandList* pNextSegment = beginNextFrameSegment();
        _pActiveFrameList                       = pNextSegment;
        if ( pNextSegment == nullptr )
        {
            _frameStreamState._bRecording = SW_FALSE;
            return;
        }

        // 새 리스트라 바인딩 캐시가 무효다. 뷰포트/시저도 다시 깔아준다.
        _frameStreamState             = D3D12RecordingState{};
        _frameStreamState._bRecording = SW_TRUE;
        _frameStreamContext->rebindCommandList( pNextSegment );

        D3D12_VIEWPORT viewport{};
        viewport.Width    = static_cast<float32>( _swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( _swapChain.getHeight() );
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        pNextSegment->RSSetViewports( 1, &viewport );

        D3D12_RECT scissor{ 0, 0, static_cast<LONG>( _swapChain.getWidth() ), static_cast<LONG>( _swapChain.getHeight() ) };
        pNextSegment->RSSetScissorRects( 1, &scissor );
    }

    void D3D12RHIDevice::submitPendingIfImmediate()
    {
        if ( _bImmediateSubmit == false || _listPendingSubmit.empty() )
            return;

        // 여기까지 기록된 구조 버퍼 갱신 복사가 이 리스트들보다 먼저 가야 한다. 앞에 끼운다. **실행까지 업로드 락을 쥔다** — 닫아서 대기열에 넣은 복사
        // 리스트를 실행 전에 다른 스레드의 갱신이 `Reset` 하면 그 복사가 사라진다.
        ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
        flushPendingUploads( false );
        _commandQueue->ExecuteCommandLists( static_cast<UINT>( _listPendingSubmit.size() ), _listPendingSubmit.data() );
        _listPendingSubmit.clear();
    }

    void D3D12RHIDevice::flushPendingUploads( bool bExecuteNow )
    {
        if ( _commandQueue == nullptr )
            return;
        SW_PROFILE_SCOPE( "RT.Upload.flush" ); // Vulkan 의 `RT.Upload.waitIdle` 과 견주는 자리(여기는 기다리지 않고 제출만 한다)
        for ( StructuredUploadSlot& slot : _arrStructuredUploadSlot )
        {
            if ( slot._bListOpen == SW_FALSE || slot._copyCommandList == nullptr )
                continue;
            requireHr( slot._copyCommandList->Close(), "ID3D12GraphicsCommandList::Close (structured upload)" );
            slot._bListOpen = SW_FALSE;
            // 이 얼로케이터의 기록이 모두 끝났다고 믿어도 되는 펜스는 **이 제출 뒤 첫 Signal** 이다. 주의: 리스트를 **열 때** 적으면, 프레임 제출 뒤 ·
            // Signal 전에 연 복사(다음 프레임에 제출된다)가 한 프레임 이른 펜스를 달고, 세 프레임 뒤 그 펜스만 기다리고 얼로케이터를 Reset 한다 —
            // `_resetFence` 가 막으려는 "실행 중 Reset" 그대로다. 부르는 쪽은 업로드 락을 쥐고 있고, 실행은 다음 Signal 보다 먼저다.
            slot._resetFence = _fenceValue.load();
            if ( bExecuteNow )
            {
                ID3D12CommandList* arrList[] = { slot._copyCommandList.Get() };
                _commandQueue->ExecuteCommandLists( 1, arrList );
            }
            else
            {
                // 프레임 리스트 앞에 넣는다. 큐 순서가 실행 순서다.
                _listPendingSubmit.insert( _listPendingSubmit.begin(), slot._copyCommandList.Get() );
            }
        }
    }

    bool D3D12RHIDevice::debugRemoveDevice()
    {
        Microsoft::WRL::ComPtr<ID3D12Device5> device5;
        if ( _device == nullptr || FAILED( _device.As( &device5 ) ) )
            return false;
        SW_LOG_WARNING( "Removing the D3D12 device on request (rhi.removeDevice) - every later GPU call fails as after a TDR" );
        device5->RemoveDevice();
        _transferSerials.markLost(); // 제거 직후부터 제출 진입이 Rejected 를 준다(다음 질의를 기다리지 않는다)
        return true;
    }

    bool D3D12RHIDevice::isDeviceRemoved()
    {
        if ( _device == nullptr || SUCCEEDED( _device->GetDeviceRemovedReason() ) )
            return false;
        _transferSerials.markLost();
        return true;
    }

    bool D3D12RHIDevice::isDeviceLost()
    {
        return _transferSerials.isLost() || isDeviceRemoved();
    }

    void D3D12RHIDevice::waitForPreviousFrame()
    {
        if ( _commandQueue == nullptr || _fence == nullptr )
            return;

        if ( isDeviceRemoved() )
            return;

        // 업로드 내보내기와 Signal · 올리기를 한 락 안에서 한다(`_fenceValue` 의 설명). 기다리는 것은 락 밖에서다.
        UINT64  fenceToWait{ 0 };
        HRESULT signalHr{ S_OK };
        {
            ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
            flushPendingUploads( true );
            fenceToWait = _fenceValue.load();
            signalHr    = _commandQueue->Signal( _fence.Get(), fenceToWait );
            _fenceValue.store( fenceToWait + 1 );
        }

        if ( FAILED( signalHr ) )
        {
            SW_LOG_ERROR( "Fence Signal failed hr=0x%# (DeviceRemoved=0x%#)",
                          static_cast<uint32>( signalHr ),
                          static_cast<uint32>( _device ? _device->GetDeviceRemovedReason() : S_OK ) );
            return;
        }

        if ( waitForFenceValue( fenceToWait ) == false )
            return;

        if ( isDeviceRemoved() == false )
            _swapChain.acquireNextImage();
    }

    void D3D12RHIDevice::waitForRingSlot()
    {
        if ( _fence == nullptr )
            return;

        if ( isDeviceRemoved() )
            return;

        const uint64 completed = _fence->GetCompletedValue();
        bool         bAdvanced = _frameRing.beginFrame( completed );
        if ( bAdvanced == false )
        {
            const uint32 nextIndex = ( _frameRing.currentIndex() + 1 ) % constant::kMaxFrameCountInFlight;
            const uint64 waitValue = _frameRing.getFenceValue( nextIndex );
            // 2 초가 지나도 **끝날 때까지** 기다린다(`waitForFenceValue`). 주의: 시간 초과를 무시하고 링을 넘기지 못한 채 이어가면, 방금 제출한
            // 프레임의 얼로케이터를 Reset 하고 GPU 가 읽는 상수버퍼 링 칸을 덮어쓴다. 장치가 제거됐으면(Windows 는 멈춘 GPU 를 TDR 로 제거한다)
            // 기다리지 않는다.
            waitForFenceValue( waitValue );
            bAdvanced = _frameRing.beginFrame( _fence->GetCompletedValue() );
        }
        // 새 칸의 GPU 사용이 끝났다. 값이 바뀔 때만 쓰는 상수버퍼 가운데 이 칸이 옛 값인 것을 채운다. 넘어가지 못했으면(장치 제거) 지금 칸은
        // GPU 가 아직 읽는 중일 수 있으니 건드리지 않는다.
        if ( bAdvanced )
            fillConstantBufferSlot();
    }

    bool D3D12RHIDevice::waitForFenceValue( uint64 fenceValue )
    {
        if ( _fence == nullptr )
            return false;
        if ( _fence->GetCompletedValue() >= fenceValue )
            return true;
        // 이벤트는 기다리는 호출마다 만든다. 렌더 스레드(링 슬롯 · 이전 프레임)와 다른 스레드(업로드 슬롯의 얼로케이터)가 동시에 기다리는데, 장치의
        // 자동 리셋 이벤트 하나를 나눠 쓰면 먼저 끝난 쪽의 신호를 다른 쪽이 가져가 아직 오지 않은 펜스를 지났다고 믿고 쓰는 중인 얼로케이터를 Reset 한다.
        const HANDLE fenceEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
        if ( fenceEvent == nullptr )
        {
            SW_LOG_ERROR( "Fence %# wait failed: CreateEventW returned null (GetLastError=%#)", fenceValue, static_cast<uint32>( GetLastError() ) );
            return false;
        }
        // 끝날 때까지 기다린다. 시간 초과로 실패를 돌려주면 부르는 쪽(링 슬롯 · 리드백 · 크기 변경 · 해제 큐 비우기)이 GPU 가 아직 쓰는 자원을
        // 풀거나 재사용한다. 2 초마다 한 번 알리고, 장치가 제거되면(TDR) 멈춘다 — 그때는 기다려도 끝나지 않는다.
        bool   bReached = SUCCEEDED( _fence->SetEventOnCompletion( fenceValue, fenceEvent ) );
        uint32 waitedSeconds{ 0 };
        while ( bReached && WaitForSingleObject( fenceEvent, 2000 ) != WAIT_OBJECT_0 )
        {
            waitedSeconds += 2;
            if ( isDeviceRemoved() )
            {
                SW_LOG_ERROR_ONCE( "Fence %# never completed — the device was removed (hr=0x%#)", fenceValue,
                                   static_cast<uint32>( _device->GetDeviceRemovedReason() ) );
                bReached = false;
                break;
            }
            if ( _fence->GetCompletedValue() >= fenceValue )
                break;
            SW_LOG_WARNING( "GPU has not reached fence %# after %# s — still waiting", fenceValue, waitedSeconds );
        }
        CloseHandle( fenceEvent );
        return bReached;
    }

    void D3D12RHIDevice::signalCurrentFrame()
    {
        if ( _commandQueue == nullptr || _fence == nullptr )
            return;

        if ( isDeviceRemoved() )
            return;

        // Signal 과 올리기를 업로드 락 안에서 한 덩어리로 한다. 다른 스레드의 업로드가 그 사이에 `_fenceValue` 를 읽고 복사를 실행하면
        // 복사는 Signal 뒤에 나가는데 앞 값을 받아, 그 펜스를 기다린 얼로케이터 Reset 이 아직 도는 복사를 지운다(`_fenceValue` 의 설명).
        UINT64  fenceToSignal{ 0 };
        HRESULT signalHr{ S_OK };
        {
            ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
            fenceToSignal = _fenceValue.load();
            signalHr      = _commandQueue->Signal( _fence.Get(), fenceToSignal );
            _fenceValue.store( fenceToSignal + 1 );
        }
        if ( FAILED( signalHr ) )
        {
            SW_LOG_ERROR_ONCE( "Fence Signal failed hr=0x%#", static_cast<uint32>( signalHr ) );
            return;
        }
        _frameRing.setFenceValue( _frameRing.currentIndex(), fenceToSignal );
        const uint64 completedFence = _fence->GetCompletedValue();
        _releaseQueue.tickCompleted( completedFence );
        recycleCompletedOnlineBlocks( completedFence );
        // 프레임을 돌리는 동안 폴링하는 쪽이 없어도 끝난 전송의 작업 · 스테이징 구간 · 전송을 기다리던 해제가 돌아오게 한다(기다리지 않는다).
        retireCompletedTransfers();
    }

    ID3D12CommandAllocator* D3D12RHIDevice::currentAllocator()
    {
        return _arrCommandAllocator[_frameRing.currentIndex()].Get();
    }

    D3D12CommandListEntry D3D12RHIDevice::acquireCommandListEntry()
    {
        // 반환 대상은 이 하나다. 이름 있는 반환 객체가 여럿이면 NRVO 가 걸리지 않아
        // 반환할 때마다 엔트리(ComPtr 두 개)가 복사된다.
        D3D12CommandListEntry entry;
        {
            ScopedLock<mutex> lock{ _cmdListPoolMutex };
            if ( _listFreeCmdListEntry.empty() == false )
            {
                entry = std::move( _listFreeCmdListEntry.back() );
                _listFreeCmdListEntry.pop_back();
                return entry;
            }
        }

        // 풀이 비었으면 새로 만든다. 생성 직후의 리스트는 열린 상태라 바로 Close 해 둔다
        // (beginCommandList 가 Reset 으로 다시 연다).
        if ( _device == nullptr )
            return entry;

        // HRESULT 를 여기서 남긴다. 부르는 쪽은 "생성 실패" 한 줄만 남기므로 원인(메모리 부족인지 디바이스 제거인지)은 여기서만 보인다.
        const HRESULT allocHr = _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( entry._allocator.GetAddressOf() ) );
        if ( FAILED( allocHr ) )
        {
            if ( _bDeviceRemovedLogged.load( std::memory_order_relaxed ) == SW_FALSE )
                SW_LOG_ERROR_ONCE( "acquireCommandListEntry: CreateCommandAllocator failed hr=0x%# removed=0x%# pooled=%#",
                                   static_cast<uint32>( allocHr ),
                                   static_cast<uint32>( _device->GetDeviceRemovedReason() ),
                                   static_cast<uint32>( _cmdListEntryCreated ) );
            flushDebugMessages( "acquireCommandListEntry" );
            entry = D3D12CommandListEntry{};
            return entry;
        }
        const HRESULT listHr = _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, entry._allocator.Get(), nullptr,
                                                           IID_PPV_ARGS( entry._list.GetAddressOf() ) );
        if ( FAILED( listHr ) )
        {
            if ( _bDeviceRemovedLogged.load( std::memory_order_relaxed ) == SW_FALSE )
                SW_LOG_ERROR_ONCE( "acquireCommandListEntry: CreateCommandList failed hr=0x%# removed=0x%# pooled=%#",
                                   static_cast<uint32>( listHr ),
                                   static_cast<uint32>( _device->GetDeviceRemovedReason() ),
                                   static_cast<uint32>( _cmdListEntryCreated ) );
            entry = D3D12CommandListEntry{};
            return entry;
        }
        const uint32 entryIndex = _cmdListEntryCreated++;
        utf16        arrName[constant::kMaxBuffer64]{};
        swprintf_s( arrName, L"PoolAllocator%u", entryIndex );
        entry._allocator->SetName( arrName );
        swprintf_s( arrName, L"PoolList%u", entryIndex );
        entry._list->SetName( arrName );
        // 빈 리스트 닫기가 실패하면 장치가 쓸 수 없는 상태다 — 빈 쌍을 돌려줘 부르는 쪽이 생성 실패로 다룬다.
        if ( isHrSuccess( entry._list->Close(), "ID3D12GraphicsCommandList::Close (pool entry)" ) == false )
            entry = D3D12CommandListEntry{};
        return entry;
    }

    void D3D12RHIDevice::recycleCommandListEntryDeferred( const D3D12CommandListEntry& entry )
    {
        if ( entry._list == nullptr || entry._allocator == nullptr )
            return;

        // 제출 직후 리스트 객체가 사라져도 GPU 는 아직 이 얼로케이터의 커맨드 메모리를 읽고 있다.
        // 해제 큐에 실어 현재 펜스가 통과한 뒤에야 재사용 풀로 돌려보낸다. 그래야 다음 사용자가
        // Reset 해도 안전하다.
        auto recycleCb = [this, entry]()
        {
            ScopedLock<mutex> lock{ _cmdListPoolMutex };
            _listFreeCmdListEntry.push_back( entry );
        };
        _releaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, recycleCb ), _fenceValue );
    }

    uint32 D3D12RHIDevice::getTimestampSlotCount() const
    {
        return ( _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampHeap != nullptr && _timestampFrequency != 0 )
                 ? constant::kMaxGPUTimestampSlot
                 : 0u;
    }

    bool D3D12RHIDevice::readTimestamps( RHITimestampFrame& outFrame )
    {
        outFrame = _timestampFrame;
        return outFrame._listMicro.empty() == false;
    }

    bool D3D12RHIDevice::readGPUClockNanos( int64& outGPUNanos )
    {
        outGPUNanos = 0;
        if ( _commandQueue == nullptr )
            return false;
        uint64 frequency = _timestampFrequency;
        if ( frequency == 0 && FAILED( _commandQueue->GetTimestampFrequency( &frequency ) ) )
            return false;
        if ( frequency == 0 )
            return false;
        // 큐의 GPU 시계와 CPU 시계를 한 순간에 읽는다(기다리지 않는다). GPU 값만 쓴다 — CPU 쪽은 Tracy 가 자기 시계로 찍는다.
        uint64 gpuTick{ 0 };
        uint64 cpuTick{ 0 };
        if ( FAILED( _commandQueue->GetClockCalibration( &gpuTick, &cpuTick ) ) )
            return false;
        outGPUNanos = RHITimestamp::convertTickToNanos( gpuTick, constant::kNanosecondsPerSecond / static_cast<float64>( frequency ) );
        return true;
    }

    void D3D12RHIDevice::ensureTimestampResources()
    {
        const bool bTimestampResourcesNotNeeded = _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE || _timestampHeap != nullptr || _device == nullptr ||
                                                  _commandQueue == nullptr;
        if ( bTimestampResourcesNotNeeded )
            return;

        UINT64 frequency{ 0 };
        if ( FAILED( _commandQueue->GetTimestampFrequency( &frequency ) ) || frequency == 0 )
            return;

        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = constant::kMaxGPUTimestampSlot * constant::kMaxFrameCountInFlight;
        if ( isHrSuccess( _device->CreateQueryHeap( &heapDesc, IID_PPV_ARGS( &_timestampHeap ) ), "CreateQueryHeap" ) == false )
            return;

        // 읽기 전용 버퍼 하나에 링 전체 분량을 담는다. 슬롯마다 구간이 겹치지 않는다.
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width            = sizeof( uint64 ) * heapDesc.Count;
        bufferDesc.Height           = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels        = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if ( isHrSuccess( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                            IID_PPV_ARGS( &_timestampReadback ) ),
                          "CreateCommittedResource" ) == false )
        {
            _timestampHeap.Reset();
            return;
        }
        _timestampFrequency = frequency;
    }

    void D3D12RHIDevice::collectTimestampsForSlot()
    {
        // 이 슬롯은 방금 펜스를 통과했다. 지난번 이 슬롯에 적은 값이 GPU 에서 이미 끝나 있다.
        _timestampFrame._listMicro.clear();
        const GPUTimestampSlotMask writtenMask         = _arrTimestampMask[_frameRing.currentIndex()];
        const bool                 bNoTimestampsToRead = _timestampReadback == nullptr || _timestampFrequency == 0 || writtenMask == 0;
        if ( bNoTimestampsToRead )
            return;

        const uint32 base       = getTimestampBase();
        const size_t byteOffset = sizeof( uint64 ) * base;
        D3D12_RANGE  range{ byteOffset, byteOffset + sizeof( uint64 ) * constant::kMaxGPUTimestampSlot };

        void* pMapped{ nullptr };
        if ( isHrSuccess( _timestampReadback->Map( 0, &range, &pMapped ), "ID3D12Resource::Map (timestamp readback)" ) == false || pMapped == nullptr )
            return;

        // DX12 는 쿼리 힙을 리셋하지 않는다. 안 적은 칸에는 **지난 사이클의 값**이 그대로 남는다.
        // 그래서 어느 칸이 이번 것인지 비트로 가려야 한다. 안 그러면 건너뛴 패스가 0us 로 보고된다.
        const uint64* pTicks = reinterpret_cast<const uint64*>( static_cast<const uint8*>( pMapped ) + byteOffset );
        // 그래서 writtenMask 가 곧 준비 비트다. 이 슬롯은 방금 펜스를 통과했다.
        RHITimestamp::resolve( pTicks, writtenMask, constant::kNanosecondsPerSecond / static_cast<float64>( _timestampFrequency ), _timestampFrame );

        const D3D12_RANGE emptyRange{ 0, 0 };
        _timestampReadback->Unmap( 0, &emptyRange );
    }

    void D3D12RHIDevice::beginFrame()
    {
        if ( _frameStreamState._bRecording == SW_FALSE )
        {
            waitForRingSlot();
            // **여기가 타임스탬프를 읽는 유일한 안전한 자리다.** 이 슬롯의 펜스를 방금 통과했으므로
            // 지난번 이 슬롯에 적은 값이 GPU 에서 끝나 있다. 기다리지 않으니 파이프라인이 안 멈춘다.
            ensureTimestampResources();
            collectTimestampsForSlot();
            _timestampWrittenMask.store( 0, std::memory_order_relaxed );
            // 링 슬롯이 정해졌다. 상수버퍼 CBV 를 그 슬롯으로 맞춘다(드로우 경로에서 하던 일).
            refreshConstantBufferViews();
            // 프레임 스트림은 세그먼트로 나뉜다. 첫 세그먼트는 디바이스 소유 리스트를 그대로 쓰고,
            // 커맨드 리스트가 제출될 때마다 executeCommandList 가 잘라 새 세그먼트를 연다.
            _listPendingSubmit.clear();
            ID3D12CommandAllocator* pAllocator = currentAllocator();
            if ( pAllocator == nullptr || _commandList == nullptr )
                return;
            // 디바이스가 제거된 상태에서는 Reset() 이 실패해 커맨드 리스트가 여전히 closed 로 남는다.
            // 그것을 무시하고 _bRecording=1 로 넘어가면 이후의 모든 커맨드 리스트 호출이 "closed command
            // list" 에러를 매번 뱉으며 프레임마다 반복 폭주하게 된다. 실패하면 이번 프레임을 건너뛴다.
            if ( isHrSuccess( pAllocator->Reset(), "Reset" ) == false || isHrSuccess( _commandList->Reset( pAllocator, nullptr ), "Reset" ) == false )
                return;
            _frameStreamState._bRecording      = SW_TRUE;
            _frameStreamState._arrSlotState[0] = D3D12SlotTableState{}; // 새 리스트라 슬롯 테이블은 첫 드로우가 다시 굳힌다
            _frameStreamState._arrSlotState[1] = D3D12SlotTableState{};
            _pActiveFrameList                  = _commandList.Get();
            _frameStreamContext->rebindCommandList( _pActiveFrameList );
            bindBindlessRootState( _pActiveFrameList );
        }
        _swapChain.acquireNextImage();

        // resize() 가 ResizeBuffers 실패로 일찍 반환하면 백버퍼가 비워진 채로 남는다. 그 상태로
        // 계속 진행하면 null 리소스가 배리어에 들어간다. 디바이스가 이미 맛이 간 프레임이므로
        // 이번 프레임은 조용히 건너뛴다.
        if ( _swapChain.isBackBufferReady() == false )
            return;

        // 백버퍼 바인딩(RENDER_TARGET 배리어 + OMSetRenderTargets + Clear)은 여기서 하지 않는다.
        // beginFrame 은 프레임 수명주기 전용이고, 백버퍼 타깃팅은 RenderThread 가 여는 beginRenderPass(핸들 0) 가
        // 배리어까지 포함해 명시적으로 한다. 뷰포트/시저는 기본값으로 남긴다.

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportY        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        D3D12_VIEWPORT viewport{};
        viewport.Width    = static_cast<float32>( _swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( _swapChain.getHeight() );
        viewport.MinDepth = kDefaultViewportMinDepth;
        viewport.MaxDepth = kDefaultViewportMaxDepth;
        viewport.TopLeftX = kDefaultViewportX;
        viewport.TopLeftY = kDefaultViewportY;
        _pActiveFrameList->RSSetViewports( 1, &viewport );

        D3D12_RECT scissorRect{ 0, 0, static_cast<LONG>( _swapChain.getWidth() ), static_cast<LONG>( _swapChain.getHeight() ) };
        _pActiveFrameList->RSSetScissorRects( 1, &scissorRect );
    }

    // 길이를 나누지 않는다: 닫기 · 타임스탬프 해석 · 제출 · 펜스 · Present 의 프레임 끝 순서가 곧 뜻이고,
    // 업로드 리스트를 프레임 리스트 앞에 끼우는 규칙이 한곳에 있어야 한다.
    void D3D12RHIDevice::endFrame( bool vsync, bool bPresent )
    {
        if ( bPresent )
            _swapChain.transitionTo( _pActiveFrameList, D3D12_RESOURCE_STATE_PRESENT );

        // Present 의 300 us 가 어디로 가는지 측정하려고 닫기 · 제출 / DXGI Present / 펜스 신호 · 다음 이미지로 나눠 측정한다.
        {
            SW_PROFILE_SCOPE( "RT.Present.submit" );
            static const uint32 s_slotSubmitListCount = engine::getSubsystem<FrameProfiler>().registerScope( "RT.Present.submit.listCount" );
            if ( _frameStreamState._bRecording != SW_FALSE && _pActiveFrameList != nullptr )
            {
                SW_PROFILE_SCOPE( "RT.Present.submit.close" );
                // 구간 전체를 읽기 버퍼로 옮긴다. 32 칸이면 256 바이트라 옮기는 값이 사실상 공짜고,
                // 어느 칸이 이번 것인지는 비트로 따로 굳혀 둔다(안 적은 칸에는 지난 사이클 값이 남아 있다).
                // 슬롯 번호는 **패스 인덱스로 고정**이라 병렬 기록에도 경쟁이 없다.
                const GPUTimestampSlotMask writtenMask       = _timestampWrittenMask.load( std::memory_order_relaxed );
                _arrTimestampMask[_frameRing.currentIndex()] = writtenMask;
                const bool bShouldResolveTimestamps          = _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampHeap != nullptr && _timestampReadback != nullptr &&
                                                      writtenMask != 0;
                if ( bShouldResolveTimestamps )
                {
                    // 적힌 칸의 연속 구간만 옮긴다. 한 번도 적지 않은 칸을 Resolve 하면 디버그 레이어 오류(1319 "Cannot Resolve query that has never been performed")다.
                    const uint32 base = getTimestampBase();
                    uint32       slot = 0;
                    while ( slot < constant::kMaxGPUTimestampSlot )
                    {
                        if ( ( writtenMask & makeGPUTimestampSlotBit( slot ) ) == 0 )
                        {
                            ++slot;
                            continue;
                        }
                        const uint32 runStart = slot;
                        while ( slot < constant::kMaxGPUTimestampSlot && ( writtenMask & makeGPUTimestampSlotBit( slot ) ) != 0 )
                        {
                            ++slot;
                        }
                        _pActiveFrameList->ResolveQueryData( _timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base + runStart, slot - runStart,
                                                             _timestampReadback.Get(), sizeof( uint64 ) * ( base + runStart ) );
                    }
                }
                requireHr( _pActiveFrameList->Close(), "ID3D12GraphicsCommandList::Close (end frame)" );
                _listPendingSubmit.push_back( _pActiveFrameList );
                _frameStreamState._bRecording = SW_FALSE;
                releaseOnlineBlocksDeferred( _frameStreamState );
            }

            // 열어 둔 구조 버퍼 갱신 복사 리스트를 프레임 리스트 앞에 끼우고 한 번에 제출한다. **실행까지 업로드 락을 쥔다** — 닫아서 대기열에 넣은 복사
            // 리스트를 실행 전에 다른 스레드의 갱신이 `Reset` 하면 GPUScene 복사가 사라진다(submitPendingIfImmediate 와 같은 이유).
            // 텍스처 전송은 제출 때 이미 큐에 나갔다 — 이 프레임 리스트들보다 먼저 실행된다(계약 3).
            {
                ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
                flushPendingUploads( false );

                // 프레임 세그먼트와 패스 리스트를 기록 순서 그대로 한 번에 제출한다.
                if ( _listPendingSubmit.empty() == false && _commandQueue != nullptr )
                {
                    SW_PROFILE_SCOPE( "RT.Present.submit.execute" );
                    engine::getSubsystem<FrameProfiler>().addCount( s_slotSubmitListCount, _listPendingSubmit.size() );
                    _commandQueue->ExecuteCommandLists( static_cast<UINT>( _listPendingSubmit.size() ),
                                                        _listPendingSubmit.data() );
                }
                _listPendingSubmit.clear();
            }
            _pActiveFrameList = nullptr;
        }

        // 빌려 쓴 추가 세그먼트는 이번 프레임 펜스를 통과한 뒤 풀로 돌아간다.
        for ( D3D12CommandListEntry& segment : _listFrameSegment )
        {
            recycleCommandListEntryDeferred( segment );
        }
        _listFrameSegment.clear();

        if ( bPresent )
        {
            HRESULT presentHr = S_OK;
            {
                SW_PROFILE_SCOPE( "RT.Present.present" );
                presentHr = _swapChain.present( vsync );
            }
            if ( FAILED( presentHr ) )
            {
                [[maybe_unused]] const HRESULT removed = _device->GetDeviceRemovedReason();
                // Present 가 장치 제거 · 리셋으로 실패하면(DXGI_ERROR_DEVICE_REMOVED / DEVICE_RESET) 전송 손실로 표시한다. 디버그 전용인 flushDebugMessages 에 맡기지 않는다.
                const bool bPresentLostDevice = presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET || FAILED( removed );
                if ( bPresentLostDevice )
                    _transferSerials.markLost();
                // 디바이스 제거는 자동 복구가 없어서 한 번 일어나면 이후 매 프레임 여기로 들어온다.
                // 첫 발견 때만 로그를 남기고 그 뒤로는 조용히 건너뛰어 로그 폭주를 막는다.
                if ( _bDeviceRemovedLogged.load( std::memory_order_relaxed ) == SW_FALSE )
                {
                    // %# 는 10진 출력이다. DXGI_ERROR_DEVICE_HUNG == 0x887A0005 == 2289696773
                    SW_LOG_ERROR_ONCE( "Present failed hr=%# (0x887A0005=DEVICE_HUNG), DeviceRemovedReason=%#",
                                       static_cast<uint32>( presentHr ), static_cast<uint32>( removed ) );
                }
                flushDebugMessages( "after Present" );
            }
        }
        {
            SW_PROFILE_SCOPE( "RT.Present.signalAcquire" );
            signalCurrentFrame();
            _swapChain.acquireNextImage();
        }
        if ( bPresent )
            _swapChain.markPresented();
    }

} // namespace sw
#endif
