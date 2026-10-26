/**
 * @file PlayerLocomotion.h
 * @brief 타일 스텝 이동 FSM(Idle / Walk / Interact)입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Time/Countdown.h"

#include "GameFramework/Base/Foundation/Utility/Grid/FacingDir.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;

    // ------------------------------------------------------------------------------
    // 1) LocomotionState. 타일 스텝 이동 FSM 의 상태이다(FacingDir 은 Utility/FacingDir.h)
    // ------------------------------------------------------------------------------
    /** @brief 타일 스텝 이동 상태입니다. */
    enum class LocomotionState : uint8
    {
        Idle = 0,
        Walk,
        Interact
    };

    // ------------------------------------------------------------------------------
    // 2) PlayerLocomotion. 상태, 바라보는 방향, 입력 가능 여부를 가진다
    //    실제 타일 좌표는 OverworldTileMover 가 가진다
    // ------------------------------------------------------------------------------
    /** @brief 타일 스텝 이동 FSM 입니다. */
    class SW_KIT_API PlayerLocomotion
    {
    public:
        PlayerLocomotion();

        /** @brief 이동 상태를 설정합니다. */
        void setState( LocomotionState state );
        /** @brief 상태 타이머를 갱신합니다. */
        void update( float32 deltaTime );
        /** @brief 타일 스텝이 시작됐음을 알립니다. */
        void notifyStepStarted( float32 stepSeconds );
        /**
         * @brief 타일 스텝을 **지금 당장** 끝냅니다. 텔레포트처럼 걷는 시간이 없을 때만 씁니다.
         * @warning 걸음을 시작한 그 프레임에 이 함수를 부르면 `Walk` 상태가 한 프레임도 유지되지 못합니다.
         *          그러면 `LocomotionState::Walk` 가 바깥에서 관측되지 않아 걷는 애니메이션을 고를 근거가 사라집니다.
         *          보통은 이 함수를 부르지 말고 `update( deltaTime )` 이 걸음 시간(`OverworldTileMoverSettings::_stepSeconds`) 뒤에 스스로 끝내게 둡니다.
         */
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
        /** @brief 상태, 바라보는 방향, 상태 타이머의 남은 시간을 씁니다. `OverworldTileMover` 의 상태 안에 실립니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 바이트가 손상됐으면 false 를 반환하고 상태는 그대로 둡니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        LocomotionState _state;
        FacingDir       _facing;
        Countdown       _stateTimer; ///< Walk 와 Interact 의 남은 시간(초)
    };

} // namespace sw
