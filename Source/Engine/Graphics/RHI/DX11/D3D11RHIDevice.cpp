#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHISwapChain.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/ShaderCache.h"

namespace sw
{
    SW_LOG_CALLER( "D3D11" );

    namespace
    {

        constexpr uint32 kDefaultNumerator  = 60;
        constexpr uint32 kDefaultDenomiator = 1;

    } // namespace

    D3D11RHIDevice::D3D11RHIDevice()
        : _device{ nullptr }
        , _deviceContext{ nullptr }
        , _contextOwnerThread{}
        , _swapChain{ nullptr }
        , _renderTargetView{ nullptr }
        , _vertexBuffer{ nullptr }
        , _gpuBuffers{}
        , _gpuTextures{}
        , _listRegisteredBindless{}
        , _listBindlessFree{}
        , _listRegisteredTexture{}
        , _listTextureFree{}
        , _listRegisteredUAV{}
        , _listUavSourceBuffer{}
        , _listUavFree{}
        , _computeRootConstantCB{ nullptr }
        , _arrComputeRootConstantShadow{}
        , _pipelineStates{}
        , _listRenderPass{}
        , _depthEnabledState{ nullptr }
        , _depthDisabledState{ nullptr }
        , _linearSampler{ nullptr }
        , _pHWnd{ nullptr }
        , _width{ 0 }
        , _height{ 0 }
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _frameStreamContext{ nullptr }
        , _swapChainImpl{ nullptr }
        , _resourceImpl{ nullptr }
    {
        _swapChainImpl = sw::make_unique<D3D11RHISwapChain>( this );
        _resourceImpl  = sw::make_unique<D3D11RHIResource>( this );
    }

    D3D11RHIDevice::~D3D11RHIDevice()
    {
        shutdown();
    }

    IRHISwapChain*      D3D11RHIDevice::getSwapChain() { return _swapChainImpl.get(); }
    IRHIResource*       D3D11RHIDevice::getResource() { return _resourceImpl.get(); }
    IRHICommandContext* D3D11RHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

    bool D3D11RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        _pHWnd  = static_cast<HWND>( desc._pWindowHandle );
        _width  = desc._width;
        _height = desc._height;

        // Use FLIP_DISCARD to match DX12 (and DXGI HWND rules): after a flip-model
        // swapchain has been created for an HWND, subsequent DISCARD/blt chains on the
        // same window can Present without updating what the user sees (frozen frame).
        DXGI_SWAP_CHAIN_DESC swapChainDesc{};
        swapChainDesc.BufferCount                        = ( desc._bufferCount < 2 ) ? 2 : desc._bufferCount;
        swapChainDesc.BufferDesc.Width                   = _width;
        swapChainDesc.BufferDesc.Height                  = _height;
        swapChainDesc.BufferDesc.Format                  = toDxgiFormat( desc._format );
        swapChainDesc.BufferDesc.RefreshRate.Numerator   = kDefaultNumerator;
        swapChainDesc.BufferDesc.RefreshRate.Denominator = kDefaultDenomiator;
        swapChainDesc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow                       = _pHWnd;
        swapChainDesc.SampleDesc.Count                   = 1;
        swapChainDesc.SampleDesc.Quality                 = 0;
        swapChainDesc.Windowed                           = TRUE;
        swapChainDesc.SwapEffect                         = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        UINT createDeviceFlags{ 0 };
    #if defined( SW_DEBUG )
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        D3D_FEATURE_LEVEL           featureLevel;
        constexpr D3D_FEATURE_LEVEL arrFeatureLevel[2] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };

        HRESULT hr = D3D11CreateDeviceAndSwapChain( nullptr,
                                                    D3D_DRIVER_TYPE_HARDWARE,
                                                    nullptr,
                                                    createDeviceFlags,
                                                    arrFeatureLevel,
                                                    SW_COUNT_OF( arrFeatureLevel ),
                                                    D3D11_SDK_VERSION,
                                                    &swapChainDesc,
                                                    _swapChain.GetAddressOf(),
                                                    _device.GetAddressOf(),
                                                    &featureLevel,
                                                    _deviceContext.GetAddressOf() );

        if ( FAILED( hr ) )
        {
            SW_LOG_ERROR( "Failed to create Direct3D 11 Device and SwapChain! HRESULT: %#", hr );
            return false;
        }

        // Deferred Context 기반 병렬 기록이 실익이 있는지는 드라이버가 커맨드 리스트를 네이티브로
        // 지원하는지에 달렸다 — 미지원이면 D3D11 런타임이 소프트웨어로 에뮬레이션하므로 병렬화
        // 이득보다 오버헤드가 커진다. 그래서 이 값으로 병렬 기록 capability를 런타임에 결정한다.
        {
            D3D11_FEATURE_DATA_THREADING threadingCaps{};
            if ( SUCCEEDED( _device->CheckFeatureSupport( D3D11_FEATURE_THREADING, &threadingCaps, sizeof( threadingCaps ) ) ) )
            {
                _bDriverCommandLists = threadingCaps.DriverCommandLists != FALSE ? SW_TRUE : SW_FALSE;
                SW_LOG_INFO( "Threading caps: DriverConcurrentCreates=%#, DriverCommandLists=%#",
                             static_cast<uint32>( threadingCaps.DriverConcurrentCreates ),
                             static_cast<uint32>( threadingCaps.DriverCommandLists ) );
            }
        }

        createRenderTargetView();

        {
            D3D11_DEPTH_STENCIL_DESC dsDesc{};
            dsDesc.DepthEnable    = TRUE;
            dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
            dsDesc.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
            _device->CreateDepthStencilState( &dsDesc, _depthEnabledState.GetAddressOf() );

            dsDesc.DepthEnable    = FALSE;
            dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            _device->CreateDepthStencilState( &dsDesc, _depthDisabledState.GetAddressOf() );

            D3D11_SAMPLER_DESC sampDesc{};
            sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampDesc.MinLOD         = 0.0f;
            sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
            _device->CreateSamplerState( &sampDesc, _linearSampler.GetAddressOf() );
        }

        SW_LOG_INFO( "Direct3D 11 RHI Backend Device Initialized Successfully (FLIP_DISCARD)." );
        _frameStreamContext = sw::make_unique<D3D11RHICommandContext>( this, _deviceContext.Get() );
        return true;
    }

    void D3D11RHIDevice::shutdownInternal()
    {
        _releaseQueue.flushAll();
        _frameStreamContext.reset();
        cleanupRenderTargetView();
        _gpuTextures.clear();
        _gpuBuffers.clear();
        _depthEnabledState.Reset();
        _depthDisabledState.Reset();
        _linearSampler.Reset();
        _recordingState._activeGraphicsPso = 0;
        _listRegisteredBindless.clear();
        _listBindlessFree.clear();
        _listRegisteredTexture.clear();
        _listTextureFree.clear();
        _listRegisteredUAV.clear();
        _listUavSourceBuffer.clear();
        _listUavFree.clear();
        _computeRootConstantCB.Reset();
        Memory::set( _arrComputeRootConstantShadow, 0, sizeof( _arrComputeRootConstantShadow ) );
        _swapChain.Reset();
        _deviceContext.Reset();
        _device.Reset();
    }

    void D3D11RHIDevice::resize( uint32 width, uint32 height )
    {
        if ( _swapChain == nullptr || ( width == 0 && height == 0 ) )
            return;

        _width  = width;
        _height = height;

        cleanupRenderTargetView();
        if ( _deviceContext != nullptr )
        {
            _deviceContext->ClearState();
            _deviceContext->Flush();
        }
        const HRESULT resizeHr = _swapChain->ResizeBuffers( 0, width, height, DXGI_FORMAT_UNKNOWN, 0 );
        if ( FAILED( resizeHr ) )
        {
            SW_LOG_ERROR( "ResizeBuffers failed hr=0x%#", static_cast<uint32>( resizeHr ) );
            return;
        }
        createRenderTargetView();
    }

    void D3D11RHIDevice::beginFrame( const float4& clearColor )
    {
        if ( _deviceContext == nullptr || _swapChain == nullptr )
            return;

        // FLIP_DISCARD rotates the back buffer; reacquire RTV each frame so we clear/draw the one Present will show.
        cleanupRenderTargetView();
        createRenderTargetView();
        if ( _renderTargetView == nullptr )
            return;

        // 백버퍼 바인딩/클리어는 더 이상 여기서 하지 않는다 — beginFrame 은 프레임 수명주기 전용이고,
        // 백버퍼를 타깃으로 삼는 건 beginRenderPass(핸들 0) 가 명시적으로 한다
        // (docs/05_RHI_FrameContract.md S2). RTV 재취득은 FLIP_DISCARD 때문에 수명주기에 속한다.
        (void)clearColor;

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportY        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        D3D11_VIEWPORT vp;
        vp.Width    = static_cast<float32>( _width );
        vp.Height   = static_cast<float32>( _height );
        vp.MinDepth = kDefaultViewportMinDepth;
        vp.MaxDepth = kDefaultViewportMaxDepth;
        vp.TopLeftX = kDefaultViewportX;
        vp.TopLeftY = kDefaultViewportY;
        _deviceContext->RSSetViewports( 1, &vp );
    }

    void D3D11RHIDevice::endFrame( bool vsync, bool bPresent )
    {
        if ( _swapChain == nullptr )
            return;

        cleanupRenderTargetView();

        if ( bPresent )
        {
            const HRESULT hr = _swapChain->Present( vsync ? 1 : 0, 0 );
            if ( FAILED( hr ) )
                SW_LOG_ERROR( "Present failed hr=%#", static_cast<uint32>( hr ) );
        }

        _releaseQueue.tickFrame();
    }

    void D3D11RHIDevice::waitIdle()
    {
        if ( _deviceContext != nullptr )
            _deviceContext->Flush();
        _releaseQueue.flushAll();
    }

    bool D3D11RHIDevice::bindGraphicsContext()
    {
        // Immediate context has no MakeCurrent — exclusivity is ownership of this thread.
        _contextOwnerThread = std::this_thread::get_id();
        return _deviceContext != nullptr;
    }

    void D3D11RHIDevice::unbindGraphicsContext()
    {
        _contextOwnerThread = std::thread::id{};
    }

    void* D3D11RHIDevice::getNativeTexturePointer( RHITextureHandle texture ) const
    {
        const TextureRecord* pRec = resolveTexture( texture );
        return pRec != nullptr ? pRec->_texture.Get() : nullptr;
    }

    unique_ptr<IRHICommandList> D3D11RHIDevice::createCommandList()
    {
        unique_ptr<D3D11RHICommandList> list = make_unique<D3D11RHICommandList>( this );
        if ( list->isValid() == false )
        {
            SW_LOG_ERROR( "D3D11RHIDevice::createCommandList: 네이티브 Deferred Context 생성 실패." );
            return nullptr;
        }
        return list;
    }

    void D3D11RHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        if ( pCmdList == nullptr || _deviceContext == nullptr )
            return;
        auto*              pNative = static_cast<D3D11RHICommandList*>( pCmdList );
        ID3D11CommandList* pList   = pNative->getNativeCommandList();
        if ( pList == nullptr )
            return;
        // DX11 은 스트림을 자를 필요가 없다. 기록 대상(Deferred Context)과 제출 대상(Immediate
        // Context)이 처음부터 분리돼 있어서, 이 호출은 Immediate Context 스트림의 '지금 이 지점'에
        // 그대로 끼워진다 — DX12/Vulkan 이 세그먼트를 잘라 얻는 순서 보장을 공짜로 갖는다.
        _deviceContext->ExecuteCommandList( pList, FALSE );

        // 남은 차이는 제출 시점뿐이다. Immediate Context 는 커맨드를 모아뒀다가 드라이버가 정한
        // 때(보통 Present)에 GPU 로 보내므로, 오류가 나면 어느 리스트 때문인지 알 수 없다.
        // 즉시 모드에서는 리스트마다 밀어내 그 경계에서 오류가 드러나게 한다.
        if ( _bImmediateSubmit )
            _deviceContext->Flush();
    }

    void D3D11RHIDevice::createRenderTargetView()
    {
        if ( _swapChain == nullptr || _device == nullptr )
            return;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT                                 hr = _swapChain->GetBuffer( 0, IID_PPV_ARGS( backBuffer.GetAddressOf() ) );
        if ( FAILED( hr ) )
        {
            SW_LOG_ERROR( "GetBuffer failed hr=0x%#", static_cast<uint32>( hr ) );
            return;
        }
        _device->CreateRenderTargetView( backBuffer.Get(), nullptr, _renderTargetView.GetAddressOf() );
    }

    void D3D11RHIDevice::cleanupRenderTargetView()
    {
        _renderTargetView.Reset();
    }

    bool D3D11RHIDevice::ensureComputeRootConstantCB()
    {
        if ( _computeRootConstantCB != nullptr )
            return true;
        if ( _device == nullptr )
            return false;

        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth      = kMaxComputeRootConstantDwords * sizeof( uint32 );
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if ( FAILED( _device->CreateBuffer( &desc, nullptr, _computeRootConstantCB.GetAddressOf() ) ) )
        {
            SW_LOG_ERROR( "Failed to create compute root-constant cbuffer." );
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------------------
    // D3D11RHISwapChain Implementation
    // ------------------------------------------------------------------------------

    // ------------------------------------------------------------------------------
    // D3D11RHIResource Implementation
    // ------------------------------------------------------------------------------

    ID3D11Buffer* D3D11RHIDevice::resolveBuffer( RHIBufferHandle handle ) const
    {
        const Microsoft::WRL::ComPtr<ID3D11Buffer>* pSlot = _gpuBuffers.get( handle );
        return pSlot != nullptr ? pSlot->Get() : nullptr;
    }

    size_t D3D11RHIDevice::bindlessBufferCount() const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        return _listRegisteredBindless.size();
    }

    RHIBufferHandle D3D11RHIDevice::bindlessBufferAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredBindless.size() )
            return 0;
        return _listRegisteredBindless[index];
    }

    RHITextureHandle D3D11RHIDevice::bindlessTextureAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredTexture.size() )
            return 0;
        return _listRegisteredTexture[index];
    }

    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> D3D11RHIDevice::bindlessUavAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listRegisteredUAV.size() )
            return nullptr;
        return _listRegisteredUAV[index];
    }

    RHIBufferHandle D3D11RHIDevice::storeBuffer( Microsoft::WRL::ComPtr<ID3D11Buffer> buffer )
    {
        if ( buffer == nullptr )
            return 0;
        return _gpuBuffers.insert( std::move( buffer ) );
    }

    D3D11RHIDevice::TextureRecord* D3D11RHIDevice::resolveTexture( RHITextureHandle handle )
    {
        return _gpuTextures.get( handle );
    }

    const D3D11RHIDevice::TextureRecord* D3D11RHIDevice::resolveTexture( RHITextureHandle handle ) const
    {
        return _gpuTextures.get( handle );
    }

    RHITextureHandle D3D11RHIDevice::storeTexture( TextureRecord record )
    {
        if ( record._texture == nullptr )
            return 0;
        return _gpuTextures.insert( std::move( record ) );
    }
} // namespace sw
#endif
