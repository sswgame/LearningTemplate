#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceTriggerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Foundation/Framework/Experience/ExperienceManager.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    ExperienceTriggerComponent::ExperienceTriggerComponent()
        : ActivatorVolumeComponent( ActivatorAuthority::Server ) // 들어가기는 게임 상태를 바꾼다 — 서버(와 혼자 하는 게임)만 판정한다
        , _experience{}
    {
    }

    bool ExperienceTriggerComponent::onVolumeActivated( const GameObject& activator )
    {
        (void)activator;
        ExperienceManager* const pManager = game::findSubsystem<ExperienceManager>();
        if ( _experience.empty() || pManager == nullptr )
        {
            const GameObject* const pOwner = getOwner();
            SW_LOG_ERROR_ID( "ExperienceTrigger.NothingToEnter", "Experience trigger '%#' %# - nothing is entered", pOwner != nullptr ? pOwner->getName().c_str() : "?",
                             _experience.empty() ? "has no experience name" : "is in a world without a running game" );
            return false;
        }
        pManager->requestEnter( _experience.view() ); // 틱 뒤 작업이다 — 허브 스냅숏 · 씬 요청은 다음 게임 프레임 앞에서 한다
        return true;
    }
} // namespace sw
