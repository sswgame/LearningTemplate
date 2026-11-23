#include "pch.h"

#include "Engine/Audio/Component/AudioReverbZoneComponent.h"

#include "Core/Math/AABB.h"

#include "Engine/Audio/Scene/SceneAudio.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    AudioReverbZoneComponent::AudioReverbZoneComponent()
        : _snapshot{}
        , _halfExtents{ 5.0f, 5.0f, 5.0f }
        , _radius{ 5.0f }
        , _fadeDistance{ 1.0f }
        , _shape{ AudioVolumeShape::Box }
        , _sceneAudioSubscription{}
    {
    }

    void AudioReverbZoneComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _sceneAudioSubscription = manager.getSubsystem<sw::SceneAudio>().addReverbZone( this );
    }

    void AudioReverbZoneComponent::onUnregister( GameObjectManager& manager )
    {
        _sceneAudioSubscription.reset();
        SceneComponent::onUnregister( manager );
    }

    float32 AudioReverbZoneComponent::computeIntensity( const float3& listenerPosition ) const
    {
        return AudioSpatializer::computeInsideWeight( _shape, getWorldPosition(), _halfExtents, _radius, _fadeDistance, listenerPosition );
    }

    bool AudioReverbZoneComponent::computeStreamingBounds( AABB& outBox ) const
    {
        const float3 half   = _shape == AudioVolumeShape::Box ? _halfExtents : float3{ _radius, _radius, _radius };
        const float3 center = getWorldPosition();
        outBox              = AABB{ center - half, center + half };
        return true;
    }

    void AudioReverbZoneComponent::setShape( AudioVolumeShape shape, const float3& halfExtents, float32 radius, float32 fadeDistance )
    {
        _shape        = shape;
        _halfExtents  = halfExtents;
        _radius       = radius;
        _fadeDistance = fadeDistance;
    }
} // namespace sw
