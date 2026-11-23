#include "pch.h"

#include "GameFramework/Client/HUD/MinimapMarkerComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    MinimapMarkerComponent::MinimapMarkerComponent()
        : _color{ 1.0f, 0.82f, 0.25f, 1.0f }
        , _size{ 8.0f }
        , _bClampToEdge{ false }
    {
    }

    void MinimapMarkerComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<MinimapMarkerComponent>( this );
    }

    void MinimapMarkerComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<MinimapMarkerComponent>( this );
        Component::onUnregister( manager );
    }
} // namespace sw
