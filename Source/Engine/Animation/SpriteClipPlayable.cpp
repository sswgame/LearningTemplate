#include "pch.h"

#include "Engine/Animation/SpriteClipPlayable.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/SpriteClipAsset.h"

namespace sw
{
    SpriteClipPlayable::SpriteClipPlayable()
        : _notifyTrack{}
        , _pClip{ nullptr }
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
        // 구간의 알림 — 같은 첫 프레임 · 프레임 수의 이름 붙은 구간에서 베낀다.
        _notifyTrack.clear();
        for ( size_t animationIndex = 0; _pClip != nullptr && animationIndex < _pClip->_listAnimation.size(); ++animationIndex )
        {
            const SpriteClipAnimation& animation = _pClip->_listAnimation[animationIndex];
            if ( animation._firstFrame != _firstFrame || animation._frameCount != _frameCount )
                continue;
            for ( const AnimNotifyEvent& event : animation._listNotify )
            {
                _notifyTrack.addEvent( event );
            }
            break;
        }
    }

    float32 SpriteClipPlayable::getFrameDuration( int32 frameInRange ) const
    {
        return ( _pClip != nullptr ) ? _pClip->getFrameDurationSeconds( _firstFrame + frameInRange, _fallbackSeconds ) : _fallbackSeconds;
    }

    float32 SpriteClipPlayable::computeFrameStart( int32 frameInRange ) const
    {
        float32 start = 0.0f;
        for ( int32 frame = 0; frame < frameInRange && frame < _frameCount; ++frame )
        {
            start += getFrameDuration( frame );
        }
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
