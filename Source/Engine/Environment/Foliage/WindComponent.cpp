#include "pch.h"

#include "Engine/Environment/Foliage/WindComponent.h"

#include "Core/Math/MathUtil.h"

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

    bool WindComponent::findWind( const GameObjectManager& manager, WindSettings& outSettings )
    {
        outSettings = WindSettings{};
        bool bFound = false;
        manager.forEachComponentOfType<WindComponent>( [&bFound, &outSettings]( WindComponent* pWind )
        {
            if ( bFound || pWind->isActive() == false )
                return;
            outSettings = pWind->makeSettings();
            bFound      = true;
        } );
        return bFound;
    }
} // namespace sw
