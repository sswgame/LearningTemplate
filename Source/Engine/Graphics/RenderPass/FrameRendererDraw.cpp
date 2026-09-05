#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/FrameRendererUtil.h"
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
        ctx._resourceRegistry.registerBuffer( hashed_string( "SwInstances" ),
                                              _gpuScene.getInstanceBuffer(), _gpuScene.getInstanceSrv() );
    }

    void FrameRenderer::bindForDraw( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex materialCb )
    {
        if ( _pDevice == nullptr || ctx._pCmd == nullptr )
            return;

        ctx._passValues.setMatrix( hashed_string( "g_World" ), ctx._world );

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

        const EngineConstantBufferSlot engineCb{ ctx._passCb, ctx._passCbIndex };
        ShaderBindingBinder::bindGraphics( *_pDevice, *ctx._pCmd, *pLayout, ctx._resourceRegistry, ctx._passValues,
                                           engineCb, materialCb, _pDevice->supportsNativeBindlessSampling() );
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

        if ( _gpuScene.getInstances().empty() == false )
        {
            drawGpuSceneMeshes( ctx, pso, cbIndex, bTransparentPass );
            return;
        }

        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );

        GameObjectManager* pObjects = ( _pScene != nullptr ) ? _pScene->getObjectManager() : nullptr;
        if ( pObjects == nullptr )
            return;

        if ( _bSceneTransformsFlushed == 0 )
        {
            pObjects->flushSceneTransforms();
            _bSceneTransformsFlushed = 1;
        }

        const utf8* pPassTypeForMat = bTransparentPass
                                        ? FrameRendererUtil::PassType::kTransparent
                                        : FrameRendererUtil::PassType::kForwardOpaque;
        const utf8* pDefaultShader  = engine::getEngineData()._shaderForwardLit.c_str();

        uint32 drawn{ 0 };
        if ( pObjects != nullptr )
        {
            struct SceneMeshDrawItem
            {
                uint64                 _sortKey{ 0 };
                shared_ptr<Mesh>       _mesh;
                MaterialInstance*      _pMaterialInstance{ nullptr };
                RHIPipelineStateHandle _pso{ 0 };
                RHIDescriptorIndex     _cbIndex{ 0 };
                float4x4               _world;
            };

            vector<SceneMeshDrawItem> listDrawItem;
            pObjects->forEachGameObject( [&]( GameObject* pObj )
            {
                if ( pObj == nullptr || pObj->isActiveInHierarchy() == false )
                    return;

                pObj->forEachComponent( [&]( Component* pComp )
                {
                    MeshComponent* pMeshComp = castTo<MeshComponent>( pComp );
                    if ( pMeshComp == nullptr || pMeshComp->isVisible() == false )
                        return;

                    const bool bTransparent = pMeshComp->getBlendMode() == RHIBlendMode::Transparent;
                    if ( bTransparent != bTransparentPass )
                        return;

                    const shared_ptr<Mesh>& mesh = pMeshComp->getMesh();
                    if ( mesh == nullptr || mesh->getVertexCount() == 0 )
                        return;
                    if ( mesh->upload( _pDevice ) == false )
                        return;

                    RHIPipelineStateHandle              drawPso          = pso;
                    Material*                           pMaterial        = pMeshComp->getMaterial();
                    const shared_ptr<MaterialInstance>& materialInstance = pMeshComp->getMaterialInstance();
                    if ( pMaterial != nullptr || materialInstance != nullptr )
                    {
                        const RHIPipelineStateHandle matPso = getOrCreateMaterialPassPso(
                            pPassTypeForMat, pDefaultShader, true, pMaterial, materialInstance.get(),
                            1, nullptr, bTransparentPass, bTransparentPass == false );
                        if ( matPso != 0 )
                            drawPso = matPso;
                    }

                    RHIDescriptorIndex drawCb = kInvalidDescriptorIndex;
                    if ( materialInstance != nullptr )
                        drawCb = materialInstance->getDescriptorIndex();
                    else if ( pMaterial != nullptr )
                        drawCb = pMaterial->getDescriptorIndex();

                    const uint64 vbId    = static_cast<uint64>( mesh->getVertexBuffer() ) & 0xFFFF;
                    const uint64 sortKey = ( static_cast<uint64>( drawPso ) << 32 ) | ( static_cast<uint64>( drawCb ) << 16 ) | vbId;

                    SceneMeshDrawItem item{};
                    item._sortKey           = sortKey;
                    item._mesh              = mesh;
                    item._pMaterialInstance = materialInstance.get();
                    item._pso               = drawPso;
                    item._cbIndex           = drawCb;
                    item._world             = pMeshComp->getWorldMatrix();
                    listDrawItem.push_back( std::move( item ) );
                } );
            } );

            if ( listDrawItem.empty() == false )
            {
                std::sort( listDrawItem.begin(), listDrawItem.end(),
                           []( const SceneMeshDrawItem& lhs, const SceneMeshDrawItem& rhs )
                { return lhs._sortKey < rhs._sortKey; } );

                RHIPipelineStateHandle lastPso    = 0;
                RHIBufferHandle        lastVb     = 0;
                bool                   bFirstItem = true;

                for ( const auto& item : listDrawItem )
                {
                    if ( item._pso != lastPso )
                    {
                        if ( item._pso != 0 )
                            ctx._pCmd->setPipelineState( item._pso );
                        lastPso = item._pso;
                    }

                    if ( bFirstItem || ctx._world != item._world )
                    {
                        ctx._world = item._world;
                        commitBindlessTextureBindings( ctx );
                        bFirstItem = false;
                    }

                    if ( item._pMaterialInstance != nullptr )
                        item._pMaterialInstance->applyToGpu( _pDevice );

                    const RHIBufferHandle vb = item._mesh->getVertexBuffer();
                    if ( vb != lastVb )
                    {
                        ctx._pCmd->setVertexBuffer( 0, vb, sizeof( RHIVertex ), 0 );
                        lastVb = vb;
                    }

                    bindForDraw( ctx, item._pso, item._cbIndex );
                    ctx._pCmd->draw( item._mesh->getVertexCount(), 0 );
                    ++drawn;
                }
            }
        }

        if ( drawn == 0 )
        {
            setIdentityWorld( ctx );
            commitBindlessTextureBindings( ctx );
        }
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
        const vector<GpuInstance>& listInstances = _gpuScene.getInstances();

        if ( pso != 0 )
            ctx._pCmd->setPipelineState( pso );

        // 언리얼 GPUScene 방식: VS 가 per-instance world 를 구조버퍼에서 읽을 수 있으면 배치당 drawInstanced 한 번.
        // 아니면(DX11/Vulkan) 드로우당 g_World 를 갱신하는 폴백.
        const bool bInstanced = _pDevice->supportsInstancedSceneDraw() &&
                                _gpuScene.getInstanceSrv() != kInvalidDescriptorIndex;
        if ( bInstanced )
            registerInstanceBuffer( ctx );

        uint32 drawn{ 0 };
        bool   bFirstItem = true;
        for ( const GpuMeshBatch& batch : batches )
        {
            Mesh* pMesh = batch._pMesh;
            if ( pMesh == nullptr || pMesh->getVertexCount() == 0 )
                continue;
            if ( pMesh->upload( _pDevice ) == false )
                continue;

            const RHIBufferHandle vb = pMesh->getVertexBuffer();
            if ( vb == 0 )
                continue;
            ctx._pCmd->setVertexBuffer( 0, vb, sizeof( RHIVertex ), 0 );

            const RHIDescriptorIndex drawCb = batch._materialCb;
            if ( batch._pMaterialInstance != nullptr )
                batch._pMaterialInstance->applyToGpu( _pDevice );

            if ( bInstanced )
            {
                if ( bFirstItem )
                {
                    commitBindlessTextureBindings( ctx );
                    bFirstItem = false;
                }
                ctx._passValues.setUint( hashed_string( "g_InstanceBase" ), batch._instanceBase );
                bindForDraw( ctx, pso, drawCb );
                ctx._pCmd->drawInstanced( pMesh->getVertexCount(), batch._instanceCount, 0, 0 );
                drawn += batch._instanceCount;
                continue;
            }

            for ( uint32 instanceIndex = 0; instanceIndex < batch._instanceCount; ++instanceIndex )
            {
                const uint32 globalIndex = batch._instanceBase + instanceIndex;
                if ( globalIndex >= listInstances.size() )
                    break;
                const GpuInstance& inst = listInstances[globalIndex];
                if ( bFirstItem || ctx._world != inst._world )
                {
                    ctx._world = inst._world;
                    commitBindlessTextureBindings( ctx );
                    bFirstItem = false;
                }
                bindForDraw( ctx, pso, drawCb );
                ctx._pCmd->draw( pMesh->getVertexCount(), 0 );
                ++drawn;
            }
        }

        if ( drawn == 0 )
        {
            setIdentityWorld( ctx );
            commitBindlessTextureBindings( ctx );
        }
    }

    void FrameRenderer::drawGpuBatches( FramePassContext& ctx, RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass )
    {
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
                ctx._passValues.setUint( hashed_string( "g_InstanceBase" ), batch._instanceBase );
            bindForDraw( ctx, pso, batch._materialCb );
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
