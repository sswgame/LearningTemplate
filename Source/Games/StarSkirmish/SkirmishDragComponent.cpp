#include "pch.h"

#include "Games/StarSkirmish/SkirmishDragComponent.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/StarSkirmish/SkirmishDirectorComponent.h"

namespace sw
{
    SkirmishDragComponent::SkirmishDragComponent()
        : _director{}
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
        const SkirmishDirectorComponent* pDirector = GameDirectorComponent::resolve<SkirmishDirectorComponent>( *pManager, _director );
        float3                           center{};
        float3                           scale{};
        const bool                       bShown = pDirector != nullptr && pDirector->findDragBox( center, scale );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        pMesh->setLocalPosition( center );
        pMesh->setLocalScale( scale );
    }
} // namespace sw
