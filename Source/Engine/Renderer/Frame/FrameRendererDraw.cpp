#include "pch.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Support/BindlessIndexValidator.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/RenderPSOCache.h"
#include "Engine/Renderer/Frame/Resource/ShaderParameterBinder.h"

namespace sw
{
    void FrameRenderer::registerPSOLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
    {
        if ( pso == 0 || _pDevice == nullptr )
            return;
        // gv_rhiBackend(전역) 대신 이 FrameRenderer 가 실제로 물려 있는 디바이스의 백엔드를 넘긴다.
        // 한 프로세스에 여러 IRHIDevice 가 동시에 있으면 전역값이 어긋날 수 있다.
        _pPSOCache->registerLayout( pso, desc, _pDevice->getBackendType() );
    }

    void FrameRenderer::onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result )
    {
        if ( result._bSuccess == false || _pDevice == nullptr )
            return;

        // 빌려 쓰는 캐시(미리보기 씬 렌더러)는 소유자인 주 렌더러가 이미 무효화하고 다시 채웠다.
        if ( _pPSOCache == &_psoCache )
            _pPSOCache->invalidateLayoutsByShaderPath( shaderPath );

        // **PSO 를 실제로 다시 만든다.** 주의: 바인딩 레이아웃만 새로 만들면 PSO 는 바이트코드를 박아 넣은
        // 객체라 화면이 시작 때 컴파일된 셰이더 그대로다.
        //
        // 순서는 loadPipeline 과 같다. 여기 도달하기 전에 ShaderRecompiler 가 ShaderCache 를 비웠고(수동 리로드)
        // (그래야 새 바이트코드를 집는다) Engine 이 렌더 스레드를 재웠으므로(waitIdle) 안전하다.
        // 셰이더 편집은 개발 중 가끔 있는 일이라 이때의 스톨은 감수한다.
        releasePassResources();
        ensurePassResources();
        ensureTransientResources();
        bindPassCallbacks();

        // 미리보기 씬 렌더러는 주 렌더러의 PSO 캐시를 함께 쓴다. 주 렌더러가 캐시를 다시 채운 **뒤에** 자기 몫(레이아웃을 가리키는 폴백 버퍼 표,
        // 바인딩 캐시, 패스 콜백)을 다시 만들어야 새 PSO 를 집는다.
        for ( ScenePreviewRenderer& preview : _listScenePreviewRenderer )
        {
            preview._renderer->onShaderRecompiled( shaderPath, result );
        }
    }

    const ShaderBindingLayout* FrameRenderer::layoutForPSO( RHIPipelineStateHandle pso ) const
    {
        return _pPSOCache->findLayout( pso );
    }

    void FrameRenderer::registerInstanceBuffer( FramePassContext& context )
    {
        if ( _gpuScene.getInstanceBuffer() == 0 || _gpuScene.getInstanceSrv() == kInvalidDescriptorIndex )
            return;
        // 이름 "SwInstances" ↔ binding.hlsli 의 g_SwInstances / PassCB g_SwInstancesIndex (canonical 매칭).
        context._resourceRegistry.registerBuffer( passConstantNames()._swInstances,
                                                  _gpuScene.getInstanceBuffer(), _gpuScene.getInstanceSrv() );
        context._passValues.setUint( passConstantNames()._swInstanceCount, static_cast<uint32>( _gpuScene.getInstances().size() ) );

        // 배치 표. **패스당 한 번** 건다. 배치마다 다른 값(인스턴스 시작 · 모프 풀 시작 · 정점 풀 시작)이 모두 여기 있어
        // 드로우는 배치 번호만 실어 나른다. 그래서 같은 PSO 의 배치들을 멀티 드로우 하나로 낼 수 있다.
        if ( _gpuScene.getBatchInfoBuffer() != 0 && _gpuScene.getBatchInfoSrv() != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swBatches, _gpuScene.getBatchInfoBuffer(), _gpuScene.getBatchInfoSrv() );
            context._passValues.setUint( passConstantNames()._swBatchCount, _gpuScene.getIndirectCommandCount() );
        }

        // 모프 결과 풀. **패스당 한 번** 건다. 배치는 시작 오프셋을 배치 표에 싣는다(드로우 사이에
        // 바인딩이 바뀌지 않는다는 이 엔진의 규약). 안 걸리면 셰이더가 g_SwMorphVerticesIndex 로 알아채고
        // 입력 스트림을 그대로 쓴다.
        const RHIStructuredBufferSlot& morphBuffer = ( _deform._bMorphBindsRest != SW_FALSE ) ? _deform._meshMorphPool.getRestBuffer()
                                                                                              : _deform._meshMorphPool.getMorphBuffer();
        if ( morphBuffer._buffer != 0 && morphBuffer._srv != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swMorphVertices, morphBuffer._buffer, morphBuffer._srv );
            // 진단(레스트를 그대로 물림)이면 레스트 버퍼는 모프 구간만 담는다 — 스킨 구간 번호는 범위 밖으로 걸러 입력 스트림을 쓴다.
            const uint32 morphElementCount = ( _deform._bMorphBindsRest != SW_FALSE ) ? _deform._meshMorphPool.getMorphVertexCount() : _deform._meshMorphPool.getVertexCount();
            context._passValues.setUint( passConstantNames()._swMorphVertexCount, morphElementCount );
        }

        // 정점 애니메이션(VAT) 표 — 패스당 한 번. 시계는 게임 스레드의 군중 시계(스냅샷)라 인스턴스 시각 오프셋과 더하면 CPU 의 클립 시각이다.
        const RHIStructuredBufferSlot& vertexAnimationBuffer = _deform._vertexAnimationPool.getBuffer();
        if ( vertexAnimationBuffer._buffer != 0 && vertexAnimationBuffer._srv != kInvalidDescriptorIndex )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swVertexAnimation, vertexAnimationBuffer._buffer, vertexAnimationBuffer._srv );
            context._passValues.setUint( passConstantNames()._swVertexAnimationCount, _deform._vertexAnimationPool.getElementCount() );
            context._passValues.setFloat( passConstantNames()._swVertexAnimationTime, _gpuScene.getVertexAnimationTime() );
        }
        // 머티리얼 애니메이션 시계(툰 UV 애니메이션) — VAT 와 같은 장면 애니메이션 시계지만 VAT 표와 무관하게 늘 건다.
        context._passValues.setFloat( passConstantNames()._swMaterialTime, _gpuScene.getVertexAnimationTime() );

        // 컬링이 실제로 목록을 만들었을 때만 건다. 안 걸리면 셰이더가 g_SwVisibleInstanceIDsIndex 로 알아채고
        // 배치 시작 + 서수를 쓴다(컬링 없음 경로). 반대로 목록만 걸고 컬링을 안 돌리면 **비어 있는
        // 목록**을 읽어 모두 0 번 인스턴스를 그린다. 그래서 둘은 반드시 같이 켜지고 같이 꺼진다.
        const GPUCullViewResources& cullView                  = _gpuScene.getCullView( context._cullViewIndex );
        const bool                  bVisibleInstanceListReady = _bGPUCullingActive != SW_FALSE && cullView._visibleInstances._buffer != 0 &&
                                               cullView._visibleInstances._srv != kInvalidDescriptorIndex;
        if ( bVisibleInstanceListReady )
        {
            context._resourceRegistry.registerBuffer( passConstantNames()._swVisibleInstanceIDs,
                                                      cullView._visibleInstances._buffer, cullView._visibleInstances._srv );
        }
    }

    void FrameRenderer::registerLightBuffer( FramePassContext& context )
    {
        // 2D 빛 쿠키 아틀라스 — 쿠키를 쓴 빛이 있는 프레임에만 역할 텍스처로 건다(셰이더는 g_Light2DCookieAtlasIndex 로 걸렸는지 본다).
        const Texture2D* pCookieAtlas = _cookieAtlas.hasCookies() ? _cookieAtlas.findAtlas() : nullptr;
        if ( pCookieAtlas != nullptr )
            context._resourceRegistry.registerTexture( attachmentNames()._light2DCookieAtlas, pCookieAtlas->getHandle(), pCookieAtlas->getSrv() );
        // 개수는 버퍼가 없어도 채운다. 셰이더는 0 이면 빛을 돌지 않는다(앰비언트만).
        context._passValues.setUint( passConstantNames()._swLightCount, _lightBuffer.isBindable() ? _lightBuffer.getCount() : 0u );
        // 지금 그리는 뷰의 라이트 그리드(t12). 없으면(gv_lightCulling 0, 투영이 깨진 뷰) 셰이더는 목록 전체를 돈다.
        // 그리드 첫 단어와 버퍼는 같이 켜지고 같이 꺼진다 — 첫 단어만 있고 버퍼가 없으면 셰이더가 빈 슬롯을 읽는다(LightGrid::hasGrid 가 둘 다 본다).
        const RHIStructuredBufferSlot* pGridBuffer = _lightBuffer.isBindable() ? _lightBuffer.findGridBuffer() : nullptr;
        const bool                     bGridReady  = pGridBuffer != nullptr && pGridBuffer->_buffer != 0 && pGridBuffer->_srv != kInvalidDescriptorIndex;
        const uint32                   gridBase    = bGridReady ? _lightBuffer.getGridBase( _pActiveView->_lightGridIndex ) : invalid_index::kUint32;
        context._passValues.setUint( passConstantNames()._swLightGridBase, gridBase );
        if ( _lightBuffer.isBindable() == false )
            return;
        const RHIStructuredBufferSlot& buffer = _lightBuffer.getBuffer();
        context._resourceRegistry.registerBuffer( passConstantNames()._swLights, buffer._buffer, buffer._srv );
        if ( bGridReady )
            context._resourceRegistry.registerBuffer( passConstantNames()._swLightGrid, pGridBuffer->_buffer, pGridBuffer->_srv );
        // 라이트 프로브 SH9(t13) — 볼륨 원소가 라이트 목록에 있는 프레임에만 있다. 셰이더는 g_SwLightProbesIndex 로 걸렸는지 본다.
        const RHIStructuredBufferSlot* pProbeBuffer = _lightBuffer.findProbeBuffer();
        if ( pProbeBuffer != nullptr )
            context._resourceRegistry.registerBuffer( passConstantNames()._swLightProbes, pProbeBuffer->_buffer, pProbeBuffer->_srv );
    }

    void FrameRenderer::registerMaterialBuffer( FramePassContext& context, const GPUMeshBatch& batch, RHIPipelineStateHandle pso )
    {
        // 배치의 셰이더 타입에 해당하는 머티리얼 데이터 버퍼. 이름 "SwMaterials" ↔ binding.hlsli 의 g_SwMaterials(t5).
        // 바인더가 리플렉션 슬롯에 걸고, 셰이더는 인스턴스의 materialIndex 로 원소를 읽는다(드로우별 CB 바인딩 없음).
        if ( batch._materialBuffer == 0 || batch._materialSrv == kInvalidDescriptorIndex )
        {
            // 머티리얼 없는 배치. 빈 슬롯으로 그리지 않는다(Vulkan 은 partially-bound 슬롯을 실제로 읽으면 정의되지 않는다).
            // 폴백은 **이 PSO 셰이더의 레이아웃**(stride · 텍스처 인덱스 필드)으로 만든 것을 고른다. 공용 256 바이트를 걸면 DX11 이 드로우마다
            // "structure stride 256 vs 24" 를 낸다(SRV 의 구조 stride 는 셰이더 선언과 같아야 한다).
            const ShaderBindingLayout* pLayout = ( context._lastLayoutPSO == pso ) ? context._pLastLayout : layoutForPSO( pso );
            const ShaderBindingSlot*   pSlot   = ( pLayout != nullptr ) ? pLayout->find( passConstantNames()._swMaterials ) : nullptr;
            if ( pSlot == nullptr || pSlot->_elementStride == 0 )
                return; // 셰이더가 머티리얼 버퍼를 선언하지 않았다. 걸 것도 없다.
            const auto                     it                   = _mapLayoutMaterialFallback.find( pLayout );
            const RHIStructuredBufferSlot* pFallback            = ( it != _mapLayoutMaterialFallback.end() ) ? &_listMaterialFallback[it->second]._buffer : nullptr;
            const bool                     bUseFallbackMaterial = pFallback != nullptr && pFallback->isValid() && pFallback->_srv != kInvalidDescriptorIndex;
            if ( bUseFallbackMaterial )
            {
                context._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, pFallback->_buffer, pFallback->_srv );
                context._drawMaterialCount = 1u;
            }
            else if ( _bMaterialFallbackMissingLogged.exchange( 1, std::memory_order_relaxed ) == 0 )
            {
                // 여기서 조용히 나가면 t5 가 빈 채로 드로우가 나가고 Vulkan 은 디바이스를 잃는다.
                // 막는 것은 ensureMaterialPSOs 쪽이고, 그래도 새면 원인을 알 수 있게 남긴다.
                SW_LOG_ERROR( "No material fallback buffer for stride %# - this draw goes out with g_SwMaterials empty.",
                              pSlot->_elementStride );
            }
            return;
        }
        context._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, batch._materialBuffer, batch._materialSrv );
        context._drawMaterialCount = batch._materialCount;
    }

    void FrameRenderer::bindForDraw( FramePassContext& context, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                                     const RHIDescriptorIndex* pMaterialTexSrv )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;

        context._passValues.setMatrix( passConstantNames()._world, context._world );

        // 같은 PSO 로 연속 드로우하는 것이 흔한 패턴이라, 패스 로컬 1칸 캐시로
        // layoutForPSO() 의 뮤텍스 + 해시맵 조회를 매 드로우 반복하지 않게 한다.
        if ( context._lastLayoutPSO != pso )
        {
            context._pLastLayout   = layoutForPSO( pso );
            context._lastLayoutPSO = pso;
        }
        const ShaderBindingLayout* pLayout = context._pLastLayout;
        if ( pLayout == nullptr || pLayout->isEmpty() )
            return; // 레이아웃을 못 얻었다(컴파일 실패 등). 조용히 건너뛴다

        // 배치마다 바뀌는 값은 **루트/푸시 상수**로 싣는다. 커맨드 리스트에 값이 그대로 들어가므로 드로우끼리
        // 덮어쓸 수 없다. 그래서 PassCB 는 패스당 하나면 충분하다(PassCB 에 넣으면 드로우마다 버퍼를 새로 잡아야
        // 한다). 언리얼의 드로우별 느슨한 파라미터와 같은 자리다.
        const uint32 arrDrawRootConstant[] = { context._drawMaterialCount };
        context._pCmd->setGraphicsRootConstants( 0, static_cast<uint32>( sizeof( arrDrawRootConstant ) / sizeof( arrDrawRootConstant[0] ) ),
                                                 arrDrawRootConstant );

        const EngineConstantBufferSlot engineCb{ context._passCb, context._passCbIndex };

        // 엔진 상수버퍼를 이 드로우에서 다시 만들 필요가 있나. 값 · 레지스트리 버전과 버퍼가 모두 그대로면 없다.
        // **PSO 도 같아야 한다.** 이 플래그는 상수버퍼 재업로드만이 아니라 리소스 재바인딩까지 건너뛰게 하는데,
        // 슬롯 상태는 PSO 가 바뀌는 순간 백엔드가 비우기 때문이다(FramePassContext::_lastBindPSO 참고).
        const uint32 valuesVersion   = context._passValues.getVersion();
        const uint32 registryVersion = context._resourceRegistry.getVersion();
        const bool   bUpToDate       = ( context._lastBindPSO == pso ) && ( context._lastCbBuffer == engineCb._buffer ) &&
                               ( context._lastCbValuesVersion == valuesVersion ) && ( context._lastCbRegistryVersion == registryVersion );

#if !defined( SW_SHIPPING )
        const BindlessIndexCheckSite  indexCheck{ _pDevice, context._passName };
        const BindlessIndexCheckSite* pIndexCheck = &indexCheck;
#else
        const BindlessIndexCheckSite* pIndexCheck = nullptr;
#endif
        ShaderParameterBinder::bindGraphics( *context._pCmd, *pLayout, context._resourceRegistry, context._passValues, engineCb, materialCb,
                                             _pDevice->supportsNativeBindlessSampling(), pMaterialTexSrv, bUpToDate, pIndexCheck );

        context._lastBindPSO           = pso;
        context._lastCbBuffer          = engineCb._buffer;
        context._lastCbValuesVersion   = valuesVersion;
        context._lastCbRegistryVersion = registryVersion;
    }

    void FrameRenderer::drawSceneMeshes( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;

        if ( _gpuScene.isUploaded() )
        {
            drawGPUBatches( context, pso, bTransparentPass, nullptr );
            return;
        }

        // 그릴 메시가 하나도 없다. 상태만 맞춰 두고 나간다.
        //
        // 주의: 씬을 다시 순회하는 폴백이나 배치마다 drawInstanced 를 부르는 두 번째 드로우 루프를 두지 않는다.
        // 네 백엔드가 모두 인다이렉트 드로우를 지원하고, 경로가 둘이면 새 기능(컬링 · 정렬 · 인스턴스 애니메이션)이
        // 한쪽에만 들어가 다른 쪽이 조용히 다른 그림을 낸다.
        if ( pso != 0 )
            context._pCmd->setPipelineState( pso );

        setIdentityWorld( context );
        commitBindlessTextureBindings( context );
    }

    void FrameRenderer::drawTransparentQueue( FramePassContext& context, RHIPipelineStateHandle pso )
    {
        if ( _pDevice == nullptr || context._pCmd == nullptr )
            return;
        const vector<GPUTransparentParticleSlot>& listParticleSlot = findActiveParticleSlots();
        if ( _gpuScene.isUploaded() )
        {
            drawGPUBatches( context, pso, true, &listParticleSlot );
            return;
        }
        // 메시가 없으면 입자만 이 뷰의 순서로 그린다.
        drawSceneMeshes( context, pso, true );
        (void)drawParticleSlots( context, vector_reference<const GPUTransparentParticleSlot>( listParticleSlot ) );
    }

    const vector<GPUTransparentParticleSlot>& FrameRenderer::findActiveParticleSlots() const
    {
        if ( isRenderingExtraView() && _pActiveView->_bHasParticleOrder == SW_TRUE )
            return _pActiveView->_listParticleSlot;
        return _gpuScene.getParticleSlots();
    }

    void FrameRenderer::drawGPUBatches( FramePassContext& context, RHIPipelineStateHandle pso, bool bTransparentPass,
                                        const vector<GPUTransparentParticleSlot>* pParticleSlot )
    {
        SW_PROFILE_SCOPE( "RT.Draw.gpuBatches" );

        const bool bCannotDraw = _pDevice == nullptr || context._pCmd == nullptr || _gpuScene.isUploaded() == false;
        if ( bCannotDraw )
            return;

        if ( pso != 0 )
            context._pCmd->setPipelineState( pso );

        setIdentityWorld( context );
        commitBindlessTextureBindings( context );

        // 지금 커맨드 리스트에 걸려 있는 PSO. 배치마다 퍼뮤테이션이 다를 수 있으므로 **바뀔 때만** 다시 건다.
        RHIPipelineStateHandle boundPSO = pso;

        const bool bInstanced = _pDevice->supportsInstancedSceneDraw() &&
                                _gpuScene.getInstanceSrv() != kInvalidDescriptorIndex;
        if ( bInstanced )
            registerInstanceBuffer( context );

        const vector_reference<const GPUMeshBatch> batches =
            bTransparentPass ? _gpuScene.getTransparentBatches() : _gpuScene.getOpaqueBatches();
        // 시간만 보면 무엇이 비싼지 알 수 없다. 배치가 몇 개로 묶였는지가 해석의 전제다.
        SW_PROFILE_COUNT( "RT.Draw.gpuBatchCount", batches.size() );

        // 간접 인자 슬롯은 업로드 순서대로 불투명 다음 투명으로 놓인다.
        const uint32 batchOffset = bTransparentPass ? static_cast<uint32>( _gpuScene.getOpaqueBatches().size() ) : 0u;

        // **같은 PSO · 정점 버퍼 · 머티리얼(버퍼 · CB · 텍스처 · 원소 수)의 연속 배치는 drawIndirect 한 번(멀티 드로우)이다.** 배치마다
        // 다른 값은 배치 표(g_SwBatches)와 인스턴스 슬롯 스트림(슬롯 1)이 준다. 간접 인자의 startInstance 가 배치 시작이라 입력
        // 어셈블러가 인스턴스마다 자기 전역 자리를 넘기고, 정점은 그 인스턴스의 meshBatchIndex 로 표를 읽는다. 드로우 ID 도 루트
        // 상수 주입도 없다(DX12 커맨드 시그니처에 루트 상수를 넣으면 ExecuteIndirect 가 두 배 느려진다. 실측이다). 멀티 드로우가 없는
        // 백엔드(DX11)는 하나씩 부른다. 드로우 루프의 비용은 호출 수라, 배치가 많은 씬에서는 RT 프레임의 큰 몫이 된다.
        const bool bMerge = isDrawMergeEnabled() && _pDevice->getCapabilities()._bMultiDrawIndirect != SW_FALSE;
        // 패스가 머티리얼로 배치를 거르면(메시 외곽선 — 외곽선을 켠 머티리얼만) 그 밖의 배치는 그리지도 묶지도 않는다.
        const RenderPassType passType = context._passType;

        const RHIBufferHandle argsBuffer = _gpuScene.getCullView( context._cullViewIndex )._indirectArgs._buffer;
        // 추가 뷰의 투명 패스는 그 뷰의 순서로 배치를 돈다(GPUViewTransparentOrder). 묶기는 그 순서에서 이웃이고 번호도 이어진 것만 — 간접 인자는 번호 순으로 놓여 있다.
        const bool            bExtraView = isRenderingExtraView();
        const vector<uint32>* pOrder =
            ( bTransparentPass && bExtraView && _pActiveView->_listTransparentBatchOrder.size() == batches.size() ) ? &_pActiveView->_listTransparentBatchOrder : nullptr;
        // 인스턴스 슬롯 스트림(슬롯 1). 패스당 한 번 건다. 씬 드로우는 모두 이 스트림에서 자기 자리를 읽는다.
        // 컬링 없는 백엔드의 추가 뷰는 꼬리를 그 뷰 순서로 다시 놓은 자기 스트림을 쓴다(GPU 정렬 백엔드는 늘 공용 스트림).
        const RHIBufferHandle slotStream =
            ( bExtraView && _pActiveView->_bUsesViewSlotStream == SW_TRUE ) ? _pActiveView->_instanceSlotStream : _gpuScene.getInstanceSlotStream();
        if ( slotStream != 0 )
            context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, slotStream, constant::kInstanceSlotStreamStride, 0 );
        // 겹침 모드(ShadedWireframe)는 배치를 두 번 그린다 — Lit PSO 로 한 번, 그 모드의 PSO(선 · 깊이 바이어스)로 한 번. 입자는 첫 번째에만 끼운다.
        // 보기 모드는 지금 그리는 뷰의 설정이다(씬 뷰만 ShadedWireframe 이고 게임 뷰는 Lit 일 수 있다).
        const RenderViewMode viewMode       = _pActiveView->_settings._viewMode;
        const bool           bOverlayDraw   = getRenderViewModeInfo( viewMode )._bOverlay && FrameRendererUtil::appliesViewMode( passType );
        const RenderViewMode arrDrawMode[]  = { bOverlayDraw ? RenderViewMode::Lit : viewMode, viewMode };
        const uint32         drawRoundCount = bOverlayDraw ? 2u : 1u;
        RHIBufferHandle      boundVertexBuffer{ 0 };
        uint32               drawCallCount{ 0 };
        const uint32         batchCount = static_cast<uint32>( batches.size() );

        // 투명 큐의 입자는 배치 순서의 자기 자리(`GPUTransparentParticleSlot::_drawPosition`) 앞에 끼운다. 입자 그리기가 PSO 와 정점 슬롯을 바꾸므로
        // 메시로 돌아올 때 PSO · 정점 버퍼를 다시 건다(`drawParticleRun` 이 슬롯 스트림과 에뮬 텍스처 슬롯을, bindForDraw 가 PSO 가 바뀐 바인딩을 다시 건다).
        const vector_reference<const GPUTransparentParticleSlot> listParticleSlot =
            ( pParticleSlot != nullptr ) ? vector_reference<const GPUTransparentParticleSlot>( *pParticleSlot ) : vector_reference<const GPUTransparentParticleSlot>{};
        uint32 particleSlotCursor{ 0 };

        for ( uint32 drawRound = 0; drawRound < drawRoundCount; ++drawRound )
        {
            const RenderViewMode drawMode        = arrDrawMode[drawRound];
            const bool           bDrawsParticles = drawRound == 0;
            uint32               orderIndex{ 0 };
            while ( orderIndex < batchCount )
            {
                if ( bDrawsParticles && drawParticleRun( context, listParticleSlot, particleSlotCursor, orderIndex, slotStream ) )
                {
                    boundPSO          = 0;
                    boundVertexBuffer = 0;
                }
                const uint32        batchIndex       = ( pOrder != nullptr ) ? ( *pOrder )[orderIndex] : orderIndex;
                const GPUMeshBatch& head             = batches[batchIndex];
                const bool          bHeadNotDrawable = head._vertexBuffer == 0 || head._instanceCount == 0 || drawsBatchInPass( passType, head ) == false;
                if ( bHeadNotDrawable )
                {
                    ++orderIndex;
                    continue;
                }
                uint32 groupCount = 1;
                if ( bMerge )
                {
                    while ( orderIndex + groupCount < batchCount )
                    {
                        const uint32 nextBatchIndex = ( pOrder != nullptr ) ? ( *pOrder )[orderIndex + groupCount] : orderIndex + groupCount;
                        // 다음 입자의 자리가 묶음 안에 들면 멀티 드로우 묶음을 그 자리에서 끊는다.
                        const bool bParticleInside = bDrawsParticles && particleSlotCursor < listParticleSlot.size() &&
                                                     listParticleSlot[particleSlotCursor]._drawPosition <= orderIndex + groupCount;
                        const bool bGroupEnds = nextBatchIndex != batchIndex + groupCount || bParticleInside;
                        if ( bGroupEnds || isSameDrawGroup( pso, passType, drawMode, head, batches[nextBatchIndex] ) == false )
                            break;
                        ++groupCount;
                    }
                }
                // **이 패스의 뷰**가 만든 인자를 쓴다. 그림자 패스가 메인 카메라 인자를 쓰면 화면 밖에서
                // 화면 안으로 그림자를 드리우는 물체가 사라진다.
                if ( drawBatchGroup( context, pso, drawMode, head, boundPSO, boundVertexBuffer, argsBuffer, batchOffset + batchIndex, groupCount ) )
                    ++drawCallCount;
                orderIndex += groupCount;
            }
        }
        // 모든 배치보다 가까운 입자(자리 = 배치 수)를 마저 그린다.
        (void)drawParticleRun( context, listParticleSlot, particleSlotCursor, MathUtil::kMaxUInt32, slotStream );
        SW_PROFILE_COUNT( "RT.Draw.indirectCalls", drawCallCount );
        _indirectDrawCallCount.fetch_add( drawCallCount, std::memory_order_relaxed );
    }

    bool FrameRenderer::layoutBindsMaterialCb( RHIPipelineStateHandle batchPSO ) const
    {
        // 머티리얼 CB 는 그 슬롯을 실제로 거는 셰이더에서만 병합 키다(`GPUMeshBatch::canShareMaterialBinding`).
        const ShaderBindingLayout* pLayout = layoutForPSO( batchPSO );
        if ( pLayout == nullptr )
            return false;
        static const hashed_string s_materialCbName{ shaderslot::cbname::kMaterial };
        for ( const ShaderBindingSlot& slot : pLayout->getSlots() )
        {
            if ( slot._name == s_materialCbName )
                return true;
        }
        return false;
    }

    bool FrameRenderer::isSameDrawGroup( RHIPipelineStateHandle pso, RenderPassType passType, RenderViewMode drawMode, const GPUMeshBatch& head,
                                         const GPUMeshBatch& other ) const
    {
        const bool bOtherBatchNotDrawable = other._vertexBuffer == 0 || other._instanceCount == 0 || drawsBatchInPass( passType, other ) == false;
        if ( bOtherBatchNotDrawable )
            return false;
        if ( other._vertexBuffer != head._vertexBuffer || psoForBatch( pso, other, drawMode ) != psoForBatch( pso, head, drawMode ) )
            return false;
        // 레이아웃을 보는 것은 CB 가 다를 때만(대부분 같다).
        const bool bCbDiffers = other._materialCb != head._materialCb;
        return GPUMeshBatch::canShareMaterialBinding( head, other, bCbDiffers && layoutBindsMaterialCb( psoForBatch( pso, head, drawMode ) ) );
    }

    bool FrameRenderer::drawBatchGroup( FramePassContext& context, RHIPipelineStateHandle pso, RenderViewMode drawMode, const GPUMeshBatch& head,
                                        RHIPipelineStateHandle& ioBoundPSO,
                                        RHIBufferHandle& ioBoundVertexBuffer, RHIBufferHandle argsBuffer, uint32 argIndex, uint32 groupCount )
    {
        // 정점 풀 하나라 보통 패스당 한 번 걸린다. 풀 밖 메시(예산 초과)만 자기 버퍼를 건다.
        if ( head._vertexBuffer != ioBoundVertexBuffer )
        {
            context._pCmd->setVertexBuffer( 0, head._vertexBuffer, sizeof( RHIVertex ), 0 );
            ioBoundVertexBuffer = head._vertexBuffer;
        }

        // **이 머티리얼의 퍼뮤테이션**으로 그린다. 주의: 패스 PSO 하나로 모두 그리면 머티리얼이 선언한
        // 정적 스위치(유리의 MATERIAL_BLEND_TRANSLUCENT 같은)가 쿠킹되기만 하고 한 번도 걸리지 않는다.
        // 캐시는 ensureMaterialPSOs 가 기록 전에 채운다. 여기서는 조회만 한다.
        const RHIPipelineStateHandle batchPSO = psoForBatch( pso, head, drawMode );
        // 변형을 만들지 못한 묶음(0)은 그리지 않는다 — 앞 묶음의 PSO 로 그리면 틀린 그림이다(만들 때 오류를 남겼다).
        if ( batchPSO == 0 )
            return false;
        if ( batchPSO != ioBoundPSO )
        {
            context._pCmd->setPipelineState( batchPSO );
            ioBoundPSO = batchPSO;
        }

        // 루트 상수 = { 머티리얼 원소 수 }. 그룹 안에서 같다.
        registerMaterialBuffer( context, head, batchPSO );
        bindForDraw( context, batchPSO, head._materialCb, head._arrMaterialTexSrv );
        context._pCmd->drawIndirect( argsBuffer, argIndex * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ), groupCount );
        return true;
    }

    bool FrameRenderer::drawParticleRun( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot, uint32& inOutCursor, uint32 position,
                                         RHIBufferHandle slotStream )
    {
        uint32 slotEnd = inOutCursor;
        while ( slotEnd < listSlot.size() && listSlot[slotEnd]._drawPosition <= position )
        {
            ++slotEnd;
        }
        if ( slotEnd == inOutCursor )
            return false;
        const vector_reference<const GPUTransparentParticleSlot> listRun = listSlot.subspan( inOutCursor, slotEnd - inOutCursor );
        inOutCursor                                                      = slotEnd;
        if ( drawParticleSlots( context, listRun ) == false )
            return false;
        // 메시 드로우가 읽는 패스 단위 상태를 되돌린다 — 입자 그리기가 인스턴스 슬롯 스트림을 비웠고 PSO 가 바뀌어 에뮬 텍스처 슬롯이 비었다.
        if ( slotStream != 0 )
            context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, slotStream, constant::kInstanceSlotStreamStride, 0 );
        commitBindlessTextureBindings( context );
        return true;
    }

    bool FrameRenderer::drawParticleSlots( FramePassContext& context, vector_reference<const GPUTransparentParticleSlot> listSlot )
    {
        const vector<GPUParticleBatch>& listBatch   = _gpuScene.getParticleBatches();
        const RHIPipelineStateHandle    pso         = findParticlePSO();
        const bool                      bCannotDraw = _pDevice == nullptr || context._pCmd == nullptr || listBatch.empty() || listSlot.empty() || pso == 0;
        if ( bCannotDraw )
            return false;
        // 정점은 셰이더가 SV_VertexID 로 만든다 — 정점 버퍼 슬롯과 인스턴스 슬롯 스트림은 비운다(캔버스 · 풀스크린 패스와 같다).
        context._pCmd->setVertexBuffer( 0, 0, 0, 0 );
        context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        context._pCmd->setPipelineState( pso );
        setIdentityWorld( context );
        // PassCB(g_ViewProj · g_InvViewProj)를 건다. 입자 버퍼 · 텍스처 · 루트 상수는 그 뒤에 입자 렌더러가 건다.
        bindForDraw( context, pso, kInvalidDescriptorIndex );
        (void)_particleRenderer.drawBatches( *context._pCmd, listBatch, listSlot, _pDevice->supportsNativeBindlessSampling() );
        return true;
    }

    void FrameRenderer::drawLight2DCookieAtlas()
    {
        const vector<Light2DCookieAtlas::CellDraw>& listDraw       = _cookieAtlas.getPendingDraws();
        const Texture2D*                            pAtlas         = _cookieAtlas.findAtlas();
        const bool                                  bNothingToDraw = listDraw.empty() || pAtlas == nullptr || _pDevice == nullptr || _pCmd == nullptr;
        if ( bNothingToDraw )
        {
            _cookieAtlas.clearPendingDraws();
            return;
        }

        // 칸에 옮겨 그리는 PSO — 전체 화면 블릿 셰이더(쿠킹되는 Present 기본 셰이더)를 아틀라스 포맷으로. 머티리얼 변형 캐시의 키 공간을 빌려 캐시가 소유한다
        // (입자 PSO 와 같은 방식 — 디바이스를 바꾸면 캐시가 비고 여기서 다시 만든다).
        const string& shaderPath = engine::getSubsystem<EngineDefaultAssets>()._shaderFullscreenBlit.get();
        const uint64  psoKey     = RenderPSOCache::materialPSOKey( 0, HashUtil::combine( hashed_string( shaderPath ).getHash(), FourCcUtil::make( "CKAT" ) ), RenderViewMode::Lit, false );
        if ( _pPSOCache->hasMaterialPSO( psoKey ) == false )
        {
            RHIPipelineStateDesc desc{};
            desc._vertexShaderPath              = shaderPath;
            desc._pixelShaderPath               = shaderPath;
            desc._vertexEntryPoint              = string( resolveEntryPoint( string_view{}, ShaderStage::Vertex ) );
            desc._pixelEntryPoint               = string( resolveEntryPoint( string_view{}, ShaderStage::Pixel ) );
            desc._numRenderTargets              = 1;
            desc._arrRtvFormat[0]               = pAtlas->getFormat();
            desc._depthStencilFormat            = RHIFormat::Unknown;
            desc._bEnableDepthTest              = 0;
            desc._bEnableDepthWrite             = 0;
            desc._cullMode                      = RHICullMode::None;
            const RHIPipelineStateHandle handle = _pDevice->getResourceFactory()->createPipelineState( desc );
            if ( handle == 0 )
                SW_LOG_ERROR( "The 2D light cookie atlas PSO could not be created from '%#' - cookie lights draw without cookies", shaderPath );
            else
                registerPSOLayout( handle, desc );
            _pPSOCache->setMaterialPSO( psoKey, RenderPSOCache::MaterialPSOEntry{ handle, static_cast<uint8>( handle != 0 ? 1 : 0 ) } );
        }
        const RHIPipelineStateHandle pso = _pPSOCache->findMaterialPSO( psoKey );
        if ( pso == 0 )
        {
            _cookieAtlas.clearPendingDraws();
            return;
        }

        // 상태 전이는 렌더 패스 밖에서 — 쿠키는 읽기, 아틀라스는 그리기.
        for ( const Light2DCookieAtlas::CellDraw& draw : listDraw )
        {
            _pCmd->prepareTextureForShaderRead( draw._pTexture->getHandle() );
        }
        _pCmd->prepareTextureForRenderTarget( pAtlas->getHandle() );

        RHIRenderPassBeginInfo beginInfo{};
        beginInfo._bBindColor        = SW_TRUE;
        beginInfo._colorTargetCount  = 1;
        beginInfo._arrColorTarget[0] = pAtlas->getHandle();
        beginInfo._arrLoadOp[0]      = RHIRenderPassLoadOp::Load; // 다른 칸은 지난 프레임에 그린 그대로다
        beginInfo._depthTarget       = 0;
        beginInfo._depthLoadOp       = RHIRenderPassLoadOp::DontCare;
        beginInfo._width             = shaderslot::kLight2DCookieAtlasSize;
        beginInfo._height            = shaderslot::kLight2DCookieAtlasSize;
        _pCmd->beginRenderPass( beginInfo );

        // 칸마다 뷰포트를 그 칸으로 잡고 쿠키를 원본(SourceColor)으로 걸어 블릿한다. 원본이 칸마다 달라 상수 슬롯도 칸마다 새로 잡는다(그림자 타일과 같은 이유).
        FramePassContext context = _frameContext;
        context._pCmd            = _pCmd;
        for ( const Light2DCookieAtlas::CellDraw& draw : listDraw )
        {
            uint32 cellX{ 0 };
            uint32 cellY{ 0 };
            uint32 cellSize{ 0 };
            Light2DCookieAtlas::computeCellRect( draw._cell, cellX, cellY, cellSize );
            RHIViewport viewport{};
            viewport._x      = static_cast<float32>( cellX );
            viewport._y      = static_cast<float32>( cellY );
            viewport._width  = static_cast<float32>( cellSize );
            viewport._height = static_cast<float32>( cellSize );
            _pCmd->setViewport( viewport );
            context._resourceRegistry.reset();
            context._resourceRegistry.registerTexture( attachmentNames()._sourceColor, draw._pTexture->getHandle(), draw._pTexture->getSrv() );
            acquirePassCb( context );
            drawFullscreen( context, pso );
        }
        _pCmd->endRenderPass();
        _pCmd->prepareTextureForShaderRead( pAtlas->getHandle() );
        _cookieAtlas.clearPendingDraws();
    }

    void FrameRenderer::drawFullscreen( FramePassContext& context, RHIPipelineStateHandle pso )
    {
        if ( context._pCmd == nullptr )
            return;
        setIdentityWorld( context );
        commitBindlessTextureBindings( context );
        context._pCmd->setVertexBuffer( 0, 0, 0, 0 );
        context._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        if ( pso != 0 )
            context._pCmd->setPipelineState( pso );
        bindForDraw( context, pso, kInvalidDescriptorIndex );
        context._pCmd->draw( 3, 0 );
    }
} // namespace sw
