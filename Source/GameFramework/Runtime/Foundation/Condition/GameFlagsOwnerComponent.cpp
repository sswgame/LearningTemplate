#include "pch.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlagsOwnerComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    void GameFlagsOwnerComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<GameFlagsOwnerComponent>( this );
    }

    void GameFlagsOwnerComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<GameFlagsOwnerComponent>( this );
        Component::onUnregister( manager );
    }

    GameFlags* GameFlagsOwnerComponent::findFirstOpen( const GameObjectManager* pManager )
    {
        if ( pManager == nullptr )
            return nullptr;
        for ( GameFlagsOwnerComponent* pOwner : pManager->getComponentRegistry().getAll<GameFlagsOwnerComponent>() )
        {
            GameFlags* pFlags = pOwner->findOpenFlags();
            if ( pFlags != nullptr )
                return pFlags;
        }
        return nullptr;
    }
} // namespace sw
