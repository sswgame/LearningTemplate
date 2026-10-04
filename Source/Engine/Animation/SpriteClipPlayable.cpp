#include "pch.h"

#include "Engine/Animation/SpriteClipPlayable.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"

namespace sw
{
    SpriteClipPlayable::SpriteClipPlayable()
        : _pClip{ nullptr }
        , _fallbackSeconds{ 1.0f / 12.0f }
        , _playLength{ 1.0f / 12.0f }
        , _rangeStartSeconds{ 0.0f }
        , _firstFrame{ 0 }
        , _frameCount{ 1 }
        , _bLoop{ SW_TRUE }
    {
    }

    void SpriteClipPlayable::configure( const SpriteClipAsset* pClip, int32 firstFrame, int32 frameCount, bool bLoop, float32 fallbackSeconds )
    {
        _pClip             = pClip;
        _fallbackSeconds   = fallbackSeconds > 0.0f ? fallbackSeconds : 1.0f / 12.0f;
        _firstFrame        = MathUtil::max( firstFrame, 0 );
        _frameCount        = MathUtil::max( frameCount, 1 );
        _bLoop             = bLoop ? SW_TRUE : SW_FALSE;
        _rangeStartSeconds = ( _pClip != nullptr ) ? _pClip->computeFrameStartSeconds( _firstFrame, _fallbackSeconds ) : 0.0f;
        _playLength        = computeFrameStart( _frameCount );
    }

    float32 SpriteClipPlayable::getFrameDuration( int32 frameInRange ) const
    {
        return ( _pClip != nullptr ) ? _pClip->getFrameDurationSeconds( _firstFrame + frameInRange, _fallbackSeconds ) : _fallbackSeconds;
    }

    float32 SpriteClipPlayable::computeFrameStart( int32 frameInRange ) const
    {
        float32 start = 0.0f;
        for ( int32 frame = 0; frame < frameInRange && frame < _frameCount; ++frame )
            start += getFrameDuration( frame );
        return start;
    }

    int32 SpriteClipPlayable::findFrameAtTime( float32 time ) const
    {
        float32 frameEnd = 0.0f;
        for ( int32 frame = 0; frame < _frameCount; ++frame )
        {
            frameEnd += getFrameDuration( frame );
            if ( time < frameEnd )
                return frame;
        }
        return _frameCount - 1;
    }

    float32 SpriteClipPlayable::computeClipTime( float32 time ) const
    {
        return _rangeStartSeconds + MathUtil::clamp( time, 0.0f, _playLength );
    }
} // namespace sw
