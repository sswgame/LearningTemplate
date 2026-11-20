#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameTriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameFlow.h"

namespace sw
{
    MinigameTriggerComponent::MinigameTriggerComponent()
        : ActivatorVolumeComponent( ActivatorAuthority::Server ) // 들어가기는 게임 상태를 바꾼다 — 서버(와 혼자 하는 게임)만 판정한다
        , _minigame{}
    {
    }

    bool MinigameTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        (void)activator;
        MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        if ( _minigame.empty() || pFlow == nullptr )
        {
            const GameObject* const pOwner = getOwner();
            SW_LOG_ERROR_ID( "MinigameTrigger.NothingToEnter", "Minigame trigger '%#' %# - nothing is entered", pOwner != nullptr ? pOwner->getName().c_str() : "?",
                             _minigame.empty() ? "has no minigame name" : "is in a world without a running game" );
            return false;
        }
        pFlow->requestEnter( _minigame.view() ); // 틱 뒤 작업이다 — 스냅숏 · 씬 요청은 다음 게임 프레임 앞에서 한다
        return true;
    }
} // namespace sw
