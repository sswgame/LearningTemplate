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
#include "Engine/Graphics/Shader/ShaderBindingLayout.h"
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
            if ( it != _mapMaterialFallback.end() && it->second._buffer != 0 && it->second._srv != kInvalidDescriptorIndex )
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

        if ( _bUseGpuDriven != 0 && _gpuScene.isUploaded() )
        {
            drawGpuBatches( ctx, pso, cbIndex, bTransparentPass );
            return;
        }

        // 여기 오면 그릴 메시가 하나도 없다.
        //
        // 예전엔 이 자리에 씬 전체를 다시 순회해 드로우 목록을 만들고 정렬하는 폴백 경로가 있었다.
        // 그런데 `GpuScene::buildFromScene` 의 수집 조건이 그 순회와 같다 — 활성 오브젝트의 보이는
        // MeshComponent 중 정점이 있는 것. 그래서 인스턴스가 비었다는 건 "그릴 메시가 없다" 는
        // 뜻이고, 폴백이 다시 순회해도 똑같이 빈손이었다. 패스마다(deferred 는 프레임당 최대 7회)
        // 씬 전체를 훑고 shared_ptr 을 복사하고 정렬까지 하던 코드가 통째로 도달 불가였다.
        //
        // 무엇보다 런타임 경로에서는 애초에 불가능했다: `executePacket` 은 `_pScene = nullptr` 로
        // 명시한다(렌더 스레드가 씬 그래프를 만지면 안 되고, 그러라고 GpuScene 스냅샷이 있다).
        // 폴백은 `_pScene` 이 없으면 첫 가드에서 바로 나가므로, GameObject 를 아무리 추가해도
        // 그릴 수 없었다. 동기 경로(`execute`)에서만 기회가 있었는데 거기서는 바로 앞줄에서
        // 같은 조건으로 buildFromScene 이 돈다.
        //
        // 정적으로만 판단하지 않고 계측해서도 확인했다 — 큐브를 그리는 RenderPassTest, 앱(에디터 ON),
        // SceneTest 어디서도 "인스턴스는 비었는데 씬에는 그릴 메시가 있는" 순간이 없었다.
        if ( _gpuScene.getInstances().empty() == false )
        {
            drawGpuSceneMeshes( ctx, pso, cbIndex, bTransparentPass );
            return;
        }

        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );

        setIdentityWorld( ctx );
        commitBindlessTextureBindings( ctx );
    }

    void FrameRenderer::drawGpuSceneMeshes( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass )
    {
        (void)cbIndex;
        if ( _pDevice == nullptr || ctx._pCmd == nullptr )
            return;
        if ( _gpuScene.getInstances().empty() )
            return;

        const vector<GpuMeshBatch>& batches =
            bTransparentPass ? _gpuScene.getTransparentBatches() : _gpuScene.getOpaqueBatches();
        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );

        // 언리얼 GPUScene 방식 하나뿐이다: VS 가 per-instance world/materialIndex 를 인스턴스 구조버퍼에서 읽고 배치당
        // drawInstanced 한 번. 예전의 "드로우마다 g_World 를 갱신하는" 폴백 경로는 지웠다 — 네 백엔드가 모두 인스턴스
        // 버퍼를 지원하고, 경로가 둘이면 새 기능이 한쪽에만 들어가는 일이 반복된다.
        if ( _pDevice->supportsInstancedSceneDraw() == false || _gpuScene.getInstanceSrv() == kInvalidDescriptorIndex )
        {
            static bool s_bWarned = false;
            if ( s_bWarned == false )
            {
                s_bWarned = true;
                SW_LOG_ERROR( "씬 메시를 그릴 수 없습니다 — 인스턴스 버퍼가 없거나 백엔드가 인스턴스드 드로우를 지원하지 않습니다." );
            }
            setIdentityWorld( ctx );
            commitBindlessTextureBindings( ctx );
            return;
        }
        registerInstanceBuffer( ctx );

        SW_PROFILE_SCOPE( "RT.Draw.sceneMeshes" );

        uint32 drawn{ 0 };
        bool   bFirstItem = true;
        for ( const GpuMeshBatch& batch : batches )
        {
            // 정점버퍼와 머티리얼 버퍼는 **GpuScene::upload 가 이미 만들어 배치에 적어 뒀다**(게임 스레드).
            // 여기는 패스 기록 중이라 TaskManager 워커에서 돌 수 있고, 그 스레드에서 Mesh::upload /
            // applyToGpu 를 부르면 RHI 자원 생성이 워커에서 일어난다 — 배치에 적힌 핸들만 쓴다.
            const Mesh* pMesh = batch._pMesh;
            if ( pMesh == nullptr || pMesh->getVertexCount() == 0 )
                continue;

            const RHIBufferHandle vb = batch._vertexBuffer;
            if ( vb == 0 )
                continue;
            ctx._pCmd->setVertexBuffer( 0, vb, sizeof( RHIVertex ), 0 );

            registerMaterialBuffer( ctx, batch, pso );
            if ( bFirstItem )
            {
                commitBindlessTextureBindings( ctx );
                bFirstItem = false;
            }
            ctx._drawInstanceBase = batch._instanceBase;
            bindForDraw( ctx, pso, batch._materialCb, batch._arrMaterialTexSrv );
            ctx._pCmd->drawInstanced( pMesh->getVertexCount(), batch._instanceCount, 0, 0 );
            drawn += batch._instanceCount;
        }

        // 드로우 수는 시간 해석의 전제다 — 배치가 몇 개로 묶였는지 모르면 ms 만 봐서는
        // 무엇이 비싼지 알 수 없다.
        SW_PROFILE_COUNT( "RT.Draw.count", drawn );
        SW_PROFILE_COUNT( "RT.Draw.batches", batches.size() );

        if ( drawn == 0 )
        {
            setIdentityWorld( ctx );
            commitBindlessTextureBindings( ctx );
        }
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
            // b0 = 패스 상수(뷰/월드), b1 = 머티리얼 상수. 예전엔 둘을 한 인자에 겹쳐 실어서
            // 지오메트리가 머티리얼 버퍼를 PassCB 로 읽었다.
            if ( bInstanced )
                ctx._drawInstanceBase = batch._instanceBase;
            registerMaterialBuffer( ctx, batch, pso );
            bindForDraw( ctx, pso, batch._materialCb, batch._arrMaterialTexSrv );
            ctx._pCmd->drawIndirect( _gpuScene.getIndirectArgsBuffer(),
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
