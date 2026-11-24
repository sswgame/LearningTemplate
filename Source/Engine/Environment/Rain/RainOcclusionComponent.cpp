#include "pch.h"

#include "Engine/Environment/Rain/RainOcclusionComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    RainOcclusionComponent::RainOcclusionComponent()
        : _extent{ 64.0f }
        , _heightAbove{ 60.0f }
        , _depthBelow{ 40.0f }
        , _depthBias{ 0.15f }
        , _normalOffset{ 0.25f }
    {
    }

    RainOcclusionSettings RainOcclusionComponent::makeSettings() const
    {
        RainOcclusionSettings settings;
        settings._extent       = _extent;
        settings._heightAbove  = _heightAbove;
        settings._depthBelow   = _depthBelow;
        settings._depthBias    = _depthBias;
        settings._normalOffset = _normalOffset;
        return settings;
    }

    void RainOcclusionComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<RainOcclusionComponent>( this );
    }

    void RainOcclusionComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<RainOcclusionComponent>( this );
        Component::onUnregister( manager );
    }

    bool RainOcclusionComponent::findRainOcclusion( const GameObjectManager& manager, RainOcclusionSettings& outSettings )
    {
        outSettings                          = RainOcclusionSettings{};
        const RainOcclusionComponent* pFound = manager.getComponentRegistry().findUnique<RainOcclusionComponent>( []( const RainOcclusionComponent& rain )
        { return rain.isPendingDestroy() == false && rain.isActive(); } );
        if ( pFound == nullptr )
            return false;
        outSettings = pFound->makeSettings();
        return true;
    }
} // namespace sw
