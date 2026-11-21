#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    IItemReceiver* IItemReceiver::findForTarget( const GameObject& target )
    {
        IItemReceiver* pOwn = target.findInterface<IItemReceiver>();
        if ( pOwn != nullptr )
            return pOwn;
        const GameObjectManager* pManager          = target.getManager();
        const GameObject*        pControllerObject = pManager != nullptr ? PawnComponent::findControllerObject( *pManager, target ) : nullptr;
        return pControllerObject != nullptr ? pControllerObject->findInterface<IItemReceiver>() : nullptr;
    }
} // namespace sw
