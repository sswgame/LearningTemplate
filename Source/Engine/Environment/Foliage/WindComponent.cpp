#include "pch.h"

#include "Engine/Environment/Foliage/WindComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    WindComponent::WindComponent()
        : _direction{ 0.6f }
        , _strength{ 0.35f }
        , _gustStrength{ 0.25f }
        , _gustFrequency{ 0.25f }
        , _swayFrequency{ 1.2f }
    {
    }

    WindSettings WindComponent::makeSettings() const
    {
        WindSettings settings;
        settings._direction     = float2{ MathUtil::cos( _direction ), MathUtil::sin( _direction ) };
        settings._strength      = _strength;
        settings._gustStrength  = _gustStrength;
        settings._gustFrequency = _gustFrequency;
        settings._swayFrequency = _swayFrequency;
        return settings;
    }

    void WindComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<WindComponent>( this );
    }

    void WindComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<WindComponent>( this );
        Component::onUnregister( manager );
    }

    bool WindComponent::findWind( const GameObjectManager& manager, WindSettings& outSettings )
    {
        outSettings = WindSettings{};
        for ( const WindComponent* pWind : manager.getComponentRegistry().getAll<WindComponent>() )
        {
            if ( pWind->isPendingDestroy() || pWind->isActive() == false )
                continue;
            outSettings = pWind->makeSettings();
            return true;
        }
        return false;
    }
} // namespace sw
