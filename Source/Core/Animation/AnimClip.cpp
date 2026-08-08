/**
 * @file AnimClip.cpp
 */
#include "AnimClip.h"

namespace sw
{
	AnimClip::AnimClip( std::string name, float32 durationSeconds )
		: _name{ std::move( name ) }
		, _durationSeconds{ durationSeconds > 0.0f ? durationSeconds : 1.0f }
	{
	}

	void AnimClip::setDuration( float32 durationSeconds )
	{
		_durationSeconds = durationSeconds > 0.0f ? durationSeconds : 1.0f;
	}

	AnimSample AnimClip::sample( float32 timeSeconds, bool bLooping ) const
	{
		AnimSample result{};
		result._transform = float4x4::Identity;

		const float32 duration = _durationSeconds > 0.0f ? _durationSeconds : 1.0f;
		float32		  t		   = timeSeconds;
		if ( bLooping )
		{
			t = std::fmod( t, duration );
			if ( t < 0.0f )
				t += duration;
		}
		else
		{
			if ( t < 0.0f )
				t = 0.0f;
			if ( t > duration )
				t = duration;
		}

		result._weight = t / duration;
		return result;
	}
} // namespace sw
