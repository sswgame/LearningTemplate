/**
 * @file PlayerLocomotion.h
 * @brief 타일 스텝 이동 FSM (Idle / Walk / Interact)
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkMinimal.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) LocomotionState — 한 칸 이동과 상호작용이 입력을 잠금
	// ------------------------------------------------------------------------------
	/** @brief 타일 스텝 이동 상태 */
	enum class LocomotionState : uint8
	{
		Idle = 0,
		Walk,
		Interact
	};

	// ------------------------------------------------------------------------------
	// 2) PlayerLocomotion — 상태·바라보는 방향·입력 가능 여부
	//    실제 타일 좌표는 PlayerController가 소유
	// ------------------------------------------------------------------------------
	/** @brief 타일 스텝 이동 FSM */
	class PlayerLocomotion
	{
	public:
		PlayerLocomotion();

		/** @brief 이동 상태를 설정합니다. */
		void setState( LocomotionState state );
		/** @brief 상태 타이머를 갱신합니다. */
		void update( float32 deltaTime );
		/** @brief 타일 스텝 시작을 알립니다. */
		void notifyStepStarted();
		/** @brief 타일 스텝 종료를 알립니다. */
		void notifyStepFinished();
		/** @brief 상호작용 상태를 시작합니다. */
		void beginInteract( float32 duration = 0.2f );

		/** @brief 현재 이동 상태를 반환합니다. */
		LocomotionState getState() const { return _state; }
		/** @brief 바라보는 방향을 반환합니다. */
		FacingDir getFacing() const { return _facing; }
		/** @brief 바라보는 방향을 설정합니다. */
		void setFacing( FacingDir dir ) { _facing = dir; }
		/** @brief 이동 델타로부터 바라보는 방향을 설정합니다. */
		void setFacingFromDelta( int32 dx, int32 dy );
		/** @brief 이동 입력을 받을 수 있는지 반환합니다. */
		bool canAcceptMoveInput() const;

	private:
		LocomotionState _state;
		FacingDir		_facing;
		float32			_stateTimer; ///< Walk/Interact 남은 시간(초)
	};

} // namespace sw
