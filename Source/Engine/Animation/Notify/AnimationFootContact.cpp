#include "pch.h"

#include "Engine/Animation/Notify/AnimationFootContact.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    AnimationFootContactTracker::AnimationFootContactTracker()
        : _previousHeight{ 0.0f }
        , _bHasPrevious{ SW_FALSE }
        , _bPlanted{ SW_FALSE }
        , _bMissingBoneReported{ SW_FALSE }
    {
    }

    bool AnimationFootContactTracker::update( const AnimationFootContactDef& def, float32 footHeight, float32 deltaSeconds )
    {
        if ( deltaSeconds <= 0.0f )
            return false;
        if ( _bHasPrevious == SW_FALSE )
        {
            _previousHeight = footHeight;
            _bHasPrevious   = SW_TRUE;
            _bPlanted       = footHeight <= def._contactHeight ? SW_TRUE : SW_FALSE;
            return false;
        }
        const float32 verticalSpeed = MathUtil::abs( footHeight - _previousHeight ) / deltaSeconds;
        _previousHeight             = footHeight;
        if ( _bPlanted == SW_TRUE )
        {
            if ( footHeight > def._releaseHeight )
                _bPlanted = SW_FALSE;
            return false;
        }
        const bool bTouched = footHeight <= def._contactHeight && verticalSpeed <= def._maxVerticalSpeed;
        if ( bTouched )
            _bPlanted = SW_TRUE;
        return bTouched;
    }

    void AnimationFootContactTracker::reset()
    {
        _previousHeight = 0.0f;
        _bHasPrevious   = SW_FALSE;
        _bPlanted       = SW_FALSE;
    }
} // namespace sw
