#include "pch.h"

#include "GameFramework/Base/Actor/Combat/HealthListenerComponent.h"

#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    HealthListenerComponent::HealthListenerComponent() = default;

    HealthListenerComponent::~HealthListenerComponent() = default;

    void HealthListenerComponent::broadcast( const GameObject& owner, const HealthChangedEvent& event )
    {
        owner.forEachComponentOfType<HealthListenerComponent>( [&event]( HealthListenerComponent* pListener )
        { pListener->onHealthChanged( event ); } );
    }
} // namespace sw
