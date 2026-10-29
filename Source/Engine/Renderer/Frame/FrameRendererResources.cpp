/**
 * @file FrameRendererResources.cpp
 * @brief 패스 자원의 수명입니다: 상수버퍼 링, 컴퓨트 상수버퍼, 자리표 버퍼. PSO 는 `FramePSOManager` 가 만든다.
 * @details 여기 있는 것은 모두 **기록 시작 전에** 만들어져야 하는 것들입니다. 기록 중에는 버퍼 생성도 bindless 등록도
 *          PSO 생성도 할 수 없고(패스가 병렬로 기록됩니다), 그래서 "프레임이 쓸 것을 미리 다 만들어 둔다" 가 한 파일의
 *          주제가 됩니다. 첨부(렌더 타깃)는 크기를 따라 다시 만들어지는 다른 수명이라 `FrameRendererTransients.cpp` 에 있습니다.
 */
#include "pch.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
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
        if ( _state._passCbRing.initialize( _pDevice ) == false )
        {
            // 준비 깃발을 세우지 않아 다음 프레임에 다시 시도한다 — 같은 실패를 프레임마다 남기지 않게 한 번만 알린다.
            SW_LOG_ERROR_ONCE( "Pass constant buffer slots could not be created - passes stay unprepared" );
            return;
        }
        // 직렬 경로 시드는 0번 슬롯을 쓴다(패스별 경로는 acquirePassCb 로 덮어쓴다).
        _state._passCbRing.getSeedSlot( _state._frameContext._passCb, _state._frameContext._passCbIndex );

        // 만들지 못한 상수버퍼는 `isValid()` 가 걸러 그 디스패치만 꺼진다(그리기는 산다). 꺼진 이유는 여기서 한 번 알린다.
        ComputeConstantBufferRow arrComputeCb[_s_kComputeConstantBufferCount]{};
        collectComputeConstantBuffers( arrComputeCb );
        for ( const ComputeConstantBufferRow& row : arrComputeCb )
        {
            if ( row._pSlot->create( _pDevice, row._byteSize ) == false )
                SW_LOG_ERROR( "Failed to create the %# constant buffer - its compute dispatch is skipped", row._pUsage );
        }

        // 패스 실행의 상주 자원(assert 링 · NaN 감시 · 컬링 자리표)을 PSO 보다 먼저 만든다 — assert 변형 PSO 는 assert 링 없이는 돌릴 수 없다.
        _passExecutor.ensureResources();

        // 엔진 패스 PSO · 출력 포맷 변형 · 머티리얼 폴백 버퍼(셋업에서만 만든다 — 패스는 병렬로 기록된다).
        _psoManager.createPassPSOS();

        // 씬 메시는 **인다이렉트 드로우 하나로만** 그린다(컬링 · 정렬 · 인스턴스 애니메이션이 모두 그 경로에 붙어 있다).
        // 지원하지 않는 백엔드가 생기면 조용히 안 그리는 대신 여기서 크게 알린다.
        const RHICapabilities caps = _pDevice->getCapabilities();
        if ( caps._bIndirectDraw == SW_FALSE )
            SW_LOG_ERROR( "This backend does not support indirect draws - scene meshes cannot be drawn." );

        _bPassResourcesReady = SW_TRUE;
        SW_LOG_INFO( "Pass PSOS/CB ready (shadow=%# forward=%# transparent=%# deferred=%# bloom=%# outline=%# gpuDriven=%#)",
                     _psoManager.getEnginePSO( RenderPassType::Shadow ), _psoManager.getEnginePSO( RenderPassType::ForwardOpaque ),
                     _psoManager.getEnginePSO( RenderPassType::Transparent ), _psoManager.getEnginePSO( RenderPassType::Lighting ),
                     _psoManager.getEnginePSO( RenderPassType::Bloom ), _psoManager.getEnginePSO( RenderPassType::Outline ), static_cast<uint32>( caps._bIndirectDraw ) );
    }

    void FrameRenderer::collectComputeConstantBuffers( ComputeConstantBufferRow ( &outArrRow )[_s_kComputeConstantBufferCount] )
    {
        // 뷰마다 하나씩. 절두체가 다르므로 하나를 나눠 쓰면 뒤 업로드가 앞 디스패치의 내용을 덮어쓴다.
        uint32 rowIndex{ 0 };
        for ( uint32 viewIndex = 0; viewIndex < static_cast<uint32>( RenderViewType::Count ); ++viewIndex )
        {
            outArrRow[rowIndex++] = { &_state._arrView[viewIndex]._cullCb, sizeof( FrameRendererUtil::GPUCullParams ), "cull" };
            outArrRow[rowIndex++] = { &_state._arrView[viewIndex]._sortCb, sizeof( FrameRendererUtil::GPUSortParams ), "instance sort" };
        }
        outArrRow[rowIndex++] = { &_state._deform._instanceAnimationCb, sizeof( FrameRendererUtil::GPUAnimationParams ), "instance animation" };
        outArrRow[rowIndex++] = { &_state._deform._meshMorphCb, sizeof( FrameRendererUtil::GPUMorphParams ), "mesh morph" };
        outArrRow[rowIndex++] = { &_state._deform._meshSkinCb, sizeof( FrameRendererUtil::GPUSkinParams ), "mesh skin" };
        SW_LOG_ASSERT( rowIndex == _s_kComputeConstantBufferCount, "compute constant buffer table has %# rows, expected %#", rowIndex,
                       _s_kComputeConstantBufferCount );
    }

    void FrameRenderer::releasePassResources()
    {
        // PSO · 레이아웃 · 패스 CB 가 여기서 사라진다. 그것을 가리키던 드로우 캐시도 같이 잊는다.
        // (새 디바이스의 핸들이 옛 값과 겹치면 캐시가 "그대로" 라고 속는다. 백엔드 교체 뒤 빈 화면의 원인이다.)
        _state._frameContext.resetBindingCache();
        // 모듈 요소의 메시(GPU 정점 버퍼)도 이 디바이스의 것이다 — 다음 프레임의 셋업이 다시 읽고 올린다.
        _extensionHost.releaseMeshes();

        // 목록은 하나다. 디바이스가 없으면(초기화 실패) 각 release 가 핸들만 잊는다.
        // 병렬 기록용 커맨드 리스트는 이 디바이스의 것이다. 디바이스가 살아 있을 때 놓는다.
        if ( _pDevice != nullptr )
            _state._graph.releaseCommandLists();

        // PSO · 레이아웃 · 머티리얼 폴백 버퍼. 빌려 쓰는 저장소(미리보기 씬 렌더러)는 소유자인 주 렌더러가 놓는다(`FramePSOManager::release`).
        _psoManager.release();

        _state._gpuScene.releaseGPU( _pDevice );
        // 캔버스의 아틀라스 텍스처 · 사각형 버퍼도 이 디바이스의 것이다(거울은 남아 다음 프레임에 다시 올린다).
        _state._canvasRenderer.release( _pDevice );
        // 입자 버퍼와 빌린 입자 텍스처도 같다(다음 프레임의 prepareFrame 이 다시 만들고 빌린다).
        _state._particleRenderer.release( _pDevice );

        _state._passCbRing.release( _pDevice );
        _state._frameContext._passCb      = 0;
        _state._frameContext._passCbIndex = kInvalidDescriptorIndex;
        ComputeConstantBufferRow arrComputeCb[_s_kComputeConstantBufferCount]{};
        collectComputeConstantBuffers( arrComputeCb );
        for ( const ComputeConstantBufferRow& row : arrComputeCb )
        {
            row._pSlot->release( _pDevice );
        }
        // 추가 뷰의 컬링 · 정렬 상수버퍼도 이 디바이스의 것이다(다음 프레임의 prepareExtraViews 가 다시 만든다).
        for ( unique_ptr<FrameViewTarget>& pView : _state._listExtraView )
        {
            pView->_cullInput._cullCb.release( _pDevice );
            pView->_cullInput._sortCb.release( _pDevice );
            releaseViewTransparentOrder( *pView );
        }
        // 주 시점 캐스케이드 컬링 상수버퍼도 마찬가지다(prepareCascadeCullInputs 가 다시 만든다).
        for ( RenderView& cascadeInput : _state._arrCascadeCullInput )
        {
            cascadeInput._cullCb.release( _pDevice );
        }
        _state._cascadeCullCount = 0;
        _passExecutor.releaseResources();
        _state._deform._meshMorphPool.release( _pDevice );
        _state._deform._vertexAnimationPool.release( _pDevice );
        _state._lightBuffer.release( _pDevice );
        _postProcess.release();
        _state._probeUpdater.release( _pDevice );
        _state._cookieAtlas.release( _pDevice );
        _bPassResourcesReady = SW_FALSE;
    }

    void FrameRenderer::resetPassCbRing()
    {
        _state._passCbRing.beginFrame();
        _state._passCbRing.getSeedSlot( _state._frameContext._passCb, _state._frameContext._passCbIndex );
    }

    void FrameRenderer::ensurePassCbCapacityForFrame()
    {
        const uint32 batchCount = static_cast<uint32>( _state._gpuScene.getOpaqueBatches().size() + _state._gpuScene.getTransparentBatches().size() );
        // 그림자 패스는 타일(캐스케이드 · 점광 · 스폿 면)마다 씬을 한 번씩 그린다 — 어림값의 그림자 한 번을 타일 수로 바꾼다.
        const uint32 shadowTileCount = MathUtil::max( _passExecutor.computeShadowTileCountPerView(), 1u );
        // 모듈 프록시의 요소는 요소마다 슬롯을 하나씩 쓴다(`drawExtensionElements`) — 그리는 패스 · 뷰 수를 모르므로 넉넉히 네 배로 어림한다(모자라면 링이 알리고 다음 프레임이 키운다).
        const uint32 moduleElementCount = static_cast<uint32>( _extensionHost.getDrawElements().size() ) * 4u;
        const uint32 estimate           = batchCount * ( _s_kDrawCbPassEstimate - 1u + shadowTileCount ) + PassConstantRing::kInitialSlotCount + moduleElementCount;
        _state._passCbRing.ensureCapacity( _pDevice, MathUtil::max( estimate, _state._passCbRing.getHighWater() + PassConstantRing::kInitialSlotCount ) );
    }

    RHIPipelineStateHandle FrameRenderer::getEnginePSO( RenderPassType passType ) const
    {
        return _psoManager.getEnginePSO( passType );
    }

    bool FrameRenderer::findPSODesc( RHIPipelineStateHandle pso, RHIPipelineStateDesc& outDesc ) const
    {
        return _psoManager.findDesc( pso, outDesc );
    }

    void FrameRenderer::onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result )
    {
        if ( result._bSuccess == false || _pDevice == nullptr )
            return;

        _psoManager.onShaderRecompiled( shaderPath );

        // **PSO 를 실제로 다시 만든다.** 주의: 바인딩 레이아웃만 새로 만들면 PSO 는 바이트코드를 박아 넣은
        // 객체라 화면이 시작 때 컴파일된 셰이더 그대로다.
        //
        // 순서는 loadPipeline 과 같다. 여기 도달하기 전에 ShaderRecompiler 가 ShaderCache 를 비웠고(수동 리로드)
        // (그래야 새 바이트코드를 집는다) Engine 이 렌더 스레드를 재웠으므로(waitIdle) 안전하다.
        // 셰이더 편집은 개발 중 가끔 있는 일이라 이때의 스톨은 감수한다.
        releasePassResources();
        ensurePassResources();
        ensureTransientResources();
        _bCallbacksBound = _passExecutor.bindPassCallbacks() ? SW_TRUE : SW_FALSE;

        // 미리보기 씬 렌더러는 주 렌더러의 PSO 캐시를 함께 쓴다. 주 렌더러가 캐시를 다시 채운 **뒤에** 자기 몫(레이아웃을 가리키는 폴백 버퍼 표,
        // 바인딩 캐시, 패스 콜백)을 다시 만들어야 새 PSO 를 집는다.
        for ( ScenePreviewRenderer& preview : _listScenePreviewRenderer )
        {
            preview._renderer->onShaderRecompiled( shaderPath, result );
        }
    }
} // namespace sw
