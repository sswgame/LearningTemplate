#include "pch.h"

#include "Engine/UI/Animation/UiAnimation.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float32 UiAnimation::computeDuration() const
    {
        float32 duration = 0.0f;
        for ( const UiAnimationTrack& track : _listTrack )
        {
            for ( const UiAnimationKey& key : track._listKey )
                duration = MathUtil::max( duration, key._time );
        }
        for ( const UiAnimationEvent& event : _listEvent )
            duration = MathUtil::max( duration, event._time );
        return duration;
    }
} // namespace sw
