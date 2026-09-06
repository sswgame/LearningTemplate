#include "pch.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
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

    bool D3D12RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        _pHWnd       = static_cast<HWND>( desc._pWindowHandle );
        _width       = desc._width;
        _height      = desc._height;
        _bufferCount = desc._bufferCount;

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

        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.BufferCount      = _bufferCount;
        scDesc.Width            = _width;
        scDesc.Height           = _height;
        scDesc.Format           = toDxgiFormat( desc._format );
        scDesc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc.Count = 1;

        if ( _pHWnd != nullptr && _width > 0 && _height > 0 )
        {
            Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
            if ( FAILED( factory->CreateSwapChainForHwnd( _commandQueue.Get(), _pHWnd, &scDesc, nullptr, nullptr, swapChain1.GetAddressOf() ) ) )
                return false;

            swapChain1.As( &_swapChain );
            _frameIndex = _swapChain->GetCurrentBackBufferIndex();
        }

        _bHeapDirectlyIndexed = 0;
        _swapchainState       = D3D12_RESOURCE_STATE_PRESENT;
        _frameStreamState     = D3D12RecordingState{};

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = _bufferCount + kMaxOffscreenRtvs;
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

        createRenderTargets();

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

        cleanupRenderTargets();
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
        _swapChain.Reset();
        _commandQueue.Reset();
        _device.Reset();

        if ( _fenceEvent != nullptr )
        {
            CloseHandle( _fenceEvent );
            _fenceEvent = nullptr;
        }

        _fenceValue        = 0;
        _frameIndex        = 0;
        _rtvDescriptorSize = 0;
        _cbvDescriptorSize = 0;
    }

    void D3D12RHIDevice::createRenderTargets()
    {
        if ( _swapChain == nullptr || _device == nullptr || _rtvHeap == nullptr )
            return;

        _listRenderTarget.resize( _bufferCount );
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle( _rtvHeap->GetCPUDescriptorHandleForHeapStart() );

        for ( UINT bufferIndex = 0; bufferIndex < _bufferCount; ++bufferIndex )
        {
            const HRESULT getHr = _swapChain->GetBuffer( bufferIndex, IID_PPV_ARGS( _listRenderTarget[bufferIndex].GetAddressOf() ) );
            if ( FAILED( getHr ) )
            {
                SW_LOG_ERROR( "GetBuffer(%#) failed hr=0x%#", bufferIndex, static_cast<uint32>( getHr ) );
                // 일부 슬롯만 null인 채로 남겨두면 크기는 정상(_bufferCount)이라 empty()/범위 체크를
                // 통과해버려서, beginFrame()이 null 리소스를 그대로 ResourceBarrier에 넘겨 GPU가
                // NULL VA를 참조하는 PageFault로 이어진다(디바이스 행 상황에서 실제로 발생 확인).
                // 부분 성공을 허용하지 말고 전체를 비워 "준비 안 됨" 상태로 통일한다.
                cleanupRenderTargets();
                return;
            }
            _device->CreateRenderTargetView( _listRenderTarget[bufferIndex].Get(), nullptr, rtvHandle );
            rtvHandle.ptr += _rtvDescriptorSize;
        }
    }

    void D3D12RHIDevice::cleanupRenderTargets()
    {
        for ( Microsoft::WRL::ComPtr<ID3D12Resource>& rt : _listRenderTarget )
        {
            rt.Reset();
        }
        _listRenderTarget.clear();
    }

} // namespace sw
#endif
