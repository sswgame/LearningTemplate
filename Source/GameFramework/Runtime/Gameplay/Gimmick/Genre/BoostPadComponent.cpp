#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/BoostPadComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"

#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"

namespace sw
{
    namespace
    {
        struct BoostPadComponentInternal
        {
            static bool canAccept( const OverlapInfo& overlap, const TagContainer& requiredTags )
            {
                return overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE &&
                       ( requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( requiredTags ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BoostPadComponent::BoostPadComponent()
        : _duration{ 1.0f }
        , _strength{ 1.5f }
        , _requiredTags{}
        , _boostCount{ 0 }
    {
    }

    void BoostPadComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&BoostPadComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void BoostPadComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        if ( BoostPadComponentInternal::canAccept( overlap, _requiredTags ) == false )
            return;
        ++_boostCount;
        GimmickBoostEvent event;
        event._target   = overlap._pOther->getHandle();
        event._duration = _duration;
        event._strength = _strength;
        GameEventUtil::send( event );
    }
} // namespace sw
