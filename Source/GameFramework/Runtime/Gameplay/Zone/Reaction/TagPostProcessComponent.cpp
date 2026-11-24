#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Zone/Reaction/TagPostProcessComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Zone/ZoneSystem.h"

namespace sw
{
    TagPostProcessComponent::TagPostProcessComponent()
        : _tag{}
        , _postProcess{}
        , _fadeInTime{ 0.5f }
        , _fadeOutTime{ 1.0f }
        , _weight{ 0.0f }
    {
    }

    void TagPostProcessComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        (void)ZoneSystem::getOrCreate( manager );
    }

    void TagPostProcessComponent::onUnregister( GameObjectManager& manager )
    {
        // 화면이 남지 않게 덧칠을 먼저 뺀다.
        applyToDirectors( manager, 0.0f );
        _weight = 0.0f;
        ZoneSystem::releaseIfUnused( manager );
        Component::onUnregister( manager );
    }

    void TagPostProcessComponent::updateScreen( GameObjectManager& manager, float32 deltaTime )
    {
        const GameObject* const             pOwner   = getOwner();
        const AbilitySystemComponent* const pAbility = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        const bool                          bOn      = pAbility != nullptr && _tag.isValid() && pAbility->getOwnedTagCounts().hasMatchingTag( _tag );
        // 시간이 0 이면 바로 닿는다 — 0 으로 나누지 않는다.
        const float32 fadeTime = bOn ? _fadeInTime : _fadeOutTime;
        const float32 target   = bOn ? 1.0f : 0.0f;
        _weight                = fadeTime > 0.0f ? MathUtil::moveToward( _weight, target, MathUtil::max( 0.0f, deltaTime ) / fadeTime ) : target;
        applyToDirectors( manager, _weight );
    }

    void TagPostProcessComponent::applyToDirectors( GameObjectManager& manager, float32 weight ) const
    {
        const GameObject* const pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        const GameObjectHandle owner = pOwner->getHandle();
        for ( CameraDirectorComponent* pDirector : manager.getComponentRegistry().getAll<CameraDirectorComponent>() )
            pDirector->setPostProcessOverlay( owner, _postProcess, pDirector->getTarget() == owner ? weight : 0.0f );
    }
} // namespace sw
