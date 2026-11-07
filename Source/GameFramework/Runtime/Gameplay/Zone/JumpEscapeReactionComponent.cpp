#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/JumpEscapeReactionComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

namespace sw
{
    JumpEscapeReactionComponent::JumpEscapeReactionComponent()
        : _effect{}
        , _stacksPerJump{ 1 }
        , _blockedTag{ "State.Engulfed"_tag }
        , _escapeCount{ 0 }
        , _bWasGrounded{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void JumpEscapeReactionComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager == nullptr )
            return;
        pManager->executeOrDeferPostTickOnComponent( *this, []( JumpEscapeReactionComponent& self, GameObjectManager& /*manager*/ )
        { self.updateJumpEscape(); } );
    }

    void JumpEscapeReactionComponent::updateJumpEscape()
    {
        GameObject* const                         pOwner      = getOwner();
        const CharacterControllerComponent* const pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        AbilityComponent* const                   pAbility    = pOwner != nullptr ? pOwner->getComponent<AbilityComponent>() : nullptr;
        if ( pController == nullptr || pAbility == nullptr )
            return;
        const bool bGrounded = pController->isGrounded();
        const bool bJumped   = _bWasGrounded == SW_TRUE && bGrounded == false && pController->getVelocity()._y > 0.0f;
        _bWasGrounded        = bGrounded ? SW_TRUE : SW_FALSE;
        const bool bBlocked  = _blockedTag.isValid() && pAbility->getOwnedTagCounts().hasMatchingTag( _blockedTag );
        if ( bJumped == false || bBlocked )
            return;
        const ActiveEffectHandle handle = pAbility->findActiveEffectByID( _effect );
        if ( handle.isValid() && pAbility->removeActiveEffect( handle, _stacksPerJump ) )
            ++_escapeCount;
    }
} // namespace sw
