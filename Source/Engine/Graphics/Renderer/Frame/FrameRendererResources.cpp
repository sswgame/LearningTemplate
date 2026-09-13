/**
 * @file FrameRendererResources.cpp
 * @brief 패스 자원의 수명 — 엔진 PSO 등록, 상수버퍼 링, 머티리얼 폴백 버퍼, Present 변종.
 * @details 여기 있는 것은 전부 **기록 시작 전에** 만들어져야 하는 것들이다. 기록 중에는 버퍼 생성도 bindless 등록도
 *          PSO 생성도 할 수 없고(패스가 병렬로 기록된다), 그래서 "프레임이 쓸 것을 미리 다 만들어 둔다" 가 한 파일의
 *          주제가 된다. 첨부(렌더타깃)는 크기를 따라 다시 만들어지는 다른 수명이라 `FrameRendererTransients.cpp` 에 있다.
 */
#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::ensurePassResources()
    {
        if ( _pDevice == nullptr || _bPassResourcesReady != SW_FALSE )
            return;

        // 패스마다 자기 상수 버퍼를 갖도록 슬롯을 미리 만들어 둔다.
        _passCbRing.initialize( _pDevice );
        // 직렬 경로 시드는 0번 슬롯을 쓴다. (패스별 경로는 acquirePassCb 로 덮어쓴다)
        _passCbRing.getSeedSlot( _frameCtx._passCb, _frameCtx._passCbIndex );

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
            renderView._cullCb.create( _pDevice, sizeof( GpuCullParams ) );
        }

        struct GpuAnimParams
        {
            float32 _time{ 0.0f };
            float32 _baseSpeed{ 0.0f };
            float32 _speedRange{ 0.0f };
            uint32  _instanceCount{ 0 };
        };
        _instanceAnimCb.create( _pDevice, sizeof( GpuAnimParams ) );

        struct GpuMorphParams
        {
            float32 _time{ 0.0f };
            float32 _amplitude{ 0.0f };
            float32 _frequency{ 0.0f };
            uint32  _vertexCount{ 0 };
        };
        _meshMorphCb.create( _pDevice, sizeof( GpuMorphParams ) );

        struct GpuSortParams
        {
            float32 _cameraPos[4]{};
            uint32  _instanceCount{ 0 };
            uint32  _batchCount{ 0 };
            uint32  _pad[2]{};
        };
        _instanceSortCb.create( _pDevice, sizeof( GpuSortParams ) );

        constexpr RHIFormat arrGbufferFormat[] = { RHIFormat::R8G8B8A8_UNORM, RHIFormat::R16G16B16A16_FLOAT };
        const EngineData&   engineData         = engine::getEngineData();
        // Shader paths prefer pipeline XML pass recipes; EngineData paths are last-resort fallbacks only.

        auto registerPso = [this]( RenderPassType passType, string_view shaderPath, bool bDepthTest = true, uint32 numRt = 1,
                                   const RHIFormat* pRtFormats = nullptr, bool bBlend = false, bool bDepthWrite = true,
                                   const vector<string>* pExtraDefine = nullptr ) -> RHIPipelineStateHandle
        {
            const RHIPipelineStateHandle pso =
                createPsoForPassType( passType, shaderPath, bDepthTest, numRt, pRtFormats, bBlend, bDepthWrite, pExtraDefine );
            if ( pso != 0 )
                _psoCache.setEnginePso( passType, pso );
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
        // G버퍼 패스의 PSO 에는 define 을 얹는다 — 이 desc 를 물려받는 **머티리얼 변형**까지 같이
        // MRT 서명으로 컴파일된다(createMaterialPsoVariant 가 패스 desc 를 통째로 복사한다).
        const vector<string> listGbufferDefine{ string{ kPassGBufferDefine } };
        registerPso( RenderPassType::GBuffer, engineData._shaderGBuffer.c_str(), true, 2, arrGbufferFormat, false, true,
                     &listGbufferDefine );
        registerPso( RenderPassType::GBufferAlbedo, engineData._shaderGBufferAlbedo.c_str(), true );
        registerPso( RenderPassType::GBufferNormal, engineData._shaderGBufferNormal.c_str(), true );
        registerPso( RenderPassType::Lighting, engineData._shaderDeferredLighting.c_str(), false );
        registerPso( RenderPassType::Bloom, engineData._shaderPostBloom.c_str(), false );

        {
            RHIPipelineStateHandle psoOutline = createPsoForPassType( RenderPassType::Outline, engineData._shaderPostOutlineCommon.c_str(), false );
            if ( psoOutline == 0 )
                psoOutline = createEnginePso( engineData._shaderPostOutlineEngine.c_str(), false );
            if ( psoOutline != 0 )
                _psoCache.setEnginePso( RenderPassType::Outline, psoOutline );
        }

        registerPso( RenderPassType::Present, engineData._shaderFullscreenBlit.c_str(), false );

        const RHIPipelineStateHandle psoSsao = registerPso( RenderPassType::SSAO, engineData._shaderSsao.c_str(), false );
        if ( psoSsao != 0 )
            _psoCache.setEnginePso( RenderPassType::SSAO, psoSsao );

        registerPso( RenderPassType::TAA, engineData._shaderTaa.c_str(), false );
        registerPso( RenderPassType::Tonemap, engineData._shaderTonemap.c_str(), false );

        // Compute PSO: GpuCull — capabilities + 실제 PSO 생성 성공 시에만 GPU-driven.
        const RHICapabilities caps = _pDevice->getCapabilities();
        if ( caps._bGpuCulling != SW_FALSE )
        {
            const RHIPipelineStateHandle psoGpuCull =
                _pDevice->getResource()->createComputePipelineState( engineData._shaderGpuCull.c_str(), FrameRendererUtil::Entry::kCSMain );
            if ( psoGpuCull != 0 )
                _psoCache.setEnginePso( RenderPassType::GpuCull, psoGpuCull );

            // 압축한 가시 목록을 깊이순으로 되돌리는 패스 — 컬링과 같은 바인딩 자리를 쓴다.
            const RHIPipelineStateHandle psoSort =
                _pDevice->getResource()->createComputePipelineState( engineData._shaderInstanceSort.c_str(), FrameRendererUtil::Entry::kCSMain );
            if ( psoSort != 0 )
                _psoCache.setEnginePso( RenderPassType::InstanceSort, psoSort );
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
                _psoCache.setEnginePso( RenderPassType::InstanceAnim, psoAnim );

            // 메시 모프도 같은 조건이다 — 구조버퍼 SRV 하나와 UAV 하나뿐이라 컬링 능력과 무관하다.
            const RHIPipelineStateHandle psoMorph =
                _pDevice->getResource()->createComputePipelineState( engineData._shaderMeshMorph.c_str(), FrameRendererUtil::Entry::kCSMain );
            if ( psoMorph != 0 )
                _psoCache.setEnginePso( RenderPassType::MeshMorph, psoMorph );
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

    void FrameRenderer::releasePassResources()
    {
        // PSO·레이아웃·패스 CB 가 여기서 사라진다 — 그것을 가리키던 드로우 캐시도 같이 잊는다.
        // (새 디바이스의 핸들이 옛 값과 겹치면 캐시가 "그대로" 라고 속는다 — 백엔드 교체 뒤 빈 화면의 원인.)
        _frameCtx.resetBindingCache();

        if ( _pDevice == nullptr )
        {
            _passCbRing.forget();
            _frameCtx._passCb      = 0;
            _frameCtx._passCbIndex = kInvalidDescriptorIndex;
            for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
            {
                _arrView[viewIndex]._cullCb.forget();
            }
            _instanceAnimCb.forget();
            _instanceSortCb.forget();
            _meshMorphCb.forget();
            _mapMaterialFallback.clear();
            _taaHistory    = 0;
            _taaHistorySrv = kInvalidDescriptorIndex;
            _psoCache.forgetAll();
            _bPassResourcesReady = SW_FALSE;
            return;
        }

        // PSO 는 캐시가 순서대로 놓는다 — 변형(소유한 것만) → 패스 → Present → 레이아웃 표.
        _psoCache.releaseAll( _pDevice );

        _gpuScene.releaseGpu( _pDevice );

        _passCbRing.release( _pDevice );
        _frameCtx._passCb      = 0;
        _frameCtx._passCbIndex = kInvalidDescriptorIndex;
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
            _arrView[viewIndex]._cullCb.release( _pDevice );
        _instanceAnimCb.release( _pDevice );
        _instanceSortCb.release( _pDevice );
        // 모프 상수버퍼와 모프 풀은 여태 **한 번도 놓지 않고 있었다** — 형제(anim·sort)만 적혀 있었다.
        // 디바이스가 바뀌면 옛 디바이스의 버퍼와 bindless 항목이 그대로 남는다.
        _meshMorphCb.release( _pDevice );
        _meshMorphPool.release( _pDevice );
        _lightBuffer.release( _pDevice );
        for ( auto& [fallbackStride, fallbackSlot] : _mapMaterialFallback )
            fallbackSlot.release( _pDevice );
        _mapMaterialFallback.clear();
        // TAA 히스토리만 **텍스처**다 — bindless 인덱스 공간이 버퍼와 달라 텍스처용 해제를 불러야 한다.
        if ( _taaHistorySrv != kInvalidDescriptorIndex )
            _pDevice->getResource()->unregisterBindlessTexture( _taaHistorySrv );
        if ( _taaHistory != 0 )
            _pDevice->getResource()->destroyTexture( _taaHistory );
        _taaHistory          = 0;
        _taaHistorySrv       = kInvalidDescriptorIndex;
        _bPassResourcesReady = SW_FALSE;
    }

    void FrameRenderer::resetPassCbRing()
    {
        _passCbRing.beginFrame();
        _passCbRing.getSeedSlot( _frameCtx._passCb, _frameCtx._passCbIndex );
    }

    void FrameRenderer::acquirePassCb( FramePassContext& ctx )
    {
        // 패스 진입 시의 기본 슬롯. 실제 드로우는 bindForDraw 가 드로우마다 새 슬롯을 잡는다.
        _passCbRing.acquire( ctx._passCb, ctx._passCbIndex );
        // 값은 드로우 직전 ShaderBindingBinder::bindGraphics 가 리플렉션 오프셋으로 채운다
        // (ctx._passValues 에 이미 프레임 시드가 들어있으므로 별도 선-업로드가 필요 없다).
    }

    void FrameRenderer::ensurePassCbCapacityForFrame()
    {
        const uint32 batchCount = static_cast<uint32>( _gpuScene.getOpaqueBatches().size() + _gpuScene.getTransparentBatches().size() );
        const uint32 estimate   = batchCount * _s_kDrawCbPassEstimate + PassConstantRing::kInitialSlotCount;
        _passCbRing.ensureCapacity( _pDevice, MathUtil::max( estimate, _passCbRing.getHighWater() + PassConstantRing::kInitialSlotCount ) );
    }

    RHIPipelineStateHandle FrameRenderer::getEnginePso( RenderPassType passType ) const
    {
        return _psoCache.findEnginePso( passType );
    }

    void FrameRenderer::ensureMaterialFallbackBuffers()
    {
        if ( _pDevice == nullptr || _pDevice->getResource() == nullptr )
            return;

        // 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 를 모은다. 셰이더 타입마다 다를 수 있고, 같은 셰이더라도
        // 백엔드마다 다르다(DX 자연 패킹 / SPIR-V std430) — 레이아웃은 이 디바이스의 백엔드로 빌드된 것이다.
        vector<uint32>                           listStride;
        vector<RenderPsoCache::RegisteredLayout> listLayout;
        _psoCache.collectLayouts( listLayout );
        {
            for ( const RenderPsoCache::RegisteredLayout& registered : listLayout )
            {
                const RHIPipelineStateHandle     pso     = registered._pso;
                const ShaderBindingLayout* const pLayout = registered._pLayout;
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
            RHIPipelineStateHandle existing{ 0 };
            if ( format == RHIFormat::Unknown || _psoCache.findPresentPso( format, existing ) )
                continue;
            const RHIFormat              arrRtvFormat[] = { format };
            const RHIPipelineStateHandle pso            = createPsoForPassType( RenderPassType::Present, engine::getEngineData()._shaderFullscreenBlit.c_str(),
                                                                                false, 1, arrRtvFormat );
            // 실패해도 기록한다 — 0 이면 호출부가 blit 폴백으로 간다.
            _psoCache.setPresentPso( format, pso );
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
        RHIPipelineStateHandle pso{ 0 };
        if ( _psoCache.findPresentPso( targetFormat, pso ) )
            return pso;

        if ( _bPresentPsoMissingLogged.exchange( 1 ) == 0 )
        {
            SW_LOG_ERROR( "Present 대상 포맷 %# 의 PSO 가 셋업에 없습니다 — buildPresentPsoVariants 에 그 포맷을 추가해야 합니다.",
                          static_cast<uint32>( targetFormat ) );
        }
        return getEnginePso( RenderPassType::Present );
    }
} // namespace sw
