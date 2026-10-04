#include "pch.h"

#include "Engine/Audio/AudioSpatial.h"

#include "Engine/Audio/AudioTypes.h"

namespace sw
{
    float32 AudioCurvePoint::evaluate( const vector<AudioCurvePoint>& listPoint, float32 x, float32 fallback )
    {
        if ( listPoint.empty() )
            return fallback;
        if ( x <= listPoint.front()._x )
            return listPoint.front()._y;
        if ( x >= listPoint.back()._x )
            return listPoint.back()._y;
        for ( size_t pointIndex = 1; pointIndex < listPoint.size(); ++pointIndex )
        {
            const AudioCurvePoint& upper = listPoint[pointIndex];
            if ( x > upper._x )
                continue;
            const AudioCurvePoint& lower = listPoint[pointIndex - 1];
            const float32          span  = upper._x - lower._x;
            if ( span <= 0.0f )
                return upper._y;
            return lower._y + ( upper._y - lower._y ) * ( x - lower._x ) / span;
        }
        return listPoint.back()._y;
    }

    float32 AudioAttenuationDesc::computeGain( float32 distance ) const
    {
        const float32 minDistance = MathUtil::max( 0.01f, _minDistance );
        const float32 maxDistance = MathUtil::max( minDistance, _maxDistance );
        switch ( _curve )
        {
            case AudioAttenuationCurve::Linear:
            {
                if ( distance <= minDistance )
                    return 1.0f;
                if ( distance >= maxDistance || maxDistance <= minDistance )
                    return 0.0f;
                return 1.0f - ( distance - minDistance ) / ( maxDistance - minDistance );
            }
            case AudioAttenuationCurve::Inverse:
            {
                const float32 clamped = MathUtil::clamp( distance, minDistance, maxDistance );
                return minDistance / clamped;
            }
            case AudioAttenuationCurve::InverseSquare:
            {
                const float32 clamped = MathUtil::clamp( distance, minDistance, maxDistance );
                return MathUtil::square( minDistance / clamped );
            }
            case AudioAttenuationCurve::Custom:
            {
                return AudioMath::dbToLinear( AudioCurvePoint::evaluate( _listCustomPoint, distance, 0.0f ) );
            }
        }
        return 1.0f;
    }

    float32 AudioAttenuationDesc::computeLowPassHz( float32 distance ) const
    {
        return AudioCurvePoint::evaluate( _listLowPassPoint, distance, audio::kFilterOpenHz );
    }

    float32 AudioSpatializer::computeDistance( const AudioListenerState& listener, const float3& emitterPosition )
    {
        const float3 offset = emitterPosition - listener._position;
        if ( listener._mode == AudioSpatialMode::Screen2D )
        {
            // 화면 평면 거리 — 시선(앞) 방향 성분(깊이 · 레이어)을 뺀다. 옆에서 본 2D(XY, +Z 를 봄)면 XY 거리, 위에서 본 직교(XZ)면 XZ 거리.
            const float3 forward = listener._forward.normalize();
            const float3 planar  = offset - forward * offset.dot( forward );
            return planar.getLength();
        }
        return offset.getLength();
    }

    float32 AudioSpatializer::computePan( const AudioListenerState& listener, const float3& emitterPosition, float32 centerRadius )
    {
        const float3 offset = emitterPosition - listener._position;
        // 왼손 좌표(+X 오른쪽 · +Y 위 · +Z 앞): 오른쪽 = 위 × 앞.
        const float3 right = listener._up.cross( listener._forward ).normalize();
        if ( listener._mode == AudioSpatialMode::Screen2D )
        {
            // 화면 가로 거리 / 화면 반폭 — 화면 세로(고도)는 팬에 쓰지 않는다.
            const float32 halfWidth = MathUtil::max( 0.01f, listener._screenHalfWidth );
            return MathUtil::clamp( offset.dot( right ) / halfWidth, -1.0f, 1.0f );
        }
        // 3D 팬은 수평면 위 방향의 오른쪽 성분이다(고도는 쓰지 않는다).
        const float32 side       = offset.dot( right );
        const float32 front      = offset.dot( listener._forward );
        const float32 horizontal = MathUtil::sqrt( side * side + front * front );
        const float32 radius     = MathUtil::max( horizontal, MathUtil::max( 1e-4f, centerRadius ) );
        return MathUtil::clamp( side / radius, -1.0f, 1.0f );
    }

    float32 AudioSpatializer::computeDopplerRatio( const AudioListenerState& listener, const AudioEmitterState& emitter, float32 factor )
    {
        if ( factor <= 0.0f )
            return 1.0f;
        float3        toListener = listener._position - emitter._position;
        const float32 distance   = toListener.getLength();
        if ( distance < 1e-4f )
            return 1.0f;
        toListener /= distance;
        // 다가오는 속도(+)를 소리 속도의 절반 안으로 묶는다 — 분모가 0 이나 음수가 되지 않게.
        const float32 limit          = kSpeedOfSound * 0.5f;
        const float32 sourceApproach = MathUtil::clamp( emitter._velocity.dot( toListener ), -limit, limit );
        const float32 listenerRecede = MathUtil::clamp( listener._velocity.dot( toListener ), -limit, limit );
        const float32 ratio          = ( kSpeedOfSound - listenerRecede ) / ( kSpeedOfSound - sourceApproach );
        const float32 scaled         = 1.0f + ( ratio - 1.0f ) * factor;
        return MathUtil::clamp( scaled, 1.0f / kMaxDopplerRatio, kMaxDopplerRatio );
    }

    float3 AudioSpatializer::computeClosestPoint( AudioVolumeShape shape, const float3& center, const float3& halfExtents, float32 radius, const float3& point )
    {
        switch ( shape )
        {
            case AudioVolumeShape::Point:
            {
                return center;
            }
            case AudioVolumeShape::Box:
            {
                return float3( MathUtil::clamp( point._x, center._x - halfExtents._x, center._x + halfExtents._x ),
                               MathUtil::clamp( point._y, center._y - halfExtents._y, center._y + halfExtents._y ),
                               MathUtil::clamp( point._z, center._z - halfExtents._z, center._z + halfExtents._z ) );
            }
            case AudioVolumeShape::Sphere:
            {
                const float3  offset   = point - center;
                const float32 distance = offset.getLength();
                if ( distance <= radius || distance <= 1e-6f )
                    return point;
                return center + offset * ( radius / distance );
            }
        }
        return center;
    }

    float32 AudioSpatializer::computeInsideWeight( AudioVolumeShape shape, const float3& center, const float3& halfExtents, float32 radius, float32 fadeDistance,
                                                   const float3& point )
    {
        float32 depth = -1.0f;
        switch ( shape )
        {
            case AudioVolumeShape::Point:
            {
                return 0.0f;
            }
            case AudioVolumeShape::Box:
            {
                const float3  offset = point - center;
                const float32 depthX = halfExtents._x - MathUtil::abs( offset._x );
                const float32 depthY = halfExtents._y - MathUtil::abs( offset._y );
                const float32 depthZ = halfExtents._z - MathUtil::abs( offset._z );
                depth                = MathUtil::min( depthX, MathUtil::min( depthY, depthZ ) );
                break;
            }
            case AudioVolumeShape::Sphere:
            {
                depth = radius - ( point - center ).getLength();
                break;
            }
        }
        if ( depth < 0.0f )
            return 0.0f;
        return fadeDistance <= 0.0f ? 1.0f : MathUtil::saturate( depth / fadeDistance );
    }

    AudioSpatialResult AudioSpatializer::compute( const AudioListenerState& listener, const AudioEmitterState& emitter, const AudioAttenuationDesc& attenuation,
                                                  const AudioOcclusionDesc& occlusion )
    {
        AudioSpatialResult result;
        result._distance = computeDistance( listener, emitter._position );
        result._gain     = attenuation.computeGain( result._distance );
        // 리스너 0.5 m 안(최소 거리가 더 작으면 그 절반 안)에서는 팬을 가운데로 모은다 — 머리를 지나가는 소리가 좌우로 튀지 않게.
        result._pan        = computePan( listener, emitter._position, MathUtil::min( attenuation._minDistance * 0.5f, 0.5f ) );
        result._pitchRatio = computeDopplerRatio( listener, emitter, attenuation._dopplerFactor );
        result._lowPassHz  = attenuation.computeLowPassHz( result._distance );
        if ( attenuation._bOcclusion && emitter._occlusion > 0.0f )
        {
            const float32 amount = MathUtil::saturate( emitter._occlusion );
            result._gain *= AudioMath::dbToLinear( occlusion._volumeDb * amount );
            result._lowPassHz = MathUtil::min( result._lowPassHz, AudioMath::lerpFrequency( audio::kFilterOpenHz, occlusion._lowPassHz, amount ) );
        }
        return result;
    }
} // namespace sw
