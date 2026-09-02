#include "pch.h"

#include "Engine/Animation/AnimPlayer.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    void AnimPlayer::play( const AnimClip* pClip, bool bLooping )
    {
        _pCurrent     = pClip;
        _pNext        = nullptr;
        _currentTime  = 0.0f;
        _nextTime     = 0.0f;
        _fadeDuration = 0.0f;
        _fadeElapsed  = 0.0f;
        _bCurrentLoop = bLooping;
        _bNextLoop    = true;
    }

    void AnimPlayer::crossfade( const AnimClip* pClip, float32 fadeSeconds, bool bLooping )
    {
        if ( pClip == nullptr )
            return;

        if ( _pCurrent == nullptr || fadeSeconds <= 0.0f )
        {
            play( pClip, bLooping );
            return;
        }

        _pNext        = pClip;
        _nextTime     = 0.0f;
        _fadeDuration = fadeSeconds;
        _fadeElapsed  = 0.0f;
        _bNextLoop    = bLooping;
    }

    void AnimPlayer::update( float32 deltaSeconds )
    {
        if ( deltaSeconds < 0.0f )
            deltaSeconds = 0.0f;

        const float32 effectiveDelta = deltaSeconds * _playSpeed;

        if ( _pCurrent != nullptr )
            _currentTime += effectiveDelta;
        if ( _pNext != nullptr )
            _nextTime += effectiveDelta;

        if ( _pNext != nullptr && _fadeDuration > 0.0f )
        {
            _fadeElapsed += effectiveDelta;
            if ( _fadeElapsed >= _fadeDuration )
            {
                _pCurrent     = _pNext;
                _currentTime  = _nextTime;
                _bCurrentLoop = _bNextLoop;
                _pNext        = nullptr;
                _nextTime     = 0.0f;
                _fadeDuration = 0.0f;
                _fadeElapsed  = 0.0f;
            }
        }
    }

    AnimSample AnimPlayer::evaluate() const
    {
        if ( _pCurrent == nullptr )
        {
            AnimSample empty{};
            empty._transform = float4x4::Identity;
            return empty;
        }

        AnimSample result = _pCurrent->sample( _currentTime, _bCurrentLoop );
        if ( _pNext == nullptr || _fadeDuration <= 0.0f )
            return result;

        const AnimSample b     = _pNext->sample( _nextTime, _bNextLoop );
        const float32    alpha = MathUtil::clamp( _fadeElapsed / _fadeDuration, 0.0f, 1.0f );
        result._weight         = result._weight * ( 1.0f - alpha ) + b._weight * alpha;
        result._transform      = float4x4::lerp( result._transform, b._transform, alpha );
        return result;
    }

    bool AnimPlayer::hasFinished() const
    {
        if ( _pCurrent == nullptr )
            return true;
        if ( _bCurrentLoop )
            return false;
        if ( _pNext != nullptr )
            return false;
        return _currentTime >= _pCurrent->getDuration();
    }
} // namespace sw
