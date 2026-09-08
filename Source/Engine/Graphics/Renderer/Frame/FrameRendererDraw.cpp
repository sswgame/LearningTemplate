#include "pch.h"

#include "Core/Math/MatrixMath.h"
#include "Core/Profile/FrameProfiler.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/Renderer/Frame/FrameRenderer.h"
#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Frame/ShaderBindingBinder.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    void FrameRenderer::registerPsoLayout( RHIPipelineStateHandle pso, const RHIPipelineStateDesc& desc )
    {
        if ( pso == 0 || _pDevice == nullptr )
            return;
        // gv_rhiBackend(전역) 대신 이 FrameRenderer 가 실제로 물려 있는 디바이스의 백엔드를 넘긴다 —
        // 한 프로세스에 여러 IRHIDevice 가 동시에 존재하면 전역값이 어긋날 수 있다.
        const ShaderBindingLayout& layout = _bindingLayoutCache.getOrBuild( desc, _pDevice->getBackendType() );
        std::scoped_lock<mutex>    lock{ _psoLayoutMutex };
        _mapPsoLayout[pso] = &layout;
        _mapPsoDesc[pso]   = desc;
    }

    void FrameRenderer::onShaderRecompiled( string_view shaderPath, const ShaderCompileResult& result )
    {
        if ( result._bSuccess == false || _pDevice == nullptr )
            return;

        _bindingLayoutCache.invalidateByShaderPath( shaderPath );

        // 영향받는 PSO 를 특정하지 않고 전부 다시 만든다 (PSO 수는 소수). getOrBuild 가 재컴파일·리플렉션한다.
        std::scoped_lock<mutex> lock{ _psoLayoutMutex };
        for ( const auto& entry : _mapPsoDesc )
        {
            const ShaderBindingLayout& layout = _bindingLayoutCache.getOrBuild( entry.second, _pDevice->getBackendType() );
            _mapPsoLayout[entry.first]        = &layout;
        }
    }

    const ShaderBindingLayout* FrameRenderer::layoutForPso( RHIPipelineStateHandle pso ) const
    {
        std::scoped_lock<mutex> lock{ _psoLayoutMutex };
        auto                    it = _mapPsoLayout.find( pso );
        return it != _mapPsoLayout.end() ? it->second : nullptr;
    }

    void FrameRenderer::registerInstanceBuffer( FramePassContext& ctx )
    {
        if ( _gpuScene.getInstanceBuffer() == 0 || _gpuScene.getInstanceSrv() == kInvalidDescriptorIndex )
            return;
        // 이름 "SwInstances" ↔ binding.hlsli 의 g_SwInstances / PassCB g_SwInstancesIndex (canonical 매칭).
        ctx._resourceRegistry.registerBuffer( passConstantNames()._swInstances,
                                              _gpuScene.getInstanceBuffer(), _gpuScene.getInstanceSrv() );
        ctx._passValues.setUint( passConstantNames()._swInstanceCount, static_cast<uint32>( _gpuScene.getInstances().size() ) );

        // 컬링이 실제로 목록을 만들었을 때만 건다 — 안 걸리면 셰이더가 g_SwVisibleInstanceIdsIndex 로 알아채고
        // 예전처럼 배치 시작 + 서수를 쓴다(컬링 없음 경로). 반대로 목록만 걸고 컬링을 안 돌리면 **비어 있는
        // 목록**을 읽어 전부 0 번 인스턴스를 그린다 — 그래서 둘은 반드시 같이 켜지고 같이 꺼진다.
        const GpuCullViewResources& cullView = _gpuScene.getCullView( ctx._cullView );
        if ( _bGpuCullingActive != 0 && cullView._visibleInstances._buffer != 0 &&
             cullView._visibleInstances._srv != kInvalidDescriptorIndex )
        {
            ctx._resourceRegistry.registerBuffer( passConstantNames()._swVisibleInstanceIds,
                                                  cullView._visibleInstances._buffer, cullView._visibleInstances._srv );
        }
    }

    void FrameRenderer::registerMaterialBuffer( FramePassContext& ctx, const GpuMeshBatch& batch, RHIPipelineStateHandle pso )
    {
        // 배치의 셰이더 타입에 해당하는 머티리얼 데이터 버퍼 — 이름 "SwMaterials" ↔ binding.hlsli 의 g_SwMaterials(t9).
        // 바인더가 리플렉션 슬롯에 걸고, 셰이더는 인스턴스의 materialIndex 로 원소를 읽는다 (드로우별 CB 바인딩 없음).
        if ( batch._materialBuffer == 0 || batch._materialSrv == kInvalidDescriptorIndex )
        {
            // 머티리얼 없는 배치 — 빈 슬롯으로 그리지 않는다(Vulkan 은 partially-bound 슬롯을 실제로 읽으면 정의되지 않는다).
            // 폴백은 **이 PSO 셰이더가 선언한 stride** 의 것을 고른다 — 공용 256 바이트를 걸면 DX11 이 드로우마다
            // "structure stride 256 vs 24" 를 낸다(SRV 의 구조 stride 는 셰이더 선언과 같아야 한다).
            const ShaderBindingLayout* pLayout = ( ctx._lastLayoutPso == pso ) ? ctx._pLastLayout : layoutForPso( pso );
            const ShaderBindingSlot*   pSlot   = ( pLayout != nullptr ) ? pLayout->find( passConstantNames()._swMaterials ) : nullptr;
            if ( pSlot == nullptr || pSlot->_elementStride == 0 )
                return; // 셰이더가 머티리얼 버퍼를 선언하지 않았다 — 걸 것도 없다.
            const auto it = _mapMaterialFallback.find( pSlot->_elementStride );
            if ( it != _mapMaterialFallback.end() && it->second.isValid() && it->second._srv != kInvalidDescriptorIndex )
            {
                ctx._resourceRegistry.registerBuffer( passConstantNames()._swMaterials, it->second._buffer, it->second._srv );
                ctx._drawMaterialCount = 1u;
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

        // 같은 PSO로 연속 드로우하는 게 흔한 패턴이라, 패스-로컬 1-entry 캐시로
        // layoutForPso()의 뮤텍스+해시맵 조회를 매 드로우 반복하지 않게 한다.
        if ( ctx._lastLayoutPso != pso )
        {
            ctx._pLastLayout   = layoutForPso( pso );
            ctx._lastLayoutPso = pso;
        }
        const ShaderBindingLayout* pLayout = ctx._pLastLayout;
        if ( pLayout == nullptr || pLayout->isEmpty() )
            return; // 레이아웃 미확보(컴파일 실패 등) — 조용히 스킵

        // 배치마다 바뀌는 값은 **루트/푸시 상수**로 싣는다 — 커맨드 리스트에 값이 그대로 들어가므로 드로우끼리
        // 덮어쓸 수 없다. 그래서 PassCB 는 패스당 하나면 충분하다(예전엔 이 둘을 PassCB 에 넣어 드로우마다
        // 버퍼를 새로 잡아야 했다). 언리얼의 드로우별 느슨한 파라미터와 같은 자리다.
        const uint32 arrDrawRootConstant[] = { ctx._drawInstanceBase, ctx._drawMaterialCount };
        ctx._pCmd->setGraphicsRootConstants( 0, static_cast<uint32>( sizeof( arrDrawRootConstant ) / sizeof( arrDrawRootConstant[0] ) ),
                                             arrDrawRootConstant );

        const EngineConstantBufferSlot engineCb{ ctx._passCb, ctx._passCbIndex };

        // 엔진 상수버퍼를 이 드로우에서 다시 만들 필요가 있나 — 값·레지스트리 버전과 버퍼가 모두 그대로면 없다.
        const uint32 valuesVersion   = ctx._passValues.getVersion();
        const uint32 registryVersion = ctx._resourceRegistry.getVersion();
        const bool   bUpToDate       = ( ctx._lastCbBuffer == engineCb._buffer ) && ( ctx._lastCbValuesVersion == valuesVersion ) &&
                                       ( ctx._lastCbRegistryVersion == registryVersion );

        ShaderBindingBinder::bindGraphics( *_pDevice, *ctx._pCmd, *pLayout, ctx._resourceRegistry, ctx._passValues,
                                           engineCb, materialCb, _pDevice->supportsNativeBindlessSampling(), pMaterialTexSrv, bUpToDate );

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
            drawGpuBatches( ctx, pso, cbIndex, bTransparentPass );
            return;
        }

        // 그릴 메시가 하나도 없다. 상태만 맞춰 두고 나간다.
        //
        // 예전엔 여기에 경로가 둘 더 있었다. (1) 씬을 다시 순회해 드로우 목록을 만드는 폴백과
        // (2) 인다이렉트 대신 배치마다 drawInstanced 를 부르는 경로다. 둘 다 지웠다 —
        // (1)은 수집 조건이 GpuScene::buildFromScene 과 같아 애초에 도달 불가였고(런타임 경로는
        // _pScene 이 null 이라 더더욱), (2)는 네 백엔드가 모두 인다이렉트 드로우를 지원하는데
        // 진단용 전역변수로만 닿는 두 번째 드로우 루프였다. 경로가 둘이면 새 기능이 한쪽에만 들어간다 —
        // 실제로 컬링·정렬·인스턴스 애니메이션이 전부 인다이렉트 경로에만 붙어 있어서, 그 변수를 끄면
        // 조용히 다른 그림이 나왔다.
        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );

        setIdentityWorld( ctx );
        commitBindlessTextureBindings( ctx );
    }

    void FrameRenderer::drawGpuBatches( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass )
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
        RHIPipelineStateHandle boundPso = pso;

        const bool bInstanced = _pDevice->supportsInstancedSceneDraw() &&
                                _gpuScene.getInstanceSrv() != kInvalidDescriptorIndex;
        if ( bInstanced )
            registerInstanceBuffer( ctx );

        const vector<GpuMeshBatch>& batches =
            bTransparentPass ? _gpuScene.getTransparentBatches() : _gpuScene.getOpaqueBatches();
        // 시간만 보면 무엇이 비싼지 알 수 없다 — 배치가 몇 개로 묶였는지가 해석의 전제다.
        SW_PROFILE_COUNT( "RT.Draw.gpuBatchCount", batches.size() );

        // Indirect slots are laid out opaque then transparent in upload order.
        uint32 batchOffset{ 0 };
        if ( bTransparentPass )
            batchOffset = static_cast<uint32>( _gpuScene.getOpaqueBatches().size() );

        for ( uint32 batchIndex = 0; batchIndex < batches.size(); ++batchIndex )
        {
            const GpuMeshBatch& batch = batches[batchIndex];
            if ( batch._vertexBuffer == 0 || batch._instanceCount == 0 )
                continue;
            ctx._pCmd->setVertexBuffer( 0, batch._vertexBuffer, sizeof( RHIVertex ), 0 );

            // **이 머티리얼의 퍼뮤테이션**으로 그린다. 예전엔 패스 PSO 하나로 전부 그려서, 머티리얼이 선언한
            // 정적 스위치(유리의 MATERIAL_BLEND_TRANSLUCENT 같은)가 구워지기만 하고 한 번도 걸리지 않았다.
            // 캐시는 ensureMaterialPsos 가 기록 전에 채운다 — 여기서는 조회만 한다.
            const RHIPipelineStateHandle batchPso = psoForBatch( pso, batch );
            if ( batchPso != boundPso && batchPso != 0 )
            {
                ctx._pCmd->setPipelineState( batchPso );
                boundPso = batchPso;
            }

            // b0 = 패스 상수(뷰/월드), b1 = 머티리얼 상수. 예전엔 둘을 한 인자에 겹쳐 실어서
            // 지오메트리가 머티리얼 버퍼를 PassCB 로 읽었다.
            if ( bInstanced )
                ctx._drawInstanceBase = batch._instanceBase;
            registerMaterialBuffer( ctx, batch, batchPso );
            bindForDraw( ctx, batchPso, batch._materialCb, batch._arrMaterialTexSrv );
            // **이 패스의 뷰**가 만든 인자를 쓴다 — 그림자 패스가 메인 카메라 인자를 쓰면 화면 밖에서
            // 화면 안으로 그림자를 드리우는 물체가 사라진다.
            ctx._pCmd->drawIndirect( _gpuScene.getCullView( ctx._cullView )._indirectArgs._buffer,
                                     ( batchOffset + batchIndex ) * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
        }
    }

    void FrameRenderer::drawFullscreen( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex )
    {
        (void)cbIndex;
        if ( ctx._pCmd == nullptr )
            return;
        setIdentityWorld( ctx );
        commitBindlessTextureBindings( ctx );
        ctx._pCmd->setVertexBuffer( 0, 0, 0, 0 );
        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );
        bindForDraw( ctx, pso, kInvalidDescriptorIndex );
        ctx._pCmd->draw( 3, 0 );
    }
} // namespace sw
