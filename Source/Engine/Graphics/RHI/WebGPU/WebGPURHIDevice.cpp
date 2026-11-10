#include "pch.h"

#include "Engine/Graphics/RHI/WebGPU/WebGPURHIDevice.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/thread.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/Backend/RHILiveCommandListUtil.h"
#include "Engine/Graphics/RHI/Diagnostics/RHIMemoryTracker.h"
#include "Engine/Graphics/RHI/WebGPU/WebGPURHICommandContext.h"
#include "Engine/Graphics/RHI/WebGPU/WebGPURHIResourceFactory.h"

namespace sw
{
    namespace
    {
        struct WebGPURHIDeviceInternal
        {
            static constexpr uint64 kConstantRingBytes      = 4ull * 1024ull * 1024ull;                     ///< 프레임마다의 상수 링(넘치면 더 큰 링을 하나 더 만든다)
            static constexpr uint64 kConstantAlignment      = 256;                                          ///< 상수 버퍼 바인딩 오프셋 정렬(기본 한도 minUniformBufferOffsetAlignment)
            static constexpr uint64 kDefaultBufferBytes     = 64ull * 1024ull;                              ///< 빈 슬롯에 거는 버퍼(셰이더가 읽는 최소 크기를 넘게 넉넉히)
            static constexpr uint64 kIdleWaitTimeoutMs      = 10000;                                        ///< 네이티브 `waitIdle` 의 상한(DX11 · 전송 대기와 같다)
            static constexpr uint32 kTimestampReadbackCount = constant::render::kMaxFrameCountInFlight + 1; ///< 되읽기 버퍼 풀 — 매핑이 늦어도 그만큼은 이어 잰다(넘치면 그 프레임을 건너뛴다)
            static constexpr uint64 kTimestampBytes         = static_cast<uint64>( constant::render::kMaxGPUTimestampSlot ) * sizeof( uint64 );

            /**
             * @brief 깊이 → R32 실수 사본의 WGSL 입니다. 큰 삼각형 하나로 덮고 같은 텍셀의 깊이를 `textureLoad` 로 옮긴다(필터 없음 — 크기가 같다).
             * @details 출력은 vec4f 다 — 단일 채널 첨부에도 넉넉한 성분은 버려진다(명세: 출력 성분 수가 형식보다 적으면 안 된다).
             *          https://www.w3.org/TR/WGSL/#textureload — `textureLoad(t: texture_depth_2d, coords, level) -> f32`.
             */
            static constexpr const utf8* kDepthCopyWGSL = R"(
@group(0) @binding(0) var sourceDepth : texture_depth_2d;
@vertex fn vsMain(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {
    let corner = vec2f(f32((vertexIndex << 1u) & 2u), f32(vertexIndex & 2u));
    return vec4f(corner * 2.0 - 1.0, 0.0, 1.0);
}
@fragment fn fsMain(@builtin(position) position : vec4f) -> @location(0) vec4f {
    return vec4f(textureLoad(sourceDepth, vec2i(position.xy), 0), 0.0, 0.0, 1.0);
}
)";

            /** @brief 어댑터 요청 결과를 받습니다(WaitAnyOnly — `waitForFuture` 가 기다린다). */
            static void onAdapter( WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
            {
                (void)pUserdata2;
                if ( status != WGPURequestAdapterStatus_Success )
                    SW_LOG_ERROR_ID( "WebGPU.NoAdapter", "WebGPU: requestAdapter failed: %#", WebGPURHIUtil::fromStringView( message ) );
                *static_cast<WGPUAdapter*>( pUserdata1 ) = status == WGPURequestAdapterStatus_Success ? adapter : nullptr;
            }

            /** @brief 장치 요청 결과를 받습니다. */
            static void onDevice( WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
            {
                (void)pUserdata2;
                if ( status != WGPURequestDeviceStatus_Success )
                    SW_LOG_ERROR_ID( "WebGPU.NoDevice", "WebGPU: requestDevice failed: %#", WebGPURHIUtil::fromStringView( message ) );
                *static_cast<WGPUDevice*>( pUserdata1 ) = status == WGPURequestDeviceStatus_Success ? device : nullptr;
            }

            /** @brief 표면 형식의 RHIFormat 입니다. 엔진이 쓰는 백버퍼 형식(RGBA8 · BGRA8)이 아니면 Unknown 입니다. */
            static RHIFormat toBackBufferFormat( WGPUTextureFormat format )
            {
                if ( format == WGPUTextureFormat_RGBA8Unorm )
                    return RHIFormat::R8G8B8A8_UNORM;
                if ( format == WGPUTextureFormat_BGRA8Unorm )
                    return RHIFormat::B8G8R8A8_UNORM;
                return RHIFormat::Unknown;
            }
        };
    } // namespace

    // ------------------------------------------------------------------------------
    // 레코드 · bindless 표
    // ------------------------------------------------------------------------------
    WebGPUBufferRecord::WebGPUBufferRecord()
        : _buffer{ nullptr }
        , _sizeBytes{ 0 }
        , _elementSize{ 0 }
        , _usage{ WGPUBufferUsage_None }
        , _overrideSerial{ 0 }
        , _overrideBuffer{ nullptr }
        , _overrideOffset{ 0 }
        , _bReadback{ SW_FALSE }
        , _bReadbackReady{ SW_FALSE }
        , _bReadbackQueued{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    // ------------------------------------------------------------------------------
    // 바인드 그룹 캐시
    // ------------------------------------------------------------------------------
    WebGPUBindGroupCache::WebGPUBindGroupCache()
        : _mapGroup{}
        , _createdCount{ 0 }
        , _hitCount{ 0 }
    {
    }

    uint64 WebGPUBindGroupCache::makeKeyHash( WGPUBindGroupLayout layout, const vector<WGPUBindGroupEntry>& listEntry )
    {
        // 칸마다 섞는다 — 구조체 통째로 해시하면 패딩 바이트(값이 정해지지 않은 자리)가 섞여 같은 항목이 다른 키가 된다.
        const auto appendValue = []( uint64 hash, const auto& value )
        { return HashUtil::appendBytes64( hash, &value, sizeof( value ) ); };
        uint64 hash = appendValue( HashUtil::kFNVOffset64, layout );
        for ( const WGPUBindGroupEntry& entry : listEntry )
        {
            hash = appendValue( hash, entry.binding );
            hash = appendValue( hash, entry.buffer );
            hash = appendValue( hash, entry.offset );
            hash = appendValue( hash, entry.size );
            hash = appendValue( hash, entry.sampler );
            hash = appendValue( hash, entry.textureView );
        }
        return hash;
    }

    bool WebGPUBindGroupCache::isSameEntryList( const vector<WGPUBindGroupEntry>& listA, const vector<WGPUBindGroupEntry>& listB )
    {
        if ( listA.size() != listB.size() )
            return false;
        for ( size_t index = 0; index < listA.size(); ++index )
        {
            const WGPUBindGroupEntry& entryA = listA[index];
            const WGPUBindGroupEntry& entryB = listB[index];
            const bool                bSame  = entryA.binding == entryB.binding && entryA.buffer == entryB.buffer && entryA.offset == entryB.offset && entryA.size == entryB.size &&
                               entryA.sampler == entryB.sampler && entryA.textureView == entryB.textureView;
            if ( bSame == false )
                return false;
        }
        return true;
    }

    WGPUBindGroup WebGPUBindGroupCache::findOrCreate( WGPUDevice device, WGPUBindGroupLayout layout, const vector<WGPUBindGroupEntry>& listEntry )
    {
        vector<CachedGroup>& listBucket = _mapGroup[makeKeyHash( layout, listEntry )];
        for ( const CachedGroup& cached : listBucket )
        {
            if ( cached._layout == layout && isSameEntryList( cached._listEntry, listEntry ) )
            {
                ++_hitCount;
                return cached._group;
            }
        }
        WGPUBindGroupDescriptor desc{};
        desc.layout               = layout;
        desc.entryCount           = listEntry.size();
        desc.entries              = listEntry.data();
        const WGPUBindGroup group = wgpuDeviceCreateBindGroup( device, &desc );
        if ( group == nullptr )
            return nullptr;
        listBucket.push_back( CachedGroup{ layout, listEntry, group } );
        ++_createdCount;
        return group;
    }

    void WebGPUBindGroupCache::clear()
    {
        for ( auto& [hash, listBucket] : _mapGroup )
        {
            (void)hash;
            for ( const CachedGroup& cached : listBucket )
            {
                wgpuBindGroupRelease( cached._group );
            }
        }
        _mapGroup.clear();
        _createdCount = 0;
        _hitCount     = 0;
    }

    // ------------------------------------------------------------------------------
    // 스왑체인(표면)
    // ------------------------------------------------------------------------------
    WebGPURHISwapChain::WebGPURHISwapChain()
        : _surface{ nullptr }
        , _adapter{ nullptr }
        , _currentTexture{ nullptr }
        , _currentView{ nullptr }
        , _format{ WGPUTextureFormat_Undefined }
        , _rhiFormat{ RHIFormat::R8G8B8A8_UNORM }
        , _width{ 0 }
        , _height{ 0 }
        , _bConfigured{ SW_FALSE }
        , _bVSync{ SW_TRUE }
        , _reserved{ 0 }
    {
    }

    WebGPURHISwapChain::~WebGPURHISwapChain()
    {
        destroy();
    }

    // 길이 유지(긴 함수 정리 기준): 플랫폼마다 표면 출처 구조체 하나라 사슬을 나누면 같은 생성을 다섯 번 쓴다.
    bool WebGPURHISwapChain::createSurface( WGPUInstance instance, WGPUAdapter adapter, const RHISwapChainDesc& desc )
    {
        _adapter = adapter;
        WGPUSurfaceDescriptor surfaceDesc{};
        surfaceDesc.label = WebGPURHIUtil::toStringView( "SW Surface" );
#if defined( SW_PLATFORM_WEB )
        if ( desc._surfaceKind != NativeSurfaceKind::WebCanvas || desc._pWindowHandle == nullptr )
        {
            SW_LOG_ERROR_ID( "WebGPU.WebNeedsCanvas", "WebGPU on the web needs a canvas selector surface (NativeSurfaceKind::WebCanvas)" );
            return false;
        }
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
        source.chain.sType      = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        source.selector         = WebGPURHIUtil::toStringView( static_cast<const utf8*>( desc._pWindowHandle ) );
        surfaceDesc.nextInChain = &source.chain;
        _surface                = wgpuInstanceCreateSurface( instance, &surfaceDesc );
#elif defined( SW_PLATFORM_WINDOWS )
        WGPUSurfaceSourceWindowsHWND source{};
        source.chain.sType      = WGPUSType_SurfaceSourceWindowsHWND;
        source.hinstance        = GetModuleHandleW( nullptr );
        source.hwnd             = desc._pWindowHandle;
        surfaceDesc.nextInChain = &source.chain;
        _surface                = wgpuInstanceCreateSurface( instance, &surfaceDesc );
#elif defined( SW_PLATFORM_LINUX )
        if ( desc._surfaceKind == NativeSurfaceKind::Wayland )
        {
            WGPUSurfaceSourceWaylandSurface source{};
            source.chain.sType      = WGPUSType_SurfaceSourceWaylandSurface;
            source.display          = desc._pWindowDisplay;
            source.surface          = desc._pWindowHandle;
            surfaceDesc.nextInChain = &source.chain;
            _surface                = wgpuInstanceCreateSurface( instance, &surfaceDesc );
        }
        else
        {
            WGPUSurfaceSourceXlibWindow source{};
            source.chain.sType      = WGPUSType_SurfaceSourceXlibWindow;
            source.display          = desc._pWindowDisplay;
            source.window           = static_cast<uint64>( reinterpret_cast<uintptr_t>( desc._pWindowHandle ) );
            surfaceDesc.nextInChain = &source.chain;
            _surface                = wgpuInstanceCreateSurface( instance, &surfaceDesc );
        }
#elif defined( SW_PLATFORM_ANDROID )
        WGPUSurfaceSourceAndroidNativeWindow source{};
        source.chain.sType      = WGPUSType_SurfaceSourceAndroidNativeWindow;
        source.window           = desc._pWindowHandle;
        surfaceDesc.nextInChain = &source.chain;
        _surface                = wgpuInstanceCreateSurface( instance, &surfaceDesc );
#else
    #error "Unsupported platform"
#endif
        if ( _surface == nullptr )
            SW_LOG_ERROR_ID( "WebGPU.SurfaceFailed", "WebGPU: wgpuInstanceCreateSurface failed" );
        return _surface != nullptr;
    }

    bool WebGPURHISwapChain::configure( WGPUDevice device, uint32 width, uint32 height, bool bVSync )
    {
        _width  = width;
        _height = height;
        _bVSync = bVSync ? SW_TRUE : SW_FALSE;
        if ( _surface == nullptr || width == 0 || height == 0 )
            return width == 0 || height == 0; // 최소화는 실패가 아니다 — 다음 크기 변경이 구성한다
        WGPUSurfaceCapabilities capabilities{};
        if ( wgpuSurfaceGetCapabilities( _surface, _adapter, &capabilities ) != WGPUStatus_Success || capabilities.formatCount == 0 )
        {
            SW_LOG_ERROR_ID( "WebGPU.SurfaceCapabilities", "WebGPU: surface capabilities are unavailable" );
            return false;
        }
        // 첫 형식이 표면이 권하는 형식이다(브라우저의 getPreferredCanvasFormat — 대개 BGRA8). 엔진이 쓰는 두 형식 중 처음 나오는 것을 쓴다.
        _format = WGPUTextureFormat_Undefined;
        for ( size_t index = 0; index < capabilities.formatCount && _format == WGPUTextureFormat_Undefined; ++index )
        {
            if ( WebGPURHIDeviceInternal::toBackBufferFormat( capabilities.formats[index] ) != RHIFormat::Unknown )
                _format = capabilities.formats[index];
        }
        WGPUPresentMode presentMode = WGPUPresentMode_Fifo; // 늘 있다(명세)
        for ( size_t index = 0; bVSync == false && index < capabilities.presentModeCount; ++index )
        {
            if ( capabilities.presentModes[index] == WGPUPresentMode_Mailbox || capabilities.presentModes[index] == WGPUPresentMode_Immediate )
                presentMode = capabilities.presentModes[index];
        }
        const WGPUTextureUsage wantedUsage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst; // 블릿 · 캡처가 백버퍼를 복사한다
        const WGPUTextureUsage usage       = wantedUsage & capabilities.usages;
        wgpuSurfaceCapabilitiesFreeMembers( capabilities );
        if ( _format == WGPUTextureFormat_Undefined )
        {
            SW_LOG_ERROR_ID( "WebGPU.SurfaceFormat", "WebGPU: the surface offers neither RGBA8Unorm nor BGRA8Unorm" );
            return false;
        }
        _rhiFormat = WebGPURHIDeviceInternal::toBackBufferFormat( _format );
        WGPUSurfaceConfiguration config{};
        config.device      = device;
        config.format      = _format;
        config.usage       = usage;
        config.width       = width;
        config.height      = height;
        config.alphaMode   = WGPUCompositeAlphaMode_Opaque;
        config.presentMode = presentMode;
        wgpuSurfaceConfigure( _surface, &config );
        _bConfigured = SW_TRUE;
        return true;
    }

    bool WebGPURHISwapChain::acquire( WGPUDevice device )
    {
        releaseCurrent();
        if ( _surface == nullptr || _bConfigured == SW_FALSE || _width == 0 || _height == 0 )
            return false;
        WGPUSurfaceTexture surfaceTexture{};
        wgpuSurfaceGetCurrentTexture( _surface, &surfaceTexture );
        const bool bUsable = surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal || surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
        if ( bUsable == false )
        {
            if ( surfaceTexture.texture != nullptr )
                wgpuTextureRelease( surfaceTexture.texture );
            // 낡았거나 잃었다 — 다시 구성하고 이번 프레임은 그리지 않는다(Vulkan 의 OUT_OF_DATE 와 같다).
            if ( surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated || surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Lost )
                (void)configure( device, _width, _height, _bVSync != SW_FALSE );
            return false;
        }
        _currentTexture = surfaceTexture.texture;
        _currentView    = wgpuTextureCreateView( _currentTexture, nullptr );
        return _currentView != nullptr;
    }

    void WebGPURHISwapChain::present()
    {
        if ( _currentTexture == nullptr )
            return;
#if defined( SW_PLATFORM_WEB )
        // 브라우저는 프레임 콜백이 돌아가면 받은 캔버스 텍스처를 스스로 표시한다 — 부를 것이 없다(emdawnwebgpu 는 wgpuSurfacePresent 를 받지 않는다, 기억).
#elif defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        (void)wgpuSurfacePresent( _surface );
#else
    #error "Unsupported platform"
#endif
    }

    void WebGPURHISwapChain::releaseCurrent()
    {
        if ( _currentView != nullptr )
            wgpuTextureViewRelease( _currentView );
        if ( _currentTexture != nullptr )
            wgpuTextureRelease( _currentTexture );
        _currentView    = nullptr;
        _currentTexture = nullptr;
    }

    void WebGPURHISwapChain::destroy()
    {
        releaseCurrent();
        if ( _surface == nullptr )
            return;
        if ( _bConfigured != SW_FALSE )
            wgpuSurfaceUnconfigure( _surface );
        wgpuSurfaceRelease( _surface );
        _surface     = nullptr;
        _bConfigured = SW_FALSE;
    }

    // ------------------------------------------------------------------------------
    // 디바이스 — 수명
    // ------------------------------------------------------------------------------
    WebGPURHIDevice::WebGPURHIDevice( uint32 deviceGeneration )
        : _instance{ nullptr }
        , _adapter{ nullptr }
        , _device{ nullptr }
        , _queue{ nullptr }
        , _limits{}
        , _swapChain{}
        , _listExtraSwapChain{}
        , _listAcquiredSwapChain{}
        , _frameEncoder{ nullptr }
        , _listPendingCommandBuffer{}
        , _resourceFactory{}
        , _frameStreamContext{}
        , _buffers{}
        , _textures{}
        , _pipelines{}
        , _renderPasses{}
        , _bindlessMutex{}
        , _textureTable{}
        , _bufferTable{}
        , _uavTable{}
        , _arrStaticSampler{}
        , _defaultTexture{ nullptr }
        , _defaultTextureView{ nullptr }
        , _defaultBuffer{ nullptr }
        , _depthCopyPipeline{ nullptr }
        , _depthCopyGroupLayout{ nullptr }
        , _bindGroupCache{}
        , _timestampProvider{ *this }
        , _timestampQuerySet{ nullptr }
        , _timestampResolveBuffer{ nullptr }
        , _listTimestampReadback{}
        , _pPendingTimestampReadback{ nullptr }
        , _timestampWrittenMask{}
        , _timestampFrame{}
        , _bTimestampEnabled{ SW_FALSE }
        , _listConstantRing{}
        , _activeRingIndex{ 0 }
        , _releaseQueue{}
        , _transferSerials{ deviceGeneration }
        , _transferQueue{ _transferSerials }
        , _readbackTable{}
        , _listQueuedReadbackBuffer{}
        , _liveCommandListMutex{}
        , _listLiveCommandList{}
        , _arrFrameFence{}
        , _frameFenceRing{}
        , _bInitialized{ SW_FALSE }
        , _bDeviceLost{ SW_FALSE }
        , _bTextureFormatsTier1{ SW_FALSE }
        , _bDepth32Stencil8{ SW_FALSE }
        , _bTextureBC{ SW_FALSE }
        , _bTextureASTC{ SW_FALSE }
        , _bTextureETC2{ SW_FALSE }
        , _bDepthClipControl{ SW_FALSE }
        , _bTimestampQuery{ SW_FALSE }
        , _reservedFeature{ 0 }
    {
        _pTimestampProvider = &_timestampProvider;
    }

    WebGPURHIDevice::~WebGPURHIDevice()
    {
        if ( _bInitialized != SW_FALSE )
            shutdownInternal();
    }

    bool WebGPURHIDevice::waitForFuture( WGPUFuture future, const utf8* pWhat ) const
    {
        WGPUFutureWaitInfo waitInfo{};
        waitInfo.future = future;
        if ( wgpuInstanceWaitAny( _instance, 1, &waitInfo, MathUtil::kMaxUInt64 ) != WGPUWaitStatus_Success || waitInfo.completed == false )
        {
            SW_LOG_ERROR_ID( "WebGPU.WaitFailed", "WebGPU: waiting for %# failed (the web build needs ASYNCIFY or JSPI - cmake/Modules/Platform/Web.cmake)", pWhat );
            return false;
        }
        return true;
    }

    // 길이 유지(긴 함수 정리 기준): 어댑터 → 기능 대조 → 한도 → 장치 요청의 순서가 곧 의미라 나누지 않는다.
    bool WebGPURHIDevice::createDeviceInternal( const RHISwapChainDesc& desc )
    {
        WGPURequestAdapterOptions adapterOptions{};
        adapterOptions.featureLevel         = WGPUFeatureLevel_Core;
        adapterOptions.powerPreference      = desc._bPreferMinimumPower ? WGPUPowerPreference_LowPower : WGPUPowerPreference_HighPerformance;
        adapterOptions.forceFallbackAdapter = desc._bSoftwareAdapter ? 1u : 0u;
        adapterOptions.compatibleSurface    = nullptr; // 표면은 장치 뒤에 구성한다 — 브라우저 캔버스는 어느 어댑터든 받는다
        WGPURequestAdapterCallbackInfo adapterCallback{};
        adapterCallback.mode      = WGPUCallbackMode_WaitAnyOnly;
        adapterCallback.callback  = &WebGPURHIDeviceInternal::onAdapter;
        adapterCallback.userdata1 = &_adapter;
        if ( waitForFuture( wgpuInstanceRequestAdapter( _instance, &adapterOptions, adapterCallback ), "requestAdapter" ) == false || _adapter == nullptr )
        {
            _initResult = RHIInitResult::DriverUnsupported; // 브라우저 · 드라이버가 WebGPU 를 주지 않는다 — 환경 탓이다
            return false;
        }

        // 필수 기능: 깊이 히스토리 · 노출을 선형으로 읽고(float32-filterable), GPUScene 이 간접 인자에 첫 인스턴스를 적는다(indirect-first-instance). 대체 없이 멈춘다.
        const WGPUFeatureName   arrRequired[] = { WGPUFeatureName_Float32Filterable, WGPUFeatureName_IndirectFirstInstance };
        vector<WGPUFeatureName> listFeature;
        for ( const WGPUFeatureName feature : arrRequired )
        {
            if ( wgpuAdapterHasFeature( _adapter, feature ) == false )
            {
                SW_LOG_ERROR_ID( "WebGPU.RequiredFeatureMissing", "WebGPU: the adapter lacks a required feature (%#) - see docs/PlatformTargets.md", static_cast<uint32>( feature ) );
                _initResult = RHIInitResult::DriverUnsupported;
                return false;
            }
            listFeature.push_back( feature );
        }
        // 선택 기능: 있으면 켜고 깃발에 적는다 — 형식 표(`toTextureFormat` · `supportsTextureFormat`)와 깊이 클립이 읽는다.
        const auto enableOptional = [this, &listFeature]( WGPUFeatureName feature ) -> uint8
        {
            if ( wgpuAdapterHasFeature( _adapter, feature ) == false )
                return SW_FALSE;
            listFeature.push_back( feature );
            return SW_TRUE;
        };
        _bTextureBC           = enableOptional( WGPUFeatureName_TextureCompressionBC );
        _bTextureASTC         = enableOptional( WGPUFeatureName_TextureCompressionASTC );
        _bTextureETC2         = enableOptional( WGPUFeatureName_TextureCompressionETC2 );
        _bDepth32Stencil8     = enableOptional( WGPUFeatureName_Depth32FloatStencil8 );
        _bTextureFormatsTier1 = enableOptional( WGPUFeatureName_TextureFormatsTier1 );
        _bDepthClipControl    = enableOptional( WGPUFeatureName_DepthClipControl );
        _bTimestampQuery      = enableOptional( WGPUFeatureName_TimestampQuery ); // GPU 패스 시간(브라우저는 기본 100 µs 단위로 거칠게 준다 — 명세의 정밀도 제한)

        // 한도는 어댑터가 주는 최대를 그대로 요구한다 — 기본 한도(텍스처 16 · 저장 버퍼 8 칸/단계)를 넘는 패스가 있다. 패스가 그마저 넘으면 PSO 생성이 검증 오류로 드러난다.
        if ( wgpuAdapterGetLimits( _adapter, &_limits ) != WGPUStatus_Success )
        {
            SW_LOG_ERROR_ID( "WebGPU.LimitsUnavailable", "WebGPU: adapter limits are unavailable" );
            return false;
        }
        SW_LOG_INFO_ID( "WebGPU.AdapterLimits", "WebGPU limits: sampled textures %#, samplers %#, storage buffers %#, uniform buffers %# per stage, bind group bindings %#",
                        _limits.maxSampledTexturesPerShaderStage, _limits.maxSamplersPerShaderStage, _limits.maxStorageBuffersPerShaderStage, _limits.maxUniformBuffersPerShaderStage,
                        _limits.maxBindingsPerBindGroup );

        WGPUDeviceDescriptor deviceDesc{};
        deviceDesc.label                                 = WebGPURHIUtil::toStringView( "SW Device" );
        deviceDesc.requiredFeatureCount                  = listFeature.size();
        deviceDesc.requiredFeatures                      = listFeature.data();
        deviceDesc.requiredLimits                        = &_limits;
        deviceDesc.uncapturedErrorCallbackInfo.callback  = &WebGPURHIDevice::onUncapturedError;
        deviceDesc.uncapturedErrorCallbackInfo.userdata1 = this;
        deviceDesc.deviceLostCallbackInfo.mode           = WGPUCallbackMode_AllowSpontaneous;
        deviceDesc.deviceLostCallbackInfo.callback       = &WebGPURHIDevice::onDeviceLost;
        deviceDesc.deviceLostCallbackInfo.userdata1      = this;
        WGPURequestDeviceCallbackInfo deviceCallback{};
        deviceCallback.mode      = WGPUCallbackMode_WaitAnyOnly;
        deviceCallback.callback  = &WebGPURHIDeviceInternal::onDevice;
        deviceCallback.userdata1 = &_device;
        if ( waitForFuture( wgpuAdapterRequestDevice( _adapter, &deviceDesc, deviceCallback ), "requestDevice" ) == false || _device == nullptr )
            return false;
        _queue = wgpuDeviceGetQueue( _device );
        return _queue != nullptr;
    }

    bool WebGPURHIDevice::createDefaultResources()
    {
        // 정적 샘플러 세트 s9..s15 — DX11 과 같은 표(SW_SAMPLER_* 순서, binding_slots.hlsli 4). WebGPU 에 테두리 주소가 없어 POINT_BORDER 는 끝 고정이다(테두리 색 0 대신 끝 텍셀).
        struct SamplerSpec
        {
            WGPUFilterMode  _filter;
            WGPUAddressMode _address;
            uint16          _maxAnisotropy;
        };
        constexpr SamplerSpec arrSpec[shaderslot::kStaticSamplerArrayCount] = {
            { WGPUFilterMode_Linear,       WGPUAddressMode_Repeat, 1}, // LINEAR_WRAP
            { WGPUFilterMode_Linear,  WGPUAddressMode_ClampToEdge, 1}, // LINEAR_CLAMP
            {WGPUFilterMode_Nearest,       WGPUAddressMode_Repeat, 1}, // POINT_WRAP
            {WGPUFilterMode_Nearest,  WGPUAddressMode_ClampToEdge, 1}, // POINT_CLAMP
            { WGPUFilterMode_Linear, WGPUAddressMode_MirrorRepeat, 1}, // LINEAR_MIRROR
            { WGPUFilterMode_Linear,       WGPUAddressMode_Repeat, 8}, // ANISO_WRAP
            {WGPUFilterMode_Nearest,  WGPUAddressMode_ClampToEdge, 1}, // POINT_BORDER
        };
        for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerArrayCount; ++samplerIndex )
        {
            const SamplerSpec&    spec = arrSpec[samplerIndex];
            WGPUSamplerDescriptor samplerDesc{};
            samplerDesc.addressModeU        = spec._address;
            samplerDesc.addressModeV        = spec._address;
            samplerDesc.addressModeW        = spec._address;
            samplerDesc.magFilter           = spec._filter;
            samplerDesc.minFilter           = spec._filter;
            samplerDesc.mipmapFilter        = spec._filter == WGPUFilterMode_Linear ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
            samplerDesc.lodMinClamp         = 0.0f;
            samplerDesc.lodMaxClamp         = 32.0f;
            samplerDesc.maxAnisotropy       = spec._maxAnisotropy;
            _arrStaticSampler[samplerIndex] = wgpuDeviceCreateSampler( _device, &samplerDesc );
            if ( _arrStaticSampler[samplerIndex] == nullptr )
                return false;
        }
        // 빈 슬롯 — 바인드 그룹은 셰이더가 쓰는 모든 번호에 자원이 있어야 한다. 엔진이 걸지 않은 칸은 1x1 검정 · 0 버퍼다(DX11 · GL 의 빈 슬롯이 읽는 값).
        WGPUTextureDescriptor textureDesc{};
        textureDesc.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        textureDesc.dimension     = WGPUTextureDimension_2D;
        textureDesc.size          = WGPUExtent3D{ 1, 1, 1 };
        textureDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        textureDesc.mipLevelCount = 1;
        textureDesc.sampleCount   = 1;
        _defaultTexture           = wgpuDeviceCreateTexture( _device, &textureDesc );
        _defaultTextureView       = _defaultTexture != nullptr ? wgpuTextureCreateView( _defaultTexture, nullptr ) : nullptr;
        WGPUBufferDescriptor bufferDesc{};
        bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        bufferDesc.size  = WebGPURHIDeviceInternal::kDefaultBufferBytes;
        _defaultBuffer   = wgpuDeviceCreateBuffer( _device, &bufferDesc );
        return _defaultTextureView != nullptr && _defaultBuffer != nullptr && createDepthCopyPipeline(); // 새 버퍼 · 텍스처는 0 으로 시작한다(명세)
    }

    bool WebGPURHIDevice::createDepthCopyPipeline()
    {
        WGPUShaderSourceWGSL source{};
        source.chain.sType = WGPUSType_ShaderSourceWGSL;
        source.code        = WebGPURHIUtil::toStringView( WebGPURHIDeviceInternal::kDepthCopyWGSL );
        WGPUShaderModuleDescriptor moduleDesc{};
        moduleDesc.nextInChain        = &source.chain;
        moduleDesc.label              = WebGPURHIUtil::toStringView( "SW Depth Sample Copy" );
        const WGPUShaderModule module = wgpuDeviceCreateShaderModule( _device, &moduleDesc );
        if ( module == nullptr )
            return false;
        WGPUColorTargetState target{};
        target.format    = WGPUTextureFormat_R32Float;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module      = module;
        fragment.entryPoint  = WebGPURHIUtil::toStringView( "fsMain" );
        fragment.targetCount = 1;
        fragment.targets     = &target;
        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.label              = WebGPURHIUtil::toStringView( "SW Depth Sample Copy" );
        pipelineDesc.layout             = nullptr; // 자동 레이아웃 — 그룹 0 은 깊이 텍스처 하나(sampleType depth)
        pipelineDesc.vertex.module      = module;
        pipelineDesc.vertex.entryPoint  = WebGPURHIUtil::toStringView( "vsMain" );
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.primitive.cullMode = WGPUCullMode_None;
        pipelineDesc.multisample.count  = 1;
        pipelineDesc.multisample.mask   = MathUtil::kMaxUInt32;
        pipelineDesc.fragment           = &fragment;
        _depthCopyPipeline              = wgpuDeviceCreateRenderPipeline( _device, &pipelineDesc );
        wgpuShaderModuleRelease( module );
        if ( _depthCopyPipeline == nullptr )
            return false;
        _depthCopyGroupLayout = wgpuRenderPipelineGetBindGroupLayout( _depthCopyPipeline, 0 );
        return _depthCopyGroupLayout != nullptr;
    }

    // 길이 유지(긴 함수 정리 기준): 사본 텍스처 · 샘플 뷰 · 면마다의 (첨부 뷰, 깊이 뷰, 그룹) 셋을 한 자리에서 짓는다 — 나누면 같은 면 설명을 세 번 넘긴다.
    bool WebGPURHIDevice::createDepthSampleCopy( WebGPUTextureRecord& record )
    {
        const uint32          layers = MathUtil::max( record._desc._arraySize, 1u );
        WGPUTextureDescriptor copyDesc{};
        copyDesc.label             = WebGPURHIUtil::toStringView( "SW Depth Sample Copy" );
        copyDesc.usage             = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        copyDesc.dimension         = WGPUTextureDimension_2D;
        copyDesc.size              = WGPUExtent3D{ record._desc._width, record._desc._height, layers };
        copyDesc.format            = WGPUTextureFormat_R32Float;
        copyDesc.mipLevelCount     = 1; // 깊이 밉은 사본에 옮기지 않는다 — 엔진의 깊이 텍스처는 밉 하나다(HZB 는 따로 만든 실수 텍스처다)
        copyDesc.sampleCount       = 1;
        record._depthSampleTexture = wgpuDeviceCreateTexture( _device, &copyDesc );
        if ( record._depthSampleTexture == nullptr )
            return false;
        WGPUTextureViewDescriptor sampleDesc{};
        sampleDesc.format          = WGPUTextureFormat_R32Float;
        sampleDesc.dimension       = record._desc._dimension == RHITextureDimension::TextureCube ? WGPUTextureViewDimension_Cube
                                                                                                 : ( record._desc._dimension == RHITextureDimension::Texture2DArray ? WGPUTextureViewDimension_2DArray
                                                                                                                                                                    : WGPUTextureViewDimension_2D );
        sampleDesc.baseMipLevel    = 0;
        sampleDesc.mipLevelCount   = 1;
        sampleDesc.baseArrayLayer  = 0;
        sampleDesc.arrayLayerCount = layers;
        sampleDesc.aspect          = WGPUTextureAspect_All;
        if ( record._sampleView != nullptr )
            wgpuTextureViewRelease( record._sampleView ); // 깊이 측면 샘플 뷰는 셰이더가 실수로 읽을 수 없다 — 사본의 뷰로 바꾼다
        record._sampleView = wgpuTextureCreateView( record._depthSampleTexture, &sampleDesc );
        for ( uint32 layer = 0; layer < layers; ++layer )
        {
            WGPUTextureViewDescriptor targetDesc{};
            targetDesc.format          = WGPUTextureFormat_R32Float;
            targetDesc.dimension       = WGPUTextureViewDimension_2D;
            targetDesc.mipLevelCount   = 1;
            targetDesc.baseArrayLayer  = layer;
            targetDesc.arrayLayerCount = 1;
            targetDesc.aspect          = WGPUTextureAspect_All;
            record._listDepthCopyTarget.push_back( wgpuTextureCreateView( record._depthSampleTexture, &targetDesc ) );
            WGPUTextureViewDescriptor readDesc{};
            readDesc.format                = record._wgpuFormat;
            readDesc.dimension             = WGPUTextureViewDimension_2D;
            readDesc.mipLevelCount         = 1;
            readDesc.baseArrayLayer        = layer;
            readDesc.arrayLayerCount       = 1;
            readDesc.aspect                = WGPUTextureAspect_DepthOnly;
            const WGPUTextureView readView = wgpuTextureCreateView( record._texture, &readDesc );
            record._listDepthReadView.push_back( readView );
            WGPUBindGroupEntry entry{};
            entry.binding     = 0;
            entry.textureView = readView;
            WGPUBindGroupDescriptor groupDesc{};
            groupDesc.layout     = _depthCopyGroupLayout;
            groupDesc.entryCount = 1;
            groupDesc.entries    = &entry;
            record._listDepthCopyGroup.push_back( readView != nullptr ? wgpuDeviceCreateBindGroup( _device, &groupDesc ) : nullptr );
            if ( record._listDepthCopyTarget.back() == nullptr || record._listDepthCopyGroup.back() == nullptr )
                return false;
        }
        return record._sampleView != nullptr;
    }

    void WebGPURHIDevice::encodeDepthSampleCopy( WGPUCommandEncoder encoder, WebGPUTextureRecord& record )
    {
        // shortcut: 부를 때마다 옮긴다(깊이를 다시 쓰지 않았어도), 프레임 시간에 사본 패스가 드러나면 깊이 첨부로 연 패스가 사본을 낡았다고 적게 한다.
        for ( size_t layer = 0; layer < record._listDepthCopyGroup.size(); ++layer )
        {
            WGPURenderPassColorAttachment color{};
            color.view       = record._listDepthCopyTarget[layer];
            color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color.loadOp     = WGPULoadOp_Clear; // 화면 전체를 덮는다 — 앞 내용을 읽지 않는다(타일 GPU 는 불러오기를 건너뛴다)
            color.storeOp    = WGPUStoreOp_Store;
            color.clearValue = WGPUColor{ 0.0, 0.0, 0.0, 0.0 };
            WGPURenderPassDescriptor passDesc{};
            passDesc.label                   = WebGPURHIUtil::toStringView( "SW Depth Sample Copy" );
            passDesc.colorAttachmentCount    = 1;
            passDesc.colorAttachments        = &color;
            const WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass( encoder, &passDesc );
            if ( pass == nullptr )
                return;
            wgpuRenderPassEncoderSetPipeline( pass, _depthCopyPipeline );
            wgpuRenderPassEncoderSetBindGroup( pass, 0, record._listDepthCopyGroup[layer], 0, nullptr );
            wgpuRenderPassEncoderDraw( pass, 3, 1, 0, 0 );
            wgpuRenderPassEncoderEnd( pass );
            wgpuRenderPassEncoderRelease( pass );
        }
        record._depthCopySerial = getFrameSerial();
    }

    void WebGPURHIDevice::releaseDepthSampleCopy( WebGPUTextureRecord& record )
    {
        if ( record._depthSampleTexture == nullptr )
            return;
        const WGPUTexture       copy      = record._depthSampleTexture;
        vector<WGPUTextureView> listView  = std::move( record._listDepthCopyTarget );
        vector<WGPUTextureView> listRead  = std::move( record._listDepthReadView );
        vector<WGPUBindGroup>   listGroup = std::move( record._listDepthCopyGroup );
        record._depthSampleTexture        = nullptr;
        enqueueGPURelease( SW_DELEGATE_LAMBDA( RHIResourceReleaseDelegate, [copy, listView, listRead, listGroup]()
        {
            for ( const WGPUBindGroup group : listGroup )
            {
                if ( group != nullptr )
                    wgpuBindGroupRelease( group );
            }
            for ( const WGPUTextureView view : listRead )
            {
                if ( view != nullptr )
                    wgpuTextureViewRelease( view );
            }
            for ( const WGPUTextureView view : listView )
            {
                if ( view != nullptr )
                    wgpuTextureViewRelease( view );
            }
            wgpuTextureDestroy( copy );
            wgpuTextureRelease( copy );
        } ) );
    }

    // ------------------------------------------------------------------------------
    // 타임스탬프
    // ------------------------------------------------------------------------------
    uint32 WebGPURHIDevice::getTimestampSlotCount() const
    {
        const bool bReady = _bTimestampEnabled.load( std::memory_order_relaxed ) != SW_FALSE && _timestampQuerySet != nullptr;
        return bReady ? constant::render::kMaxGPUTimestampSlot : 0u;
    }

    bool WebGPURHIDevice::readTimestamps( RHITimestampFrame& outFrame )
    {
        outFrame = _timestampFrame;
        return outFrame._listMicro.empty() == false;
    }

    void WebGPURHIDevice::ensureTimestampQuerySet()
    {
        const bool bNotNeeded = _bTimestampQuery == SW_FALSE || _timestampQuerySet != nullptr || _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE;
        if ( bNotNeeded )
            return;
        WGPUQuerySetDescriptor queryDesc{};
        queryDesc.label    = WebGPURHIUtil::toStringView( "SW GPU Timestamps" );
        queryDesc.type     = WGPUQueryType_Timestamp;
        queryDesc.count    = constant::render::kMaxGPUTimestampSlot;
        _timestampQuerySet = wgpuDeviceCreateQuerySet( _device, &queryDesc );
        WGPUBufferDescriptor resolveDesc{};
        resolveDesc.label       = WebGPURHIUtil::toStringView( "SW Timestamp Resolve" );
        resolveDesc.usage       = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        resolveDesc.size        = WebGPURHIDeviceInternal::kTimestampBytes;
        _timestampResolveBuffer = wgpuDeviceCreateBuffer( _device, &resolveDesc );
        for ( uint32 index = 0; index < WebGPURHIDeviceInternal::kTimestampReadbackCount; ++index )
        {
            unique_ptr<WebGPUTimestampReadback> readback = make_unique<WebGPUTimestampReadback>();
            WGPUBufferDescriptor                readDesc{};
            readDesc.label    = WebGPURHIUtil::toStringView( "SW Timestamp Readback" );
            readDesc.usage    = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            readDesc.size     = WebGPURHIDeviceInternal::kTimestampBytes;
            readback->_buffer = wgpuDeviceCreateBuffer( _device, &readDesc );
            _listTimestampReadback.push_back( std::move( readback ) );
        }
        if ( _timestampQuerySet == nullptr || _timestampResolveBuffer == nullptr )
            SW_LOG_ERROR_ID( "WebGPU.TimestampQuerySetFailed", "WebGPU: the timestamp query set or its resolve buffer could not be created - GPU pass times stay empty" );
    }

    void WebGPURHIDevice::writeTimestampSlot( WGPUCommandEncoder encoder, uint32 slotIndex )
    {
        const bool bCannotWrite = encoder == nullptr || _timestampQuerySet == nullptr || slotIndex >= constant::render::kMaxGPUTimestampSlot ||
                                  _bTimestampEnabled.load( std::memory_order_relaxed ) == SW_FALSE;
        if ( bCannotWrite )
            return;
        // WebGPU 는 패스 경계에서만 시각을 적는다(인코더의 writeTimestamp 는 명세에서 빠졌다) — 빈 계산 패스의 시작 시각을 이 칸에 적는다.
        // https://www.w3.org/TR/webgpu/#dom-gpucomputepassdescriptor-timestampwrites
        WGPUPassTimestampWrites writes{};
        writes.querySet                  = _timestampQuerySet;
        writes.beginningOfPassWriteIndex = slotIndex;
        writes.endOfPassWriteIndex       = WGPU_QUERY_SET_INDEX_UNDEFINED;
        WGPUComputePassDescriptor passDesc{};
        passDesc.timestampWrites          = &writes;
        const WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass( encoder, &passDesc );
        if ( pass == nullptr )
            return;
        wgpuComputePassEncoderEnd( pass );
        wgpuComputePassEncoderRelease( pass );
        _timestampWrittenMask.set( slotIndex );
    }

    void WebGPURHIDevice::resolveTimestamps()
    {
        const GPUTimestampSlotMask mask = _timestampWrittenMask;
        _timestampWrittenMask           = GPUTimestampSlotMask{};
        _pPendingTimestampReadback      = nullptr;
        if ( mask.any() == false || _timestampQuerySet == nullptr || _timestampResolveBuffer == nullptr || _frameEncoder == nullptr )
            return;
        WebGPUTimestampReadback* pFree = nullptr;
        for ( const unique_ptr<WebGPUTimestampReadback>& readback : _listTimestampReadback )
        {
            if ( readback->_buffer != nullptr && readback->_state.load( std::memory_order_acquire ) == WebGPUTimestampReadback::Free )
            {
                pFree = readback.get();
                break;
            }
        }
        if ( pFree == nullptr )
            return; // 앞 프레임들의 매핑이 아직이다 — 이번 프레임은 재지 않는다(기다리면 재려던 파이프라인이 멈춘다)
        wgpuCommandEncoderResolveQuerySet( _frameEncoder, _timestampQuerySet, 0, constant::render::kMaxGPUTimestampSlot, _timestampResolveBuffer, 0 );
        wgpuCommandEncoderCopyBufferToBuffer( _frameEncoder, _timestampResolveBuffer, 0, pFree->_buffer, 0, WebGPURHIDeviceInternal::kTimestampBytes );
        pFree->_writtenMask = mask;
        pFree->_state.store( WebGPUTimestampReadback::Mapping, std::memory_order_release );
        _pPendingTimestampReadback = pFree;
    }

    void WebGPURHIDevice::mapTimestampReadback()
    {
        WebGPUTimestampReadback* const pReadback = _pPendingTimestampReadback;
        _pPendingTimestampReadback               = nullptr;
        if ( pReadback == nullptr )
            return;
        WGPUBufferMapCallbackInfo mapInfo{};
        mapInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
        mapInfo.callback  = &WebGPURHIDevice::onTimestampMapped;
        mapInfo.userdata1 = pReadback;
        (void)wgpuBufferMapAsync( pReadback->_buffer, WGPUMapMode_Read, 0, WebGPURHIDeviceInternal::kTimestampBytes, mapInfo );
    }

    void WebGPURHIDevice::onTimestampMapped( WGPUMapAsyncStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
    {
        (void)pUserdata2;
        WebGPUTimestampReadback* const pReadback = static_cast<WebGPUTimestampReadback*>( pUserdata1 );
        if ( status != WGPUMapAsyncStatus_Success )
            SW_LOG_ERROR_ONCE_ID( "WebGPU.TimestampMapFailed", "WebGPU: timestamp readback mapping failed: %#", WebGPURHIUtil::fromStringView( message ) );
        pReadback->_state.store( status == WGPUMapAsyncStatus_Success ? WebGPUTimestampReadback::Mapped : WebGPUTimestampReadback::Failed, std::memory_order_release );
    }

    void WebGPURHIDevice::collectTimestampReadbacks()
    {
        for ( const unique_ptr<WebGPUTimestampReadback>& readback : _listTimestampReadback )
        {
            const uint8 state = readback->_state.load( std::memory_order_acquire );
            if ( state == WebGPUTimestampReadback::Mapped )
            {
                // 값은 나노초다(명세) — 틱 하나 = 1 ns. 적지 않은 칸은 `RHITimestampFrame::resolve` 가 음수로 둔다(마스크).
                const uint64* const pTick = static_cast<const uint64*>( wgpuBufferGetConstMappedRange( readback->_buffer, 0, WebGPURHIDeviceInternal::kTimestampBytes ) );
                if ( pTick != nullptr )
                    (void)RHITimestampFrame::resolve( pTick, readback->_writtenMask, 1.0, _timestampFrame );
                wgpuBufferUnmap( readback->_buffer );
            }
            if ( state == WebGPUTimestampReadback::Mapped || state == WebGPUTimestampReadback::Failed )
                readback->_state.store( WebGPUTimestampReadback::Free, std::memory_order_release );
        }
    }

    bool WebGPURHIDevice::initializeInternal( const RHISwapChainDesc& desc )
    {
        const WGPUInstanceFeatureName arrInstanceFeature[] = { WGPUInstanceFeatureName_TimedWaitAny }; // 어댑터 · 장치 요청을 기다린다(`waitForFuture`)
        WGPUInstanceDescriptor        instanceDesc{};
        instanceDesc.requiredFeatureCount = std::size( arrInstanceFeature );
        instanceDesc.requiredFeatures     = arrInstanceFeature;
        _instance                         = wgpuCreateInstance( &instanceDesc );
        if ( _instance == nullptr )
        {
            SW_LOG_ERROR_ID( "WebGPU.NoInstance", "WebGPU: wgpuCreateInstance failed (navigator.gpu missing?)" );
            _initResult = RHIInitResult::DriverUnsupported;
            return false;
        }
        markInitPhase( "instance" );
        if ( createDeviceInternal( desc ) == false )
            return false;
        markInitPhase( "device" );
        if ( _swapChain.createSurface( _instance, _adapter, desc ) == false || _swapChain.configure( _device, desc._width, desc._height, desc._bVSync ) == false )
            return false;
        markInitPhase( "swapchain" );
        if ( createDefaultResources() == false )
        {
            SW_LOG_ERROR_ID( "WebGPU.DefaultResourcesFailed", "WebGPU: static samplers or default slot resources could not be created" );
            return false;
        }
        _resourceFactory    = make_unique<WebGPURHIResourceFactory>( this );
        _frameStreamContext = make_unique<WebGPURHICommandContext>( this, false );
        _bInitialized       = SW_TRUE;
        markInitPhase( "resources" );
        SW_LOG_INFO_ID( "WebGPU.Initialized", "WebGPU RHI initialized (%#x%#, BC %#, ASTC %#, ETC2 %#, rg16snorm %#, depth32-stencil8 %#)", desc._width, desc._height,
                        static_cast<uint32>( _bTextureBC ), static_cast<uint32>( _bTextureASTC ), static_cast<uint32>( _bTextureETC2 ), static_cast<uint32>( _bTextureFormatsTier1 ),
                        static_cast<uint32>( _bDepth32Stencil8 ) );
        return true;
    }

    void WebGPURHIDevice::shutdownInternal()
    {
        // GPU 는 종료 2단계(waitIdle)에서 쉬었다 — 남은 해제는 모두 비었다. 장치 객체를 놓는다.
        _releaseQueue.flushAll();
        _transferQueue.retireAll( []( unique_ptr<WebGPUTransferProbe>& probe )
        { (void)probe; } );
        _readbackTable.clear();
        for ( WebGPUFrameFence& fence : _arrFrameFence )
        {
            fence.clear();
        }
        _frameFenceRing.reset();
        _bindGroupCache.clear();
        for ( const unique_ptr<WebGPUTimestampReadback>& readback : _listTimestampReadback )
        {
            if ( readback->_buffer != nullptr )
                wgpuBufferRelease( readback->_buffer );
        }
        _listTimestampReadback.clear();
        _pPendingTimestampReadback = nullptr;
        if ( _timestampResolveBuffer != nullptr )
            wgpuBufferRelease( _timestampResolveBuffer );
        if ( _timestampQuerySet != nullptr )
            wgpuQuerySetRelease( _timestampQuerySet );
        _timestampResolveBuffer = nullptr;
        _timestampQuerySet      = nullptr;
        if ( _depthCopyGroupLayout != nullptr )
            wgpuBindGroupLayoutRelease( _depthCopyGroupLayout );
        if ( _depthCopyPipeline != nullptr )
            wgpuRenderPipelineRelease( _depthCopyPipeline );
        _depthCopyGroupLayout = nullptr;
        _depthCopyPipeline    = nullptr;
        for ( ConstantRing& ring : _listConstantRing )
        {
            if ( ring._buffer != nullptr )
                wgpuBufferRelease( ring._buffer );
        }
        _listConstantRing.clear();
        for ( WGPUSampler& sampler : _arrStaticSampler )
        {
            if ( sampler != nullptr )
                wgpuSamplerRelease( sampler );
            sampler = nullptr;
        }
        if ( _defaultTextureView != nullptr )
            wgpuTextureViewRelease( _defaultTextureView );
        if ( _defaultTexture != nullptr )
            wgpuTextureRelease( _defaultTexture );
        if ( _defaultBuffer != nullptr )
            wgpuBufferRelease( _defaultBuffer );
        _defaultTextureView = nullptr;
        _defaultTexture     = nullptr;
        _defaultBuffer      = nullptr;
        _listExtraSwapChain.clear();
        _swapChain.destroy();
        _resourceFactory.reset();
        if ( _queue != nullptr )
            wgpuQueueRelease( _queue );
        if ( _device != nullptr )
            wgpuDeviceRelease( _device );
        if ( _adapter != nullptr )
            wgpuAdapterRelease( _adapter );
        if ( _instance != nullptr )
            wgpuInstanceRelease( _instance );
        _queue        = nullptr;
        _device       = nullptr;
        _adapter      = nullptr;
        _instance     = nullptr;
        _bInitialized = SW_FALSE;
    }

    void WebGPURHIDevice::detachCommandRecordingInternal()
    {
        _frameStreamContext.reset();
        RHILiveCommandListUtil::detachAll( _liveCommandListMutex, _listLiveCommandList );
    }

    void WebGPURHIDevice::registerCommandList( WebGPURHICommandList* pList )
    {
        ScopedLock<mutex> lock{ _liveCommandListMutex };
        _listLiveCommandList.push_back( pList );
    }

    void WebGPURHIDevice::unregisterCommandList( WebGPURHICommandList* pList )
    {
        ScopedLock<mutex> lock{ _liveCommandListMutex };
        (void)VectorUtil::removeValue( _listLiveCommandList, pList );
    }

    void WebGPURHIDevice::resizeInternal( uint32 width, uint32 height )
    {
        // 표면 구성은 GPU 를 기다리지 않는다 — 앞 프레임이 쓰던 표면 텍스처는 구현이 끝날 때까지 살린다(명세).
        (void)_swapChain.configure( _device, width, height, isVSyncEnabled() );
    }

    void WebGPURHIDevice::applyVSyncInternal()
    {
        (void)_swapChain.configure( _device, _swapChain.getWidth(), _swapChain.getHeight(), isVSyncEnabled() );
    }

    // ------------------------------------------------------------------------------
    // 능력 · 형식
    // ------------------------------------------------------------------------------
    RHICapabilities WebGPURHIDevice::getCapabilities() const
    {
        return RHIStaticCapabilities::makeWebGPUCapabilities();
    }

    WGPUTextureFormat WebGPURHIDevice::toTextureFormat( RHIFormat format ) const
    {
        if ( format == RHIFormat::R16G16_SNORM && _bTextureFormatsTier1 == SW_FALSE )
            return WGPUTextureFormat_RG16Float; // 셰이더는 `.rg` 를 [-1, 1] 실수로 읽는다 — 값이 같다(정밀도는 반정밀 실수)
        if ( format == RHIFormat::D24_UNORM_S8_UINT && _bDepth32Stencil8 != SW_FALSE )
            return WGPUTextureFormat_Depth32FloatStencil8; // 깊이를 되읽을 수 있다(depth24plus 는 복사 원본이 될 수 없다)
        const WebGPURHIUtil::TextureFormatRow& row = WebGPURHIUtil::findTextureFormatRow( format );
        if ( row._requiredFeature == WGPUFeatureName_TextureCompressionBC && _bTextureBC == SW_FALSE )
            return WGPUTextureFormat_Undefined;
        if ( row._requiredFeature == WGPUFeatureName_TextureCompressionASTC && _bTextureASTC == SW_FALSE )
            return WGPUTextureFormat_Undefined;
        if ( row._requiredFeature == WGPUFeatureName_TextureCompressionETC2 && _bTextureETC2 == SW_FALSE )
            return WGPUTextureFormat_Undefined;
        return row._wgpuFormat;
    }

    bool WebGPURHIDevice::supportsTextureFormat( RHIFormat format ) const
    {
        return toTextureFormat( format ) != WGPUTextureFormat_Undefined;
    }

    bool WebGPURHIDevice::isBindlessIndexLive( RHIBindlessIndexSpace space, RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        const WebGPUBindlessTable&     table = space == RHIBindlessIndexSpace::Texture ? _textureTable : _bufferTable;
        return table.isLive( index );
    }

    WebGPUBindlessEntry WebGPURHIDevice::findBindless( const WebGPUBindlessTable& table, RHIDescriptorIndex index ) const
    {
        ScopedSharedLock<shared_mutex> lock{ _bindlessMutex };
        return table.get( index );
    }

    IRHIResourceFactory* WebGPURHIDevice::getResourceFactory()
    {
        return _resourceFactory.get();
    }

    IRHICommandContext* WebGPURHIDevice::getFrameStreamContext()
    {
        return _frameStreamContext.get();
    }

    void* WebGPURHIDevice::getNativeTexturePointer( RHITextureHandle texture ) const
    {
        const WebGPUTextureRecord* const pRecord = findTexture( texture );
        return pRecord != nullptr ? pRecord->_sampleView : nullptr;
    }

    bool WebGPURHIDevice::queryGPUMemoryBudgetInternal( RHIMemoryBudget& outBudget )
    {
        (void)outBudget; // WebGPU 는 메모리 예산을 알려 주지 않는다(브라우저가 숨긴다) — 모든 칸이 "모름" 이다
        return false;
    }

    // ------------------------------------------------------------------------------
    // 커맨드 리스트 · 제출
    // ------------------------------------------------------------------------------
    unique_ptr<IRHICommandList> WebGPURHIDevice::createCommandList()
    {
        return make_unique<WebGPURHICommandList>( this );
    }

    void WebGPURHIDevice::openFrameEncoder()
    {
        if ( _frameEncoder != nullptr || _device == nullptr )
            return;
        WGPUCommandEncoderDescriptor desc{};
        desc.label    = WebGPURHIUtil::toStringView( "SW Frame" );
        _frameEncoder = wgpuDeviceCreateCommandEncoder( _device, &desc );
    }

    void WebGPURHIDevice::closeFrameEncoder()
    {
        if ( _frameEncoder == nullptr )
            return;
        if ( _frameStreamContext != nullptr )
            _frameStreamContext->endComputePass();
        WGPUCommandBufferDescriptor desc{};
        _listPendingCommandBuffer.push_back( wgpuCommandEncoderFinish( _frameEncoder, &desc ) );
        wgpuCommandEncoderRelease( _frameEncoder );
        _frameEncoder = nullptr;
    }

    void WebGPURHIDevice::submitPending()
    {
        if ( _activeRingIndex < _listConstantRing.size() )
        {
            // 이 제출이 읽는 상수 칸을 먼저 올린다 — 큐 쓰기는 뒤 제출보다 앞에 실행된다(명세).
            ConstantRing& ring = _listConstantRing[_activeRingIndex];
            if ( ring._used > ring._uploaded )
            {
                wgpuQueueWriteBuffer( _queue, ring._buffer, ring._uploaded, ring._staging.data() + ring._uploaded, ring._used - ring._uploaded );
                ring._uploaded = ring._used;
            }
        }
        if ( _listPendingCommandBuffer.empty() )
            return;
        wgpuQueueSubmit( _queue, _listPendingCommandBuffer.size(), _listPendingCommandBuffer.data() );
        for ( const WGPUCommandBuffer buffer : _listPendingCommandBuffer )
        {
            wgpuCommandBufferRelease( buffer );
        }
        _listPendingCommandBuffer.clear();
    }

    void WebGPURHIDevice::executeCommandList( IRHICommandList* pCmdList )
    {
        WebGPURHICommandList* const pList = static_cast<WebGPURHICommandList*>( pCmdList );
        if ( pList == nullptr )
            return;
        // [지금까지의 세그먼트][이 리스트][새 세그먼트] — 같은 큐의 제출 순서가 곧 실행 순서다(DX12 · Vulkan 과 같다).
        closeFrameEncoder();
        const WGPUCommandBuffer commands = pList->takeFinishedCommandBuffer();
        if ( commands != nullptr )
            _listPendingCommandBuffer.push_back( commands );
        if ( _bImmediateSubmit != SW_FALSE )
            submitPending();
        openFrameEncoder();
    }

    void WebGPURHIDevice::executeCommandListImmediate( IRHICommandList* pCmdList )
    {
        WebGPURHICommandList* const pList    = static_cast<WebGPURHICommandList*>( pCmdList );
        const WGPUCommandBuffer     commands = pList != nullptr ? pList->takeFinishedCommandBuffer() : nullptr;
        if ( commands == nullptr )
            return;
        _listPendingCommandBuffer.push_back( commands );
        submitPending();
    }

    void WebGPURHIDevice::allocateConstantSlice( const void* pData, uint32 size, WGPUBuffer& outBuffer, uint64& outOffset )
    {
        using Internal           = WebGPURHIDeviceInternal;
        const uint64 alignedSize = MathUtil::align( static_cast<uint64>( size ), Internal::kConstantAlignment );
        const bool   bNoRoom     = _activeRingIndex >= _listConstantRing.size() || _listConstantRing[_activeRingIndex]._used + alignedSize > _listConstantRing[_activeRingIndex]._capacity;
        if ( bNoRoom )
        {
            // 링이 찼다 — 지금까지 담은 칸을 올리고 링을 하나 더 둔다(그 링도 이 프레임 것이다).
            if ( _activeRingIndex < _listConstantRing.size() )
                submitPending();
            appendConstantRing( MathUtil::max( Internal::kConstantRingBytes, alignedSize ) );
        }
        ConstantRing& ring = _listConstantRing[_activeRingIndex];
        Memory::copy( ring._staging.data() + ring._used, pData, size );
        outBuffer = ring._buffer;
        outOffset = ring._used;
        ring._used += alignedSize;
        ring._retireSerial = getFrameSerial();
    }

    void WebGPURHIDevice::appendConstantRing( uint64 capacity )
    {
        ConstantRing         ring{};
        WGPUBufferDescriptor desc{};
        desc.label     = WebGPURHIUtil::toStringView( "SW Constant Ring" );
        desc.usage     = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        desc.size      = capacity;
        ring._buffer   = wgpuDeviceCreateBuffer( _device, &desc );
        ring._capacity = capacity;
        ring._staging.resize( static_cast<size_t>( capacity ) );
        ring._retireSerial = getFrameSerial();
        _listConstantRing.push_back( std::move( ring ) );
        _activeRingIndex = static_cast<uint32>( _listConstantRing.size() - 1 );
    }

    // ------------------------------------------------------------------------------
    // 프레임
    // ------------------------------------------------------------------------------
    void WebGPURHIDevice::beginFrameInternal()
    {
        processCompletions();
        if ( _bDeviceLost != SW_FALSE )
            return;
        // 앞서가기 상한(`IRHIDevice` 계약): 이 칸을 앞 프레임이 아직 쓰면 그 프레임을 기다린다. 기다리지 않으면 상수 링 · 프레임 완료가 GPU 가 뒤처진 만큼 자란다.
        // 브라우저도 같은 대기다 — 웹 빌드는 ASYNCIFY · JSPI 로 링크해 `wgpuInstanceWaitAny` 가 이벤트 루프에 양보한다(`waitForFuture` 와 같은 길).
        const uint64 busySerial = _frameFenceRing.getRecordingSlotBusySerial();
        if ( busySerial != 0 )
        {
            if ( waitFrameFence( _arrFrameFence[RHIFrameFenceRing::getSlot( busySerial )] ) == false )
                SW_LOG_ERROR_ONCE_ID( "WebGPU.FrameFenceWaitFailed", "WebGPU: waiting for frame %# to finish failed", busySerial );
            retireFinishedFrames();
        }
        // 지난 프레임의 그룹은 그 프레임의 커맨드가 붙들고 있다(명세) — 캐시는 놓는다. 프레임을 넘겨 들면 놓인 자원의 핸들 값이 새 객체에 다시 나갈 수 있다.
        _bindGroupCache.clear();
        ensureTimestampQuerySet();
        // GPU 가 끝낸 프레임의 링을 다시 쓴다 — 끝나지 않았으면 이 프레임의 첫 칸 요청이 새 링을 만든다(앞서가기 상한이 링 수를 프레임 인 플라이트 수 언저리로 묶는다).
        const uint64 completed = _frameFenceRing.getCompletedSerial();
        _activeRingIndex       = static_cast<uint32>( _listConstantRing.size() );
        for ( uint32 ringIndex = 0; ringIndex < _listConstantRing.size(); ++ringIndex )
        {
            ConstantRing& ring = _listConstantRing[ringIndex];
            if ( ring._retireSerial > completed )
                continue;
            ring._used       = 0;
            ring._uploaded   = 0;
            _activeRingIndex = ringIndex;
            break;
        }
        openFrameEncoder();
    }

    void WebGPURHIDevice::endFrameInternal( bool vsync, bool bPresent )
    {
        (void)vsync;         // 스왑체인 구성이 정한다(`applyVSyncInternal`)
        resolveTimestamps(); // 프레임 인코더는 이 프레임의 마지막 세그먼트다 — 앞서 낸 리스트의 시각까지 모두 풀린다
        closeFrameEncoder();
        submitPending();
        mapTimestampReadback();

        // 칸은 beginFrame 이 비웠다. 비지 않았으면 앞 프레임의 콜백이 이 칸을 아직 가리킨다 — 덮으면 그 콜백이 새 프레임의 셈을 깎는다.
        const uint64 busySerial = _frameFenceRing.getRecordingSlotBusySerial();
        if ( busySerial != 0 )
            SW_FATAL( "WebGPU: frame %# is submitted into a ring slot still held by frame %#", _frameFenceRing.getRecordingSerial(), busySerial );
        WebGPUFrameFence* const pFence = &_arrFrameFence[_frameFenceRing.getRecordingSlot()];
        pFence->arm( _frameFenceRing.getRecordingSerial(), this );
        WGPUQueueWorkDoneCallbackInfo doneInfo{};
        doneInfo.mode           = WGPUCallbackMode_AllowSpontaneous;
        doneInfo.callback       = &WebGPURHIDevice::onFrameWorkDone;
        doneInfo.userdata1      = pFence;
        pFence->_workDoneFuture = wgpuQueueOnSubmittedWorkDone( _queue, doneInfo );
        mapQueuedReadbacks( _frameFenceRing.getRecordingSerial() );

        if ( bPresent )
        {
            if ( _listAcquiredSwapChain.empty() )
                _swapChain.present(); // 메인 창만 그린 프레임(대부분) — acquireSwapChainImage 를 부르지 않는 렌더러도 백버퍼 패스가 이미 받았다
            for ( WebGPURHISwapChain* pSwapChain : _listAcquiredSwapChain )
            {
                pSwapChain->present();
            }
        }
        _swapChain.releaseCurrent();
        for ( WebGPURHISwapChain* pSwapChain : _listAcquiredSwapChain )
        {
            pSwapChain->releaseCurrent();
        }
        _listAcquiredSwapChain.clear();
        (void)_frameFenceRing.markSubmitted();
    }

    void WebGPURHIDevice::mapQueuedReadbacks( uint64 frameSerial )
    {
        if ( _listQueuedReadbackBuffer.empty() )
            return;
        WebGPUFrameFence* const pFence = &_arrFrameFence[RHIFrameFenceRing::getSlot( frameSerial )];
        pFence->_listMappedBuffer      = std::move( _listQueuedReadbackBuffer );
        _listQueuedReadbackBuffer.clear();
        pFence->_pendingMapCount.store( static_cast<uint32>( pFence->_listMappedBuffer.size() ), std::memory_order_release );
        for ( const RHIBufferHandle handle : pFence->_listMappedBuffer )
        {
            WebGPUBufferRecord* const pRecord = findBuffer( handle );
            if ( pRecord == nullptr )
            {
                pFence->_pendingMapCount.fetch_sub( 1, std::memory_order_acq_rel );
                continue;
            }
            pRecord->_bReadbackQueued = SW_FALSE;
            WGPUBufferMapCallbackInfo mapInfo{};
            mapInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
            mapInfo.callback  = &WebGPURHIDevice::onFrameBufferMapped;
            mapInfo.userdata1 = pFence;
            pFence->_listMapFuture.push_back( wgpuBufferMapAsync( pRecord->_buffer, WGPUMapMode_Read, 0, pRecord->_sizeBytes, mapInfo ) );
        }
    }

    void WebGPURHIDevice::onFrameWorkDone( WGPUQueueWorkDoneStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
    {
        (void)pUserdata2;
        WebGPUFrameFence* const pFence = static_cast<WebGPUFrameFence*>( pUserdata1 );
        if ( status != WGPUQueueWorkDoneStatus_Success )
            SW_LOG_ERROR_ONCE_ID( "WebGPU.FrameWorkFailed", "WebGPU: a submitted frame did not complete (status %#): %#", static_cast<uint32>( status ), WebGPURHIUtil::fromStringView( message ) );
        pFence->_bWorkDone.store( SW_TRUE, std::memory_order_release );
    }

    void WebGPURHIDevice::onFrameBufferMapped( WGPUMapAsyncStatus status, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
    {
        (void)pUserdata2;
        WebGPUFrameFence* const pFence = static_cast<WebGPUFrameFence*>( pUserdata1 );
        if ( status != WGPUMapAsyncStatus_Success )
            SW_LOG_ERROR_ID( "WebGPU.FrameMapFailed", "WebGPU: readback buffer mapping failed: %#", WebGPURHIUtil::fromStringView( message ) );
        pFence->_pendingMapCount.fetch_sub( 1, std::memory_order_acq_rel );
    }

    void WebGPURHIDevice::retireFinishedFrames()
    {
        const uint64 completed = _frameFenceRing.retire( [this]( uint32 slot, uint64 serial )
        {
            (void)serial;
            const WebGPUFrameFence& fence = _arrFrameFence[slot];
            if ( fence.isDone() == false )
                return false;
            for ( const RHIBufferHandle handle : fence._listMappedBuffer )
            {
                WebGPUBufferRecord* const pRecord = findBuffer( handle );
                if ( pRecord != nullptr )
                    pRecord->_bReadbackReady = SW_TRUE;
            }
            return true;
        } );
        // 그 프레임의 지연 해제를 부른다 — 읽기 버퍼를 읽는 콜백(`readReadbackBuffer`)이 여기서 돈다.
        _releaseQueue.tickCompleted( completed );
    }

    bool WebGPURHIDevice::waitFrameFence( const WebGPUFrameFence& fence ) const
    {
        // 큐 완료와 매핑을 하나씩 기다린다(이미 끝난 퓨처는 바로 돌아온다). 상한은 두지 않는다 — Vulkan 칸 펜스 대기(`vkWaitForFences` UINT64_MAX)와 같다.
        // AllowSpontaneous 콜백도 WaitAny 로 기다릴 수 있다(AllowProcessEvents 와 같은 까닭으로 불린다 — https://webgpu-native.github.io/webgpu-headers/Asynchronous-Operations.html).
        if ( fence._workDoneFuture.id != 0 && waitForFuture( fence._workDoneFuture, "the frame's submitted work" ) == false )
            return false;
        for ( const WGPUFuture future : fence._listMapFuture )
        {
            if ( future.id != 0 && waitForFuture( future, "the frame's readback mapping" ) == false )
                return false;
        }
        return fence.isDone();
    }

    void WebGPURHIDevice::processCompletions()
    {
        if ( _instance == nullptr )
            return;
        // 네이티브(Dawn)는 이벤트를 돌려야 콜백이 온다. 브라우저는 이벤트 루프가 이미 불렀다 — 불러도 해가 없다.
        wgpuInstanceProcessEvents( _instance );
        retireFinishedFrames();
        collectTimestampReadbacks();
        (void)_transferQueue.retireDone( []( const unique_ptr<WebGPUTransferProbe>& probe )
        { return probe->_bDone.load( std::memory_order_acquire ) != SW_FALSE; },
                                         []( unique_ptr<WebGPUTransferProbe>& probe )
        { (void)probe; } );
    }

    void WebGPURHIDevice::enqueueGPURelease( const RHIResourceReleaseDelegate& releaseDelegate )
    {
        _releaseQueue.enqueueGPURelease( releaseDelegate, _frameFenceRing.getRecordingSerial() );
    }

    void WebGPURHIDevice::waitIdleInternal()
    {
        if ( _queue == nullptr )
            return;
        // 열린 프레임 밖이다(렌더 스레드를 비웠다). 내지 않은 리스트가 있으면 마저 낸다.
        submitPending();
#if defined( SW_PLATFORM_WEB )
        // 브라우저는 GPU 를 동기로 기다릴 수 없다. WebGPU 는 GPU 가 쓰는 객체를 놓아도 구현이 끝날 때까지 살린다(명세) — 해제 큐는 그대로 비워도 된다.
        // 되읽기는 기다리지 않는다: 끝나면 프레임 완료가 부른다(`readReadbackBuffer` 의 계약은 콜백 안이다). 시나리오의 "기다린 직후 읽기" 길은 웹에 없다.
#elif defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
        // Dawn: 마지막 프레임이 끝날 때까지 이벤트를 돌린다. 잃은 장치는 끝나지 않을 일을 기다리지 않는다.
        // 상한은 벽시계 마감이다 — 돈 횟수로 세면 1 ms 잠이 타이머 해상도(Windows 약 15.6 ms)로 늘어 상한이 열 배 넘게 길어진다(GL 문맥 대기와 같은 `Deadline`).
        // 폴링으로 둔다: 이 길은 waitIdle(기동 · 종료 · 백엔드 교체 · 창 크기)만 지나고 프레임 경로가 아니다.
        const Deadline deadline = Deadline::afterMilliseconds( static_cast<int64>( WebGPURHIDeviceInternal::kIdleWaitTimeoutMs ) );
        bool           bTimedOut{ false };
        while ( _bDeviceLost == SW_FALSE && _frameFenceRing.isIdle() == false )
        {
            processCompletions();
            if ( deadline.isExpired() )
            {
                bTimedOut = true;
                break;
            }
            ThreadUtil::sleepForMilliseconds( 1 );
        }
        if ( bTimedOut )
            SW_LOG_ERROR_ID( "WebGPU.WaitIdleTimeout", "WebGPU: waitIdle - the GPU did not finish within %# ms", WebGPURHIDeviceInternal::kIdleWaitTimeoutMs );
#else
    #error "Unsupported platform"
#endif
        processCompletions();
        _releaseQueue.flushAll();
    }

    // ------------------------------------------------------------------------------
    // 창 스왑체인
    // ------------------------------------------------------------------------------
    IRHISwapChain* WebGPURHIDevice::createSwapChainInternal( const RHISwapChainDesc& desc )
    {
        unique_ptr<WebGPURHISwapChain> swapChain = make_unique<WebGPURHISwapChain>();
        if ( swapChain->createSurface( _instance, _adapter, desc ) == false || swapChain->configure( _device, desc._width, desc._height, isVSyncEnabled() ) == false )
            return nullptr;
        _listExtraSwapChain.push_back( std::move( swapChain ) );
        return _listExtraSwapChain.back().get();
    }

    void WebGPURHIDevice::resizeSwapChainInternal( IRHISwapChain& swapChain, uint32 width, uint32 height )
    {
        (void)static_cast<WebGPURHISwapChain&>( swapChain ).configure( _device, width, height, isVSyncEnabled() );
    }

    void WebGPURHIDevice::destroySwapChainInternal( IRHISwapChain& swapChain )
    {
        for ( auto it = _listExtraSwapChain.begin(); it != _listExtraSwapChain.end(); ++it )
        {
            if ( it->get() == &swapChain )
            {
                _listExtraSwapChain.erase( it );
                return;
            }
        }
    }

    bool WebGPURHIDevice::acquireSwapChainImageInternal( IRHISwapChain& swapChain )
    {
        WebGPURHISwapChain& target = static_cast<WebGPURHISwapChain&>( swapChain );
        if ( target.acquire( _device ) == false )
            return false;
        _listAcquiredSwapChain.push_back( &target );
        return true;
    }

    // ------------------------------------------------------------------------------
    // 장치 콜백
    // ------------------------------------------------------------------------------
    void WebGPURHIDevice::onUncapturedError( const WGPUDevice* pDevice, WGPUErrorType type, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
    {
        (void)pDevice;
        (void)pUserdata1;
        (void)pUserdata2;
        SW_LOG_ERROR_ID( "WebGPU.ValidationError", "WebGPU error (type %#): %#", static_cast<uint32>( type ), WebGPURHIUtil::fromStringView( message ) );
    }

    void WebGPURHIDevice::onDeviceLost( const WGPUDevice* pDevice, WGPUDeviceLostReason reason, WGPUStringView message, void* pUserdata1, void* pUserdata2 )
    {
        (void)pDevice;
        (void)pUserdata2;
        WebGPURHIDevice* const pThis = static_cast<WebGPURHIDevice*>( pUserdata1 );
        // 종료 중의 장치 파괴(Destroyed)도 여기로 온다 — 그것은 잃은 것이 아니다.
        if ( reason == WGPUDeviceLostReason_Destroyed || pThis == nullptr )
            return;
        SW_LOG_ERROR_ID( "WebGPU.DeviceLost", "WebGPU device lost (reason %#): %#", static_cast<uint32>( reason ), WebGPURHIUtil::fromStringView( message ) );
        pThis->_bDeviceLost = SW_TRUE;
        (void)pThis->_transferSerials.markLost();
    }
} // namespace sw
