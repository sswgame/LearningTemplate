#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/LaunchPadComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"

namespace sw
{
    LaunchPadComponent::LaunchPadComponent()
        : _launchVelocity{ 0.0f, 12.0f, 0.0f }
        , _requiredTags{}
        , _launchCount{ 0 }
    {
    }

    void LaunchPadComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&LaunchPadComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void LaunchPadComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        const bool bAccepted = overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE &&
                               ( _requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( _requiredTags ) );
        if ( bAccepted )
            launch( *overlap._pOther );
    }

    void LaunchPadComponent::launch( GameObject& target )
    {
        ++_launchCount;
        // 캐릭터 컨트롤러는 쏜 속도를 그대로 받는다. 수직 속도는 덮어쓰고, 수평 속도는 다시 디딜 때까지 유지한다.
        CharacterControllerComponent* pController = target.getComponent<CharacterControllerComponent>();
        if ( pController != nullptr )
            pController->launch( _launchVelocity );
        GimmickLaunchEvent event;
        event._target   = target.getHandle();
        event._source   = getOwner() != nullptr ? getOwner()->getHandle() : GameObjectHandle{};
        event._velocity = _launchVelocity;
        GameEventUtil::send( event );
    }
} // namespace sw
