#include "pch.h"

#include "GameFramework/Transition/TransitionOrchestrator.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
	FadeService::FadeService()
		: _duration{ 0.35f }
		, _elapsed{ 0.0f }
		, _alpha{ 0.0f }
		, _phase{ FadePhase::Idle }
		, _bFinished{ 0 }
		, _reserved{ 0 }
	{
	}

	void FadeService::beginFadeOut( float32 duration )
	{
		_phase	   = FadePhase::FadingOut;
		_duration  = MathUtil::max( duration, 0.001f );
		_elapsed   = 0.0f;
		_bFinished = 0;
	}

	void FadeService::beginFadeIn( float32 duration )
	{
		_phase	   = FadePhase::FadingIn;
		_duration  = MathUtil::max( duration, 0.001f );
		_elapsed   = 0.0f;
		_alpha	   = 1.0f;
		_bFinished = 0;
	}

	void FadeService::update( float32 deltaTime )
	{
		if ( _phase == FadePhase::Idle )
			return;

		_elapsed += deltaTime;
		const float32 t = MathUtil::saturate( _elapsed / _duration );

		if ( _phase == FadePhase::FadingOut )
		{
			_alpha = t;
			if ( t >= 1.0f )
			{
				_phase	   = FadePhase::HoldBlack;
				_alpha	   = 1.0f;
				_bFinished = 1;
			}
		}
		else if ( _phase == FadePhase::FadingIn )
		{
			_alpha = 1.0f - t;
			if ( t >= 1.0f )
			{
				_phase	   = FadePhase::Idle;
				_alpha	   = 0.0f;
				_bFinished = 1;
			}
		}
		else if ( _phase == FadePhase::HoldBlack )
			_alpha = 1.0f;
	}

	bool FadeService::isFinished() const
	{
		return _bFinished != 0;
	}


	TransitionOrchestrator::TransitionOrchestrator()
		: _fade{}
		, _callbacks{}
		, _pendingWarpMap{}
		, _pendingWarpX{ 1 }
		, _pendingWarpY{ 1 }
		, _phase{ Phase::None }
		, _arrReserved{}
	{
	}

	void TransitionOrchestrator::beginWarp( string_view mapPath, int32 spawnX, int32 spawnY )
	{
		_pendingWarpMap = mapPath;
		_pendingWarpX	= spawnX;
		_pendingWarpY	= spawnY;
		_phase			= Phase::WarpFadeOut;
		_fade.beginFadeOut();
		if ( _callbacks.setPlayerInputEnabled.isBound() )
			_callbacks.setPlayerInputEnabled( false );
		SW_LOG_INFO( "[Transition] Warp fade-out → '%#' (%#,%#)", mapPath, spawnX, spawnY );
	}

	void TransitionOrchestrator::beginBattle()
	{
		_phase = Phase::BattleFadeOut;
		_fade.beginFadeOut();
		if ( _callbacks.setPlayerInputEnabled.isBound() )
			_callbacks.setPlayerInputEnabled( false );
		SW_LOG_INFO( "[Transition] Battle fade-out" );
	}

	void TransitionOrchestrator::beginReturn()
	{
		_phase = Phase::ReturnFadeOut;
		_fade.beginFadeOut();
		SW_LOG_INFO( "[Transition] Return fade-out" );
	}

	void TransitionOrchestrator::update( float32 deltaTime )
	{
		_fade.update( deltaTime );

		switch ( _phase )
		{
			case Phase::WarpFadeOut:
				if ( _fade.isFinished() )
					_phase = Phase::WarpLoad;
				break;

			case Phase::WarpLoad:
				if ( _callbacks.loadMap.isBound() )
					_callbacks.loadMap( _pendingWarpMap, _pendingWarpX, _pendingWarpY );
				_pendingWarpMap.clear();
				_fade.beginFadeIn();
				_phase = Phase::WarpFadeIn;
				break;

			case Phase::WarpFadeIn:
				if ( _fade.isFinished() )
				{
					_phase = Phase::None;
					if ( _callbacks.setPlayerInputEnabled.isBound() )
						_callbacks.setPlayerInputEnabled( true );
					SW_LOG_INFO( "[Transition] Warp complete." );
				}
				break;

			case Phase::BattleFadeOut:
				if ( _fade.isFinished() )
					_phase = Phase::BattleLoad;
				break;

			case Phase::BattleLoad:
				if ( _callbacks.startBattle.isBound() )
					_callbacks.startBattle();
				_fade.beginFadeIn();
				_phase = Phase::BattleFadeIn;
				break;

			case Phase::BattleFadeIn:
				if ( _fade.isFinished() )
				{
					_phase = Phase::None;
					SW_LOG_INFO( "[Transition] Battle fade-in complete." );
				}
				break;

			case Phase::ReturnFadeOut:
				if ( _fade.isFinished() )
					_phase = Phase::ReturnLoad;
				break;

			case Phase::ReturnLoad:
				if ( _callbacks.finishBattleReturn.isBound() )
					_callbacks.finishBattleReturn();
				_fade.beginFadeIn();
				_phase = Phase::ReturnFadeIn;
				break;

			case Phase::ReturnFadeIn:
				if ( _fade.isFinished() )
				{
					_phase = Phase::None;
					if ( _callbacks.setPlayerInputEnabled.isBound() )
						_callbacks.setPlayerInputEnabled( true );
					SW_LOG_INFO( "[Transition] Return fade-in complete." );
				}
				break;

			case Phase::None:
			default:
				break;
		}
	}

	void TransitionOrchestrator::reset()
	{
		_phase = Phase::None;
		_pendingWarpMap.clear();
		_fade = FadeService{};
	}
} // namespace sw
