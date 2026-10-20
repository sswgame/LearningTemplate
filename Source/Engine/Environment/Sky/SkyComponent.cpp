#include "pch.h"

#include "Engine/Environment/Sky/SkyComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    SkyComponent::SkyComponent()
        : _zenithColor{ 0.25f, 0.45f, 0.85f }
        , _horizonColor{ 0.75f, 0.82f, 0.90f }
        , _groundColor{ 0.30f, 0.27f, 0.24f }
        , _intensity{ 1.0f }
        , _horizonExponent{ 4.0f }
    {
    }

    SkyGradient SkyComponent::makeGradient() const
    {
        SkyGradient gradient;
        gradient._zenithColor     = _zenithColor;
        gradient._horizonColor    = _horizonColor;
        gradient._groundColor     = _groundColor;
        gradient._intensity       = _intensity;
        gradient._horizonExponent = _horizonExponent;
        return gradient;
    }

    void SkyComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<SkyComponent>( this );
    }

    void SkyComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SkyComponent>( this );
        Component::onUnregister( manager );
    }

    bool SkyComponent::findSky( const GameObjectManager& manager, SkyGradient& outGradient )
    {
        outGradient = SkyGradient{};
        for ( const SkyComponent* pSky : manager.getComponentRegistry().getAll<SkyComponent>() )
        {
            if ( pSky->isPendingDestroy() || pSky->isActive() == false )
                continue;
            outGradient = pSky->makeGradient();
            return true;
        }
        return false;
    }
} // namespace sw
