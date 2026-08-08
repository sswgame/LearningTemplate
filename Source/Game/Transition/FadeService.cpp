/**
 * @file FadeService.cpp
 */
#include "FadeService.h"

namespace sw
{
	FadeService::FadeService()
		: _bFinished{ 0 }
		, _reserved{ 0 }
	{
	}

	void FadeService::beginFadeOut( float32 duration )
	{
		_phase	   = FadePhase::FadingOut;
		_duration  = duration > 0.01f ? duration : 0.35f;
		_elapsed   = 0.0f;
		_bFinished = 0;
	}

	void FadeService::beginFadeIn( float32 duration )
	{
		_phase	   = FadePhase::FadingIn;
		_duration  = duration > 0.01f ? duration : 0.35f;
		_elapsed   = 0.0f;
		_alpha	   = 1.0f;
		_bFinished = 0;
	}

	bool FadeService::isFinished() const
	{
		return _bFinished != 0;
	}

	void FadeService::update( float32 deltaTime )
	{
		if ( _phase == FadePhase::Idle )
			return;

		_elapsed += deltaTime;
		const float32 t = _elapsed / _duration;

		if ( _phase == FadePhase::FadingOut )
		{
			_alpha = t >= 1.0f ? 1.0f : t;
			if ( t >= 1.0f )
			{
				_phase	   = FadePhase::HoldBlack;
				_bFinished = 1;
			}
		}
		else if ( _phase == FadePhase::FadingIn )
		{
			_alpha = t >= 1.0f ? 0.0f : ( 1.0f - t );
			if ( t >= 1.0f )
			{
				_phase	   = FadePhase::Idle;
				_alpha	   = 0.0f;
				_bFinished = 1;
			}
		}
		else if ( _phase == FadePhase::HoldBlack )
		{
			_alpha = 1.0f;
		}
	}
} // namespace sw
