/**
 * @file D3D11RHIDeviceInit.cpp
 * @brief 디바이스·스왑체인 생성과 해제, 리사이즈 (DX12 · Vulkan · GL 의 같은 이름 파일과 같은 자리).
 */
#include "pch.h"

#include "Engine/Graphics/RHI/DX11/D3D11RHICommandContext.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHICommandList.h"
#include "Engine/Graphics/RHI/DX11/D3D11RHIDevice.h"
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
} // namespace sw
#endif
