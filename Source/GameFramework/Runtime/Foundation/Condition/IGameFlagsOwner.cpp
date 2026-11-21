#include "pch.h"

#include "GameFramework/Runtime/Foundation/Condition/IGameFlagsOwner.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    GameFlags* IGameFlagsOwner::findOpen( const GameObjectManager* pManager )
    {
        if ( pManager == nullptr )
            return nullptr;
        GameFlags*       pFound      = nullptr;
        const Component* pFoundOwner = nullptr;
        const auto       view        = pManager->getComponentRegistry().getAllInterfaces<IGameFlagsOwner>();
        for ( size_t index = 0; index < view.size(); ++index )
        {
            IGameFlagsOwner* pOwner = view[index];
            GameFlags*       pFlags = pOwner != nullptr ? pOwner->findOpenFlags() : nullptr;
            if ( pFlags == nullptr )
                continue;
            const Component* pComponent = view.getComponent( index );
            if ( pFound != nullptr )
            {
                SW_LOG_ERROR_ID( "GameFlagsOwner.SeveralOpen", "the scene has more than one open world flags owner ('%#', '%#') - world flags are one per scene, nothing is read or written",
                                 pFoundOwner->getOwnerName().c_str(), pComponent->getOwnerName().c_str() );
                return nullptr;
            }
            pFound      = pFlags;
            pFoundOwner = pComponent;
        }
        return pFound;
    }
} // namespace sw
