#pragma once
/**
 * @file PlayerLocomotion.h
 * @brief Tile-step locomotion FSM (Idle / Walk / Interact)
 */

#include "Core/Common/Types.h"

namespace sw
{
	enum class LocomotionState : uint8
	{
		Idle = 0,
		Walk,
		Interact
	};

	enum class FacingDir : uint8
	{
		Down = 0,
		Left,
		Right,
		Up
	};

	class PlayerLocomotion
	{
	public:
		void setState( LocomotionState state );
		void update( float32 deltaTime );
		void notifyStepStarted();
		void notifyStepFinished();
		void beginInteract( float32 duration = 0.2f );

		LocomotionState getState() const { return _state; }
		FacingDir		getFacing() const { return _facing; }
		void			setFacing( FacingDir dir ) { _facing = dir; }
		void			setFacingFromDelta( int32 dx, int32 dy );
		bool			canAcceptMoveInput() const;

	private:
		LocomotionState _state		 = LocomotionState::Idle;
		FacingDir		_facing		 = FacingDir::Down;
		float32			_stateTimer	 = 0.0f;
	};
} // namespace sw
