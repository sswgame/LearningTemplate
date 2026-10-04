/**
 * @file FrameRendererResources.cpp
 * @brief 패스 자원의 수명입니다: 엔진 PSO 등록, 상수버퍼 링, 머티리얼 폴백 버퍼, Present 변종.
 * @details 여기 있는 것은 모두 **기록 시작 전에** 만들어져야 하는 것들입니다. 기록 중에는 버퍼 생성도 bindless 등록도
 *          PSO 생성도 할 수 없고(패스가 병렬로 기록됩니다), 그래서 "프레임이 쓸 것을 미리 다 만들어 둔다" 가 한 파일의
 *          주제가 됩니다. 첨부(렌더 타깃)는 크기를 따라 다시 만들어지는 다른 수명이라 `FrameRendererTransients.cpp` 에 있습니다.
 */
#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    void FrameRenderer::ensurePassResources()
    {
        if ( _pDevice == nullptr || _bPassResourcesReady != SW_FALSE )
            return;

        // 패스마다 자기 상수 버퍼를 갖도록 슬롯을 미리 만들어 둔다.
        _passCbRing.initialize( _pDevice );
        // 직렬 경로 시드는 0번 슬롯을 쓴다(패스별 경로는 acquirePassCb 로 덮어쓴다).
        _passCbRing.getSeedSlot( _frameCtx._passCb, _frameCtx._passCbIndex );

        // 만들지 못한 상수버퍼는 `isValid()` 가 걸러 그 디스패치만 꺼진다(그리기는 산다). 꺼진 이유는 여기서 한 번 알린다.
        ComputeConstantBufferRow arrComputeCb[_s_kComputeConstantBufferCount]{};
        collectComputeConstantBuffers( arrComputeCb );
        for ( const ComputeConstantBufferRow& row : arrComputeCb )
        {
            if ( row._pSlot->create( _pDevice, row._byteSize ) == false )
                SW_LOG_ERROR( "Failed to create the %# constant buffer - its compute dispatch is skipped", row._pUsage );
        }

        // 엔진 PSO 는 패스 종류의 표(RenderPassTypeInfo)를 enum 순서로 훑어 만든다. 셰이더 경로는 파이프라인 XML 패스 설정이 먼저이고
        // 표의 EngineDefaultAssets 경로는 마지막 폴백일 뿐이다. 셰이더 쿠커가 같은 표를 훑는다.
        const EngineDefaultAssets& engineDefaultAssets = engine::getEngineDefaultAssets();
        const RHICapabilities      caps                = _pDevice->getCapabilities();
        for ( uint32 typeIndex = 0; typeIndex < kRenderPassTypeCount; ++typeIndex )
        {
            const RenderPassType      passType = static_cast<RenderPassType>( typeIndex );
            const RenderPassTypeInfo& info     = getRenderPassTypeInfo( passType );
            if ( info._pDefaultShader == nullptr )
                continue;

            RHIPipelineStateHandle pso{ 0 };
            if ( info.hasFlag( RenderPassTraitFlag::kCompute ) )
            {
                // 컬링 · 정렬은 간접 인자 버퍼 능력(_bGpuCulling)을, 애니메이션 · 모프는 구조버퍼 UAV(_bCompute)만 요구한다.
                // DX11 은 한 버퍼에 STRUCTURED 와 DRAWINDIRECT_ARGS 를 같이 못 걸어 _bGpuCulling 이 0 이지만 _bCompute 는 1 이다.
                const bool bCapable = info.hasFlag( RenderPassTraitFlag::kRequiresGpuCulling ) ? caps._bGpuCulling != SW_FALSE : caps._bCompute != SW_FALSE;
                if ( bCapable )
                    pso = _pDevice->getResourceFactory()->createComputePipelineState( ( engineDefaultAssets.*info._pDefaultShader ).c_str(), FrameRendererUtil::Entry::kCSMain );
            }
            else
            {
                pso = createPsoForPassType( passType );
            }
            if ( pso != 0 )
                _psoCache.setEnginePso( passType, pso );
        }

        // 씬 메시는 **인다이렉트 드로우 하나로만** 그린다(컬링 · 정렬 · 인스턴스 애니메이션이 모두 그 경로에 붙어 있다).
        // 지원하지 않는 백엔드가 생기면 조용히 안 그리는 대신 여기서 크게 알린다.
        if ( caps._bIndirectDraw == SW_FALSE )
            SW_LOG_ERROR( "이 백엔드는 인다이렉트 드로우를 지원하지 않습니다 — 씬 메시를 그릴 수 없습니다." );

        // Present 변종도 PSO 등록 단계에서 만든다. 기록 중에는 PSO 를 만들 수 없다(ensurePresentPso 주석 참고).
        buildPresentPsoVariants();

        // 폴백 원소는 PSO 를 모두 등록한 뒤에 만든다. 필요한 stride 를 레이아웃에서 읽어야 하고, 기록 중에는 만들 수 없다.
        ensureMaterialFallbackBuffers();

        _bPassResourcesReady = SW_TRUE;
        SW_LOG_INFO( "Pass PSOs/CB ready (shadow=%# forward=%# transparent=%# deferred=%# bloom=%# outline=%# gpuDriven=%#)",
                     getEnginePso( RenderPassType::Shadow ), getEnginePso( RenderPassType::ForwardOpaque ),
                     getEnginePso( RenderPassType::Transparent ), getEnginePso( RenderPassType::Lighting ),
                     getEnginePso( RenderPassType::Bloom ), getEnginePso( RenderPassType::Outline ), static_cast<uint32>( caps._bIndirectDraw ) );
    }

    void FrameRenderer::collectComputeConstantBuffers( ComputeConstantBufferRow ( &outArrRow )[_s_kComputeConstantBufferCount] )
    {
        // 뷰마다 하나씩. 절두체가 다르므로 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치의 내용을 덮어쓴다.
        uint32 rowIndex{ 0 };
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
            outArrRow[rowIndex++] = { &_arrView[viewIndex]._cullCb, sizeof( FrameRendererUtil::GpuCullParams ), "cull" };
        outArrRow[rowIndex++] = { &_instanceAnimCb, sizeof( FrameRendererUtil::GpuAnimParams ), "instance animation" };
        outArrRow[rowIndex++] = { &_meshMorphCb, sizeof( FrameRendererUtil::GpuMorphParams ), "mesh morph" };
        outArrRow[rowIndex++] = { &_instanceSortCb, sizeof( FrameRendererUtil::GpuSortParams ), "instance sort" };
        SW_LOG_ASSERT( rowIndex == _s_kComputeConstantBufferCount, "compute constant buffer table has %# rows, expected %#", rowIndex,
                       _s_kComputeConstantBufferCount );
    }

    void FrameRenderer::releasePassResources()
    {
        // PSO · 레이아웃 · 패스 CB 가 여기서 사라진다. 그것을 가리키던 드로우 캐시도 같이 잊는다.
        // (새 디바이스의 핸들이 옛 값과 겹치면 캐시가 "그대로" 라고 속는다. 백엔드 교체 뒤 빈 화면의 원인이다.)
        _frameCtx.resetBindingCache();

        // 목록은 하나다. 디바이스가 없으면(초기화 실패) 각 release 가 핸들만 잊는다.
        // 병렬 기록용 커맨드 리스트는 이 디바이스의 것이다. 디바이스가 살아 있을 때 놓는다.
        if ( _pDevice != nullptr )
            _graph.releaseCommandLists();

        // PSO 는 캐시가 순서대로 놓는다: 변형(소유한 것만) → 패스 → Present → 레이아웃 표.
        _psoCache.releaseAll( _pDevice );

        _gpuScene.releaseGpu( _pDevice );

        _passCbRing.release( _pDevice );
        _frameCtx._passCb      = 0;
        _frameCtx._passCbIndex = kInvalidDescriptorIndex;
        ComputeConstantBufferRow arrComputeCb[_s_kComputeConstantBufferCount]{};
        collectComputeConstantBuffers( arrComputeCb );
        for ( const ComputeConstantBufferRow& row : arrComputeCb )
            row._pSlot->release( _pDevice );
        _meshMorphPool.release( _pDevice );
        _lightBuffer.release( _pDevice );
        for ( auto& [fallbackStride, fallbackSlot] : _mapMaterialFallback )
            fallbackSlot.release( _pDevice );
        _mapMaterialFallback.clear();
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
        // 값은 드로우 직전 ShaderParameterBinder::bindGraphics 가 리플렉션 오프셋으로 채운다
        // (ctx._passValues 에 이미 프레임 시드가 들어 있으므로 따로 먼저 올릴 필요가 없다).
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

    RHIPipelineStateHandle FrameRenderer::findPassPso( RenderPassType passType ) const
    {
        const RHIPipelineStateHandle pso = _psoCache.findEnginePso( passType );
        if ( pso != 0 )
            return pso;
        const RenderPassType fallbackType = getRenderPassTypeInfo( passType )._psoFallbackType;
        return fallbackType != RenderPassType::Invalid ? _psoCache.findEnginePso( fallbackType ) : 0;
    }

    void FrameRenderer::ensureMaterialFallbackBuffers()
    {
        if ( _pDevice == nullptr || _pDevice->getResourceFactory() == nullptr )
            return;

        // 등록된 PSO 레이아웃이 선언한 머티리얼 원소 stride 를 모은다. 셰이더 타입마다 다를 수 있고, 레이아웃은
        // 이 디바이스의 백엔드로 빌드된 것이다(SPIR-V 도 지금은 `-fvk-use-dx-layout` 으로 DX 규칙을 따른다).
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
                    continue; // 이 셰이더는 머티리얼 버퍼를 선언하지 않는다(풀스크린 등). 걸 것이 없다.
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

        // Present 가 그리는 대상은 백버퍼(디바이스가 실제 채택한 포맷), 오프스크린 렌더 타깃(에디터 GameView 등,
        // 계약값 kOffscreenColorFormat), 스크린샷 캡처 텍스처(계약값 kBackBufferFormat)다. 모두 **셋업에서** 만들어 둔다.
        const RHIFormat arrTargetFormat[] = { _pDevice->getBackBufferFormat(), constant::kOffscreenColorFormat, constant::kBackBufferFormat };
        for ( const RHIFormat format : arrTargetFormat )
        {
            RHIPipelineStateHandle existing{ 0 };
            if ( format == RHIFormat::Unknown || _psoCache.findPresentPso( format, existing ) )
                continue;
            const RHIFormat              arrRtvFormat[] = { format };
            const RHIPipelineStateHandle pso            = createPsoForPassType( RenderPassType::Present, arrRtvFormat );
            // 실패해도 기록한다. 0 이면 부르는 쪽이 blit 폴백으로 간다.
            _psoCache.setPresentPso( format, pso );
        }
    }

    RHIPipelineStateHandle FrameRenderer::ensurePresentPso( RHIFormat targetFormat )
    {
        // **조회만 한다.** 없다고 여기서 만들면 안 된다 — 이 함수는 Present 패스 실행 중 = 태스크 워커에서
        // 불린다. PSO 생성은 RHIHandleTable(락 없음)과 Vulkan 렌더 패스 캐시(락 없음)를 건드리므로, 같은
        // 레벨의 다른 패스가 드로우하며 그 표를 읽는 중이면 레이스다. assertRegistryMutableNow 는 bindless
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
