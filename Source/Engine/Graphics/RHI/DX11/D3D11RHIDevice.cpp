#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIResource.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Engine/Common/EnginePlatformHeaders.h"
    #include "Engine/Config/EngineData.h"
    #include "Engine/Graphics/RHI/DX/RHIDxgiFormat.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"

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
        , _swapChain{}
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
        , _arrStaticSampler{}
        , _pHWnd{ nullptr }
        , _backBufferFormat{ constant::kBackBufferFormat }
        , _releaseQueue{ constant::kGpuReleaseFrameLatency }
        , _frameStreamContext{ nullptr }
        , _resourceImpl{ nullptr }
    {
        _resourceImpl = sw::make_unique<D3D11RHIResource>( this );
    }

    D3D11RHIDevice::~D3D11RHIDevice()
    {
        shutdown();
    }

    IRHIResource*       D3D11RHIDevice::getResource() { return _resourceImpl.get(); }
    IRHICommandContext* D3D11RHIDevice::getFrameStreamContext() { return _frameStreamContext.get(); }

    bool D3D11RHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        _pHWnd            = static_cast<HWND>( desc._pWindowHandle );
        _backBufferFormat = desc._format;

        // Use FLIP_DISCARD to match DX12 (and DXGI HWND rules): after a flip-model
        // swapchain has been created for an HWND, subsequent DISCARD/blt chains on the
        // same window can Present without updating what the user sees (frozen frame).
        DXGI_SWAP_CHAIN_DESC swapChainDesc{};
        swapChainDesc.BufferCount                        = ( desc._bufferCount < 2 ) ? 2 : desc._bufferCount;
        swapChainDesc.BufferDesc.Width                   = desc._width;
        swapChainDesc.BufferDesc.Height                  = desc._height;
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

        Microsoft::WRL::ComPtr<IDXGISwapChain> createdSwapChain;
        HRESULT                                hr = D3D11CreateDeviceAndSwapChain( nullptr,
                                                                                   D3D_DRIVER_TYPE_HARDWARE,
                                                                                   nullptr,
                                                                                   createDeviceFlags,
                                                                                   arrFeatureLevel,
                                                                                   SW_COUNT_OF( arrFeatureLevel ),
                                                                                   D3D11_SDK_VERSION,
                                                                                   &swapChainDesc,
                                                                                   createdSwapChain.GetAddressOf(),
                                                                                   _device.GetAddressOf(),
                                                                                   &featureLevel,
                                                                                   _deviceContext.GetAddressOf() );

        if ( FAILED( hr ) )
        {
            SW_LOG_ERROR( "Failed to create Direct3D 11 Device and SwapChain! HRESULT: %#", hr );
            return false;
        }

        // D3D11 은 디바이스와 스왑체인이 한 호출에서 함께 나온다 — 만들어진 것을 넘겨 소유시킨다.
        _swapChain.attach( createdSwapChain.Get(), _pHWnd, desc._width, desc._height );

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

        _swapChain.acquireNextImage( _device.Get() );

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

            // 정적 샘플러 세트 s9..s15 — DX12 루트 시그니처 정적 샘플러(D3D12RHIDeviceDescriptor.cpp)와 같은 표. 비교 샘플러(7)는
            // 에뮬 경로가 깊이를 직접 비교하므로 없다.
            struct StaticSamplerSpec
            {
                D3D11_FILTER               _filter;
                D3D11_TEXTURE_ADDRESS_MODE _address;
                uint32                     _anisotropy;
            };
            const StaticSamplerSpec arrSpec[shaderslot::kStaticSamplerArrayCount] = {
                {D3D11_FILTER_MIN_MAG_MIP_LINEAR,   D3D11_TEXTURE_ADDRESS_WRAP, 1}, // LINEAR_WRAP
                {D3D11_FILTER_MIN_MAG_MIP_LINEAR,  D3D11_TEXTURE_ADDRESS_CLAMP, 1}, // LINEAR_CLAMP
                { D3D11_FILTER_MIN_MAG_MIP_POINT,   D3D11_TEXTURE_ADDRESS_WRAP, 1}, // POINT_WRAP
                { D3D11_FILTER_MIN_MAG_MIP_POINT,  D3D11_TEXTURE_ADDRESS_CLAMP, 1}, // POINT_CLAMP
                {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_MIRROR, 1}, // LINEAR_MIRROR
                {       D3D11_FILTER_ANISOTROPIC,   D3D11_TEXTURE_ADDRESS_WRAP, 8}, // ANISO_WRAP
                { D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_BORDER, 1}, // POINT_BORDER
            };
            for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerArrayCount; ++samplerIndex )
            {
                D3D11_SAMPLER_DESC staticDesc{};
                staticDesc.Filter         = arrSpec[samplerIndex]._filter;
                staticDesc.AddressU       = arrSpec[samplerIndex]._address;
                staticDesc.AddressV       = arrSpec[samplerIndex]._address;
                staticDesc.AddressW       = arrSpec[samplerIndex]._address;
                staticDesc.MaxAnisotropy  = arrSpec[samplerIndex]._anisotropy;
                staticDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
                staticDesc.MinLOD         = 0.0f;
                staticDesc.MaxLOD         = D3D11_FLOAT32_MAX;
                _device->CreateSamplerState( &staticDesc, _arrStaticSampler[samplerIndex].GetAddressOf() );
            }
            bindStaticSamplers( _deviceContext.Get() );
        }

        // 풀스크린 삼각형 정점버퍼. **DX11 만 이게 없었다** — 멤버는 선언돼 있고 draw() 가 읽는데
        // 아무도 만들지 않아 항상 nullptr 이었다. 그래서 메시 VB 없이 그리는 패스(Present/Bloom/
        // Outline/Tonemap 등 전부)가 정점 없이 그려 아무것도 나오지 않았다. 오래 살아남은 이유는
        // 오프스크린 스모크가 "크래시 안 났다" 만 봤기 때문이다(RHITest.OffscreenDrawIsReadable 이 그 공백).
        // 좌표는 다른 백엔드와 같은 NDC 큰 삼각형이다 — fullscreentriangle.hlsl 이 변환 없이 그대로 쓴다.
        {
            const RHIVertex arrFullscreenVert[3] = {
                {{ -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
                { { 3.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
                { { -1.0f, 3.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            };
            D3D11_BUFFER_DESC vbDesc{};
            vbDesc.Usage     = D3D11_USAGE_IMMUTABLE;
            vbDesc.ByteWidth = static_cast<UINT>( sizeof( arrFullscreenVert ) );
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vbData{};
            vbData.pSysMem = arrFullscreenVert;
            if ( FAILED( _device->CreateBuffer( &vbDesc, &vbData, _vertexBuffer.GetAddressOf() ) ) )
                SW_LOG_WARNING( "Failed to create the fullscreen triangle vertex buffer." );
        }

        SW_LOG_INFO( "Direct3D 11 RHI Backend Device Initialized Successfully (FLIP_DISCARD)." );
        _frameStreamContext = sw::make_unique<D3D11RHICommandContext>( this, _deviceContext.Get() );
        return true;
    }

    void D3D11RHIDevice::shutdownInternal()
    {
        _releaseQueue.flushAll();
        _frameStreamContext.reset();

        // 커맨드 리스트는 디바이스보다 오래 살 수 있다. 여기서 연결을 끊지 않으면 그쪽 소멸자가
        // 이미 파괴된 이 디바이스의 등록 목록을 잠그려 든다.
        {
            std::scoped_lock<mutex> lock{ _liveCmdListMutex };
            for ( D3D11RHICommandList* pLiveList : _listLiveCmdList )
            {
                pLiveList->detachFromDevice();
            }
            _listLiveCmdList.clear();
        }

        _swapChain.shutdown();
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
        _deviceContext.Reset();
        _device.Reset();
    }

    void D3D11RHIDevice::resize( uint32 width, uint32 height )
    {
        if ( _swapChain.isValid() == false || ( width == 0 && height == 0 ) )
            return;

        // 백버퍼를 가리키는 참조가 하나라도 남아 있으면 ResizeBuffers 가 거부된다
        // (DXGI_ERROR_INVALID_CALL). 참조는 세 군데에 있다 — 백버퍼 RTV, Immediate Context 의
        // 바인딩, 그리고 **기록이 끝난 커맨드 리스트**다. 마지막 것을 빠뜨려서 이 백엔드는
        // 창 크기 변경이 매번 조용히 실패하고 있었다.
        _swapChain.releaseBackBufferRtv();
        {
            std::scoped_lock<mutex> lock{ _liveCmdListMutex };
            for ( D3D11RHICommandList* pLiveList : _listLiveCmdList )
            {
                pLiveList->releaseRecordedState();
            }
        }
        if ( _deviceContext != nullptr )
        {
            _deviceContext->ClearState();
            _deviceContext->Flush();
        }
        if ( _swapChain.resize( width, height ) == false )
            return;
        _swapChain.acquireNextImage( _device.Get() );
    }

    void D3D11RHIDevice::bindStaticSamplers( ID3D11DeviceContext* pContext ) const
    {
        if ( pContext == nullptr || _arrStaticSampler[0] == nullptr )
            return;
        ID3D11SamplerState* arrSampler[shaderslot::kStaticSamplerArrayCount]{};
        for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerArrayCount; ++samplerIndex )
            arrSampler[samplerIndex] = _arrStaticSampler[samplerIndex].Get();
        pContext->PSSetSamplers( shaderslot::dx11::kStaticSampler0, shaderslot::kStaticSamplerArrayCount, arrSampler );
    }

    void D3D11RHIDevice::beginFrame( const float4& clearColor )
    {
        if ( _deviceContext == nullptr || _swapChain.isValid() == false )
            return;

        // FLIP_DISCARD 는 백버퍼를 돌려 쓴다 — Present 가 보여줄 그 버퍼에 그리도록 매 프레임 다시 잡는다.
        _swapChain.acquireNextImage( _device.Get() );
        // 정적 샘플러 세트는 컨텍스트 상태라 ClearState 로 사라질 수 있다 — 프레임마다 다시 건다(값싸다).
        bindStaticSamplers( _deviceContext.Get() );
        if ( _swapChain.getBackBufferRtv() == nullptr )
            return;

        // 백버퍼 바인딩/클리어는 더 이상 여기서 하지 않는다 — beginFrame 은 프레임 수명주기 전용이고,
        // 백버퍼를 타깃으로 삼는 건 beginRenderPass(핸들 0) 가 명시적으로 한다
        // (docs/05_RHI_FrameContract.md S2). RTV 재취득은 FLIP_DISCARD 때문에 수명주기에 속한다.
        (void)clearColor;

        constexpr float32 kDefaultViewportX        = 0.0f;
        constexpr float32 kDefaultViewportY        = 0.0f;
        constexpr float32 kDefaultViewportMinDepth = 0.0f;
        constexpr float32 kDefaultViewportMaxDepth = 1.0f;

        D3D11_VIEWPORT viewport;
        viewport.Width    = static_cast<float32>( _swapChain.getWidth() );
        viewport.Height   = static_cast<float32>( _swapChain.getHeight() );
        viewport.MinDepth = kDefaultViewportMinDepth;
        viewport.MaxDepth = kDefaultViewportMaxDepth;
        viewport.TopLeftX = kDefaultViewportX;
        viewport.TopLeftY = kDefaultViewportY;
        _deviceContext->RSSetViewports( 1, &viewport );
    }

    namespace
    {
        /** @brief 출력과 입력에 같은 리소스가 동시에 걸렸을 때 D3D11 이 내는 메시지 ID 목록. */
        constexpr D3D11_MESSAGE_ID arrHazardMessageId[] = {
            D3D11_MESSAGE_ID_DEVICE_VSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_PSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_GSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_HSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_DSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_CSSETSHADERRESOURCES_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_CSSETUNORDEREDACCESSVIEWS_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETSANDUNORDEREDACCESSVIEWS_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETS_HAZARD,
            D3D11_MESSAGE_ID_DEVICE_SOSETTARGETS_HAZARD,
        };

        /**
         * @brief 리소스가 출력과 입력에 동시에 걸린 "해저드" 메시지인지 판별합니다.
         * @details D3D11 은 이걸 **WARNING** 으로 낸다. 그런데 결과는 조용한 실패다 — 런타임이 한쪽을
         *          NULL 로 강제하고 셰이더는 0 을 읽는다. 인스턴스 버퍼(t4)가 컴퓨트 UAV 에 걸린 채
         *          남아서 DX11 만 화면에 아무것도 못 그리던 게 이 경고 뒤에 숨어 있었고, 심각도로
         *          거른 탓에 로그에 한 줄도 안 나왔다. 그래서 해저드만은 ERROR 로 올린다.
         */
        bool isHazardMessage( D3D11_MESSAGE_ID id )
        {
            // switch 로 적으면 -Wswitch-enum 이 나머지 1318개를 다루라고 요구한다. 경고를 끄는
            // 대신 목록 순회로 바꾼다 — ID 를 더 넣을 때도 한 줄이다.
            for ( const D3D11_MESSAGE_ID hazardId : arrHazardMessageId )
            {
                if ( id == hazardId )
                    return true;
            }
            return false;
        }
    } // namespace

    void D3D11RHIDevice::flushDebugMessages( const utf8* pStage )
    {
    #if defined( SW_DEBUG )
        // 디버그 레이어의 CORRUPTION/ERROR 만 로그로 올린다 — WARNING(null 샘플러 → 기본 상태 등)은 정상 경로에서도 매 드로우
        // 나오므로 버린다. DX12 의 flushDebugMessages 와 같은 자리(프레임 끝)에서 부른다.
        Microsoft::WRL::ComPtr<ID3D11InfoQueue> queue;
        if ( _device == nullptr || FAILED( _device.As( &queue ) ) || queue == nullptr )
            return;
        const UINT64 count = queue->GetNumStoredMessages();
        for ( UINT64 messageIndex = 0; messageIndex < count; ++messageIndex )
        {
            SIZE_T length = 0;
            queue->GetMessage( messageIndex, nullptr, &length );
            vector<uint8>  bytes( length );
            D3D11_MESSAGE* pMessage = reinterpret_cast<D3D11_MESSAGE*>( bytes.data() );
            if ( FAILED( queue->GetMessage( messageIndex, pMessage, &length ) ) )
                continue;
            if ( pMessage->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION || pMessage->Severity == D3D11_MESSAGE_SEVERITY_ERROR ||
                 isHazardMessage( pMessage->ID ) )
                SW_LOG_ERROR( "[%#] %#", pStage, pMessage->pDescription );
        }
        queue->ClearStoredMessages();
    #else
        (void)pStage;
    #endif
    }

    void D3D11RHIDevice::endFrame( bool vsync, bool bPresent )
    {
    #if defined( SW_DEBUG )
        flushDebugMessages( "endFrame" );
    #endif
        if ( _swapChain.isValid() == false )
            return;

        _swapChain.releaseBackBufferRtv();

        if ( bPresent )
        {
            const HRESULT hr = _swapChain.present( vsync );
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

    void D3D11RHIDevice::registerCommandList( D3D11RHICommandList* pCmdList )
    {
        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        _listLiveCmdList.push_back( pCmdList );
    }

    void D3D11RHIDevice::unregisterCommandList( D3D11RHICommandList* pCmdList )
    {
        std::scoped_lock<mutex> lock{ _liveCmdListMutex };
        for ( size_t index = 0; index < _listLiveCmdList.size(); ++index )
        {
            if ( _listLiveCmdList[index] != pCmdList )
                continue;
            _listLiveCmdList[index] = _listLiveCmdList.back();
            _listLiveCmdList.pop_back();
            return;
        }
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

    RHIBufferHandle D3D11RHIDevice::uavSourceBufferAt( RHIDescriptorIndex index ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _bindlessMutex };
        if ( index >= _listUavSourceBuffer.size() )
            return RHIBufferHandle{ 0 };
        return _listUavSourceBuffer[index];
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
