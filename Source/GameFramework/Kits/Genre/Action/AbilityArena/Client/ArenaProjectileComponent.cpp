#include "pch.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Client/ArenaProjectileComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaGameModeComponent.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"

namespace sw
{
    ArenaProjectileComponent::ArenaProjectileComponent()
        : _visualLift{ 0.9f }
        , _projectileID{ 0 }
        , _bFinished{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ArenaProjectileComponent::bind( GameObjectHandle gameMode, uint32 projectileID, const float3& position )
    {
        bindGameMode( gameMode );
        _projectileID = projectileID;
        _bFinished    = SW_FALSE;
        placeMesh( position );
        setMeshVisible( true ); // 풀에서 다시 꺼낸 모습은 끝날 때 숨긴 그대로다
    }

    SpawnStateReset ArenaProjectileComponent::resetToSpawnState()
    {
        if ( _bFinished == SW_TRUE )
            setMeshVisible( true ); // `finish` 가 숨긴 것이다 — 프리팹의 메시는 보이는 채 스폰한다
        _projectileID = 0;
        _bFinished    = SW_FALSE;
        return SpawnStateReset::Reset;
    }

    void ArenaProjectileComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _projectileID == 0 || _bFinished == SW_TRUE )
            return;
        const ArenaGameModeComponent* pGameMode   = findGameMode<ArenaGameModeComponent>();
        const ProjectileState*        pProjectile = pGameMode != nullptr ? pGameMode->findProjectile( _projectileID ) : nullptr;
        if ( pProjectile == nullptr )
        {
            finish(); // 맞았거나 사거리 · 판 밖에서 끝났다(디렉터가 이번 프레임 `PrePhysics` 에서 뺐다)
            return;
        }
        placeMesh( pProjectile->_position );
    }

    void ArenaProjectileComponent::placeMesh( const float3& position ) const
    {
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setLocalPosition( pOwner->getManager()->getWorldOrigin().toLocal( position + float3{ 0.0f, _visualLift, 0.0f } ) ); // 탄은 절대 자리다
    }

    void ArenaProjectileComponent::setMeshVisible( bool bVisible ) const
    {
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh != nullptr )
            pMesh->setVisible( bVisible );
    }

    void ArenaProjectileComponent::finish()
    {
        // 지우지 않는다 — 뷰가 틱 밖에서 풀에 돌려받아 꺼 두었다가 다음 탄에 다시 쓴다(`ArenaViewComponent`).
        _bFinished = SW_TRUE;
        setMeshVisible( false );
    }
} // namespace sw
