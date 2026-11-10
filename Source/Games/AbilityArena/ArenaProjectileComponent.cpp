#include "pch.h"

#include "Games/AbilityArena/ArenaProjectileComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"

#include "Games/AbilityArena/ArenaGameModeComponent.h"

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
            pMesh->setLocalPosition( position + float3{ 0.0f, _visualLift, 0.0f } );
    }

    void ArenaProjectileComponent::finish()
    {
        _bFinished                        = SW_TRUE;
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        // 틱에서 부른다 — 지우기는 명령 버퍼로 틱 뒤에 한다.
        if ( pManager != nullptr )
            pManager->executeOrDeferPostTickOnComponent( *this, []( ArenaProjectileComponent& self, GameObjectManager& sceneManager )
            { sceneManager.destroyGameObject( self.getOwner() ); } );
    }
} // namespace sw
