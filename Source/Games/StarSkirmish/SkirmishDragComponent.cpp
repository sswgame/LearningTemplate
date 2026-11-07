#include "pch.h"

#include "Games/StarSkirmish/SkirmishDragComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Rendering/MeshComponent.h"

#include "Games/StarSkirmish/SkirmishGameModeComponent.h"

namespace sw
{
    SkirmishDragComponent::SkirmishDragComponent()
        : _gameMode{}
    {
        setCanEverTick( true );
    }

    void SkirmishDragComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 디렉터(PrePhysics)가 이 프레임의 끌기를 정한 뒤에 읽는다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void SkirmishDragComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*     pMesh    = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr )
            return;
        const SkirmishGameModeComponent* pGameMode = GameModeComponent::resolve<SkirmishGameModeComponent>( *pManager, _gameMode );
        float3                           center{};
        float3                           scale{};
        const bool                       bShown = pGameMode != nullptr && pGameMode->findDragBox( center, scale );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        pMesh->setLocalPosition( center );
        pMesh->setLocalScale( scale );
    }
} // namespace sw
