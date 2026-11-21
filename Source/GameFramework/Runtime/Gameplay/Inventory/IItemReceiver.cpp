#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"

#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    IItemReceiver* IItemReceiver::findForTarget( const GameObject& target )
    {
        IItemReceiver* pOwn = target.findInterface<IItemReceiver>();
        if ( pOwn != nullptr )
            return pOwn;
        const GameObjectManager*   pManager         = target.getManager();
        const PawnComponent*       pPawn            = target.getComponent<PawnComponent>();
        const ControllerComponent* pController      = pManager != nullptr && pPawn != nullptr ? pManager->resolveComponent( pPawn->getController() ) : nullptr;
        const GameObject*          pControllerOwner = pController != nullptr ? pController->getOwner() : nullptr;
        return pControllerOwner != nullptr ? pControllerOwner->findInterface<IItemReceiver>() : nullptr;
    }
} // namespace sw
