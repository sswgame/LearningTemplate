#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/Support/RHIMemoryLedger.h"
#include "Engine/Graphics/RHI/Support/RHIStagingRingUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"

// 텍스처 전송(docs/plans/RhiAsyncUploads.md 2.2, U5): 영속 매핑 업로드 스테이징 링, 전송마다 얼로케이터를 쥔 전송 작업, 전용 전송 펜스, 연속 구간 워터마크.
// 전송은 그래픽스(DIRECT) 큐 하나로 즉시 내고 프레임 제출 락(`_uploadSlotMutex`)을 잡지 않는다 — 같은 큐라 제출 순서가 곧 실행 순서이고,
// 계약 3(앞서 제출된 프레임 뒤, 뒤에 제출될 프레임 앞)은 endFrame 이 프레임 리스트를 모아 한 번에 내는 구조가 만든다.

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    bool D3D12RHIDevice::initializeTransfers( const RHISwapChainDesc& desc )
    {
        if ( desc._stagingRingBytes == 0 )
        {
            SW_LOG_ERROR( "The D3D12 backend needs a staging ring capacity (gv_rhiStagingRingMegabytes must be positive)" );
            return false;
        }
        return createStagingRing( _uploadRing, desc._stagingRingBytes, desc._stagingSoloMaxBytes );
    }

    void D3D12RHIDevice::shutdownTransfers()
    {
        // GPU 는 종료 2단계(waitIdle)에서 쉬었다. 잃은 장치면 끝나지 않은 작업이 남아 있을 수 있지만 더 기다리지 않는다(isDeviceLost 설명).
        _transferFenceQueue.retireAll( [this]( D3D12TransferJob& job )
        { recycleTransferJob( std::move( job ) ); } );
        // 전송을 기다리던 해제를 프레임 해제 큐로 넘기고 바로 비운다 — 네이티브 디바이스가 사라진 뒤의 소멸자에 맡기지 않는다(종료 3 · 4단계가 넣은 해제는 부를 단계가 없다).
        _transferReleaseQueue.flushAll();
        _releaseQueue.flushAll();
        {
            std::scoped_lock<mutex> jobLock{ _transferJobMutex };
            _listFreeTransferJob.clear();
        }
        {
            std::scoped_lock<mutex> lock{ _textureTransferMutex };
            _mapTextureTransferSerial.clear();
        }
        // 가져가지 않은 읽기는 읽기 버퍼와 함께 사라진다. 그 뒤의 가져가기 · 버리기는 업로드 티켓처럼 아무 일도 하지 않는다.
        _readbackTable.clear();
        destroyStagingRing( _uploadRing );
    }

    bool D3D12RHIDevice::createStagingRing( D3D12StagingRing& ring, uint64 capacityBytes, uint64 soloMaxBytes )
    {
        if ( createMappedStagingBuffer( capacityBytes, ring._buffer, ring._pMapped ) == false )
            return false;
        // 구간 시작 정렬은 텍스처 복사의 배치 오프셋 규칙(512)이다 — 구간 안에서 다시 맞추지 않아도 된다.
        ring._ledger.initialize( capacityBytes, static_cast<uint64>( D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT ), soloMaxBytes );
        return true;
    }

    void D3D12RHIDevice::destroyStagingRing( D3D12StagingRing& ring )
    {
        {
            std::scoped_lock<mutex> soloLock{ ring._soloMutex };
            destroyMappedStagingBuffer( ring._soloBuffer, ring._pSoloMapped );
        }
        destroyMappedStagingBuffer( ring._buffer, ring._pMapped );
        ring._ledger.shutdown();
    }

    bool D3D12RHIDevice::createMappedStagingBuffer( uint64 sizeBytes, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, uint8*& pOutMapped )
    {
        pOutMapped = nullptr;
        void* pMapped{ nullptr };
        if ( createMappedUploadBuffer( sizeBytes, outBuffer, pMapped ) == false )
        {
            SW_LOG_ERROR( "Failed to create or map the D3D12 staging upload buffer (%# bytes, gv_rhiStagingRingMegabytes · gv_rhiStagingSingleMaxMegabytes)", sizeBytes );
            return false;
        }
        pOutMapped = static_cast<uint8*>( pMapped );
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeDeviceObject( outBuffer.Get() ), RHIMemoryKind::Staging, computeAllocationBytes( outBuffer.Get() ) );
        return true;
    }

    void D3D12RHIDevice::destroyMappedStagingBuffer( Microsoft::WRL::ComPtr<ID3D12Resource>& rBuffer, uint8*& pMapped )
    {
        // 자원을 놓으면 매핑도 풀린다. 장부에는 매핑까지 끝난 버퍼만 올렸다.
        if ( rBuffer != nullptr )
            getMemoryLedger().recordFree( RHIMemoryKey::makeDeviceObject( rBuffer.Get() ) );
        rBuffer.Reset();
        pMapped = nullptr;
    }

    RHISubmitResult D3D12RHIDevice::allocateStagingSpan( D3D12StagingRing& ring, uint64 sizeBytes, const utf8* pCaller, RHIStagingSpan& outSpan )
    {
        RHIStagingAllocateResult result = ring._ledger.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        if ( result == RHIStagingAllocateResult::Full )
        {
            retireCompletedTransfers();
            result = ring._ledger.allocate( sizeBytes, _transferSerials.getWatermark(), outSpan );
        }
        // 결과 옮기기 · 못 받은 구간 놓기 · 이유 로그는 세 백엔드가 같이 쓰는 한 곳이다(잃은 장치면 Full 도 Rejected, 계약 2).
        const RHISubmitResult submitResult = RHIStagingRingUtil::settleAllocation( ring._ledger, _transferSerials, result, sizeBytes, "D3D12", pCaller, outSpan );
        if ( submitResult != RHISubmitResult::Accepted || outSpan._bSolo == SW_FALSE )
            return submitResult;
        // 장부가 단독 구간을 내줬다면 앞선 단독 구간은 이미 돌아왔다 — 남은 버퍼(아직 회수가 지우지 않은 것)를 GPU 가 더 쓰지 않는다.
        std::scoped_lock<mutex> soloLock{ ring._soloMutex };
        destroyMappedStagingBuffer( ring._soloBuffer, ring._pSoloMapped );
        if ( createMappedStagingBuffer( outSpan._sizeBytes, ring._soloBuffer, ring._pSoloMapped ) )
            return RHISubmitResult::Accepted;
        return RHIStagingRingUtil::rejectSoloBufferFailure( ring._ledger, outSpan, "D3D12", pCaller );
    }

    void D3D12RHIDevice::destroyIdleSoloBuffer( D3D12StagingRing& ring )
    {
        std::scoped_lock<mutex> soloLock{ ring._soloMutex };
        if ( ring._soloBuffer == nullptr )
            return;
        // 판정은 락 안에서 한다 — 밖에서 보면 그 사이 다른 스레드가 새 단독 구간을 받아 버퍼를 만들 수 있다.
        ring._ledger.freeCompleted( _transferSerials.getWatermark() );
        if ( ring._ledger.hasLiveSolo() )
            return;
        destroyMappedStagingBuffer( ring._soloBuffer, ring._pSoloMapped );
    }

    bool D3D12RHIDevice::beginTransferJob( D3D12TransferJob& outJob )
    {
        outJob = D3D12TransferJob{};
        {
            std::scoped_lock<mutex> jobLock{ _transferJobMutex };
            if ( _listFreeTransferJob.empty() == false )
            {
                outJob = std::move( _listFreeTransferJob.back() );
                _listFreeTransferJob.pop_back();
            }
        }

        if ( outJob._list == nullptr )
        {
            // 새 리스트는 열린 채로 만들어진다 — 바로 기록한다.
            const HRESULT allocatorHr = _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( outJob._allocator.GetAddressOf() ) );
            const HRESULT listHr      = SUCCEEDED( allocatorHr ) ? _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, outJob._allocator.Get(), nullptr,
                                                                                               IID_PPV_ARGS( outJob._list.GetAddressOf() ) )
                                                                 : allocatorHr;
            if ( FAILED( listHr ) )
            {
                SW_LOG_ERROR( "Failed to create a D3D12 transfer command allocator or list hr=0x%# (DeviceRemoved=0x%#)", static_cast<uint32>( listHr ),
                              static_cast<uint32>( _device->GetDeviceRemovedReason() ) );
                outJob = D3D12TransferJob{};
                return false;
            }
            outJob._allocator->SetName( L"TransferAllocator" );
            return true;
        }

        // 놀고 있던 작업은 전송 펜스를 지난 것만 돌아온다 — 얼로케이터를 곧바로 Reset 해도 GPU 가 읽지 않는다.
        const HRESULT resetHr = isHrSuccess( outJob._allocator->Reset(), "Reset" ) == false ? E_FAIL : outJob._list->Reset( outJob._allocator.Get(), nullptr );
        if ( FAILED( resetHr ) )
        {
            SW_LOG_ERROR( "Failed to reset a pooled D3D12 transfer command allocator or list (DeviceRemoved=0x%#)", static_cast<uint32>( _device->GetDeviceRemovedReason() ) );
            outJob = D3D12TransferJob{};
            return false;
        }
        return true;
    }

    uint64 D3D12RHIDevice::submitTransferJob( D3D12TransferJob job, const utf8* pWhere )
    {
        const HRESULT closeHr = job._list->Close();
        if ( FAILED( closeHr ) )
        {
            SW_LOG_ERROR( "%#: failed to close the transfer command list hr=0x%#", pWhere, static_cast<uint32>( closeHr ) );
            recycleTransferJob( std::move( job ) );
            return 0;
        }

        std::scoped_lock<mutex> submitLock{ _transferSubmitMutex };
        // 기록하는 사이에 장치를 잃었으면(다른 스레드의 손실 · rhi.removeDevice) 내지 않는다.
        if ( isDeviceLost() )
        {
            SW_LOG_ERROR( "%#: the D3D12 device was lost while the transfer was recorded; the transfer is rejected", pWhere );
            recycleTransferJob( std::move( job ) );
            return 0;
        }
        ID3D12CommandList* arrList[] = { job._list.Get() };
        _commandQueue->ExecuteCommandLists( 1, arrList );
        const UINT64  fenceToSignal = _transferFenceValue + 1;
        const HRESULT signalHr      = _commandQueue->Signal( _transferFence.Get(), fenceToSignal );
        if ( FAILED( signalHr ) )
        {
            // 리스트는 이미 큐에 났지만 끝났는지 알 방법이 없다. 작업은 돌려놓지 않고 버린다(재사용하면 실행 중인 얼로케이터를 Reset 할 수 있다).
            if ( _transferSerials.markLost() )
                SW_LOG_ERROR( "%#: transfer fence Signal failed hr=0x%# (DeviceRemoved=0x%#)", pWhere, static_cast<uint32>( signalHr ),
                              static_cast<uint32>( _device->GetDeviceRemovedReason() ) );
            return 0;
        }
        _transferFenceValue = fenceToSignal;
        job._fenceValue     = fenceToSignal;
        // 시리얼은 큐에 낸 직후 같은 락 안에서 매긴다 — 락 밖에서 매기면 시리얼 순서가 큐 순서와 어긋나 워터마크가 끝나지 않은 제출을 덮는다(계약 5).
        return _transferFenceQueue.enqueue( std::move( job ) );
    }

    void D3D12RHIDevice::recycleTransferJob( D3D12TransferJob&& job )
    {
        job._fenceValue = 0;
        std::scoped_lock<mutex> jobLock{ _transferJobMutex };
        _listFreeTransferJob.push_back( std::move( job ) );
    }

    void D3D12RHIDevice::retireCompletedTransfers()
    {
        if ( _transferFence == nullptr )
            return;
        // 잃은 장치의 펜스는 묻지 않는다: 제거된 장치의 펜스는 끝난 값(UINT64_MAX)을 돌려줘 끝나지 않은 전송이 Complete 로 보인다. 워터마크가 멈추고 남은 티켓은 Failed 다.
        if ( _device != nullptr && isDeviceLost() == false )
        {
            const UINT64 completedFence = _transferFence->GetCompletedValue();
            _transferFenceQueue.retireDone(
                [completedFence]( const D3D12TransferJob& job )
            { return job._fenceValue <= completedFence; },
                [this]( D3D12TransferJob& job )
            { recycleTransferJob( std::move( job ) ); } );
        }
        // 워터마크가 지난 전송을 기다리던 해제를 프레임 해제 큐로 넘긴다(그 프레임 펜스 뒤에 놓인다).
        _transferReleaseQueue.tickCompleted( _transferSerials.getWatermark() );
        // 장부가 단독 구간을 돌려받았으면 그 일회성 버퍼를 지운다(업로드 힙 메모리를 링 하나 크기 넘게 쥐고 있지 않는다).
        destroyIdleSoloBuffer( _uploadRing );
    }

    void D3D12RHIDevice::releaseTextureAfterTransfer( Microsoft::WRL::ComPtr<ID3D12Resource> owned, RHITextureHandle texture, uint64 lastSerial )
    {
        // 전송이 끝난 뒤의 프레임 펜스까지 한 번 더 미뤄 이 텍스처를 그린 프레임도 지나게 한다. 전송과 프레임이 같은 큐여도 해제가 그 공유에 기대지 않게 전송 완료를 직접 본다.
        _transferReleaseQueue.enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [this, owned, texture]()
        { releaseTrackedResourceDeferred( owned, RHIMemoryKey::makeTexture( texture ) ); } ),
                                                 lastSerial );
    }

    void D3D12RHIDevice::noteTextureTransfer( RHITextureHandle texture, uint64 serial )
    {
        std::scoped_lock<mutex> lock{ _textureTransferMutex };
        // 스레드마다 제출하므로 늦게 기록한 작은 시리얼이 큰 시리얼을 덮지 않게 한다(파괴 지연과 bindless 등록 보호는 가장 나중에 제출한 전송을 기다려야 한다).
        uint64& rSerial = _mapTextureTransferSerial[texture];
        if ( serial > rSerial )
            rSerial = serial;
    }

    uint64 D3D12RHIDevice::findTextureTransferSerial( RHITextureHandle texture )
    {
        std::scoped_lock<mutex> lock{ _textureTransferMutex };
        const auto              foundIt = _mapTextureTransferSerial.find( texture );
        return foundIt != _mapTextureTransferSerial.end() ? foundIt->second : 0;
    }

    void D3D12RHIDevice::eraseTextureTransfer( RHITextureHandle texture )
    {
        std::scoped_lock<mutex> lock{ _textureTransferMutex };
        _mapTextureTransferSerial.erase( texture );
    }
} // namespace sw
#endif
