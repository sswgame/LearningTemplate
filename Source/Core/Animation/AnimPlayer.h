#pragma once
/**
 * @file AnimPlayer.h
 * @brief Plays / crossfades between two AnimClips.
 */
#include "Core/Animation/AnimClip.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/**
	 * @class AnimPlayer
	 * @brief Two-slot clip player with linear crossfade.
	 */
	class SW_API AnimPlayer
	{
	public:
		AnimPlayer() = default;

		void play( const AnimClip* clip, bool bLooping = true );
		void crossfade( const AnimClip* clip, float32 fadeSeconds, bool bLooping = true );

		void update( float32 deltaSeconds );
		AnimSample evaluate() const;

		const AnimClip* getCurrentClip() const { return _current; }
		const AnimClip* getNextClip() const { return _next; }
		bool			isCrossfading() const { return _fadeDuration > 0.0f && _next != nullptr; }

	private:
		const AnimClip* _current		= nullptr;
		const AnimClip* _next			= nullptr;
		float32			_currentTime	= 0.0f;
		float32			_nextTime		= 0.0f;
		float32			_fadeDuration	= 0.0f;
		float32			_fadeElapsed	= 0.0f;
		bool			_bCurrentLoop	= true;
		bool			_bNextLoop		= true;
	};
} // namespace sw
