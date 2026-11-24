#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSDragBoxComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/Client/RTSCommandInputComponent.h"

namespace sw
{
    RTSDragBoxComponent::RTSDragBoxComponent()
    {
        setCanEverTick( true );
    }

    void RTSDragBoxComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*    pOwner = getOwner();
        MeshComponent* pMesh  = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        if ( pMesh == nullptr )
            return;
        const GameObject* const               pDirectorObject = context.getManager().resolveGameObject( getGameMode() );
        const RTSCommandInputComponent* const pCommandInput   = pDirectorObject != nullptr ? pDirectorObject->getTickSafeComponent<RTSCommandInputComponent>() : nullptr;
        float3                                center{};
        float3                                scale{};
        const bool                            bShown = pCommandInput != nullptr && pCommandInput->findDragBox( center, scale );
        if ( pMesh->isVisible() != bShown )
            pMesh->setVisible( bShown );
        if ( bShown == false )
            return;
        pMesh->setLocalPosition( context.getManager().getWorldOrigin().toLocal( center ) ); // 끌기 자리는 RTS 월드의 절대 자리다
        pMesh->setLocalScale( scale );
    }
} // namespace sw
