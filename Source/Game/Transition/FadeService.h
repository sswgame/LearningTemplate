#pragma once
/**
 * @file FadeService.h
 * @brief Screen fade out/in handshake for warp / battle transitions
 */

#include "Core/Common/Types.h"

namespace sw
{
	enum class FadePhase : uint8
	{
		Idle = 0,
		FadingOut,
		HoldBlack,
		FadingIn
	};

	class FadeService
	{
	public:
		FadeService();

		void beginFadeOut( float32 duration = 0.35f );
		void beginFadeIn( float32 duration = 0.35f );
		void update( float32 deltaTime );

		bool	  isFinished() const;
		bool	  isBusy() const { return _phase != FadePhase::Idle; }
		FadePhase getPhase() const { return _phase; }
		/** @brief 0 = clear, 1 = full black overlay alpha. */
		float32	  getOverlayAlpha() const { return _alpha; }

	private:
		FadePhase _phase	  = FadePhase::Idle;
		float32	  _duration	  = 0.35f;
		float32	  _elapsed	  = 0.0f;
		float32	  _alpha	  = 0.0f;
		uint8	  _bFinished : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};
} // namespace sw
