#include "pch.h"

#include "Engine/Animation/Sprite/SpriteClipPlayable.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Sprite/SpriteClipAsset.h"

namespace sw
{
    SpriteClipPlayable::SpriteClipPlayable()
        : _notifyTrack{}
        , _pClip{ nullptr }
        , _animatorFrameSeconds{ 1.0f / 12.0f }
        , _playLength{ 1.0f / 12.0f }
        , _rangeStartSeconds{ 0.0f }
        , _firstFrame{ 0 }
        , _frameCount{ 1 }
        , _bLoop{ SW_TRUE }
    {
    }

    bool SpriteClipPlayable::configure( const SpriteClipAsset* pClip, int32 firstFrame, int32 frameCount, bool bLoop, float32 animatorFrameSeconds )
    {
        // 맞추지 않고 거절한다(예전에는 첫 프레임 0 · 프레임 수 1 · 프레임 길이 1/12 초로 조용히 바꿨다).
        const bool bValid = firstFrame >= 0 && frameCount >= 1 && animatorFrameSeconds > 0.0f;
        if ( bValid == false )
        {
            SW_LOG_ERROR( "SpriteClipPlayable: refused first frame %# frame count %# frame seconds %# - the first frame must not be negative, the count must be at least 1 and the seconds positive", firstFrame, frameCount,
                          animatorFrameSeconds );
            return false;
        }
        _pClip                = pClip;
        _animatorFrameSeconds = animatorFrameSeconds;
        _firstFrame           = firstFrame;
        _frameCount           = frameCount;
        _bLoop                = bLoop ? SW_TRUE : SW_FALSE;
        _rangeStartSeconds    = ( _pClip != nullptr ) ? _pClip->computeFrameStartSeconds( _firstFrame, _animatorFrameSeconds ) : 0.0f;
        _playLength           = computeFrameStart( _frameCount );
        // 구간의 알림은 같은 첫 프레임과 프레임 수를 가진 이름 붙은 구간에서 복사한다.
        _notifyTrack.clear();
        for ( size_t animationIndex = 0; _pClip != nullptr && animationIndex < _pClip->_listAnimation.size(); ++animationIndex )
        {
            const SpriteClipAnimation& animation = _pClip->_listAnimation[animationIndex];
            if ( animation._firstFrame != _firstFrame || animation._frameCount != _frameCount )
                continue;
            for ( const AnimationNotifyEvent& event : animation._listNotify )
            {
                _notifyTrack.addEvent( event );
            }
            break;
        }
        return true;
    }

    float32 SpriteClipPlayable::getFrameDuration( int32 frameInRange ) const
    {
        return ( _pClip != nullptr ) ? _pClip->getFrameDurationSeconds( _firstFrame + frameInRange, _animatorFrameSeconds ) : _animatorFrameSeconds;
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
