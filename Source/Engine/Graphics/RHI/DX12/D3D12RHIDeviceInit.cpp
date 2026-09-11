#include "pch.h"

#include "Core/Process/CrashHandler.h"

#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

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

        // 크래시 리포트용 어댑터 정보 — 실패해도 디바이스 생성에는 영향이 없다.
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if ( SUCCEEDED( factory->EnumAdapters1( 0, adapter.GetAddressOf() ) ) && adapter != nullptr )
            {
                DXGI_ADAPTER_DESC1 adapterDesc{};
                if ( SUCCEEDED( adapter->GetDesc1( &adapterDesc ) ) )
                {
                    utf8         arrName[constant::kMaxBuffer256]{};
                    const string name = StringUtil::utf16ToUtf8( adapterDesc.Description );
                    formatstring( arrName, constant::kMaxBuffer256, "%# (vendor %#, device %#, VRAM %# MB)", name.c_str(),
                                  adapterDesc.VendorId, adapterDesc.DeviceId,
                                  static_cast<uint32>( adapterDesc.DedicatedVideoMemory / ( 1024ull * 1024ull ) ) );
                    CrashHandler::setContextValue( "GPU", arrName );
                }
            }
        }

        if ( FAILED( D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( _device.GetAddressOf() ) ) ) )
            return false;

        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if ( SUCCEEDED( _device->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) ) ) )
        {
            if ( options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 )
                SW_LOG_TRACE( "Device supports Resource Binding Tier 3 (Bindless)." );
            else
                SW_LOG_WARNING( "Device does NOT support Resource Binding Tier 3. Fallback may be required." );
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

        _swapChain.setBarrierWatcher( this );
        if ( _swapChain.initialize( factory.Get(), _commandQueue.Get(), desc ) == false )
            return false;

        _bBindlessRootSignature = 0;
        _frameStreamState       = D3D12RecordingState{};

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

        // 오프라인(CPU 전용) 뷰 힙 — 슬롯 테이블은 CopyDescriptors 로 굳히는데 셰이더 가시 힙은 복사 원본이 될 수 없다.
        // 등록은 뷰를 두 힙에 같은 인덱스로 만들고, 마지막 두 칸은 안 걸린 슬롯을 채우는 null 뷰다.
        D3D12_DESCRIPTOR_HEAP_DESC offlineHeapDesc{};
        offlineHeapDesc.NumDescriptors = kOfflineDescriptorCount;
        offlineHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        offlineHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( FAILED( _device->CreateDescriptorHeap( &offlineHeapDesc, IID_PPV_ARGS( _offlineViewHeap.GetAddressOf() ) ) ) )
            return false;
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
            nullSrv.ViewDimension           = D3D12_SRV_DIMENSION_BUFFER;
            nullSrv.Format                  = DXGI_FORMAT_R32_TYPELESS;
            nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullSrv.Buffer.NumElements      = 1;
            nullSrv.Buffer.Flags            = D3D12_BUFFER_SRV_FLAG_RAW;
            _device->CreateShaderResourceView( nullptr, &nullSrv, offlineDescriptorAt( kOfflineNullSrvIndex ) );

            D3D12_UNORDERED_ACCESS_VIEW_DESC nullUav{};
            nullUav.ViewDimension      = D3D12_UAV_DIMENSION_BUFFER;
            nullUav.Format             = DXGI_FORMAT_R32_TYPELESS;
            nullUav.Buffer.NumElements = 1;
            nullUav.Buffer.Flags       = D3D12_BUFFER_UAV_FLAG_RAW;
            _device->CreateUnorderedAccessView( nullptr, nullptr, &nullUav, offlineDescriptorAt( kOfflineNullUavIndex ) );
        }
        {
            std::scoped_lock<mutex> lock{ _onlineBlockMutex };
            _listFreeOnlineBlock.clear();
            for ( uint32 block = kOnlineBlockCount; block > 0; --block )
                _listFreeOnlineBlock.push_back( block - 1 );
            _bOnlineHeapExhaustedLogged = 0;
        }

        for ( uint32 frameIndex = 0; frameIndex < constant::kMaxFrameCountInFlight; ++frameIndex )
        {
            if ( FAILED( _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _arrCommandAllocator[frameIndex].GetAddressOf() ) ) ) )
                return false;
            if ( FAILED( _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _arrFrameCmdAllocator[frameIndex].GetAddressOf() ) ) ) )
                return false;
            // 디버그 레이어의 "allocator is being reset [in use]" 메시지는 객체 이름을 찍는다 — 이름이 없으면
            // 어느 얼로케이터가 문제인지 주소만 남아 추적이 안 된다.
            utf16 arrName[constant::kMaxBuffer64]{};
            swprintf_s( arrName, L"FrameStreamAllocator%u", frameIndex );
            _arrCommandAllocator[frameIndex]->SetName( arrName );
            swprintf_s( arrName, L"FrameCmdAllocator%u", frameIndex );
            _arrFrameCmdAllocator[frameIndex]->SetName( arrName );
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

        // 커맨드 리스트는 디바이스보다 오래 살 수 있다. 여기서 연결을 끊지 않으면 그쪽 소멸자가
        // 이미 파괴된 이 디바이스에 리스트·온라인 블록을 반납하려 든다. DX11 은 이 보호를 갖고
        // 있었는데 DX12 에는 없었다.
        {
            std::scoped_lock<mutex> lock{ _liveCmdListMutex };
            for ( D3D12RHICommandList* pLiveList : _listLiveCmdList )
            {
                if ( pLiveList != nullptr )
                    pLiveList->detachFromDevice();
            }
            _listLiveCmdList.clear();
        }

        _mapOffscreenTexture.clear();
        _pipelineStates.clear();
        _listRenderPass.clear();
        _listRegisteredBindless.clear();
        _listFreeBindless.clear();
        _listRegisteredUAV.clear();
        _mapStructuredBufferState.clear();
        _gpuBuffers.clear();
        _gpuTextures.clear();
        _frameStreamState          = D3D12RecordingState{};
        _nextOffscreenRtvIndex     = 0;
        _allocatedDescriptorsCount = 0;

        _swapChain.shutdown();
        _vertexBuffer.Reset();
        _rootSignature.Reset();
        _drawCommandSignature.Reset();
        _drawIndexedCommandSignature.Reset();
        _dispatchCommandSignature.Reset();
        _cbvHeap.Reset();
        _offlineViewHeap.Reset();
        {
            std::scoped_lock<mutex> lock{ _onlineBlockMutex };
            _listFreeOnlineBlock.clear();
        }
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
        _bBindlessRootSignature = 0;
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
            releaseOnlineBlocksDeferred( _frameStreamState );
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
