#include "pch.h"

#include "Engine/UI/Animation/UIAnimation.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    float32 UIAnimation::computeDuration() const
    {
        float32 duration = 0.0f;
        for ( const UIAnimationTrack& track : _listTrack )
        {
            for ( const UIAnimationKey& key : track._listKey )
            {
                duration = MathUtil::max( duration, key._time );
            }
        }
        for ( const UIAnimationEvent& event : _listEvent )
        {
            duration = MathUtil::max( duration, event._time );
        }
        return duration;
    }
} // namespace sw
