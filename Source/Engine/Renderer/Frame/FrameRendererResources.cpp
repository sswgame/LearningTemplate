/**
 * @file FrameRendererResources.cpp
 * @brief 패스 자원의 수명입니다: 엔진 PSO 등록, 상수버퍼 링, 머티리얼 폴백 버퍼, Present 변종.
 * @details 여기 있는 것은 모두 **기록 시작 전에** 만들어져야 하는 것들입니다. 기록 중에는 버퍼 생성도 bindless 등록도
 *          PSO 생성도 할 수 없고(패스가 병렬로 기록됩니다), 그래서 "프레임이 쓸 것을 미리 다 만들어 둔다" 가 한 파일의
 *          주제가 됩니다. 첨부(렌더 타깃)는 크기를 따라 다시 만들어지는 다른 수명이라 `FrameRendererTransients.cpp` 에 있습니다.
 */
#include "pch.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::ensurePassResources()
    {
        if ( _pDevice == nullptr || _bPassResourcesReady != SW_FALSE )
            return;

        // 패스마다 자기 상수 버퍼를 갖도록 슬롯을 미리 만들어 둔다.
        if ( _passCbRing.initialize( _pDevice ) == false )
        {
            // 준비 깃발을 세우지 않아 다음 프레임에 다시 시도한다 — 같은 실패를 프레임마다 남기지 않게 한 번만 알린다.
            SW_LOG_ERROR_ONCE( "Pass constant buffer slots could not be created - passes stay unprepared" );
            return;
        }
        // 직렬 경로 시드는 0번 슬롯을 쓴다(패스별 경로는 acquirePassCb 로 덮어쓴다).
        _passCbRing.getSeedSlot( _frameContext._passCb, _frameContext._passCbIndex );

        // 만들지 못한 상수버퍼는 `isValid()` 가 걸러 그 디스패치만 꺼진다(그리기는 산다). 꺼진 이유는 여기서 한 번 알린다.
        ComputeConstantBufferRow arrComputeCb[_s_kComputeConstantBufferCount]{};
        collectComputeConstantBuffers( arrComputeCb );
        for ( const ComputeConstantBufferRow& row : arrComputeCb )
        {
            if ( row._pSlot->create( _pDevice, row._byteSize ) == false )
                SW_LOG_ERROR( "Failed to create the %# constant buffer - its compute dispatch is skipped", row._pUsage );
        }

        // 셰이더 assert 링은 PSO 보다 먼저 만든다 — assert 변형 PSO 는 이 버퍼 없이는 돌릴 수 없으므로 만들지 못하면 멈춘다(폴백 없음).
        if ( _gpuAssert.initialize( _pDevice ) == false )
            SW_FATAL( "gv_gpuAssert is on but the GPU assert ring could not be created - see the log above" );
        if ( _nanCheck.initialize( _pDevice ) == false )
            SW_FATAL( "gv_nanCheck is on but the NaN check counters could not be created - see the log above" );

        // 엔진 PSO 는 패스 종류의 표(RenderPassTypeInfo)를 enum 순서로 훑어 만든다. 셰이더 경로는 파이프라인 XML 패스 설정이 먼저이고
        // 표의 EngineDefaultAssets 경로는 마지막 폴백일 뿐이다. 셰이더 쿠커가 같은 표를 훑는다.
        // 캐시에 이미 있는 패스는 만들지 않는다 — 미리보기 씬 렌더러는 주 렌더러의 캐시를 함께 쓰고(`_pPSOCache`), 주 렌더러가 같은 파이프라인으로 먼저 만들었다.
        const EngineDefaultAssets& engineDefaultAssets = engine::getSubsystem<EngineDefaultAssets>();
        const RHICapabilities      caps                = _pDevice->getCapabilities();
        for ( uint32 typeIndex = 0; typeIndex < kRenderPassTypeCount; ++typeIndex )
        {
            const RenderPassType      passType = static_cast<RenderPassType>( typeIndex );
            const RenderPassTypeInfo& info     = getRenderPassTypeInfo( passType );
            const bool                bMissing = info._pDefaultShader != nullptr && _pPSOCache->findEnginePSO( passType ) == 0;
            if ( bMissing == false )
                continue;
            // 셰이더가 파이프라인의 퍼뮤테이션을 요구하는 패스는 그 파이프라인에 있을 때만 만든다(없으면 실행도 없다).
            if ( info.hasFlag( RenderPassTraitFlag::kRequiresPassDesc ) && findPassDescByType( passType ) == nullptr )
                continue;
            // 진단 패스는 요청한 실행에서만 만든다. 네이티브 bindless 가 필요한 패스는 에뮬 백엔드에서 쓰지 않는다(능력 질의로 가린 것이지 실패가 아니다).
            if ( info.hasFlag( RenderPassTraitFlag::kDiagnostic ) && isDiagnosticPassRequested( passType ) == false )
                continue;
            if ( info.hasFlag( RenderPassTraitFlag::kRequiresNativeBindless ) && _pDevice->supportsNativeBindlessSampling() == false )
                continue;
            // 출력 패스는 엔진 PSO 가 없다 — 대상 포맷마다 아래 createOutputPSOVariants 가 만든다.
            if ( info.hasFlag( RenderPassTraitFlag::kOutputPass ) )
                continue;
            // 컬러 포맷을 정할 근거(표의 고정 포맷 · 파이프라인의 출력 선언)가 없는 그래픽스 패스는 이 파이프라인에서 실행되지 않는다 — RGBA8 로 짐작해 만들지 않는다.
            if ( info.hasFlag( RenderPassTraitFlag::kCompute ) == false && hasRenderTargetFormatSource( passType ) == false )
                continue;

            RHIPipelineStateHandle pso{ 0 };
            if ( info.hasFlag( RenderPassTraitFlag::kCompute ) )
            {
                // 컬링 · 정렬은 간접 인자 버퍼 능력(_bGPUCulling)을, 애니메이션 · 모프는 구조버퍼 UAV(_bCompute)만 요구한다.
                // DX11 은 한 버퍼에 STRUCTURED 와 DRAWINDIRECT_ARGS 를 같이 못 걸어 _bGPUCulling 이 0 이지만 _bCompute 는 1 이다.
                const bool bCapable = info.hasFlag( RenderPassTraitFlag::kRequiresGPUCulling ) ? caps._bGPUCulling != SW_FALSE : caps._bCompute != SW_FALSE;
                if ( bCapable == false )
                    continue; // 능력이 없는 백엔드는 이 디스패치를 쓰지 않는다(능력 질의로 가린 것이지 실패가 아니다)
                // 셰이더 경로와 define 은 쿠커와 같은 함수가 정한다(셰이더 assert 변형이 define 으로 온다).
                const RenderPassShaderSelection computeShader = selectRenderPassShader( passType, nullptr, engineDefaultAssets );
                pso                                           = _pDevice->getResourceFactory()->createComputePipelineState( computeShader._shaderPath, {}, computeShader._listDefine );
            }
            else
            {
                pso = createPSOForPassType( passType );
            }
            // 다른 패스의 PSO 로 대신하지 않는다 — 이 패스는 그리지 않고(drawFullscreen 은 PSO 없이 그리지 않는다) 여기서 이유를 한 번 남긴다.
            if ( pso == 0 )
            {
                SW_LOG_ERROR( "Engine PSO for pass %# could not be created (shader '%#') - the pass draws nothing", passType,
                              ( engineDefaultAssets.*info._pDefaultShader ).c_str() );
                continue;
            }
            _pPSOCache->setEnginePSO( passType, pso );
        }

        // 씬 메시는 **인다이렉트 드로우 하나로만** 그린다(컬링 · 정렬 · 인스턴스 애니메이션이 모두 그 경로에 붙어 있다).
        // 지원하지 않는 백엔드가 생기면 조용히 안 그리는 대신 여기서 크게 알린다.
        if ( caps._bIndirectDraw == SW_FALSE )
            SW_LOG_ERROR( "This backend does not support indirect draws - scene meshes cannot be drawn." );

        // 출력 패스(Present · Canvas)의 포맷별 변종도 PSO 등록 단계에서 만든다. 기록 중에는 PSO 를 만들 수 없다(findOutputPSO 주석 참고).
        createOutputPSOVariants();

        // 폴백 원소는 PSO 를 모두 등록한 뒤에 만든다. 필요한 stride 를 레이아웃에서 읽어야 하고, 기록 중에는 만들 수 없다.
        ensureMaterialFallbackBuffers();
        // 정렬 디스패치의 t2 자리표 — 순번 표가 없는 뷰도 빈 슬롯으로 디스패치하지 않는다(DX12 는 빈 디스크립터를 읽으면 폴트).
        constexpr uint32 kZeroRank = 0;
        if ( _transparentRankPlaceholder.ensureCapacity( _pDevice, sizeof( uint32 ), 1, RHIBufferUsage::ShaderResource, true, false, &kZeroRank ) == false )
            SW_LOG_ERROR( "Failed to create the transparent rank placeholder - extra views draw transparency in the main view order" );
        // 컬링 디스패치의 t2 자리표 — 가림을 보지 않는 슬롯도 빈 슬롯으로 디스패치하지 않는다(위와 같은 이유). 깊이 1 은 아무것도 가리지 않는다.
        constexpr float32 kFarDepth = 1.0f;
        if ( _occlusionTilesPlaceholder.ensureCapacity( _pDevice, sizeof( float32 ), 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, &kFarDepth ) == false )
            SW_LOG_ERROR( "Failed to create the occlusion tiles placeholder - GPU culling binds no buffer at t2" );

        _bPassResourcesReady = SW_TRUE;
        SW_LOG_INFO( "Pass PSOS/CB ready (shadow=%# forward=%# transparent=%# deferred=%# bloom=%# outline=%# gpuDriven=%#)",
                     getEnginePSO( RenderPassType::Shadow ), getEnginePSO( RenderPassType::ForwardOpaque ),
                     getEnginePSO( RenderPassType::Transparent ), getEnginePSO( RenderPassType::Lighting ),
                     getEnginePSO( RenderPassType::Bloom ), getEnginePSO( RenderPassType::Outline ), static_cast<uint32>( caps._bIndirectDraw ) );
    }

    void FrameRenderer::collectComputeConstantBuffers( ComputeConstantBufferRow ( &outArrRow )[_s_kComputeConstantBufferCount] )
    {
        // 뷰마다 하나씩. 절두체가 다르므로 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치의 내용을 덮어쓴다.
        uint32 rowIndex{ 0 };
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
        {
            outArrRow[rowIndex++] = { &_arrView[viewIndex]._cullCb, sizeof( FrameRendererUtil::GPUCullParams ), "cull" };
            outArrRow[rowIndex++] = { &_arrView[viewIndex]._sortCb, sizeof( FrameRendererUtil::GPUSortParams ), "instance sort" };
        }
        outArrRow[rowIndex++] = { &_deform._instanceAnimationCb, sizeof( FrameRendererUtil::GPUAnimationParams ), "instance animation" };
        outArrRow[rowIndex++] = { &_deform._meshMorphCb, sizeof( FrameRendererUtil::GPUMorphParams ), "mesh morph" };
        outArrRow[rowIndex++] = { &_deform._meshSkinCb, sizeof( FrameRendererUtil::GPUSkinParams ), "mesh skin" };
        SW_LOG_ASSERT( rowIndex == _s_kComputeConstantBufferCount, "compute constant buffer table has %# rows, expected %#", rowIndex,
                       _s_kComputeConstantBufferCount );
    }

    void FrameRenderer::releasePassResources()
    {
        // PSO · 레이아웃 · 패스 CB 가 여기서 사라진다. 그것을 가리키던 드로우 캐시도 같이 잊는다.
        // (새 디바이스의 핸들이 옛 값과 겹치면 캐시가 "그대로" 라고 속는다. 백엔드 교체 뒤 빈 화면의 원인이다.)
        _frameContext.resetBindingCache();
        // 모듈 요소의 메시(GPU 정점 버퍼)도 이 디바이스의 것이다 — 다음 프레임의 셋업이 다시 읽고 올린다.
        _mapExtensionMesh.clear();

        // 목록은 하나다. 디바이스가 없으면(초기화 실패) 각 release 가 핸들만 잊는다.
        // 병렬 기록용 커맨드 리스트는 이 디바이스의 것이다. 디바이스가 살아 있을 때 놓는다.
        if ( _pDevice != nullptr )
            _graph.releaseCommandLists();

        // PSO 는 캐시가 순서대로 놓는다: 변형(소유한 것만) → 패스 → Present → 레이아웃 표.
        // 빌려 쓰는 캐시(미리보기 씬 렌더러의 `_pPSOCache`)는 소유자인 주 렌더러가 놓는다 — 다른 미리보기 창과 주 시점이 아직 그 PSO 로 그린다.
        if ( _pPSOCache == &_psoCache )
            _pPSOCache->releaseAll( _pDevice );

        _gpuScene.releaseGPU( _pDevice );
        // 캔버스의 아틀라스 텍스처 · 사각형 버퍼도 이 디바이스의 것이다(거울은 남아 다음 프레임에 다시 올린다).
        _canvasRenderer.release( _pDevice );
        // 입자 버퍼와 빌린 입자 텍스처도 같다(다음 프레임의 prepareFrame 이 다시 만들고 빌린다).
        _particleRenderer.release( _pDevice );

        _passCbRing.release( _pDevice );
        _frameContext._passCb      = 0;
        _frameContext._passCbIndex = kInvalidDescriptorIndex;
        ComputeConstantBufferRow arrComputeCb[_s_kComputeConstantBufferCount]{};
        collectComputeConstantBuffers( arrComputeCb );
        for ( const ComputeConstantBufferRow& row : arrComputeCb )
        {
            row._pSlot->release( _pDevice );
        }
        // 추가 뷰의 컬링 · 정렬 상수버퍼도 이 디바이스의 것이다(다음 프레임의 prepareExtraViews 가 다시 만든다).
        for ( unique_ptr<ViewTarget>& pView : _listExtraView )
        {
            pView->_cullInput._cullCb.release( _pDevice );
            pView->_cullInput._sortCb.release( _pDevice );
            releaseViewTransparentOrder( *pView );
        }
        // 주 시점 캐스케이드 컬링 상수버퍼도 마찬가지다(prepareCascadeCullInputs 가 다시 만든다).
        for ( RenderView& cascadeInput : _arrCascadeCullInput )
        {
            cascadeInput._cullCb.release( _pDevice );
        }
        _cascadeCullCount = 0;
        _transparentRankPlaceholder.release( _pDevice );
        _occlusionTilesPlaceholder.release( _pDevice );
        _occlusionReadback.shutdown();
        _occlusionMailbox.clear();
        _gpuAssert.shutdown();
        _nanCheckCommandList.reset();
        _nanCheck.shutdown();
        _deform._meshMorphPool.release( _pDevice );
        _deform._vertexAnimationPool.release( _pDevice );
        _lightBuffer.release( _pDevice );
        _fogVolumeBuffer.release( _pDevice );
        _fogVolumeParamBuffer.release( _pDevice );
        _probeUpdater.release( _pDevice );
        _cookieAtlas.release( _pDevice );
        for ( MaterialFallback& fallback : _listMaterialFallback )
        {
            fallback._buffer.release( _pDevice );
        }
        _listMaterialFallback.clear();
        _mapLayoutMaterialFallback.clear();
        _bPassResourcesReady = SW_FALSE;
    }

    void FrameRenderer::resetPassCbRing()
    {
        _passCbRing.beginFrame();
        _passCbRing.getSeedSlot( _frameContext._passCb, _frameContext._passCbIndex );
    }

    bool FrameRenderer::acquirePassCb( FramePassContext& context )
    {
        // 슬롯 하나를 잡는다. 패스 진입 때 한 번 부르고, 한 패스 안에서 상수가 바뀌는 반복(그림자 타일)은 반복마다 다시 부른다.
        // 못 잡으면(링 고갈) false — 부르는 쪽은 그 패스 · 타일을 그리지 않는다. 앞 슬롯으로 그리면 상수가 섞인다(링이 이유를 남겼다).
        return _passCbRing.acquire( context._passCb, context._passCbIndex );
        // 값은 드로우 직전 ShaderParameterBinder::bindGraphics 가 리플렉션 오프셋으로 채운다
        // (context._passValues 에 이미 프레임 시드가 들어 있으므로 따로 먼저 올릴 필요가 없다).
    }

    void FrameRenderer::ensurePassCbCapacityForFrame()
    {
        const uint32 batchCount = static_cast<uint32>( _gpuScene.getOpaqueBatches().size() + _gpuScene.getTransparentBatches().size() );
        // 그림자 패스는 타일(캐스케이드 · 점광 · 스폿 면)마다 씬을 한 번씩 그린다 — 어림값의 그림자 한 번을 타일 수로 바꾼다.
        const uint32 shadowTileCount = MathUtil::max( computeShadowTileCountPerView(), 1u );
        // 모듈 프록시의 요소는 요소마다 슬롯을 하나씩 쓴다(`drawExtensionElements`) — 그리는 패스 · 뷰 수를 모르므로 넉넉히 네 배로 어림한다(모자라면 링이 알리고 다음 프레임이 키운다).
        const uint32 moduleElementCount = static_cast<uint32>( _listExtensionDrawElement.size() ) * 4u;
        const uint32 estimate           = batchCount * ( _s_kDrawCbPassEstimate - 1u + shadowTileCount ) + PassConstantRing::kInitialSlotCount + moduleElementCount;
        _passCbRing.ensureCapacity( _pDevice, MathUtil::max( estimate, _passCbRing.getHighWater() + PassConstantRing::kInitialSlotCount ) );
    }

    RHIPipelineStateHandle FrameRenderer::getEnginePSO( RenderPassType passType ) const
    {
        return _pPSOCache->findEnginePSO( passType );
    }

    void FrameRenderer::ensureMaterialFallbackBuffers()
    {
        if ( _pDevice == nullptr || _pDevice->getResourceFactory() == nullptr )
            return;

        // 등록된 PSO 레이아웃마다 머티리얼 폴백 원소를 만든다. stride 와 텍스처 인덱스 필드 위치는 셰이더 타입마다 다를 수 있고, 레이아웃은
        // 이 디바이스의 백엔드로 빌드된 것이다(SPIR-V 도 지금은 `-fvk-use-dx-layout` 으로 DX 규칙을 따른다). 내용이 같은 원소는 버퍼 하나를 나눠 쓴다.
        vector<RenderPSOCache::RegisteredLayout> listLayout;
        _pPSOCache->collectLayouts( listLayout );
        for ( const RenderPSOCache::RegisteredLayout& registered : listLayout )
        {
            const RHIPipelineStateHandle     pso     = registered._pso;
            const ShaderBindingLayout* const pLayout = registered._pLayout;
            if ( pLayout == nullptr || _mapLayoutMaterialFallback.find( pLayout ) != _mapLayoutMaterialFallback.end() )
                continue;
            const ShaderBindingSlot* pSlot = pLayout->find( passConstantNames()._swMaterials );
            if ( pSlot == nullptr )
                continue; // 이 셰이더는 머티리얼 버퍼를 선언하지 않는다(풀스크린 등). 걸 것이 없다.
            if ( pSlot->_elementStride == 0 )
            {
                // 선언은 있는데 원소 레이아웃이 없다 = 리플렉션 공백. 그대로 두면 머티리얼 없는 배치가 슬롯을 비운 채
                // 그리게 되고(Vulkan 은 partially-bound 슬롯을 읽으면 정의되지 않는다) 원인을 찾기 어렵다.
                SW_LOG_ERROR( "The material buffer element stride of PSO %# is 0 - reflection did not provide the g_SwMaterials element layout.", pso );
                continue;
            }

            vector<uint8> bytes         = MaterialUtil::makeFallbackElement( pSlot->_listElementMember, pSlot->_elementStride );
            uint32        fallbackIndex = static_cast<uint32>( _listMaterialFallback.size() );
            for ( uint32 index = 0; index < static_cast<uint32>( _listMaterialFallback.size() ); ++index )
            {
                if ( _listMaterialFallback[index]._bytes == bytes )
                {
                    fallbackIndex = index;
                    break;
                }
            }
            if ( fallbackIndex == _listMaterialFallback.size() )
            {
                const uint32            stride = pSlot->_elementStride;
                RHIStructuredBufferSlot buffer{};
                if ( buffer.ensureCapacity( _pDevice, stride, 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, bytes.data() ) == false )
                {
                    SW_LOG_ERROR( "Failed to create the material fallback buffer (stride %#).", stride );
                    continue;
                }
                buffer.upload( _pDevice, bytes.data(), stride );
                MaterialFallback& fallback = _listMaterialFallback.emplace_back();
                fallback._bytes            = std::move( bytes );
                fallback._buffer           = buffer;
            }
            _mapLayoutMaterialFallback.insert_or_assign( pLayout, fallbackIndex );
        }
    }

    void FrameRenderer::createOutputPSOVariants()
    {
        if ( _pDevice == nullptr )
            return;

        // 주 출력에 그리는 패스(Present · Canvas · Present 를 대신하는 FSR 의 RCAS)의 대상은 백버퍼(디바이스가 실제 채택한 포맷), 오프스크린 렌더 타깃(에디터 씬 뷰 · 게임 뷰 등,
        // 계약값 kOffscreenColorFormat), 스크린샷 캡처 텍스처(계약값 kBackBufferFormat)다. 모두 **셋업에서** 만들어 둔다.
        // 출력 패스는 표의 kOutputPass 가 정한다.
        const RHIFormat arrTargetFormat[] = { _pDevice->getBackBufferFormat(), constant::kOffscreenColorFormat, constant::kBackBufferFormat };
        for ( uint32 typeIndex = 0; typeIndex < kRenderPassTypeCount; ++typeIndex )
        {
            const RenderPassType passType = static_cast<RenderPassType>( typeIndex );
            if ( getRenderPassTypeInfo( passType ).hasFlag( RenderPassTraitFlag::kOutputPass ) == false )
                continue;
            for ( const RHIFormat format : arrTargetFormat )
            {
                RHIPipelineStateHandle existing{ 0 };
                if ( format == RHIFormat::Unknown || _pPSOCache->findOutputPSO( passType, format, existing ) )
                    continue;
                const RHIFormat              arrRTVFormat[] = { format };
                const RHIPipelineStateHandle pso            = createPSOForPassType( passType, arrRTVFormat );
                // 주 출력에 그리는 PSO 다 — 없으면 화면에 아무것도 낼 수 없고, 다른 PSO · 복사로 대신하지 않으므로 여기서 멈춘다.
                if ( pso == 0 )
                    SW_FATAL( "Output pass %# could not create its PSO for target format %# - nothing can be presented", passType, static_cast<uint32>( format ) );
                _pPSOCache->setOutputPSO( passType, format, pso );
            }
        }
    }

    RHIPipelineStateHandle FrameRenderer::findOutputPSO( RenderPassType passType, RHIFormat targetFormat )
    {
        // **조회만 한다.** 없다고 여기서 만들면 안 된다 — 이 함수는 패스 실행 중 = 태스크 워커에서
        // 불린다. PSO 생성은 RHIHandleTable(락 없음)과 Vulkan 렌더 패스 캐시(락 없음)를 건드리므로, 같은
        // 레벨의 다른 패스가 드로우하며 그 표를 읽는 중이면 레이스다. requireRegistryMutableNow 는 bindless
        // 레지스트리만 감시해서 이 경우를 못 잡는다. 변종은 createOutputPSOVariants 가 셋업에서 만든다.
        // 셋업에 없는 포맷(장치가 알리지 않은 Unknown 포함)의 대상은 설정 결함이다 — 다른 포맷의 PSO · 복사로 대신 그리지 않고 멈춘다(createOutputPSOVariants 의 목록에 더한다).
        RHIPipelineStateHandle pso{ 0 };
        const bool             bFound = _pPSOCache->findOutputPSO( passType, targetFormat, pso ) && pso != 0;
        if ( bFound == false )
            SW_FATAL( "Output pass %# has no PSO for target format %# from setup - add the format to createOutputPSOVariants", passType, static_cast<uint32>( targetFormat ) );
        return pso;
    }
} // namespace sw
