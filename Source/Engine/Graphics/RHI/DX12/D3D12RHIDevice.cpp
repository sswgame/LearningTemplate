#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResourceFactory.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResourcePreset.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiMemoryBudget.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    D3D12RHIDevice::D3D12RHIDevice()
        : _device{ nullptr }
        , _memoryAdapter{ nullptr }
        , _commandQueue{ nullptr }
        , _rtvHeap{ nullptr }
        , _dsvHeap{ nullptr }
        , _cbvHeap{ nullptr }
        , _offlineViewHeap{ nullptr }
        , _rootSignature{ nullptr }
        , _vertexBuffer{ nullptr }
        , _drawCommandSignature{ nullptr }
        , _drawIndexedCommandSignature{ nullptr }
        , _dispatchCommandSignature{ nullptr }
        , _arrCommandAllocator{}
        , _commandList{ nullptr }
        , _pActiveFrameList{ nullptr }
        , _cmdListPoolMutex{}
        , _listFreeCmdListEntry{}
        , _onlineBlockMutex{}
        , _listFreeOnlineBlock{}
        , _frameRing{}
        , _timestampFrequency{ 0 }
        , _timestampWrittenMask{ 0 }
        , _arrTimestampMask{}
        , _gpuBuffers{}
        , _gpuTextures{}
        , _resourceStateMutex{}
        , _mapStructuredBufferState{}
        , _mapOffscreenTexture{}
        , _nextOffscreenRtvIndex{ 0 }
        , _nextOffscreenDsvIndex{ 0 }
        , _listFreeOffscreenRtvIndex{}
        , _listFreeOffscreenDsvIndex{}
        , _mapCbAlignedSize{}
        , _mapCbMapped{}
        , _constantBufferShadow{}
        , _pipelineStates{}
        , _listRenderPass{}
        , _swapChain{}
        , _bBindlessRootSignature{ SW_FALSE }
        , _bDeviceRemovedLogged{ SW_FALSE }
        , _reservedPassFlags{ 0 }
        , _bBlitMismatchLogged{ SW_FALSE }
        , _bOnlineHeapExhaustedLogged{ SW_FALSE }
        , _bTimestampEnabled{ SW_FALSE }
        , _frameStreamState{}
        , _listRegisteredBindless{}
        , _listFreeBindless{}
        , _listRegisteredUAV{}
        , _rtvDescriptorSize{ 0 }
        , _cbvDescriptorSize{ 0 }
        , _allocatedDescriptorsCount{ 0 }
        , _cmdListEntryCreated{ 0 }
        , _fenceEvent{ nullptr }
        , _fence{ nullptr }
        , _fenceValue{ 0 }
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
    {
        _resourceImpl = sw::make_unique<D3D12RHIResourceFactory>( this );
    }

    D3D12RHIDevice::~D3D12RHIDevice()
    {
        shutdown();
    }

    void* D3D12RHIDevice::getNativeTexturePointer( RHITextureHandle texture ) const
    {
        return resolveTexture( texture );
    }

    // ------------------------------------------------------------------------------
    // 리소스 조회 · 디스크립터 힙 · 온라인 블록 (D3D12RHIResourceFactory 와 컨텍스트가 쓴다)
    // ------------------------------------------------------------------------------

    ID3D12Resource* D3D12RHIDevice::resolveBuffer( RHIBufferHandle handle ) const
    {
        const Microsoft::WRL::ComPtr<ID3D12Resource>* slot = _gpuBuffers.get( handle );
        return slot != nullptr ? slot->Get() : nullptr;
    }

    ID3D12Resource* D3D12RHIDevice::resolveTexture( RHITextureHandle handle ) const
    {
        const Microsoft::WRL::ComPtr<ID3D12Resource>* slot = _gpuTextures.get( handle );
        return slot != nullptr ? slot->Get() : nullptr;
    }

    D3D12_RESOURCE_STATES D3D12RHIDevice::getTrackedTextureState( RHITextureHandle texture )
    {
        std::scoped_lock<mutex> lock{ _resourceStateMutex };
        const auto              offscreenIt = _mapOffscreenTexture.find( texture );
        if ( offscreenIt == _mapOffscreenTexture.end() )
            return D3D12_RESOURCE_STATE_COMMON;
        return offscreenIt->second._state;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHIDevice::getOffscreenRtvHandle( uint32 rtvIndex ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>( rtvIndex ) * _rtvDescriptorSize;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHIDevice::getOffscreenDsvHandle( uint32 dsvIndex ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>( dsvIndex ) * _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHIDevice::offlineDescriptorAt( uint32 index ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if ( _offlineViewHeap == nullptr || index >= kOfflineDescriptorCount )
            return handle;
        handle = _offlineViewHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>( index ) * _cbvDescriptorSize;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12RHIDevice::shaderVisibleCpuAt( uint32 index ) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if ( _cbvHeap == nullptr || index >= kMaxShaderVisibleDescriptors )
            return handle;
        handle = _cbvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>( index ) * _cbvDescriptorSize;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12RHIDevice::shaderVisibleGpuAt( uint32 index ) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if ( _cbvHeap == nullptr || index >= kMaxShaderVisibleDescriptors )
            return handle;
        handle = _cbvHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>( index ) * _cbvDescriptorSize;
        return handle;
    }

    uint32 D3D12RHIDevice::acquireOnlineBlock()
    {
        std::scoped_lock<mutex> lock{ _onlineBlockMutex };
        if ( _listFreeOnlineBlock.empty() )
        {
            if ( _bOnlineHeapExhaustedLogged == SW_FALSE )
            {
                _bOnlineHeapExhaustedLogged = SW_TRUE;
                SW_LOG_ERROR( "온라인 디스크립터 블록이 바닥났습니다 (%#×%#) — 이후 드로우는 직전 슬롯 테이블로 그립니다.",
                              kOnlineBlockCount, kOnlineBlockDescriptorCount );
            }
            return UINT32_MAX;
        }
        const uint32 block = _listFreeOnlineBlock.back();
        _listFreeOnlineBlock.pop_back();
        return block;
    }

    void D3D12RHIDevice::releaseOnlineBlocksDeferred( D3D12RecordingState& state )
    {
        state._onlineCursor = 0;
        state._onlineEnd    = 0;
        if ( state._listOnlineBlock.empty() )
            return;

        // GPU 가 이 리스트의 테이블을 아직 읽는 중이다. 현재 펜스가 지난 뒤에야 블록을 다시 내준다.
        // 벡터를 복사하지 않고 통째로 묶음에 옮긴다. 상태는 풀에서 온 빈 벡터를 받아 다음 기록에 그대로 쓴다.
        std::scoped_lock<mutex> lock{ _onlineBlockMutex };
        OnlineBlockRecycleBatch batch{};
        batch._fence = _fenceValue;
        if ( _listOnlineRecyclePool.empty() == false )
        {
            batch._listBlock.swap( _listOnlineRecyclePool.back() );
            _listOnlineRecyclePool.pop_back();
        }
        batch._listBlock.swap( state._listOnlineBlock );
        _listPendingOnlineRecycle.push_back( std::move( batch ) );
    }

    void D3D12RHIDevice::recycleCompletedOnlineBlocks( uint64 completedFence )
    {
        std::scoped_lock<mutex> lock{ _onlineBlockMutex };
        for ( size_t index = 0; index < _listPendingOnlineRecycle.size(); )
        {
            OnlineBlockRecycleBatch& batch = _listPendingOnlineRecycle[index];
            if ( batch._fence > completedFence )
            {
                ++index;
                continue;
            }
            for ( const uint32 block : batch._listBlock )
                _listFreeOnlineBlock.push_back( block );
            batch._listBlock.clear();
            _listOnlineRecyclePool.push_back( std::move( batch._listBlock ) );
            VectorUtil::removeAtSwap( _listPendingOnlineRecycle, index );
        }
    }

    RHIBufferHandle D3D12RHIDevice::storeBuffer( Microsoft::WRL::ComPtr<ID3D12Resource> buffer )
    {
        if ( buffer == nullptr )
            return 0;
        const uint64          bytes  = computeAllocationBytes( buffer.Get() );
        const RHIBufferHandle handle = _gpuBuffers.insert( std::move( buffer ) );
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeBuffer( handle ), RHIMemoryKind::Buffer, bytes );
        return handle;
    }

    uint64 D3D12RHIDevice::computeAllocationBytes( ID3D12Resource* pResource ) const
    {
        if ( pResource == nullptr || _device == nullptr )
            return kRHIMemoryUnknownBytes;
        const D3D12_RESOURCE_DESC            desc = pResource->GetDesc();
        const D3D12_RESOURCE_ALLOCATION_INFO info = _device->GetResourceAllocationInfo( 0, 1, &desc );
        // 서술이 잘못됐으면 SizeInBytes 가 UINT64_MAX 다. 지어내지 않고 "크기 모름" 으로 센다.
        return info.SizeInBytes == UINT64_MAX ? kRHIMemoryUnknownBytes : static_cast<uint64>( info.SizeInBytes );
    }

    void D3D12RHIDevice::releaseTrackedResourceDeferred( Microsoft::WRL::ComPtr<ID3D12Resource> owned, const RHIMemoryKey& key )
    {
        // 람다가 델리게이트의 인라인 칸(24 바이트)에 들도록 키를 id 하나로 담고 공간은 갈래로 가른다 — 자원 · 장부 · id 로 꽉 찬다.
        RHIMemoryLedger* pLedger = &getMemoryLedger();
        const uint64     id      = key._id;
        if ( key._space == RHIMemoryKeySpace::Texture )
        {
            auto releaseCb = [owned, pLedger, id]()
            {
                (void)owned.Get();
                pLedger->recordFree( RHIMemoryKey::makeTexture( id ) );
            };
            _releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ), _fenceValue );
        }
        else
        {
            auto releaseCb = [owned, pLedger, id]()
            {
                (void)owned.Get();
                pLedger->recordFree( RHIMemoryKey::makeBuffer( id ) );
            };
            _releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, releaseCb ), _fenceValue );
        }
    }

    bool D3D12RHIDevice::createMappedUploadBuffer( uint64 sizeBytes, Microsoft::WRL::ComPtr<ID3D12Resource>& outBuffer, void*& pOutMapped )
    {
        pOutMapped = nullptr;
        outBuffer.Reset();

        const D3D12_HEAP_PROPERTIES heapProps    = D3D12RHIResourcePreset::heapProperties( D3D12_HEAP_TYPE_UPLOAD );
        const D3D12_RESOURCE_DESC   resourceDesc = D3D12RHIResourcePreset::bufferDesc( sizeBytes );
        if ( FAILED( _device->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       IID_PPV_ARGS( outBuffer.GetAddressOf() ) ) ) )
            return false;

        if ( FAILED( outBuffer->Map( 0, nullptr, &pOutMapped ) ) || pOutMapped == nullptr )
        {
            pOutMapped = nullptr;
            outBuffer.Reset();
            return false;
        }
        return true;
    }

    RHITextureHandle D3D12RHIDevice::storeTexture( Microsoft::WRL::ComPtr<ID3D12Resource> texture, RHIMemoryKind kind )
    {
        if ( texture == nullptr )
            return 0;
        const uint64           bytes  = computeAllocationBytes( texture.Get() );
        const RHITextureHandle handle = _gpuTextures.insert( std::move( texture ) );
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeTexture( handle ), kind, bytes );
        return handle;
    }

    void D3D12RHIDevice::flushDebugMessages( const utf8* pStage )
    {
    #if defined( SW_DEBUG )
        // 디바이스가 이미 제거된 상태로 한 번 로그를 남겼으면, 프레임마다 똑같은 검증 메시지
        // 수십 줄 + DRED 덤프를 무한 반복하지 않는다. 자동 복구가 없어서 그 이후 매 프레임
        // 여기로 다시 들어오는데, 정보량 없이 로그만 무한히 쌓인다.
        const bool bAlreadyDeviceRemoved = _bDeviceRemovedLogged != 0;

        Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
        if ( SUCCEEDED( _device.As( &infoQueue ) ) && infoQueue != nullptr )
        {
            const uint64 messageCount = infoQueue->GetNumStoredMessages();
            for ( uint64 messageIndex = 0; messageIndex < messageCount; ++messageIndex )
            {
                SIZE_T messageLength{ 0 };
                infoQueue->GetMessage( messageIndex, nullptr, &messageLength );
                vector<uint8>  bytes( messageLength );
                D3D12_MESSAGE* pMessage = reinterpret_cast<D3D12_MESSAGE*>( bytes.data() );
                if ( SUCCEEDED( infoQueue->GetMessage( messageIndex, pMessage, &messageLength ) ) && bAlreadyDeviceRemoved == false )
                    SW_LOG_ERROR( "[%#] %#", pStage, pMessage->pDescription );
            }
            infoQueue->ClearStoredMessages();
        }

        if ( bAlreadyDeviceRemoved )
            return;

        if ( _device != nullptr && FAILED( _device->GetDeviceRemovedReason() ) )
        {
            _bDeviceRemovedLogged = SW_TRUE;
            Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
            if ( SUCCEEDED( _device.As( &dred ) ) && dred != nullptr )
            {
                D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT autoBreadcrumbsOutput{};
                if ( SUCCEEDED( dred->GetAutoBreadcrumbsOutput( &autoBreadcrumbsOutput ) ) )
                {
                    const D3D12_AUTO_BREADCRUMB_NODE* pNode = autoBreadcrumbsOutput.pHeadAutoBreadcrumbNode;
                    while ( pNode != nullptr )
                    {
                        const uint32 executed = ( pNode->pCommandHistory != nullptr && pNode->pLastBreadcrumbValue != nullptr )
                                                  ? *pNode->pLastBreadcrumbValue
                                                  : 0;
                        SW_LOG_ERROR( "CommandList='%#', Total=%#, Executed=%#",
                                      pNode->pCommandListDebugNameA ? pNode->pCommandListDebugNameA : "unnamed",
                                      pNode->BreadcrumbCount, executed );
                        if ( pNode->pCommandHistory != nullptr && 0 < executed && executed <= pNode->BreadcrumbCount )
                        {
                            SW_LOG_ERROR( "Last completed Op index=%#, OpType=%#",
                                          executed - 1, static_cast<uint32>( pNode->pCommandHistory[executed - 1] ) );
                            if ( executed < pNode->BreadcrumbCount )
                            {
                                SW_LOG_ERROR( "Failed/In-Flight Op index=%#, OpType=%#",
                                              executed, static_cast<uint32>( pNode->pCommandHistory[executed] ) );
                            }
                        }
                        pNode = pNode->pNext;
                    }
                }

                D3D12_DRED_PAGE_FAULT_OUTPUT pageFaultOutput{};
                if ( SUCCEEDED( dred->GetPageFaultAllocationOutput( &pageFaultOutput ) ) )
                    SW_LOG_ERROR( "PageFault VA=0x%#", Fmt( static_cast<uint64>( pageFaultOutput.PageFaultVA ), Format( 16, Format::Padding::Zero ).hexUpper() ) );
            }
        }
    #else
        (void)pStage;
    #endif
    }

    bool D3D12RHIDevice::queryGpuMemoryBudgetInternal( RHIGpuMemoryBudget& outBudget )
    {
        return queryDxgiMemoryBudget( _memoryAdapter.Get(), outBudget );
    }

    IRHIResourceFactory* D3D12RHIDevice::getResourceFactory() { return _resourceImpl.get(); }
    IRHICommandContext*  D3D12RHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

} // namespace sw
#endif
