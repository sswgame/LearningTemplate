#include "pch.h"

#include "Engine/Animation/AnimClip.h"

namespace sw
{
    AnimClip::AnimClip( string_view name, float32 durationSeconds )
        : _name{ name }
        , _durationSeconds{ durationSeconds > 0.0f ? durationSeconds : 1.0f } {}

    void AnimClip::setDuration( float32 durationSeconds )
    {
        _durationSeconds = durationSeconds > 0.0f ? durationSeconds : 1.0f;
    }

    AnimSample AnimClip::sample( float32 timeSeconds, bool bLooping ) const
    {
        AnimSample result{};
        result._transform = float4x4::Identity;

        const float32 duration = _durationSeconds > 0.0f ? _durationSeconds : 1.0f;
        float32       t        = timeSeconds;
        if ( bLooping )
        {
            t = MathUtil::fmod( t, duration );
            if ( t < 0.0f )
                t += duration;
        }
        else
        {
            t = MathUtil::clamp( t, 0.0f, duration );
        }

        result._weight = t / duration;
        return result;
    }
} // namespace sw
