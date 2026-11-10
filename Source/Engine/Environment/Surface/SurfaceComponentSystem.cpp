#include "pch.h"

#include "Engine/Environment/Surface/SurfaceComponentSystem.h"

#include "Engine/Environment/Surface/GroundSurfaceComponent.h"
#include "Engine/Environment/Surface/SurfaceDeformMeshComponent.h"
#include "Engine/Environment/Surface/SurfaceStampComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"

namespace sw
{
    // 예전 틱 뒤 큐와 같은 자리다 — PrePhysics 틱이 남긴 것을 적용한 뒤(구조 변경 · 트랜스폼 쓰기 · 틱 뒤 큐), 물리 앞.
    SW_SCENE_SERVICE( SurfaceComponentSystem, ApplyPrePhysics, makeSceneFrameAccess( SceneDataKindSet{}, SceneDataKindSet{}, SceneDataKindSet{ SceneDataKind::Structure, SceneDataKind::Transform } ) )

    SurfaceComponentSystem::SurfaceComponentSystem( GameObjectManager& manager )
        : _pManager{ &manager }
        , _listHandleScratch{}
    {
    }

    void SurfaceComponentSystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        GameObjectManager& manager = *_pManager;
        // 발 아래 읽기가 먼저다 — 이동 계수가 이번 물리에 들고, 지역 접촉이 먼저 읽었으면 다시 읽지 않는다(`sampleGroundOnce`).
        manager.forEachActiveRegistered<GroundSurfaceComponent>( _listHandleScratch, [&manager]( GroundSurfaceComponent& ground )
        { ground.finishFrameSample( manager ); } );
        manager.forEachActiveRegistered<SurfaceStampComponent>( _listHandleScratch, [&manager]( SurfaceStampComponent& stamp )
        { stamp.applyStamp( manager ); } );
        manager.forEachActiveRegistered<SurfaceDeformMeshComponent>( _listHandleScratch, [&manager]( SurfaceDeformMeshComponent& mesh )
        { mesh.bindMaterial( manager ); } );
    }
} // namespace sw
