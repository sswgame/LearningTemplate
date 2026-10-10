#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/ShaderParameterBinder.h"

namespace sw
{
    void FrameRenderer::registerPSOLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
    {
        if ( pso == 0 || _pDevice == nullptr )
            return;
        // gv_rhiBackend(전역) 대신 이 FrameRenderer 가 실제로 물려 있는 디바이스의 백엔드를 넘긴다.
        // 한 프로세스에 여러 IRHIDevice 가 동시에 있으면 전역값이 어긋날 수 있다.
        _psoCache.registerLayout( pso, desc, _pDevice->getBackendType() );
    }

    void FrameRenderer::onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result )
    {
        if ( result._bSuccess == false || _pDevice == nullptr )
            return;

        _psoCache.invalidateLayoutsByShaderPath( shaderPath );

        // **PSO 를 실제로 다시 만든다.** 주의: 바인딩 레이아웃만 새로 만들면 PSO 는 바이트코드를 박아 넣은
        // 객체라 화면이 시작 때 컴파일된 셰이더 그대로다.
        //
        // 순서는 loadPipeline 과 같다. 여기 도달하기 전에 ShaderRecompiler 가 ShaderCache 를 비웠고(수동 리로드)
        // (그래야 새 바이트코드를 집는다) EngineLoop 이 렌더 스레드를 재웠으므로(waitIdle) 안전하다.
        // 셰이더 편집은 개발 중 가끔 있는 일이라 이때의 스톨은 감수한다.
        releasePassResources();
        ensurePassResources();
        ensureTransientResources();
        bindPassCallbacks();
    }

    const ShaderBindingLayout* FrameRenderer::layoutForPSO( RHIPipelineStateHandle pso ) const
    {
        return _psoCache.findLayout( pso );
    }

    void FrameRenderer::registerInstanceBuffer( FramePassContext& ctx )
    {
        if ( _gpuScene.getInstanceBuffer() == 0 || _gpuScene.getInstanceSrv() == kInvalidDescriptorIndex )
            return;
        // 이름 "SwInstances" ↔ binding.hlsli 의 g_SwInstances / PassCB g_SwInstancesIndex (canonical 매칭).
        ctx._resourceRegistry.registerBuffer( passConstantNames()._swInstances,
                                              _gpuScene.getInstanceBuffer(), _gpuScene.getInstanceSrv() );
        ctx._passValues.setUint( passConstantNames()._swInstanceCount, static_cast<uint32>( _gpuScene.getInstances().size() ) );

        // 배치 표. **패스당 한 번** 건다. 배치마다 다른 값(인스턴스 시작 · 모프 풀 시작 · 정점 풀 시작)이 모두 여기 있어
        // 드로우는 배치 번호만 실어 나른다. 그래서 같은 PSO 의 배치들을 멀티 드로우 하나로 낼 수 있다.
        if ( _gpuScene.getBatchInfoBuffer() != 0 && _gpuScene.getBatchInfoSrv() != kInvalidDescriptorIndex )
        {
            ctx._resourceRegistry.registerBuffer( passConstantNames()._swBatches, _gpuScene.getBatchInfoBuffer(), _gpuScene.getBatchInfoSrv() );
            ctx._passValues.setUint( passConstantNames()._swBatchCount, _gpuScene.getIndirectCommandCount() );
        }

        // 모프 결과 풀. **패스당 한 번** 건다. 배치는 시작 오프셋을 배치 표에 싣는다(드로우 사이에
        // 바인딩이 바뀌지 않는다는 이 엔진의 규약). 안 걸리면 셰이더가 g_SwMorphVerticesIndex 로 알아채고
        // 입력 스트림을 그대로 쓴다.
        const RHIStructuredBufferSlot& morphBuffer = ( _bMorphBindsRest != SW_FALSE ) ? _meshMorphPool.getRestBuffer()
                                                                                      : _meshMorphPool.getMorphBuffer();
        if ( morphBuffer._buffer != 0 && morphBuffer._srv != kInvalidDescriptorIndex )
        {
            ctx._resourceRegistry.registerBuffer( passConstantNames()._swMorphVertices, morphBuffer._buffer, morphBuffer._srv );
            // 진단(레스트를 그대로 물림)이면 레스트 버퍼는 모프 구간만 담는다 — 스킨 구간 번호는 범위 밖으로 걸러 입력 스트림을 쓴다.
            const uint32 morphElementCount = ( _bMorphBindsRest != SW_FALSE ) ? _meshMorphPool.getMorphVertexCount() : _meshMorphPool.getVertexCount();
            ctx._passValues.setUint( passConstantNames()._swMorphVertexCount, morphElementCount );
        }

        // 정점 애니메이션(VAT) 표 — 패스당 한 번. 시계는 게임 스레드의 군중 시계(스냅샷)라 인스턴스 시각 오프셋과 더하면 CPU 의 클립 시각이다.
        const RHIStructuredBufferSlot& vertexAnimationBuffer = _vertexAnimationPool.getBuffer();
        if ( vertexAnimationBuffer._buffer != 0 && vertexAnimationBuffer._srv != kInvalidDescriptorIndex )
        {
            ctx._resourceRegistry.registerBuffer( passConstantNames()._swVertexAnimation, vertexAnimationBuffer._buffer, vertexAnimationBuffer._srv );
            ctx._passValues.setUint( passConstantNames()._swVertexAnimationCount, _vertexAnimationPool.getElementCount() );
            ctx._passValues.setFloat( passConstantNames()._swVertexAnimationTime, _gpuScene.getVertexAnimationTime() );
        }

        // 컬링이 실제로 목록을 만들었을 때만 건다. 안 걸리면 셰이더가 g_SwVisibleInstanceIdsIndex 로 알아채고
        // 배치 시작 + 서수를 쓴다(컬링 없음 경로). 반대로 목록만 걸고 컬링을 안 돌리면 **비어 있는
        // 목록**을 읽어 모두 0 번 인스턴스를 그린다. 그래서 둘은 반드시 같이 켜지고 같이 꺼진다.
        const GPUCullViewResources& cullView = _gpuScene.getCullView( ctx._cullViewIndex );
        if ( _bGPUCullingActive != SW_FALSE && cullView._visibleInstances._buffer != 0 &&
             cullView._visibleInstances._srv != kInvalidDescriptorIndex )
        {
            ctx._resourceRegistry.registerBuffer( passConstantNames()._swVisibleInstanceIds,
                                                  cullView._visibleInstances._buffer, cullView._visibleInstances._srv );
        }
    }

    void FrameRenderer::registerLightBuffer( FramePassContext& ctx )
    {
        // 개수는 버퍼가 없어도 채운다. 셰이더는 0 이면 PassCB 키라이트로 폴백한다.
        ctx._passValues.setUint( passConstantNames()._swLightCount, _lightBuffer.isBindable() ? _lightBuffer.getCount() : 0u );
        if ( _lightBuffer.isBindable() == false )
            return;
        const RHIStructuredBufferSlot& buffer = _lightBuffer.getBuffer();
        ctx._resourceRegistry.registerBuffer( passConstantNames()._swLights, buffer._buffer, buffer._srv );
    }

    void FrameRenderer::registerMaterialBuffer( FramePassContext& ctx, const GPUMeshBatch& batch, RHIPipelineStateHandle pso )
    {
        // 배치의 셰이더 타입에 해당하는 머티리얼 데이터 버퍼. 이름 "SwMaterials" ↔ binding.hlsli 의 g_SwMaterials(t9).
        // 바인더가 리플렉션 슬롯에 걸고, 셰이더는 인스턴스의 materialIndex 로 원소를 읽는다(드로우별 CB 바인딩 없음).
        if ( batch._materialBuffer == 0 || batch._materialSrv == kInvalidDescriptorIndex )
        {
            // 머티리얼 없는 배치. 빈 슬롯으로 그리지 않는다(Vulkan 은 partially-bound 슬롯을 실제로 읽으면 정의되지 않는다).
            // 폴백은 **이 PSO 셰이더가 선언한 stride** 의 것을 고른다. 공용 256 바이트를 걸면 DX11 이 드로우마다
            // "structure stride 256 vs 24" 를 낸다(SRV 의 구조 stride 는 셰이더 선언과 같아야 한다).
            const ShaderBindingLayout* pLayout = ( ctx._lastLayoutPSO == pso ) ? ctx._pLastLayout : layoutForPSO( pso );
            const ShaderBindingSlot*   pSlot   = ( pLayout != nullptr ) ? pLayout->find( passConstantNames()._swMaterials ) : nullptr;
            if ( pSlot == nullptr || pSlot->_elementStride == 0 )
                return; // 셰이더가 머티리얼 버퍼를 선언하지 않았다. 걸 것도 없다.
            const auto it = _mapMaterialFallback.find( pSlot->_elementStride );
            if ( it != _mapMaterialFallback.end() && it->second.isValid() && it->second._srv != kInvalidDescriptorIndex )
            {
                ctx._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, it->second._buffer, it->second._srv );
                ctx._drawMaterialCount = 1u;
            }
            else if ( _bMaterialFallbackMissingLogged.exchange( 1, std::memory_order_relaxed ) == 0 )
            {
                // 여기서 조용히 나가면 t9 가 빈 채로 드로우가 나가고 Vulkan 은 디바이스를 잃는다.
                // 막는 것은 ensureMaterialPSOs 쪽이고, 그래도 새면 원인을 알 수 있게 남긴다.
                SW_LOG_ERROR( "머티리얼 폴백 버퍼가 stride %# 에 없습니다 — 이 드로우는 g_SwMaterials 를 비운 채 나갑니다.",
                              pSlot->_elementStride );
            }
            return;
        }
        ctx._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, batch._materialBuffer, batch._materialSrv );
        ctx._drawMaterialCount = batch._materialCount;
    }

    void FrameRenderer::bindForDraw( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb,
                                     const RHIDescriptorIndex* pMaterialTexSrv )
    {
        if ( _pDevice == nullptr || ctx._pCmd == nullptr )
            return;

        ctx._passValues.setMatrix( passConstantNames()._world, ctx._world );

        // 같은 PSO 로 연속 드로우하는 것이 흔한 패턴이라, 패스 로컬 1칸 캐시로
        // layoutForPSO() 의 뮤텍스 + 해시맵 조회를 매 드로우 반복하지 않게 한다.
        if ( ctx._lastLayoutPSO != pso )
        {
            ctx._pLastLayout   = layoutForPSO( pso );
            ctx._lastLayoutPSO = pso;
        }
        const ShaderBindingLayout* pLayout = ctx._pLastLayout;
        if ( pLayout == nullptr || pLayout->isEmpty() )
            return; // 레이아웃을 못 얻었다(컴파일 실패 등). 조용히 건너뛴다

        // 배치마다 바뀌는 값은 **루트/푸시 상수**로 싣는다. 커맨드 리스트에 값이 그대로 들어가므로 드로우끼리
        // 덮어쓸 수 없다. 그래서 PassCB 는 패스당 하나면 충분하다(PassCB 에 넣으면 드로우마다 버퍼를 새로 잡아야
        // 한다). 언리얼의 드로우별 느슨한 파라미터와 같은 자리다.
        const uint32 arrDrawRootConstant[] = { ctx._drawMaterialCount };
        ctx._pCmd->setGraphicsRootConstants( 0, static_cast<uint32>( sizeof( arrDrawRootConstant ) / sizeof( arrDrawRootConstant[0] ) ),
                                             arrDrawRootConstant );

        const EngineConstantBufferSlot engineCb{ ctx._passCb, ctx._passCbIndex };

        // 엔진 상수버퍼를 이 드로우에서 다시 만들 필요가 있나. 값 · 레지스트리 버전과 버퍼가 모두 그대로면 없다.
        // **PSO 도 같아야 한다.** 이 플래그는 상수버퍼 재업로드만이 아니라 리소스 재바인딩까지 건너뛰게 하는데,
        // 슬롯 상태는 PSO 가 바뀌는 순간 백엔드가 비우기 때문이다(FramePassContext::_lastBindPSO 참고).
        const uint32 valuesVersion   = ctx._passValues.getVersion();
        const uint32 registryVersion = ctx._resourceRegistry.getVersion();
        const bool   bUpToDate       = ( ctx._lastBindPSO == pso ) && ( ctx._lastCbBuffer == engineCb._buffer ) &&
                               ( ctx._lastCbValuesVersion == valuesVersion ) && ( ctx._lastCbRegistryVersion == registryVersion );

        ShaderParameterBinder::bindGraphics( *ctx._pCmd, *pLayout, ctx._resourceRegistry, ctx._passValues,
                                             engineCb, materialCb, _pDevice->supportsNativeBindlessSampling(), pMaterialTexSrv, bUpToDate );

        ctx._lastBindPSO           = pso;
        ctx._lastCbBuffer          = engineCb._buffer;
        ctx._lastCbValuesVersion   = valuesVersion;
        ctx._lastCbRegistryVersion = registryVersion;
    }

    void FrameRenderer::drawSceneMeshes( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass )
    {
        if ( _pDevice == nullptr || ctx._pCmd == nullptr )
            return;

        if ( _gpuScene.isUploaded() )
        {
            drawGPUBatches( ctx, pso, cbIndex, bTransparentPass );
            return;
        }

        // 그릴 메시가 하나도 없다. 상태만 맞춰 두고 나간다.
        //
        // 주의: 씬을 다시 순회하는 폴백이나 배치마다 drawInstanced 를 부르는 두 번째 드로우 루프를 두지 않는다.
        // 네 백엔드가 모두 인다이렉트 드로우를 지원하고, 경로가 둘이면 새 기능(컬링 · 정렬 · 인스턴스 애니메이션)이
        // 한쪽에만 들어가 다른 쪽이 조용히 다른 그림을 낸다.
        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );

        setIdentityWorld( ctx );
        commitBindlessTextureBindings( ctx );
    }

    void FrameRenderer::drawGPUBatches( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass )
    {
        SW_PROFILE_SCOPE( "RT.Draw.gpuBatches" );

        (void)cbIndex;
        if ( _pDevice == nullptr || ctx._pCmd == nullptr || _gpuScene.isUploaded() == false )
            return;

        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );

        setIdentityWorld( ctx );
        commitBindlessTextureBindings( ctx );

        // 지금 커맨드 리스트에 걸려 있는 PSO. 배치마다 퍼뮤테이션이 다를 수 있으므로 **바뀔 때만** 다시 건다.
        RHIPipelineStateHandle boundPSO = pso;

        const bool bInstanced = _pDevice->supportsInstancedSceneDraw() &&
                                _gpuScene.getInstanceSrv() != kInvalidDescriptorIndex;
        if ( bInstanced )
            registerInstanceBuffer( ctx );

        const vector<GPUMeshBatch>& batches =
            bTransparentPass ? _gpuScene.getTransparentBatches() : _gpuScene.getOpaqueBatches();
        // 시간만 보면 무엇이 비싼지 알 수 없다. 배치가 몇 개로 묶였는지가 해석의 전제다.
        SW_PROFILE_COUNT( "RT.Draw.gpuBatchCount", batches.size() );

        // 간접 인자 슬롯은 업로드 순서대로 불투명 다음 투명으로 놓인다.
        uint32 batchOffset{ 0 };
        if ( bTransparentPass )
            batchOffset = static_cast<uint32>( _gpuScene.getOpaqueBatches().size() );

        // **같은 PSO · 정점 버퍼 · 머티리얼(버퍼 · CB · 텍스처 · 원소 수)의 연속 배치는 drawIndirect 한 번(멀티 드로우)이다.** 배치마다
        // 다른 값은 배치 표(g_SwBatches)와 인스턴스 슬롯 스트림(슬롯 1)이 준다. 간접 인자의 startInstance 가 배치 시작이라 입력
        // 어셈블러가 인스턴스마다 자기 전역 자리를 넘기고, 정점은 그 인스턴스의 meshBatchIndex 로 표를 읽는다. 드로우 ID 도 루트
        // 상수 주입도 없다(DX12 커맨드 시그니처에 루트 상수를 넣으면 ExecuteIndirect 가 두 배 느려진다. 실측이다). 멀티 드로우가 없는
        // 백엔드(DX11)는 하나씩 부른다. 드로우 루프의 비용은 호출 수라, 배치가 많은 씬에서는 RT 프레임의 큰 몫이 된다.
        const bool bMerge = isDrawMergeEnabled() && _pDevice->getCapabilities()._bMultiDrawIndirect != SW_FALSE;

        // 머티리얼 CB 는 그 슬롯을 실제로 거는 셰이더에서만 병합 키다(`GPUMeshBatch::canShareMaterialBinding`).
        auto layoutBindsMaterialCb = [this]( RHIPipelineStateHandle batchPSO ) -> bool
        {
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
        };

        // 패스가 머티리얼로 배치를 거르면(메시 외곽선 — 외곽선을 켠 머티리얼만) 그 밖의 배치는 그리지도 묶지도 않는다.
        const RenderPassType passType      = ctx._passType;
        auto                 sameDrawGroup = [this, pso, passType, &layoutBindsMaterialCb]( const GPUMeshBatch& head, const GPUMeshBatch& other ) -> bool
        {
            if ( other._vertexBuffer == 0 || other._instanceCount == 0 || drawsBatchInPass( passType, other ) == false )
                return false;
            if ( other._vertexBuffer != head._vertexBuffer || psoForBatch( pso, other ) != psoForBatch( pso, head ) )
                return false;
            // 레이아웃을 보는 것은 CB 가 다를 때만(대부분 같다).
            const bool bCbDiffers = other._materialCb != head._materialCb;
            return GPUMeshBatch::canShareMaterialBinding( head, other, bCbDiffers && layoutBindsMaterialCb( psoForBatch( pso, head ) ) );
        };

        const RHIBufferHandle argsBuffer = _gpuScene.getCullView( ctx._cullViewIndex )._indirectArgs._buffer;
        // 추가 뷰의 투명 패스는 그 뷰의 순서로 배치를 돈다(GPUViewTransparentOrder). 묶기는 그 순서에서 이웃이고 번호도 이어진 것만 — 간접 인자는 번호 순으로 놓여 있다.
        const bool            bExtraView = isRenderingExtraView();
        const vector<uint32>* pOrder =
            ( bTransparentPass && bExtraView && _pActiveView->_listTransparentBatchOrder.size() == batches.size() ) ? &_pActiveView->_listTransparentBatchOrder : nullptr;
        // 인스턴스 슬롯 스트림(슬롯 1). 패스당 한 번 건다. 씬 드로우는 모두 이 스트림에서 자기 자리를 읽는다.
        // 컬링 없는 백엔드의 추가 뷰는 꼬리를 그 뷰 순서로 다시 놓은 자기 스트림을 쓴다(GPU 정렬 백엔드는 늘 공용 스트림).
        const RHIBufferHandle slotStream =
            ( bExtraView && _pActiveView->_bUsesViewSlotStream == SW_TRUE ) ? _pActiveView->_instanceSlotStream : _gpuScene.getInstanceSlotStream();
        if ( slotStream != 0 )
            ctx._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, slotStream, constant::kInstanceSlotStreamStride, 0 );
        RHIBufferHandle boundVertexBuffer{ 0 };
        uint32          drawCallCount{ 0 };
        uint32          orderIndex{ 0 };
        const uint32    batchCount = static_cast<uint32>( batches.size() );
        while ( orderIndex < batchCount )
        {
            const uint32        batchIndex = ( pOrder != nullptr ) ? ( *pOrder )[orderIndex] : orderIndex;
            const GPUMeshBatch& head       = batches[batchIndex];
            if ( head._vertexBuffer == 0 || head._instanceCount == 0 || drawsBatchInPass( passType, head ) == false )
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
                    if ( nextBatchIndex != batchIndex + groupCount || sameDrawGroup( head, batches[nextBatchIndex] ) == false )
                        break;
                    ++groupCount;
                }
            }

            // 정점 풀 하나라 보통 패스당 한 번 걸린다. 풀 밖 메시(예산 초과)만 자기 버퍼를 건다.
            if ( head._vertexBuffer != boundVertexBuffer )
            {
                ctx._pCmd->setVertexBuffer( 0, head._vertexBuffer, sizeof( RHIVertex ), 0 );
                boundVertexBuffer = head._vertexBuffer;
            }

            // **이 머티리얼의 퍼뮤테이션**으로 그린다. 주의: 패스 PSO 하나로 모두 그리면 머티리얼이 선언한
            // 정적 스위치(유리의 MATERIAL_BLEND_TRANSLUCENT 같은)가 쿠킹되기만 하고 한 번도 걸리지 않는다.
            // 캐시는 ensureMaterialPSOs 가 기록 전에 채운다. 여기서는 조회만 한다.
            const RHIPipelineStateHandle batchPSO = psoForBatch( pso, head );
            if ( batchPSO != boundPSO && batchPSO != 0 )
            {
                ctx._pCmd->setPipelineState( batchPSO );
                boundPSO = batchPSO;
            }

            // 루트 상수 = { 머티리얼 원소 수 }. 그룹 안에서 같다.
            registerMaterialBuffer( ctx, head, batchPSO );
            bindForDraw( ctx, batchPSO, head._materialCb, head._arrMaterialTexSrv );
            // **이 패스의 뷰**가 만든 인자를 쓴다. 그림자 패스가 메인 카메라 인자를 쓰면 화면 밖에서
            // 화면 안으로 그림자를 드리우는 물체가 사라진다.
            ctx._pCmd->drawIndirect( argsBuffer, ( batchOffset + batchIndex ) * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ), groupCount );
            ++drawCallCount;
            orderIndex += groupCount;
        }
        SW_PROFILE_COUNT( "RT.Draw.indirectCalls", drawCallCount );
        _indirectDrawCallCount.fetch_add( drawCallCount, std::memory_order_relaxed );
    }

    void FrameRenderer::drawFullscreen( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex )
    {
        (void)cbIndex;
        if ( ctx._pCmd == nullptr )
            return;
        setIdentityWorld( ctx );
        commitBindlessTextureBindings( ctx );
        ctx._pCmd->setVertexBuffer( 0, 0, 0, 0 );
        ctx._pCmd->setVertexBuffer( constant::kInstanceSlotStreamSlot, 0, 0, 0 );
        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );
        bindForDraw( ctx, pso, kInvalidDescriptorIndex );
        ctx._pCmd->draw( 3, 0 );
    }
} // namespace sw
