#include "pch.h"

#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"

#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    GameObject* PlayerComponentUtil::findControllerObject( GameObjectManager& manager, const GameObject& pawnObject )
    {
        const PawnComponent*       pPawn       = pawnObject.getComponent<PawnComponent>();
        const ControllerComponent* pController = pPawn != nullptr ? manager.resolveComponent( pPawn->getController() ) : nullptr;
        return pController != nullptr ? pController->getOwner() : nullptr;
    }
} // namespace sw
