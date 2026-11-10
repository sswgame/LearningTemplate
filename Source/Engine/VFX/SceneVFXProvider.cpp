#include "pch.h"

#include "Engine/VFX/SceneVFXProvider.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/VFX/VFXComponent.h"
#include "Engine/VFX/VFXEffectDef.h"

namespace sw
{
    SW_SCENE_SERVICE( SceneVFXProvider, FlushEnd, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    SceneVFXProvider::SceneVFXProvider( GameObjectManager& manager )
        : _manager{ manager }
    {
    }

    void SceneVFXProvider::contributeLights( const SceneLightPhase phase, vector<GPULight>& inoutList ) const
    {
        if ( phase != SceneLightPhase::Particles )
            return;
        // 파티클 라이트(점광, 그림자 없음)는 라이트 컴포넌트 뒤 · 2D 가림막 앞에 붙는다. 그리드는 종류를 묻지 않고 같은 목록에서 만든다(LightGrid).
        for ( const VFXComponent* pVFX : _manager.getComponentRegistry().getAll<VFXComponent>() )
        {
            if ( pVFX != nullptr && pVFX->isPendingDestroy() == false )
                pVFX->appendGPULights( inoutList );
        }
    }

    void SceneVFXProvider::contributeParticles( ISceneParticleSink& sink ) const
    {
        for ( const VFXComponent* pVFX : _manager.getComponentRegistry().getAll<VFXComponent>() )
        {
            const bool bHidden = pVFX->isPendingDestroy() || pVFX->isActive() == false;
            if ( bHidden )
                continue;
            const float4x4 world     = pVFX->getWorldMatrix();
            const float32  sizeScale = world.getMaximumAxisScale();
            for ( uint32 emitterIndex = 0; emitterIndex < pVFX->getEmitterCount(); ++emitterIndex )
            {
                // 메시 방출기는 메시 인스턴스 배치로 메시 경로를 탄다(`VFXComponent`). 띠는 이웃 둘이 있어야 한 마디다.
                // GPU 방출기는 입자 수를 GT 가 모른다 — 재생 중이면 싣고, 그릴 입자 수(띠는 마디 수)는 컴퓨트가 간접 인자에 쓴다.
                const VFXEmitterDef&        def      = pVFX->getEmitterDef( emitterIndex );
                const bool                  bRibbon  = def._renderMode == VFXRenderMode::Ribbon;
                const bool                  bGPU     = pVFX->getEmitterSimulationTarget( emitterIndex ) == VFXSimulationTarget::GPU;
                const VFXGPUEmitterRequest* pRequest = bGPU ? pVFX->findEmitterGPURequest( emitterIndex ) : nullptr;
                if ( def._renderMode == VFXRenderMode::Mesh )
                {
                    // GPU 메시 방출기는 그리기가 아니라 인스턴스 칸 목록을 싣는다 — 컴퓨트가 그 칸에 쓰고 메시 경로가 그린다. 재생 중이 아니거나 배치가 없으면 싣지 않는다.
                    const MeshInstanceBatch* pBatch = bGPU ? pVFX->findEmitterMeshBatch( emitterIndex ) : nullptr;
                    if ( pRequest != nullptr && pBatch != nullptr )
                        sink.addGPUMeshEmitter( *pRequest, *pBatch, world, pVFX->getEmitterMeshRadius( emitterIndex ) );
                    continue;
                }
                SceneParticleBatchDesc desc{};
                desc._world       = world;
                desc._texturePath = hashed_string( def._texturePath.c_str() );
                desc._sizeScale   = sizeScale;
                desc._bAdditive   = def._blendMode == VFXBlendMode::Additive ? SW_TRUE : SW_FALSE;
                desc._bRibbon     = bRibbon ? SW_TRUE : SW_FALSE;
                if ( bGPU )
                {
                    if ( pRequest != nullptr )
                        sink.addGPUParticles( *pRequest, desc );
                    continue;
                }
                const shared_ptr<const vector<GPUParticle>>& pListParticle = pVFX->getEmitterParticles( emitterIndex );
                const size_t                                 minParticles  = bRibbon ? 2u : 1u;
                if ( pListParticle != nullptr && pListParticle->size() >= minParticles )
                    sink.addCPUParticles( pListParticle, desc );
            }
        }
    }
} // namespace sw
