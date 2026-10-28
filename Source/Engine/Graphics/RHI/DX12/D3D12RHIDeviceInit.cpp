#include "pch.h"

#include "Core/Diagnostics/CrashHandler.h"

#include "Engine/Graphics/Debug/GpuBreadcrumbs.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandContext.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHICommandList.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIDevice.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResourceFactory.h"
#include "Engine/Graphics/RHI/DX12/D3D12RHIResourcePreset.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineDefaultAssets.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiMemoryBudget.h"
    #include "Engine/Graphics/RHI/Support/RHILiveCommandListUtil.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D12" );

    // 길이 유지(긴 함수 정리 기준): 디바이스 · 큐 · 힙 · 전역 리소스 생성의 순서가 곧 의미라 나누지 않는다.
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
        }
    #endif
    #if !defined( SW_SHIPPING )
        // DRED(Device Removed Extended Data)의 자동 브레드크럼 · 페이지 폴트 - 디바이스를 만들기 **전에** 켜야 한다. 디버그 레이어가 없는 개발 빌드(Release)에서도 켠다
        // (`-gv_gpuBreadcrumbs` 가 기본 켬). 소실 뒤 마지막 패스 이름은 우리 표지(`createBreadcrumbBuffer`)가 싣고, DRED 는 명령 종류별 진행과 폴트 주소를 싣는다.
        if ( GpuBreadcrumbs::isEnabled() )
        {
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

        // 소프트웨어 어댑터(WARP) — CI 러너와 같은 래스터라이저(gv_rhiSoftwareAdapter).
        Microsoft::WRL::ComPtr<IDXGIAdapter1> softwareAdapter;
        if ( desc._bSoftwareAdapter && FAILED( factory->EnumWarpAdapter( IID_PPV_ARGS( softwareAdapter.GetAddressOf() ) ) ) )
        {
            SW_LOG_ERROR( "WARP adapter is not available (gv_rhiSoftwareAdapter)" );
            return false;
        }

        // 크래시 리포트용 어댑터 정보. 실패해도 디바이스 생성에는 영향이 없다.
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter = softwareAdapter;
            if ( adapter != nullptr || ( SUCCEEDED( factory->EnumAdapters1( 0, adapter.GetAddressOf() ) ) && adapter != nullptr ) )
            {
                DXGI_ADAPTER_DESC1 adapterDesc{};
                if ( SUCCEEDED( adapter->GetDesc1( &adapterDesc ) ) )
                {
                    utf8         arrName[constant::kMaxBuffer256]{};
                    const string name = StringUtil::utf16ToUtf8( adapterDesc.Description );
                    formatstring( arrName, constant::kMaxBuffer256, "%# (vendor %#, device %#, VRAM %# MB)", name.c_str(),
                                  adapterDesc.VendorId, adapterDesc.DeviceId,
                                  static_cast<uint32>( adapterDesc.DedicatedVideoMemory / constant::kBytesPerMegabyte ) );
                    CrashHandler::setContextValue( "GPU", arrName );
                }
            }
        }

        if ( FAILED( D3D12CreateDevice( softwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( _device.GetAddressOf() ) ) ) )
            return false;
    #if !defined( SW_SHIPPING )
        createBreadcrumbBuffer();
    #endif
        // 메모리 질의는 디바이스가 실제로 쓰는 어댑터에 한다(기본 어댑터를 가정하지 않는다). 못 찾으면 드라이버 값은 "모름" 이다.
        _memoryAdapter = findDxgiAdapterByLuid( _device->GetAdapterLuid() );
        // 실제로 선 어댑터가 소프트웨어인지 — 요청이 아니라 결과를 적는다.
        if ( softwareAdapter != nullptr )
        {
            DXGI_ADAPTER_DESC1 softwareDesc{};
            const LUID         deviceLuid = _device->GetAdapterLuid();
            _bSoftwareAdapter             = SUCCEEDED( softwareAdapter->GetDesc1( &softwareDesc ) ) && softwareDesc.AdapterLuid.LowPart == deviceLuid.LowPart &&
                                softwareDesc.AdapterLuid.HighPart == deviceLuid.HighPart;
        }

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
                // 생성 때 준 최적 클리어 값과 다른 색으로 지우면 "느릴 수 있다" 는 성능 안내뿐이다(지우기는 맞게 된다). 렌더 타깃을 여러 클리어 색으로 다시 쓰는
                // 그래프 · 에디터 뷰에서 늘 나므로 언리얼 D3D12RHI 처럼 거른다.
                D3D12_MESSAGE_ID        arrDeniedID[] = { D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE, D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE };
                D3D12_INFO_QUEUE_FILTER filter{};
                filter.DenyList.NumIDs  = static_cast<UINT>( std::size( arrDeniedID ) );
                filter.DenyList.pIDList = arrDeniedID;
                if ( FAILED( infoQueue->PushStorageFilter( &filter ) ) )
                    SW_LOG_INFO( "D3D12 debug layer storage filter was not applied" );
            }
            // 큐는 장치 실패 때만 비운다(flushDebugMessages). 경고가 나는 그 자리에서 보이게 콜백을 단다(Windows 11 의 디버그 레이어부터 있다).
            Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue1;
            const bool                               bCallbackRegistered = SUCCEEDED( _device.As( &infoQueue1 ) ) &&
                                             SUCCEEDED( infoQueue1->RegisterMessageCallback( &D3D12RHIDevice::onValidationMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                                                                             this, &_validationCallbackCookie ) );
            if ( bCallbackRegistered == false )
            {
                _validationCallbackCookie = 0;
                SW_LOG_INFO( "D3D12 debug layer has no message callback (ID3D12InfoQueue1) - validation warnings show only when the device fails" );
            }
        }
    #endif

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if ( isHrSuccess( _device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( _commandQueue.GetAddressOf() ) ), "CreateCommandQueue" ) == false )
            return false;

        _swapChain.setBarrierWatcher( this );
        if ( _swapChain.initialize( factory.Get(), _commandQueue.Get(), desc ) == false )
            return false;

        _bBindlessRootSignature = SW_FALSE;
        _frameStreamState       = D3D12RecordingState{};

        // 백버퍼와 오프스크린 렌더 타깃이 **같은 RTV 힙**을 나눠 쓴다. 앞쪽 bufferCount 칸이 백버퍼,
        // 그 뒤가 오프스크린이다. 그래서 이 힙은 스왑체인이 아니라 디바이스가 소유한다.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.NumDescriptors = _swapChain.getBufferCount() + kMaxOffscreenRtvs;
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( isHrSuccess( _device->CreateDescriptorHeap( &rtvHeapDesc, IID_PPV_ARGS( _rtvHeap.GetAddressOf() ) ), "CreateDescriptorHeap" ) == false )
            return false;

        _rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.NumDescriptors = kMaxOffscreenDsvs;
        dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( isHrSuccess( _device->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( _dsvHeap.GetAddressOf() ) ), "CreateDescriptorHeap" ) == false )
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
        cbvHeapDesc.NumDescriptors = kMaxShaderVisibleDescriptors;
        cbvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if ( isHrSuccess( _device->CreateDescriptorHeap( &cbvHeapDesc, IID_PPV_ARGS( _cbvHeap.GetAddressOf() ) ), "CreateDescriptorHeap" ) == false )
            return false;

        _cbvDescriptorSize = _device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
        // GPU 메모리를 차지하는 것은 셰이더 가시 힙뿐이다(RTV · DSV · 오프라인 힙은 CPU 메모리). 크기는 칸 수 × 칸 크기다.
        getMemoryLedger().recordAllocation( RHIMemoryKey::makeDeviceObject( _cbvHeap.Get() ), RHIMemoryKind::Descriptor,
                                            static_cast<uint64>( cbvHeapDesc.NumDescriptors ) * _cbvDescriptorSize );

        // 오프라인(CPU 전용) 뷰 힙. 슬롯 테이블은 CopyDescriptors 로 굳히는데 셰이더 가시 힙은 복사 원본이 될 수 없다.
        // 등록은 뷰를 두 힙에 같은 인덱스로 만들고, 마지막 두 칸은 안 걸린 슬롯을 채우는 null 뷰다.
        D3D12_DESCRIPTOR_HEAP_DESC offlineHeapDesc{};
        offlineHeapDesc.NumDescriptors = kOfflineDescriptorCount;
        offlineHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        offlineHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if ( isHrSuccess( _device->CreateDescriptorHeap( &offlineHeapDesc, IID_PPV_ARGS( _offlineViewHeap.GetAddressOf() ) ), "CreateDescriptorHeap" ) == false )
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

            // 레지스트리 0 번 = null Texture2D SRV(kNullTextureBindlessIndex). 등록은 그 다음 칸부터 받는다 — 0 으로 남은 텍스처 인덱스가
            // 상수버퍼 · 구조버퍼 뷰를 Texture2D 로 읽지 않고 0 을 읽는다.
            D3D12_SHADER_RESOURCE_VIEW_DESC nullTexture{};
            nullTexture.ViewDimension              = D3D12_SRV_DIMENSION_TEXTURE2D;
            nullTexture.Format                     = DXGI_FORMAT_R8G8B8A8_UNORM;
            nullTexture.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            nullTexture.Texture2D.MipLevels        = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE onlineNull = _cbvHeap->GetCPUDescriptorHandleForHeapStart();
            onlineNull.ptr += static_cast<SIZE_T>( kNullTextureBindlessIndex ) * _cbvDescriptorSize;
            _device->CreateShaderResourceView( nullptr, &nullTexture, onlineNull );
            _device->CreateShaderResourceView( nullptr, &nullTexture, offlineDescriptorAt( kNullTextureBindlessIndex ) );
            _allocatedDescriptorsCount = kNullTextureBindlessIndex + 1;
        }
        {
            ScopedLock<mutex> lock{ _onlineBlockMutex };
            _listFreeOnlineBlock.clear();
            for ( uint32 block = kOnlineBlockCount; block > 0; --block )
            {
                _listFreeOnlineBlock.push_back( block - 1 );
            }
            _bOnlineHeapExhaustedLogged = SW_FALSE;
        }

        for ( uint32 frameIndex = 0; frameIndex < constant::kMaxFrameCountInFlight; ++frameIndex )
        {
            if ( isHrSuccess( _device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( _arrCommandAllocator[frameIndex].GetAddressOf() ) ), "CreateCommandAllocator" ) == false )
                return false;
            // 디버그 레이어의 "allocator is being reset [in use]" 메시지는 객체 이름을 찍는다. 이름이 없으면
            // 어느 얼로케이터가 문제인지 주소만 남아 추적이 안 된다.
            utf16 arrName[constant::kMaxBuffer64]{};
            swprintf_s( arrName, L"FrameStreamAllocator%u", frameIndex );
            _arrCommandAllocator[frameIndex]->SetName( arrName );
        }

        if ( isHrSuccess( _device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, _arrCommandAllocator[0].Get(), nullptr, IID_PPV_ARGS( _commandList.GetAddressOf() ) ), "CreateCommandList" ) == false )
            return false;

        // 갓 만든 리스트는 열린 채라 닫아 둔다(첫 프레임이 Reset 으로 연다). 빈 리스트 닫기가 실패하면 장치가 쓸 수 없는 상태다.
        if ( isHrSuccess( _commandList->Close(), "ID3D12GraphicsCommandList::Close (initial)" ) == false )
            return false;
        _frameStreamState._bRecording = SW_FALSE;
        _frameRing.reset();

        _swapChain.createBackBuffers( _device.Get(), _rtvHeap.Get(), _rtvDescriptorSize );

        if ( isHrSuccess( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _fence.GetAddressOf() ) ), "CreateFence" ) == false )
            return false;
        _fenceValue = 1;
        // 전송 전용 펜스: 전송 작업(복사 리스트 하나)을 낼 때마다 한 값을 올린다. 프레임 펜스와 따로라 전송 완료를 프레임 없이도 알 수 있다(RhiAsyncUploads).
        if ( isHrSuccess( _device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( _transferFence.GetAddressOf() ) ), "CreateFence" ) == false )
            return false;
        _transferFenceValue = 0;
        if ( initializeTransfers( desc ) == false )
            return false;

        if ( createGlobalResources() == false )
            return false;

        _frameStreamContext = sw::make_unique<D3D12RHICommandContext>( this, _commandList.Get(), &_frameStreamState );

        return true;
    }

    void D3D12RHIDevice::detachCommandRecordingInternal()
    {
        _frameStreamContext.reset();
        RHILiveCommandListUtil::detachAll( _liveCmdListMutex, _listLiveCmd );
    }

    #if !defined( SW_SHIPPING )
    void D3D12RHIDevice::createBreadcrumbBuffer()
    {
        if ( GpuBreadcrumbs::isEnabled() == false || _device == nullptr )
            return;
        // 읽기 힙(CPU 가 읽는 시스템 메모리) 4 바이트 - GPU 가 소실 중에 쓴 값이 소실 뒤에도 CPU 에서 읽힌다. 읽기 힙 버퍼의 상태는 COPY_DEST 로 고정이다.
        const D3D12_HEAP_PROPERTIES heapProperties = D3D12RHIResourcePreset::heapProperties( D3D12_HEAP_TYPE_READBACK );
        const D3D12_RESOURCE_DESC   bufferDesc     = D3D12RHIResourcePreset::bufferDesc( sizeof( uint32 ) );
        if ( isHrSuccess( _device->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                            IID_PPV_ARGS( _breadcrumbBuffer.GetAddressOf() ) ),
                          "CreateCommittedResource(breadcrumb)" ) == false )
        {
            SW_LOG_WARNING( "The D3D12 GPU breadcrumb buffer could not be created - device lost reports only the CPU-recorded last pass" );
            return;
        }
        void*       pMapped = nullptr;
        D3D12_RANGE noRead{ 0, 0 };
        if ( isHrSuccess( _breadcrumbBuffer->Map( 0, &noRead, &pMapped ), "Map(breadcrumb)" ) == false || pMapped == nullptr )
        {
            _breadcrumbBuffer.Reset();
            return;
        }
        *static_cast<uint32*>( pMapped ) = 0;
        _pBreadcrumbMapped               = static_cast<const volatile uint32*>( pMapped );
        _breadcrumbAddress               = _breadcrumbBuffer->GetGPUVirtualAddress();
        SW_LOG_INFO( "D3D12 GPU breadcrumbs enabled (WriteBufferImmediate markers)" );
    }

    uint32 D3D12RHIDevice::readLastBreadcrumbs( uint32* pOutId, uint32 capacity )
    {
        if ( _pBreadcrumbMapped == nullptr || pOutId == nullptr || capacity == 0 )
            return 0;
        const uint32 marker = *_pBreadcrumbMapped;
        if ( marker == 0 )
            return 0;
        pOutId[0] = marker;
        return 1;
    }
    #else
    uint32 D3D12RHIDevice::readLastBreadcrumbs( uint32* pOutId, uint32 capacity )
    {
        (void)pOutId;
        (void)capacity;
        return 0;
    }
    #endif

    void D3D12RHIDevice::shutdownInternal()
    {
    #if !defined( SW_SHIPPING )
        if ( _breadcrumbBuffer != nullptr && _pBreadcrumbMapped != nullptr )
            _breadcrumbBuffer->Unmap( 0, nullptr );
        _pBreadcrumbMapped = nullptr;
        _breadcrumbAddress = 0;
        _breadcrumbBuffer.Reset();
    #endif
        _mapOffscreenTexture.clear();
        _pipelineStates.clear();
        _renderPassLedger.clear();
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
        getMemoryLedger().recordFree( RHIMemoryKey::makeDeviceObject( _cbvHeap.Get() ) );
        _cbvHeap.Reset();
        _offlineViewHeap.Reset();
        {
            ScopedLock<mutex> lock{ _onlineBlockMutex };
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
            ScopedLock<mutex> lock{ _cmdListPoolMutex };
            _listFreeCmdListEntry.clear();
        }
        _frameStreamState._bRecording = SW_FALSE;
        _bBindlessRootSignature       = SW_FALSE;
        // 전송 작업 · 스테이징 링 · 전송을 기다리던 해제는 큐와 펜스를 놓기 전에 정리한다(GPU 는 종료 2단계에서 쉬었다).
        shutdownTransfers();
        _fence.Reset();
        _transferFence.Reset();
        _commandQueue.Reset();
        _memoryAdapter.Reset();
        Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue1;
        const bool                               bHasValidationCallback = _validationCallbackCookie != 0 && _device != nullptr && SUCCEEDED( _device.As( &infoQueue1 ) );
        if ( bHasValidationCallback )
            infoQueue1->UnregisterMessageCallback( _validationCallbackCookie );
        _validationCallbackCookie = 0;
        infoQueue1.Reset();
        _device.Reset();

        _fenceValue        = 0;
        _rtvDescriptorSize = 0;
        _cbvDescriptorSize = 0;
    }

    void D3D12RHIDevice::resizeInternal( uint32 width, uint32 height )
    {
        // resize 는 렌더 스레드를 세운 뒤(`App::onResize` 의 `waitIdle`) 부른다 — 그동안 프레임 진행 상태(백 버퍼 · 프레임 펜스)는 이 스레드가 렌더 스레드 역할을 빌려 만진다.
        ScopedThreadRoleBorrow borrow{ _swapChain.getRenderThreadRole() };
        if ( _swapChain.isValid() == false || ( width == 0 && height == 0 ) )
            return;

        if ( _frameStreamState._bRecording != SW_FALSE && _commandList != nullptr )
        {
            {
                ScopedLock<mutex> uploadLock{ _uploadSlotMutex };
                flushPendingUploads( true );
            }
            requireHr( _commandList->Close(), "ID3D12GraphicsCommandList::Close (resize)" );
            ID3D12CommandList* arrCommandList[] = { _commandList.Get() };
            if ( _commandQueue != nullptr )
                _commandQueue->ExecuteCommandLists( 1, arrCommandList );
            _frameStreamState._bRecording = SW_FALSE;
            releaseOnlineBlocksDeferred( _frameStreamState );
        }

        waitForPreviousFrame();
        _swapChain.releaseBackBuffers();
        if ( _swapChain.resize( width, height ) == false )
            return;
        _swapChain.createBackBuffers( _device.Get(), _rtvHeap.Get(), _rtvDescriptorSize );
        _swapChain.acquireNextImage();
    }

    void D3D12RHIDevice::onRenderThreadRoleChanged( std::thread::id owner )
    {
        _swapChain.bindRenderThread( owner );
    }
} // namespace sw
#endif
