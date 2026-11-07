#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

namespace sw
{
    SequenceTriggerComponent::SequenceTriggerComponent()
        : _sequence{}
    {
    }

    bool SequenceTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        (void)activator;
        const GameObject*        pOwner    = getOwner();
        GameObjectManager*       pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*              pSequence = pManager != nullptr ? pManager->resolveGameObject( _sequence ) : nullptr;
        SequencePlayerComponent* pPlayer   = pSequence != nullptr ? pSequence->getComponent<SequencePlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
        {
            SW_LOG_ERROR_ID( "SequenceTrigger.SequenceObjectNoPlayer", "Sequence trigger '%#' points at %# - nothing plays", pOwner != nullptr ? pOwner->getName().c_str() : "?",
                             pSequence == nullptr ? "no object" : "an object without a SequencePlayerComponent" );
            return false;
        }
        pPlayer->play();
        return true;
    }
} // namespace sw
