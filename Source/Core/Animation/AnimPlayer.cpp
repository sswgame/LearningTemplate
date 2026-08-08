/**
 * @file AnimPlayer.cpp
 */
#include "AnimPlayer.h"

namespace sw
{
	void AnimPlayer::play( const AnimClip* clip, bool bLooping )
	{
		_current		= clip;
		_next			= nullptr;
		_currentTime	= 0.0f;
		_nextTime		= 0.0f;
		_fadeDuration	= 0.0f;
		_fadeElapsed	= 0.0f;
		_bCurrentLoop	= bLooping;
		_bNextLoop		= true;
	}

	void AnimPlayer::crossfade( const AnimClip* clip, float32 fadeSeconds, bool bLooping )
	{
		if ( clip == nullptr )
			return;

		if ( _current == nullptr || fadeSeconds <= 0.0f )
		{
			play( clip, bLooping );
			return;
		}

		_next		  = clip;
		_nextTime	  = 0.0f;
		_fadeDuration = fadeSeconds;
		_fadeElapsed  = 0.0f;
		_bNextLoop	  = bLooping;
	}

	void AnimPlayer::update( float32 deltaSeconds )
	{
		if ( deltaSeconds < 0.0f )
			deltaSeconds = 0.0f;

		if ( _current != nullptr )
			_currentTime += deltaSeconds;
		if ( _next != nullptr )
			_nextTime += deltaSeconds;

		if ( _next != nullptr && _fadeDuration > 0.0f )
		{
			_fadeElapsed += deltaSeconds;
			if ( _fadeElapsed >= _fadeDuration )
			{
				_current		= _next;
				_currentTime	= _nextTime;
				_bCurrentLoop	= _bNextLoop;
				_next			= nullptr;
				_nextTime		= 0.0f;
				_fadeDuration	= 0.0f;
				_fadeElapsed	= 0.0f;
			}
		}
	}

	AnimSample AnimPlayer::evaluate() const
	{
		if ( _current == nullptr )
		{
			AnimSample empty{};
			empty._transform = float4x4::Identity;
			return empty;
		}

		AnimSample result = _current->sample( _currentTime, _bCurrentLoop );
		if ( _next == nullptr || _fadeDuration <= 0.0f )
			return result;

		const AnimSample b	   = _next->sample( _nextTime, _bNextLoop );
		const float32	 alpha = _fadeElapsed / _fadeDuration;
		result._weight		   = result._weight * ( 1.0f - alpha ) + b._weight * alpha;
		// Stub blend: pick next transform once past midpoint.
		if ( alpha >= 0.5f )
			result._transform = b._transform;
		return result;
	}
} // namespace sw
