#include "pch.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlagsOwnerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
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

    GameFlags* GameFlagsOwnerComponent::findOpen( const GameObjectManager* pManager )
    {
        if ( pManager == nullptr )
            return nullptr;
        GameFlags*                     pFound      = nullptr;
        const GameFlagsOwnerComponent* pFoundOwner = nullptr;
        for ( GameFlagsOwnerComponent* pOwner : pManager->getComponentRegistry().getAll<GameFlagsOwnerComponent>() )
        {
            GameFlags* pFlags = pOwner->findOpenFlags();
            if ( pFlags == nullptr )
                continue;
            if ( pFound != nullptr )
            {
                SW_LOG_ERROR_ID( "GameFlagsOwner.SeveralOpen", "the scene has more than one open world flags owner ('%#', '%#') - world flags are one per scene, nothing is read or written",
                                 pFoundOwner->getOwner() != nullptr ? pFoundOwner->getOwner()->getName().c_str() : "?", pOwner->getOwner() != nullptr ? pOwner->getOwner()->getName().c_str() : "?" );
                return nullptr;
            }
            pFound      = pFlags;
            pFoundOwner = pOwner;
        }
        return pFound;
    }
} // namespace sw
