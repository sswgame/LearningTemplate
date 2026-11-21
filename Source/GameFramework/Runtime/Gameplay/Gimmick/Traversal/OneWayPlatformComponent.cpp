#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Traversal/OneWayPlatformComponent.h"

namespace sw
{
    OneWayPlatformComponent::OneWayPlatformComponent()
        : _normal{ 0.0f, 1.0f, 0.0f }
        , _bDropThrough{ true }
    {
    }

    bool OneWayPlatformComponent::isSolidFor( const float3& velocity, bool bDropRequested ) const
    {
        if ( _bDropThrough && bDropRequested )
            return false;
        return _normal.dot( velocity ) < 0.0f;
    }
} // namespace sw
