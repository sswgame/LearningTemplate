#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/Shader/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    bool D3D12RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
    #if defined( SW_DEBUG )
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
            if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( debugController.GetAddressOf() ) ) ) )
            {
                debugController->EnableDebugLayer();
                SW_LOG_INFO( "Debug layer enabled." );
            }

            Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings;
            if ( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( dredSettings.GetAddressOf() ) ) ) )
            {
                dredSettings->SetAutoBreadcrumbsEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
                dredSettings->SetPageFaultEnablement( D3D12_DRED_ENABLEMENT_FORCED_ON );
                SW_LOG_INFO( "DRED (Device Removed Extended Data) diagnostic enabled." );
            }
        }
    #endif

        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        if ( FAILED( CreateDXGIFactory1( IID_PPV_ARGS( factory.GetAddressOf() ) ) ) )
            return false;

        if ( FAILED( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( _device.GetAddressOf() ) ) ) )
            return false;

        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if ( SUCCEEDED( _device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) ) ) )
        {
            if ( options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 )
            {
                SW_LOG_TRACE( "Device supports Resource Binding Tier 3 (Bindless)." );
            }
            else
            {
                SW_LOG_WARNING( "Device does NOT support Resource Binding Tier 3. Fallback may be required." );
            }
        }

    #if defined( SW_DEBUG )
        {
            Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
            if ( SUCCEEDED( _device.As( &infoQueue ) ) )
            {
                infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE );
                infoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, FALSE );
            }
        }
    #endif

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if ( FAILED( _device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( _commandQueue.GetAddressOf() ) ) ) )
            return false;

        if ( _swapChain.initialize( factory.Get(), _commandQueue.Get(), desc ) == false )
            return false;

        _bHeapDirectlyIndexed = 0;
        _frameStreamState     = D3D12RecordingState{};

        // 백버퍼와 오프스크린 렌더타깃이 **같은 RTV 힙**을 나눠 쓴다. 앞쪽 bufferCount 칸이 백버퍼,
        // 그 뒤가 오프스크린이다 — 그래서 이 힙은 스왑체인이 아니라 디바이스가 소유한다.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = _swapChain.getBufferCount() + kMaxOffscreenRtvs;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( FAILED( _device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( _rtvHeap.GetAddressOf() ) ) ) )
            return false;

        _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = kMaxOffscreenDsvs;
        dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( FAILED( _device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( _dsvHeap.GetAddressOf() ) ) ) )
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
        cbvHeapDesc.NumDescriptors = kMaxShaderVisibleDescriptors;
        cbvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if ( FAILED( _device->CreateDescriptorHeap( &cbvHeapDesc, IID_PPV_ARGS( _cbvHeap.GetAddressOf() ) ) ) )
            return false;

        _cbvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

        for ( uint32 frameIndex = 0; frameIndex < constant::kMaxFrameCountInFlight; ++frameIndex )
        {
            if ( FAILED( _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _arrCommandAllocator[frameIndex].GetAddressOf() ) ) ) )
                return false;
            if ( FAILED( _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _arrFrameCmdAllocator[frameIndex].GetAddressOf() ) ) ) )
                return false;
        }

        if ( FAILED( _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, _arrCommandAllocator[0].Get(), nullptr, IID_PPV_ARGS( _commandList.GetAddressOf() ) ) ) )
            return false;

        _commandList->Close();
        _frameStreamState._bRecording = 0;
        _frameRing.reset( 0 );

        _swapChain.createBackBuffers( _device.Get(), _rtvHeap.Get(), _rtvDescriptorSize );

        if ( FAILED( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _fence.GetAddressOf() ) ) ) )
            return false;
        _fenceValue = 1;
        _fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

        if ( createGlobalResources() == false )
            return false;

        _frameStreamContext = sw::make_unique<D3D12RHICommandContext>( this, _commandList.Get(), &_frameStreamState );

        return true;
    }

    void D3D12RHIDevice::shutdownInternal()
    {
        waitForPreviousFrame();
        _releaseQueue.flushAll();

        _mapOffscreenTexture.clear();
        _pipelineStates.clear();
        _listRenderPass.clear();
        _listRegisteredBindless.clear();
        _listFreeBindless.clear();
        _listRegisteredUAV.clear();
        _listFreeUav.clear();
        _mapStructuredBufferState.clear();
        _gpuBuffers.clear();
        _gpuTextures.clear();
        _frameStreamState          = D3D12RecordingState{};
        _nextOffscreenRtvIndex     = 0;
        _allocatedDescriptorsCount = 0;

        _swapChain.shutdown();
        _vertexBuffer.Reset();
        _rootSignature.Reset();
        _computeRootSignature.Reset();
        _drawCommandSignature.Reset();
        _drawIndexedCommandSignature.Reset();
        _dispatchCommandSignature.Reset();
        _cbvHeap.Reset();
        _dsvHeap.Reset();
        _rtvHeap.Reset();
        _nextOffscreenDsvIndex = 0;
        _commandList.Reset();
        for ( Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& allocator : _arrCommandAllocator )
        {
            allocator.Reset();
        }
        {
            std::scoped_lock<mutex> lock{ _cmdListPoolMutex };
            _listFreeCmdListEntry.clear();
        }
        for ( Microsoft::WRL::ComPtr<ID3D12CommandAllocator>& allocator : _arrFrameCmdAllocator )
        {
            allocator.Reset();
        }
        _frameStreamState._bRecording = 0;
        _frameStreamContext.reset();
        _bHeapDirectlyIndexed = 0;
        _fence.Reset();
        _commandQueue.Reset();
        _device.Reset();

        if ( _fenceEvent != nullptr )
        {
            CloseHandle( _fenceEvent );
            _fenceEvent = nullptr;
        }

        _fenceValue        = 0;
        _rtvDescriptorSize = 0;
        _cbvDescriptorSize = 0;
    }

    void D3D12RHIDevice::resize( uint32 width, uint32 height )
    {
        if ( _swapChain.isValid() == false || ( width == 0 && height == 0 ) )
            return;

        if ( _frameStreamState._bRecording != 0 && _commandList != nullptr )
        {
            _commandList->Close();
            ID3D12CommandList* arrCommandList[] = { _commandList.Get() };
            if ( _commandQueue != nullptr )
                _commandQueue->ExecuteCommandLists( 1, arrCommandList );
            _frameStreamState._bRecording = 0;
        }

        waitForPreviousFrame();
        _swapChain.releaseBackBuffers();
        if ( _swapChain.resize( width, height ) == false )
            return;
        _swapChain.createBackBuffers( _device.Get(), _rtvHeap.Get(), _rtvDescriptorSize );
        _swapChain.acquireNextImage();
    }

} // namespace sw
#endif
