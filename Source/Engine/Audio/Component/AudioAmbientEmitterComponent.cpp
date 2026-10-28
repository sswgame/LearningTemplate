#include "pch.h"

#include "Engine/Audio/Component/AudioAmbientEmitterComponent.h"

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
        // 소리 자리는 모양도 따른다 — 트랜스폼 번호가 그대로여도 다음 갱신에 다시 구하게 한다(0 은 칸이 낼 수 없는 번호).
        _seenTransformVersion = 0;
    }

    void AudioAmbientEmitterComponent::onPropertyChanged( hashed_string propertyName )
    {
        AudioEmitterComponent::onPropertyChanged( propertyName );
        _seenTransformVersion = 0; // `setShape` 과 같은 이유 — 인스펙터가 모양을 바꿨을 수 있다
    }
} // namespace sw
