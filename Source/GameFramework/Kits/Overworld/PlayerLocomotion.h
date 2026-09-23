/**
 * @file PlayerLocomotion.h
 * @brief 타일 스텝 이동 FSM(Idle / Walk / Interact)입니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/Base/FacingDir.h"
#include "GameFramework/GameFrameworkMinimal.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) LocomotionState — 타일 스텝 이동 FSM (FacingDir 은 Base/FacingDir.h)
    // ------------------------------------------------------------------------------
    /** @brief 타일 스텝 이동 상태입니다. */
    enum class LocomotionState : uint8
    {
        Idle = 0,
        Walk,
        Interact
    };

    // ------------------------------------------------------------------------------
    // 2) PlayerLocomotion — 상태 · 바라보는 방향 · 입력 가능 여부
    //    실제 타일 좌표는 PlayerController 가 소유
    // ------------------------------------------------------------------------------
    /** @brief 타일 스텝 이동 FSM 입니다. */
    class SW_GF_API PlayerLocomotion
    {
    public:
        /**
         * @brief 한 칸을 밟는 데 걸리는 시간(초)입니다. **여기가 기준입니다.**
         * @details `PlayerController` 가 다음 입력을 막는 시간으로 같은 `0.18` 을 따로 들고
         *          있었습니다. 한쪽만 바꾸면 걷는 연출과 입력 잠금이 어긋납니다.
         */
        static constexpr float32 kStepDuration = 0.18f;

        PlayerLocomotion();

        /** @brief 이동 상태를 설정합니다. */
        void setState( LocomotionState state );
        /** @brief 상태 타이머를 갱신합니다. */
        void update( float32 deltaTime );
        /** @brief 타일 스텝 시작을 알립니다. */
        void notifyStepStarted();
        /**
         * @brief 타일 스텝을 **지금 당장** 끝냅니다. 텔레포트처럼 걷는 시간이 없을 때만 씁니다.
         * @warning 걸음을 시작한 그 프레임에 이것을 부르면 `Walk` 상태가 **한 프레임도 살지
         *          못합니다.** 예전 `PlayerController::update` 가 정확히 그렇게 해서
         *          `LocomotionState::Walk` 는 바깥에서 **한 번도 관측되지 않았습니다.** 걷는
         *          애니메이션을 고를 근거가 통째로 죽어 있었습니다. 보통은 이것을 부르지 말고
         *          `update( deltaTime )` 이 `kStepDuration` 뒤에 스스로 끝내게 둡니다.
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

    private:
        LocomotionState _state;
        FacingDir       _facing;
        float32         _stateTimer; ///< Walk · Interact 남은 시간(초)
    };

} // namespace sw
