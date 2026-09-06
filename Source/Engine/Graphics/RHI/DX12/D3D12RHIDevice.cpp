#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    D3D12RHIDevice::D3D12RHIDevice()
        : _device{ nullptr }
        , _commandQueue{ nullptr }
        , _swapChain{ nullptr }
        , _rtvHeap{ nullptr }
        , _dsvHeap{ nullptr }
        , _cbvHeap{ nullptr }
        , _rootSignature{ nullptr }
        , _computeRootSignature{ nullptr }
        , _vertexBuffer{ nullptr }
        , _drawCommandSignature{ nullptr }
        , _drawIndexedCommandSignature{ nullptr }
        , _dispatchCommandSignature{ nullptr }
        , _arrCommandAllocator{}
        , _commandList{ nullptr }
        , _arrFrameCmdAllocator{}
        , _cmdListPoolMutex{}
        , _listFreeCmdListEntry{}
        , _frameRing{}
        , _listRenderTarget{}
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
        , _swapchainState{ D3D12_RESOURCE_STATE_PRESENT }
        , _bHeapDirectlyIndexed{ SW_FALSE }
        , _bDeviceRemovedLogged{ SW_FALSE }
        , _reservedPassFlags{ 0 }
        , _frameStreamState{}
        , _listRegisteredBindless{}
        , _listFreeBindless{}
        , _listRegisteredUAV{}
        , _listFreeUav{}
        , _rtvDescriptorSize{ 0 }
        , _cbvDescriptorSize{ 0 }
        , _allocatedDescriptorsCount{ 0 }
        , _frameIndex{ 0 }
        , _fenceEvent{ nullptr }
        , _fence{ nullptr }
        , _fenceValue{ 0 }
        , _pHWnd{ nullptr }
        , _width{ 0 }
        , _height{ 0 }
        , _bufferCount{ 2 }
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _frameStreamContext{ nullptr }
        , _swapChainImpl{ nullptr }
        , _resourceImpl{ nullptr }
    {
        _swapChainImpl = sw::make_unique<D3D12RHISwapChain>( this );
        _resourceImpl  = sw::make_unique<D3D12RHIResource>( this );
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
                {
                    SW_LOG_ERROR( "PageFault VA=0x%#", Fmt( static_cast<uint64>( pageFaultOutput.PageFaultVA ), Format( 16, Format::Padding::Zero ).hexUpper() ) );
                }
            }
        }
    #else
        (void)pStage;
    #endif
    }

    IRHISwapChain*      D3D12RHIDevice::getSwapChain() { return _swapChainImpl.get(); }
    IRHIResource*       D3D12RHIDevice::getResource() { return _resourceImpl.get(); }
    IRHICommandContext* D3D12RHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

} // namespace sw
#endif
