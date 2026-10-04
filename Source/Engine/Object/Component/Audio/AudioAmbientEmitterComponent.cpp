#include "pch.h"

#include "Engine/Object/Component/Audio/AudioAmbientEmitterComponent.h"

namespace sw
{
    AudioAmbientEmitterComponent::AudioAmbientEmitterComponent()
        : _halfExtents{ 5.0f, 5.0f, 5.0f }
        , _radius{ 5.0f }
        , _shape{ AudioVolumeShape::Box }
    {
        // 영역 소리는 벽을 넘어 번진다 — 레이캐스트 가림은 기본으로 끈다.
        setOcclusion( false );
    }

    float3 AudioAmbientEmitterComponent::computeAudioPosition( const float3& listenerPosition ) const
    {
        return AudioSpatializer::computeClosestPoint( _shape, getWorldPosition(), _halfExtents, _radius, listenerPosition );
    }

    void AudioAmbientEmitterComponent::setShape( AudioVolumeShape shape, const float3& halfExtents, float32 radius )
    {
        _shape       = shape;
        _halfExtents = halfExtents;
        _radius      = radius;
    }
} // namespace sw
