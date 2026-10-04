#include "pch.h"

#include "Engine/Object/Component/Audio/AudioReverbZoneComponent.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneAudio.h"

namespace sw
{
    AudioReverbZoneComponent::AudioReverbZoneComponent()
        : _snapshot{}
        , _halfExtents{ 5.0f, 5.0f, 5.0f }
        , _radius{ 5.0f }
        , _fadeDistance{ 1.0f }
        , _shape{ AudioVolumeShape::Box }
    {
    }

    void AudioReverbZoneComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getSceneAudio().addReverbZone( this );
    }

    void AudioReverbZoneComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getSceneAudio().removeReverbZone( this );
        SceneComponent::onUnregister( manager );
    }

    float32 AudioReverbZoneComponent::computeIntensity( const float3& listenerPosition ) const
    {
        return AudioSpatializer::computeInsideWeight( _shape, getWorldPosition(), _halfExtents, _radius, _fadeDistance, listenerPosition );
    }

    void AudioReverbZoneComponent::setShape( AudioVolumeShape shape, const float3& halfExtents, float32 radius, float32 fadeDistance )
    {
        _shape        = shape;
        _halfExtents  = halfExtents;
        _radius       = radius;
        _fadeDistance = fadeDistance;
    }
} // namespace sw
