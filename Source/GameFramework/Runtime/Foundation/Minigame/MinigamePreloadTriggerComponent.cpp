#include "pch.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigamePreloadTriggerComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"

namespace sw
{
    MinigamePreloadTriggerComponent::MinigamePreloadTriggerComponent()
        : TriggerComponent( TriggerAuthority::Local )
        , _minigame{}
    {
    }

    bool MinigamePreloadTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        (void)activator;
        MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        return pFlow != nullptr && pFlow->preload( _minigame.view() );
    }
} // namespace sw
