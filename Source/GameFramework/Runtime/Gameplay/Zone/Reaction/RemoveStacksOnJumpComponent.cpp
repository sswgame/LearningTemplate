#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/Reaction/RemoveStacksOnJumpComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"

#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneSystem.h"

namespace sw
{
    RemoveStacksOnJumpComponent::RemoveStacksOnJumpComponent()
        : _effect{}
        , _stacksPerJump{ 1 }
        , _blockedTag{}
        , _escapeCount{ 0 }
        , _bWasGrounded{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void RemoveStacksOnJumpComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        (void)ZoneSystem::getOrCreate( manager );
    }

    void RemoveStacksOnJumpComponent::onUnregister( GameObjectManager& manager )
    {
        ZoneSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void RemoveStacksOnJumpComponent::updateRemoveStacksOnJump()
    {
        GameObject* const                         pOwner      = getOwner();
        const CharacterControllerComponent* const pController = pOwner != nullptr ? pOwner->getComponent<CharacterControllerComponent>() : nullptr;
        AbilitySystemComponent* const             pAbility    = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        if ( pController == nullptr || pAbility == nullptr )
            return;
        const bool bGrounded = pController->isGrounded();
        const bool bJumped   = _bWasGrounded == SW_TRUE && bGrounded == false && pController->getVelocity()._y > 0.0f;
        _bWasGrounded        = bGrounded ? SW_TRUE : SW_FALSE;
        const bool bBlocked  = _blockedTag.isValid() && pAbility->getOwnedTagCounts().hasMatchingTag( _blockedTag );
        if ( bJumped == false || bBlocked )
            return;
        const ActiveEffectHandle handle = pAbility->findActiveEffectByID( _effect.key() );
        if ( handle.isValid() && pAbility->removeActiveEffect( handle, _stacksPerJump ) )
            ++_escapeCount;
    }
} // namespace sw
