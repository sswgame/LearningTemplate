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
            return MathUtil::sqrt( offset._x * offset._x + offset._y * offset._y );
        return offset.getLength();
    }

    float32 AudioSpatializer::computePan( const AudioListenerState& listener, const float3& emitterPosition, float32 centerRadius )
    {
        const float3 offset = emitterPosition - listener._position;
        if ( listener._mode == AudioSpatialMode::Screen2D )
        {
            const float32 halfWidth = MathUtil::max( 0.01f, listener._screenHalfWidth );
            return MathUtil::clamp( offset._x / halfWidth, -1.0f, 1.0f );
        }
        // 왼손 좌표(+X 오른쪽 · +Y 위 · +Z 앞): 오른쪽 = 위 × 앞. 팬은 수평면 위 방향의 오른쪽 성분이다(고도는 쓰지 않는다).
        const float3  right      = listener._up.cross( listener._forward ).normalize();
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

    AudioSpatialResult AudioSpatializer::compute( const AudioListenerState& listener, const AudioEmitterState& emitter, const AudioAttenuationDesc& attenuation,
                                                  const AudioOcclusionDesc& occlusion )
    {
        AudioSpatialResult result;
        result._distance   = computeDistance( listener, emitter._position );
        result._gain       = attenuation.computeGain( result._distance );
        result._pan        = computePan( listener, emitter._position, attenuation._minDistance * 0.5f );
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
