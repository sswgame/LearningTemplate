#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    D3D12RHIDevice::D3D12RHIDevice()
        : _device{ nullptr }
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
        , _arrFrameCmdAllocator{}
        , _cmdListPoolMutex{}
        , _listFreeCmdListEntry{}
        , _onlineBlockMutex{}
        , _listFreeOnlineBlock{}
        , _bOnlineHeapExhaustedLogged{ 0 }
        , _frameRing{}
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
        , _pipelineStates{}
        , _listRenderPass{}
        , _swapChain{}
        , _bBindlessRootSignature{ SW_FALSE }
        , _bDeviceRemovedLogged{ SW_FALSE }
        , _reservedPassFlags{ 0 }
        , _frameStreamState{}
        , _listRegisteredBindless{}
        , _listFreeBindless{}
        , _listRegisteredUAV{}
        , _rtvDescriptorSize{ 0 }
        , _cbvDescriptorSize{ 0 }
        , _allocatedDescriptorsCount{ 0 }
        , _fenceEvent{ nullptr }
        , _fence{ nullptr }
        , _fenceValue{ 0 }
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
    {
        _resourceImpl = sw::make_unique<D3D12RHIResource>( this );
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
    // D3D12RHISwapChain Implementation
    // ------------------------------------------------------------------------------

    // ------------------------------------------------------------------------------
    // D3D12RHIResource Implementation
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
            if ( _bOnlineHeapExhaustedLogged == 0 )
            {
                _bOnlineHeapExhaustedLogged = 1;
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

        // GPU 가 이 리스트의 테이블을 아직 읽는 중이다 — 현재 펜스가 지난 뒤에야 블록을 다시 내준다.
        vector<uint32> listBlock = std::move( state._listOnlineBlock );
        state._listOnlineBlock.clear();
        auto recycleCb = [this, listBlock]()
        {
            std::scoped_lock<mutex> lock{ _onlineBlockMutex };
            for ( const uint32 block : listBlock )
                _listFreeOnlineBlock.push_back( block );
        };
        _releaseQueue.enqueueGpuRelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, recycleCb ), _fenceValue );
    }

    RHIBufferHandle D3D12RHIDevice::storeBuffer( Microsoft::WRL::ComPtr<ID3D12Resource> buffer )
    {
        if ( buffer == nullptr )
            return 0;
        return _gpuBuffers.insert( std::move( buffer ) );
    }

    RHITextureHandle D3D12RHIDevice::storeTexture( Microsoft::WRL::ComPtr<ID3D12Resource> texture )
    {
        if ( texture == nullptr )
            return 0;
        return _gpuTextures.insert( std::move( texture ) );
    }

    void D3D12RHIDevice::flushDebugMessages( const utf8* pStage )
    {
    #if defined( SW_DEBUG )
        // 디바이스가 이미 제거된 상태로 한 번 로그를 남겼으면, 프레임마다 똑같은 검증 메시지
        // 수십 줄 + DRED 덤프를 무한 반복하지 않는다 — 자동 복구가 없어서 그 이후 매 프레임
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

    IRHIResource*       D3D12RHIDevice::getResource() { return _resourceImpl.get(); }
    IRHICommandContext* D3D12RHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

} // namespace sw
#endif
