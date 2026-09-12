#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Window/IWindow.h"

namespace sw
{

    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::ensurePassResources()
    {
        if ( _pDevice == nullptr || _bPassResourcesReady != SW_FALSE )
            return;

        // 패스마다 자기 상수 버퍼를 갖도록 슬롯을 미리 만들어 둔다.
        _listPassCbSlot.clear();
        _listPassCbSlot.reserve( _s_kPassCbSlotCount );
        for ( uint32 slotIndex = 0; slotIndex < _s_kPassCbSlotCount; ++slotIndex )
        {
            PassCbSlot slot{};
            slot._buffer = _pDevice->getResource()->createConstantBuffer( _s_kEnginePassCbSize );
            if ( slot._buffer == 0 )
                break;
            slot._index = _pDevice->getResource()->registerBindlessResource( slot._buffer );
            _listPassCbSlot.push_back( slot );
        }
        _passCbCursor.store( 0, std::memory_order_relaxed );
        // 직렬 경로 시드는 0번 슬롯을 쓴다. (패스별 경로는 acquirePassCb 로 덮어쓴다)
        if ( _listPassCbSlot.empty() == false )
        {
            _frameCtx._passCb      = _listPassCbSlot[0]._buffer;
            _frameCtx._passCbIndex = _listPassCbSlot[0]._index;
        }

        struct GpuCullParams
        {
            float32 _planes[6][4]{};
            uint32  _instanceCount{ 0 };
            uint32  _batchCount{ 0 };
            uint32  _pad[2]{};
        };
        // 뷰마다 하나씩 — 절두체가 다르므로 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치의 내용을 덮어쓴다.
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
        {
            RenderView& renderView = _arrView[viewIndex];
            renderView._cullCb     = _pDevice->getResource()->createConstantBuffer( sizeof( GpuCullParams ) );
            if ( renderView._cullCb != 0 )
                renderView._cullCbIndex = _pDevice->getResource()->registerBindlessResource( renderView._cullCb );
        }

        struct GpuAnimParams
        {
            float32 _time{ 0.0f };
            float32 _baseSpeed{ 0.0f };
            float32 _speedRange{ 0.0f };
            uint32  _instanceCount{ 0 };
        };
        _instanceAnimCb = _pDevice->getResource()->createConstantBuffer( sizeof( GpuAnimParams ) );
        if ( _instanceAnimCb != 0 )
            _instanceAnimCbIndex = _pDevice->getResource()->registerBindlessResource( _instanceAnimCb );

        struct GpuSortParams
        {
            float32 _cameraPos[4]{};
            uint32  _instanceCount{ 0 };
            uint32  _batchCount{ 0 };
            uint32  _pad[2]{};
        };
        _instanceSortCb = _pDevice->getResource()->createConstantBuffer( sizeof( GpuSortParams ) );
        if ( _instanceSortCb != 0 )
            _instanceSortCbIndex = _pDevice->getResource()->registerBindlessResource( _instanceSortCb );

        constexpr RHIFormat arrGbufferFormat[] = { RHIFormat::R8G8B8A8_UNORM, RHIFormat::R16G16B16A16_FLOAT };
        const EngineData&   engineData         = engine::getEngineData();
        // Shader paths prefer pipeline XML pass recipes; EngineData paths are last-resort fallbacks only.

        auto registerPso = [this]( RenderPassType passType, string_view shaderPath, bool bDepthTest = true, uint32 numRt = 1,
                                   const RHIFormat* pRtFormats = nullptr, bool bBlend = false, bool bDepthWrite = true ) -> RHIPipelineStateHandle
        {
            const RHIPipelineStateHandle pso =
                createPsoForPassType( passType, shaderPath, bDepthTest, numRt, pRtFormats, bBlend, bDepthWrite );
            if ( pso != 0 )
                _mapEnginePso.insert_or_assign( passType, pso );
            return pso;
        };

        registerPso( RenderPassType::Shadow, engineData._shaderShadowDepth.c_str(), true, 0, nullptr, false, true );
        registerPso( RenderPassType::DepthPrepass, engineData._shaderShadowDepth.c_str(), true, 0, nullptr, false, true );
        registerPso( RenderPassType::ForwardOpaque, engineData._shaderForwardLit.c_str(), true );
        registerPso( RenderPassType::ForwardOpaqueNoDepthWrite, engineData._shaderForwardLit.c_str(), true, 1, nullptr, false, false );
        // 반투명 패스는 블렌드를 켜고 뎁스 쓰기를 끄는 것까지가 **패스의 몫**이다. 어떤 셰이더 퍼뮤테이션으로
        // 그릴지는 머티리얼이 정한다 — glassmaterial 이 MATERIAL_BLEND_TRANSLUCENT 를 always-define 으로 들고
        // 있고, ensureMaterialPsos 가 그 변형을 만들어 배치에 걸어 준다. 예전엔 이 define 을 여기에 박아 두어
        // "반투명 패스에 들어온 것은 무조건 반투명" 이었다 — 머티리얼이 뭘 선언했든 상관이 없었다.
        registerPso( RenderPassType::Transparent, engineData._shaderForwardLit.c_str(), true, 1, nullptr, true, false );
        registerPso( RenderPassType::GBuffer, engineData._shaderGBuffer.c_str(), true, 2, arrGbufferFormat );
        registerPso( RenderPassType::GBufferAlbedo, engineData._shaderGBufferAlbedo.c_str(), true );
        registerPso( RenderPassType::GBufferNormal, engineData._shaderGBufferNormal.c_str(), true );
        registerPso( RenderPassType::Lighting, engineData._shaderDeferredLighting.c_str(), false );
        registerPso( RenderPassType::Bloom, engineData._shaderPostBloom.c_str(), false );

        {
            RHIPipelineStateHandle psoOutline = createPsoForPassType( RenderPassType::Outline, engineData._shaderPostOutlineCommon.c_str(), false );
            if ( psoOutline == 0 )
                psoOutline = createEnginePso( engineData._shaderPostOutlineEngine.c_str(), false );
            if ( psoOutline != 0 )
                _mapEnginePso.insert_or_assign( RenderPassType::Outline, psoOutline );
        }

        registerPso( RenderPassType::Present, engineData._shaderFullscreenBlit.c_str(), false );

        const RHIPipelineStateHandle psoSsao = registerPso( RenderPassType::SSAO, engineData._shaderSsao.c_str(), false );
        if ( psoSsao != 0 )
            _mapEnginePso.insert_or_assign( RenderPassType::SSAO, psoSsao );

        registerPso( RenderPassType::TAA, engineData._shaderTaa.c_str(), false );
        registerPso( RenderPassType::Tonemap, engineData._shaderTonemap.c_str(), false );

        // Compute PSO: GpuCull — capabilities + 실제 PSO 생성 성공 시에만 GPU-driven.
        const RHICapabilities caps = _pDevice->getCapabilities();
        if ( caps._bGpuCulling != SW_FALSE )
        {
            const RHIPipelineStateHandle psoGpuCull =
                _pDevice->getResource()->createComputePipelineState( engineData._shaderGpuCull.c_str(), FrameRendererUtil::Entry::kCSMain );
            if ( psoGpuCull != 0 )
                _mapEnginePso.insert_or_assign( RenderPassType::GpuCull, psoGpuCull );

            // 압축한 가시 목록을 깊이순으로 되돌리는 패스 — 컬링과 같은 바인딩 자리를 쓴다.
            const RHIPipelineStateHandle psoSort =
                _pDevice->getResource()->createComputePipelineState( engineData._shaderInstanceSort.c_str(), FrameRendererUtil::Entry::kCSMain );
            if ( psoSort != 0 )
                _mapEnginePso.insert_or_assign( RenderPassType::InstanceSort, psoSort );
        }

        // 인스턴스 애니메이션은 **컬링 능력과 무관하다** — 구조버퍼 UAV 하나만 있으면 된다.
        // 예전엔 위 GpuCull 블록 안에 있었는데, DX11 은 "한 버퍼에 STRUCTURED 와 DRAWINDIRECT_ARGS 를
        // 같이 못 건다"는 **간접 인자 쪽 제약** 때문에 _bGpuCulling 이 0 이다. 애니메이션은 인스턴스
        // 버퍼만 쓰므로 그 제약과 상관이 없는데 같이 꺼져서, DX11 에서만 큐브가 아예 돌지 않았다.
        if ( caps._bCompute != SW_FALSE )
        {
            const RHIPipelineStateHandle psoAnim =
                _pDevice->getResource()->createComputePipelineState( engineData._shaderInstanceAnim.c_str(), FrameRendererUtil::Entry::kCSMain );
            if ( psoAnim != 0 )
                _mapEnginePso.insert_or_assign( RenderPassType::InstanceAnim, psoAnim );
        }

        // 씬 메시는 **인다이렉트 드로우 하나로만** 그린다 — 예전엔 진단용 전역변수로 끌 수 있는 두 번째
        // 드로우 루프가 있었지만, 컬링·정렬·인스턴스 애니메이션이 전부 인다이렉트 경로에만 붙어 있어
        // 그걸 끄면 조용히 다른 그림이 나왔다. 지원하지 않는 백엔드가 생기면 조용히 안 그리는 대신
        // 여기서 크게 알린다.
        if ( caps._bIndirectDraw == SW_FALSE )
            SW_LOG_ERROR( "이 백엔드는 인다이렉트 드로우를 지원하지 않습니다 — 씬 메시를 그릴 수 없습니다." );

        // Present 변종도 PSO 등록 단계에서 만든다 — 기록 중에는 PSO 를 만들 수 없다(ensurePresentPso 주석 참고).
        buildPresentPsoVariants();

        // 폴백 원소는 PSO 를 다 등록한 뒤에 만든다 — 필요한 stride 를 레이아웃에서 읽어야 하고, 기록 중에는 만들 수 없다.
        ensureMaterialFallbackBuffers();

        _bPassResourcesReady = SW_TRUE;
        SW_LOG_INFO( "Pass PSOs/CB ready (shadow=%# forward=%# transparent=%# deferred=%# bloom=%# outline=%# gpuDriven=%#)",
                     getEnginePso( RenderPassType::Shadow ), getEnginePso( RenderPassType::ForwardOpaque ),
                     getEnginePso( RenderPassType::Transparent ), getEnginePso( RenderPassType::Lighting ),
                     getEnginePso( RenderPassType::Bloom ), getEnginePso( RenderPassType::Outline ), static_cast<uint32>( caps._bIndirectDraw ) );
    }

    bool FrameRenderer::markAttachmentCleared( const hashed_string& key )
    {
        std::scoped_lock<mutex> lock{ _clearedMutex };
        if ( std::find( _listClearedThisFrame.begin(), _listClearedThisFrame.end(), key ) != _listClearedThisFrame.end() )
            return false;
        _listClearedThisFrame.push_back( key );
        return true;
    }

    void FrameRenderer::resetClearedAttachments()
    {
        std::scoped_lock<mutex> lock{ _clearedMutex };
        _listClearedThisFrame.clear();
    }

    void FrameRenderer::resetPassCbRing()
    {
        // 0번은 프레임 시드 전용이라 패스에는 1번부터 나눠 준다.
        _passCbCursor.store( 1, std::memory_order_relaxed );
        _bPassCbExhaustedLogged.store( 0 );
        if ( _listPassCbSlot.empty() )
            return;
        _frameCtx._passCb      = _listPassCbSlot[0]._buffer;
        _frameCtx._passCbIndex = _listPassCbSlot[0]._index;
    }

    bool FrameRenderer::acquireCbSlot( RHIBufferHandle& outBuffer, RHIDescriptorIndex& outIndex )
    {
        if ( _listPassCbSlot.empty() )
            return false;

        uint32 ticket = _passCbCursor.fetch_add( 1, std::memory_order_relaxed );

        // 다음 프레임 용량 산정용 최댓값. 단조 증가라 한 번 커진 용량은 줄지 않는다.
        uint32 previousHigh = _passCbHighWater.load( std::memory_order_relaxed );
        while ( previousHigh < ticket + 1 &&
                _passCbHighWater.compare_exchange_weak( previousHigh, ticket + 1,
                                                        std::memory_order_relaxed,
                                                        std::memory_order_relaxed ) == false )
        {
        }

        if ( ticket >= static_cast<uint32>( _listPassCbSlot.size() ) )
        {
            // 슬롯이 모자라면 마지막 슬롯을 공유한다 — 그 프레임은 배치 상수가 섞인다. 예전엔 0번으로
            // 되돌렸는데 0번은 프레임 시드 전용이라(resetPassCbRing 참고) 시드까지 덮어써 더 크게 망가졌다.
            // 경고는 프레임당 한 번만 — 드로우마다 찍으면 로그가 잠긴다.
            if ( _bPassCbExhaustedLogged.exchange( 1 ) == 0 )
            {
                SW_LOG_WARNING( "상수버퍼 슬롯이 부족합니다 (%#개) — 이 프레임의 남은 드로우는 마지막 슬롯을 공유해 배치 상수가 섞입니다.",
                                static_cast<uint32>( _listPassCbSlot.size() ) );
            }
            ticket = static_cast<uint32>( _listPassCbSlot.size() ) - 1;
        }

        // 슬롯 배열은 기록 시작 전에 잡아 두고 병렬 구간에서 크기가 변하지 않는다. 분배도 위의
        // atomic 커서가 하므로 락이 필요 없다 — 다만 **const 로 읽어야** 한다. 비-const 접근은
        // "쓰기" 로 취급되어, 서로 다른 슬롯을 읽기만 하는 드로우 둘도 레이스로 잡힌다.
        const vector<FrameRenderer::PassCbSlot>& listSlot = _listPassCbSlot;
        outBuffer                                         = listSlot[ticket]._buffer;
        outIndex                                          = listSlot[ticket]._index;
        return true;
    }

    void FrameRenderer::acquirePassCb( FramePassContext& ctx )
    {
        // 패스 진입 시의 기본 슬롯. 실제 드로우는 bindForDraw 가 드로우마다 새 슬롯을 잡는다.
        acquireCbSlot( ctx._passCb, ctx._passCbIndex );
        // 값은 드로우 직전 ShaderBindingBinder::bindGraphics 가 리플렉션 오프셋으로 채운다
        // (ctx._passValues 에 이미 프레임 시드가 들어있으므로 별도 선-업로드가 필요 없다).
    }

    void FrameRenderer::ensurePassCbCapacity( uint32 needed )
    {
        if ( _pDevice == nullptr || _pDevice->getResource() == nullptr )
            return;
        const uint32 target = MathUtil::min( needed, _s_kMaxPassCbSlotCount );
        if ( static_cast<uint32>( _listPassCbSlot.size() ) >= target )
            return;

        _listPassCbSlot.reserve( target );
        while ( static_cast<uint32>( _listPassCbSlot.size() ) < target )
        {
            PassCbSlot slot{};
            slot._buffer = _pDevice->getResource()->createConstantBuffer( _s_kEnginePassCbSize );
            if ( slot._buffer == 0 )
                break;
            slot._index = _pDevice->getResource()->registerBindlessResource( slot._buffer );
            _listPassCbSlot.push_back( slot );
        }
    }

    void FrameRenderer::releasePassResources()
    {
        // PSO·레이아웃·패스 CB 가 여기서 사라진다 — 그것을 가리키던 드로우 캐시도 같이 잊는다.
        // (새 디바이스의 핸들이 옛 값과 겹치면 캐시가 "그대로" 라고 속는다 — 백엔드 교체 뒤 빈 화면의 원인.)
        _frameCtx.resetBindingCache();

        if ( _pDevice == nullptr )
        {
            _listPassCbSlot.clear();
            _passCbCursor.store( 0, std::memory_order_relaxed );
            _frameCtx._passCb      = 0;
            _frameCtx._passCbIndex = kInvalidDescriptorIndex;
            for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
            {
                _arrView[viewIndex]._cullCb      = 0;
                _arrView[viewIndex]._cullCbIndex = kInvalidDescriptorIndex;
            }
            _instanceAnimCb      = 0;
            _instanceAnimCbIndex = kInvalidDescriptorIndex;
            _instanceSortCb      = 0;
            _instanceSortCbIndex = kInvalidDescriptorIndex;
            _mapMaterialFallback.clear();
            _taaHistory    = 0;
            _taaHistorySrv = kInvalidDescriptorIndex;
            _mapEnginePso.clear();
            _mapPresentPso.clear();
            {
                std::scoped_lock<mutex> lock{ _materialPsoMutex };
                _mapMaterialPso.clear();
            }
            {
                std::scoped_lock<mutex> lock{ _psoLayoutMutex };
                _mapPsoLayout.clear();
                _mapPsoDesc.clear();
            }
            _bPassResourcesReady = SW_FALSE;
            return;
        }

        // 퍼뮤테이션 변형을 **패스 PSO 보다 먼저** 파괴한다. `_bOwned` 가 0 인 항목은 패스 PSO 를 그대로
        // 담고 있을 뿐이라 여기서 파괴하면 두 번 파괴하는 셈이 된다.
        {
            std::scoped_lock<mutex> lock{ _materialPsoMutex };
            for ( auto& [key, entry] : _mapMaterialPso )
            {
                if ( entry._bOwned != 0 && entry._pso != 0 )
                    _pDevice->getResource()->destroyPipelineState( entry._pso );
            }
            _mapMaterialPso.clear();
        }

        for ( auto& [name, pso] : _mapEnginePso )
        {
            if ( pso != 0 )
            {
                _pDevice->getResource()->destroyPipelineState( pso );
                pso = 0;
            }
        }
        _mapEnginePso.clear();
        for ( auto& [format, pso] : _mapPresentPso )
        {
            if ( pso != 0 )
                _pDevice->getResource()->destroyPipelineState( pso );
        }
        _mapPresentPso.clear();

        // 두 맵은 방금 파괴한 PSO 핸들로 키를 잡고 있다. 핸들이 generation 팩드라 되살아난
        // 핸들이 옛 항목을 집는 일은 없지만, 셰이더 리로드마다 재생성을 도는 지금은 그대로 두면
        // 죽은 항목(RHIPipelineStateDesc 통째)이 계속 쌓인다.
        {
            std::scoped_lock<mutex> lock{ _psoLayoutMutex };
            _mapPsoLayout.clear();
            _mapPsoDesc.clear();
        }

        _gpuScene.releaseGpu( _pDevice );

        auto releaseResource = [this]( RHIBufferHandle& handle, RHIDescriptorIndex& srvIndex, bool bIsTexture = false )
        {
            if ( srvIndex != kInvalidDescriptorIndex )
            {
                // 텍스처와 버퍼는 인덱스 공간이 다르다 — 종류에 맞는 해제를 불러야 한다.
                if ( bIsTexture )
                    _pDevice->getResource()->unregisterBindlessTexture( srvIndex );
                else
                    _pDevice->getResource()->unregisterBindlessResource( srvIndex );
                srvIndex = kInvalidDescriptorIndex;
            }
            if ( handle != 0 )
            {
                if ( bIsTexture )
                    _pDevice->getResource()->destroyTexture( handle );
                else
                    _pDevice->getResource()->destroyBuffer( handle );
                handle = 0;
            }
        };

        for ( PassCbSlot& slot : _listPassCbSlot )
            releaseResource( slot._buffer, slot._index );
        _listPassCbSlot.clear();
        _passCbCursor.store( 0, std::memory_order_relaxed );
        _frameCtx._passCb      = 0;
        _frameCtx._passCbIndex = kInvalidDescriptorIndex;
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
            releaseResource( _arrView[viewIndex]._cullCb, _arrView[viewIndex]._cullCbIndex );
        releaseResource( _instanceAnimCb, _instanceAnimCbIndex );
        releaseResource( _instanceSortCb, _instanceSortCbIndex );
        for ( auto& [fallbackStride, fallbackSlot] : _mapMaterialFallback )
            fallbackSlot.release( _pDevice );
        _mapMaterialFallback.clear();
        releaseResource( _taaHistory, _taaHistorySrv, true );
        _bPassResourcesReady = SW_FALSE;
    }

    void FrameRenderer::ensureTransientResources( uint32 overrideWidth, uint32 overrideHeight )
    {
        if ( _pDevice == nullptr )
            return;

        uint32 width  = FrameRendererUtil::kDefaultTransientSize;
        uint32 height = FrameRendererUtil::kDefaultTransientSize;
        if ( overrideWidth > 0 && overrideHeight > 0 )
        {
            width  = overrideWidth;
            height = overrideHeight;
        }
        else
        {
            IWindow* pWindow = IWindow::getActiveWindow();
            if ( pWindow != nullptr )
            {
                if ( pWindow->getWidth() > 0 )
                    width = pWindow->getWidth();
                if ( pWindow->getHeight() > 0 )
                    height = pWindow->getHeight();
            }
        }

        if ( width == _transientWidth && height == _transientHeight && _mapTransient.empty() == false )
            return;

        releaseTransientResources();
        _transientWidth  = width;
        _transientHeight = height;

        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            const RHIFormat format = FrameRendererUtil::parseAttachmentFormat( att._format );
            allocTransient( att._name, format, FrameRendererUtil::isDepthFormat( format ), att._clearColor );
        }

        auto ensureNamed = [&]( string_view name )
        {
            if ( _mapTransient.find( name ) != _mapTransient.end() || name == FrameRendererUtil::Attachment::kSwapchain )
                return;

            float4     clearColor{};
            const bool bHasClear = tryGetAttachmentClearColor( name, clearColor );

            if ( name == FrameRendererUtil::Attachment::kShadowMap || name == FrameRendererUtil::Attachment::kSceneDepth )
                allocTransient( name, RHIFormat::D24_UNORM_S8_UINT, true, bHasClear ? clearColor : FrameRendererUtil::kDepthClear );
            else if ( name == FrameRendererUtil::Attachment::kGBufferNormal || name == FrameRendererUtil::Attachment::kLitColor || name == FrameRendererUtil::Attachment::kBloomColor || name == FrameRendererUtil::Attachment::kBloomBright )
                allocTransient( name, RHIFormat::R16G16B16A16_FLOAT, false, bHasClear ? clearColor : FrameRendererUtil::kBloomClear );
            else if ( name == FrameRendererUtil::Attachment::kSceneColor )
                allocTransient( name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : FrameRendererUtil::kSceneClear );
            else
                allocTransient( name, RHIFormat::R8G8B8A8_UNORM, false, bHasClear ? clearColor : FrameRendererUtil::kBlackClear );
        };

        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            for ( const string& in : pass._listInput )
            {
                ensureNamed( in );
            }
            for ( const string& out : pass._listOutput )
            {
                ensureNamed( out );
            }
        }

        ensureTaaHistory();
    }

    void FrameRenderer::ensureTaaHistory()
    {
        if ( _pDevice == nullptr || _taaHistory != 0 )
            return;

        // 파이프라인에 TAA 패스가 없으면 히스토리도 필요 없다.
        bool bHasTaaPass = false;
        for ( const RenderGraphPassDesc& pass : _pipelineResource.getGraphPass() )
        {
            if ( pass._resolvedType == RenderPassType::TAA )
            {
                bHasTaaPass = true;
                break;
            }
        }
        if ( bHasTaaPass == false )
            return;

        // 히스토리는 TAA 출력의 복사본이다 — CopyResource 는 포맷이 정확히 같아야 하므로 대상
        // 첨부의 포맷을 그대로 따라간다.
        const bool        bHasTaaColor = _mapTransient.find( string_view{ "TaaColor" } ) != _mapTransient.end();
        const string_view taaTarget    = bHasTaaColor ? string_view{ "TaaColor" }
                                                      : string_view{ FrameRendererUtil::Attachment::kSceneColor };

        RHITextureDesc histDesc{};
        histDesc._width             = _transientWidth != 0 ? _transientWidth : FrameRendererUtil::kDefaultTransientSize;
        histDesc._height            = _transientHeight != 0 ? _transientHeight : FrameRendererUtil::kDefaultTransientSize;
        histDesc._format            = attachmentFormatOrDefault( taaTarget, constant::kBackBufferFormat );
        histDesc._bIsRenderTarget   = SW_TRUE;
        histDesc._bIsShaderResource = SW_TRUE;

        _taaHistory = _pDevice->getResource()->createTexture2D( histDesc );
        if ( _taaHistory != 0 )
            _taaHistorySrv = _pDevice->getResource()->registerBindlessTexture( _taaHistory );
    }

    bool FrameRenderer::readbackTransient( string_view attachmentName, vector<uint8>& outBytes, RHITextureMipSpan& outLayout, RHIFormat& outFormat )
    {
        if ( _pDevice == nullptr )
            return false;
        const RHITextureHandle texture = findTransient( attachmentName );
        if ( texture == 0 )
        {
            SW_LOG_ERROR( "readbackTransient: 트랜지언트 '%#' 를 찾지 못했습니다.", string( attachmentName ).c_str() );
            return false;
        }
        outFormat = RHIFormat::R8G8B8A8_UNORM;
        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            if ( att._name == attachmentName )
            {
                outFormat = FrameRendererUtil::parseAttachmentFormat( att._format );
                break;
            }
        }
        if ( _pDevice->getResource()->readbackTexture2D( texture, 0, outBytes, outLayout ) == false )
        {
            SW_LOG_ERROR( "readbackTransient: readbackTexture2D 실패 ('%#').", string( attachmentName ).c_str() );
            return false;
        }
        return outLayout._width != 0 && outLayout._height != 0;
    }

    bool FrameRenderer::dumpTransientToPpm( string_view attachmentName, string_view outFilePath )
    {
        if ( _pDevice == nullptr || outFilePath.empty() )
            return false;

        vector<uint8>     bytes;
        RHITextureMipSpan layout{};
        RHIFormat         format = RHIFormat::R8G8B8A8_UNORM;
        if ( readbackTransient( attachmentName, bytes, layout, format ) == false )
            return false;
        const uint32 bytesPerPixel = getRHIFormatBytesPerPixel( format );
        if ( bytesPerPixel < 3 )
        {
            SW_LOG_ERROR( "dumpTransientToPpm: PPM 으로 덤프할 수 없는 포맷입니다 ('%#').", string( attachmentName ).c_str() );
            return false;
        }

        // PPM(P6): 아스키 헤더 + RGB 8bit. 트랜지언트는 R8G8B8A8 / B8G8R8A8 이라 채널 순서만 맞춘다.
        const bool                            bBgra = ( format == RHIFormat::B8G8R8A8_UNORM );
        StringBuilder<constant::kMaxBuffer64> header;
        // PPM 헤더의 구분자는 임의의 공백이면 된다 — 공백만 써서 이스케이프 없이 적는다.
        header.appendFormat( "P6 %# %# 255 ", layout._width, layout._height );

        vector<uint8> outBytes;
        outBytes.reserve( header.size() + static_cast<size_t>( layout._width ) * layout._height * 3 );
        outBytes.insert( outBytes.end(), reinterpret_cast<const uint8*>( header.c_str() ),
                         reinterpret_cast<const uint8*>( header.c_str() ) + header.size() );
        for ( uint32 row = 0; row < layout._height; ++row )
        {
            const uint8* pRow = bytes.data() + static_cast<size_t>( row ) * layout._rowBytes;
            for ( uint32 col = 0; col < layout._width; ++col )
            {
                const uint8* pPixel = pRow + static_cast<size_t>( col ) * bytesPerPixel;
                outBytes.push_back( bBgra ? pPixel[2] : pPixel[0] );
                outBytes.push_back( pPixel[1] );
                outBytes.push_back( bBgra ? pPixel[0] : pPixel[2] );
            }
        }

        if ( FileUtil::writeFile( outFilePath, outBytes.data(), outBytes.size() ) == false )
        {
            SW_LOG_ERROR( "dumpTransientToPpm: 파일 쓰기 실패 (%#).", string( outFilePath ).c_str() );
            return false;
        }
        SW_LOG_INFO( "Screenshot: '%#' %#×%# -> %#", string( attachmentName ).c_str(), layout._width, layout._height,
                     string( outFilePath ).c_str() );
        return true;
    }

    void FrameRenderer::releaseTransientResources()
    {
        if ( _pDevice == nullptr )
        {
            _mapTransient.clear();
            _taaHistory      = 0;
            _taaHistorySrv   = kInvalidDescriptorIndex;
            _transientWidth  = 0;
            _transientHeight = 0;
            return;
        }

        // 히스토리는 TAA 출력의 복사본이라 크기가 정확히 같아야 한다(CopyResource 제약) — 트랜지언트가
        // 새 크기로 다시 잡히면 이것도 같이 버려야 ensureTaaHistory 가 새 크기로 다시 만든다.
        if ( _taaHistorySrv != kInvalidDescriptorIndex )
        {
            _pDevice->getResource()->unregisterBindlessTexture( _taaHistorySrv );
            _taaHistorySrv = kInvalidDescriptorIndex;
        }
        if ( _taaHistory != 0 )
        {
            _pDevice->getResource()->destroyTexture( _taaHistory );
            _taaHistory = 0;
        }

        for ( auto& [name, attachment] : _mapTransient )
        {
            // 텍스처 SRV 인덱스다. 예전엔 버퍼용 해제로 넘겨서 버퍼 프리리스트가 오염됐고, 그 자리를
            // 인스턴스 구조버퍼가 차지해 살아 있는 패스 CB 슬롯이 STORAGE 세트로 바뀌었다(Vulkan 검증 에러).
            if ( attachment._srv != kInvalidDescriptorIndex )
                _pDevice->getResource()->unregisterBindlessTexture( attachment._srv );
            if ( attachment._texture != 0 )
                _pDevice->getResource()->destroyTexture( attachment._texture );
        }
        _mapTransient.clear();
        _transientWidth  = 0;
        _transientHeight = 0;
    }

    void FrameRenderer::allocTransient( string_view name, RHIFormat format, bool bDepth, const float4& clearColor )
    {
        if ( _mapTransient.find( name ) != _mapTransient.end() || _pDevice == nullptr )
            return;

        RHITextureDesc desc{};
        desc._width                   = _transientWidth;
        desc._height                  = _transientHeight;
        desc._format                  = format;
        desc._bIsRenderTarget         = bDepth ? 0 : 1;
        desc._bIsDepthStencil         = bDepth ? 1 : 0;
        desc._bIsShaderResource       = SW_TRUE;
        desc._clearDepth              = clearColor._x;
        desc._clearColor              = clearColor;
        const RHITextureHandle handle = _pDevice->getResource()->createTexture2D( desc );
        if ( handle == 0 )
        {
            SW_LOG_WARNING( "Failed to allocate transient '%#'", name );
            return;
        }
        const RHIDescriptorIndex srv = _pDevice->getResource()->registerBindlessTexture( handle );
        _mapTransient.emplace( name, TransientAttachment{ handle, srv } );
    }

    bool FrameRenderer::tryGetAttachmentClearColor( string_view attachmentName, float4& outClearColor ) const
    {
        for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
        {
            if ( att._name == attachmentName )
            {
                outClearColor = att._clearColor;
                return att._bClear;
            }
        }
        return false;
    }

    float4 FrameRenderer::getAttachmentClearColorOrDefault( string_view attachmentName, const float4& fallback ) const
    {
        float4 clearColor = fallback;
        tryGetAttachmentClearColor( attachmentName, clearColor );
        return clearColor;
    }

    FrameRenderer::TransientAttachment FrameRenderer::findTransientAttachment( string_view name ) const
    {
        const auto it = _mapTransient.find( name );
        return it != _mapTransient.end() ? it->second : TransientAttachment{};
    }

    RHITextureHandle FrameRenderer::findTransient( string_view name ) const
    {
        const auto it = _mapTransient.find( name );
        return it != _mapTransient.end() ? it->second._texture : 0;
    }

    string FrameRenderer::resolvePresentSource() const
    {
        const utf8* pName = FrameRendererUtil::pickFirstExisting(
            _mapTransient,
            { "TonemapColor", "OutlineColor", "BloomColor", "TaaColor",
              "TransparentColor", "LitColor", "SceneColor", "GBufferAlbedo" } );
        return pName != nullptr ? string( pName ) : string{};
    }

    RHIPipelineStateHandle FrameRenderer::getEnginePso( RenderPassType passType ) const
    {
        const auto it = _mapEnginePso.find( passType );
        return ( it != _mapEnginePso.end() ) ? it->second : 0;
    }

    void FrameRenderer::ensureMaterialFallbackBuffers()
    {
        if ( _pDevice == nullptr || _pDevice->getResource() == nullptr )
            return;

        // 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 를 모은다. 셰이더 타입마다 다를 수 있고, 같은 셰이더라도
        // 백엔드마다 다르다(DX 자연 패킹 / SPIR-V std430) — 레이아웃은 이 디바이스의 백엔드로 빌드된 것이다.
        vector<uint32> listStride;
        {
            std::scoped_lock<mutex> lock{ _psoLayoutMutex };
            for ( const auto& [pso, pLayout] : _mapPsoLayout )
            {
                if ( pLayout == nullptr )
                    continue;
                const ShaderBindingSlot* pSlot = pLayout->find( passConstantNames()._swMaterials );
                if ( pSlot == nullptr )
                    continue; // 이 셰이더는 머티리얼 버퍼를 선언하지 않는다(풀스크린 등) — 걸 것이 없다.
                if ( pSlot->_elementStride == 0 )
                {
                    // 선언은 있는데 원소 레이아웃이 없다 = 리플렉션 공백. 그대로 두면 머티리얼 없는 배치가 슬롯을 비운 채
                    // 그리게 되고(Vulkan 은 partially-bound 슬롯을 읽으면 정의되지 않는다) 원인을 찾기 어렵다.
                    SW_LOG_ERROR( "PSO %# 의 머티리얼 버퍼 원소 stride 가 0 입니다 — 리플렉션이 g_SwMaterials 원소 레이아웃을 주지 않았습니다.", pso );
                    continue;
                }
                if ( std::find( listStride.begin(), listStride.end(), pSlot->_elementStride ) == listStride.end() )
                    listStride.push_back( pSlot->_elementStride );
            }
        }

        for ( const uint32 stride : listStride )
        {
            if ( _mapMaterialFallback.find( stride ) != _mapMaterialFallback.end() )
                continue;

            const vector<uint8>     zeroBytes( stride, 0 );
            RHIStructuredBufferSlot fallback{};
            if ( fallback.ensureCapacity( _pDevice, stride, 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false,
                                          zeroBytes.data() ) == false )
            {
                SW_LOG_ERROR( "머티리얼 폴백 버퍼 생성 실패 (stride %#).", stride );
                continue;
            }
            fallback.upload( _pDevice, zeroBytes.data(), stride );
            _mapMaterialFallback.insert_or_assign( stride, fallback );
        }
    }

    void FrameRenderer::buildPresentPsoVariants()
    {
        if ( _pDevice == nullptr )
            return;

        // Present 가 그릴 수 있는 대상은 둘뿐이다 — 백버퍼(디바이스가 실제 채택한 포맷)와 오프스크린
        // 렌더타깃(에디터 GameView 등, 계약값 kOffscreenColorFormat). 둘 다 **셋업에서** 만들어 둔다.
        const RHIFormat arrTargetFormat[] = { _pDevice->getBackBufferFormat(), constant::kOffscreenColorFormat, constant::kBackBufferFormat };
        for ( const RHIFormat format : arrTargetFormat )
        {
            if ( format == RHIFormat::Unknown || _mapPresentPso.find( format ) != _mapPresentPso.end() )
                continue;
            const RHIFormat              arrRtvFormat[] = { format };
            const RHIPipelineStateHandle pso            = createPsoForPassType( RenderPassType::Present, engine::getEngineData()._shaderFullscreenBlit.c_str(),
                                                                                false, 1, arrRtvFormat );
            // 실패해도 기록한다 — 0 이면 호출부가 blit 폴백으로 간다.
            _mapPresentPso.insert_or_assign( format, pso );
        }
    }

    RHIPipelineStateHandle FrameRenderer::ensurePresentPso( RHIFormat targetFormat )
    {
        // **조회만 한다.** 예전엔 없으면 여기서 만들었는데, 이 함수는 Present 패스 실행 중 = 태스크 워커에서
        // 불린다. PSO 생성은 RHIHandleTable(락 없음)과 Vulkan 렌더패스 캐시(락 없음)를 건드리므로, 같은
        // 웨이브의 다른 패스가 드로우하며 그 표를 읽는 중이면 레이스다. checkRegistryMutableNow 는 bindless
        // 레지스트리만 감시해서 이 경우를 못 잡는다. 변종은 buildPresentPsoVariants 가 셋업에서 만든다.
        if ( targetFormat == RHIFormat::Unknown )
            return getEnginePso( RenderPassType::Present );
        const auto it = _mapPresentPso.find( targetFormat );
        if ( it != _mapPresentPso.end() )
            return it->second;

        if ( _bPresentPsoMissingLogged.exchange( 1 ) == 0 )
        {
            SW_LOG_ERROR( "Present 대상 포맷 %# 의 PSO 가 셋업에 없습니다 — buildPresentPsoVariants 에 그 포맷을 추가해야 합니다.",
                          static_cast<uint32>( targetFormat ) );
        }
        return getEnginePso( RenderPassType::Present );
    }
} // namespace sw
