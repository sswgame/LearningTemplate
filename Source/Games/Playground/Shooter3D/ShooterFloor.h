/**
 * @file ShooterFloor.h
 * @brief 사격장 몸이 딛는 움직이는 바닥(틱 함수 바닥 `InterpToMovementComponent`)의 한 걸음 계산입니다 — 실려 가기, 올라타기 · 내려서기.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /**
     * @brief 움직이는 바닥 하나의 걸음 앞 · 뒤 자세입니다(절대 자리). 씬 없이 계산합니다 — 서버 · 예측 · 재시뮬레이션이 같은 값을 낸다.
     * @details 걸음마다 바닥은 정해진 시각의 자세다 — 한 프레임에 명령이 여럿이어도 명령마다 다른 자세 위에서 걷는다(언리얼 CharacterMovement 의 `MovementBase`).
     *          사라진 바닥은 크기가 0 이라 아무것도 받치지 않는다(목록의 번호는 그대로다).
     */
    struct ShooterFloorStep
    {
        static constexpr float32 kStepHeight        = 0.3f;  ///< 이만큼까지 걸어서 올라선다
        static constexpr float32 kStandingTolerance = 0.05f; ///< 발이 윗면에서 이만큼 안이면 딛고 있다

        float3  _fromPosition{};
        float3  _toPosition{};
        float3  _halfSize{};
        float32 _fromYaw{ 0.0f };
        float32 _toYaw{ 0.0f };

        /** @brief @p feet 가 걸음 뒤의 바닥 위에 서 있으면 true 입니다. */
        bool isStandingAfter( const float3& feet ) const;
        /** @brief 걸음 앞의 발 @p feet 가 바닥에 실려 간 자리입니다. */
        float3 carry( const float3& feet ) const;

        /** @brief 땅에 선 몸 @p feet 가 딛은 바닥에 실려 간 자리입니다. 딛은 바닥이 없으면 그대로입니다. */
        static float3 carryStanding( const vector<ShooterFloorStep>& listFloor, const float3& feet, bool bOnGround );
        /** @brief @p feet 아래의 받쳐 주는 높이입니다(없으면 아레나 바닥 0). @p startHeight 는 걸음을 시작한 발 높이다 — 그보다 걸어 오를 수 없게 높은 윗면은 받치지 않는다. */
        static float32 computeGround( const vector<ShooterFloorStep>& listFloor, const float3& feet, float32 startHeight );
        /** @brief @p feet 가 서 있는 바닥의 번호(1 부터)입니다. 없으면 0 입니다. */
        static uint8 findNumber( const vector<ShooterFloorStep>& listFloor, const float3& feet );
        /** @brief 바닥 자세 (@p position, @p yaw) 기준의 점을 절대 자리로, 절대 자리를 바닥 기준으로 바꿉니다. */
        static float3 toAbsolute( const float3& position, float32 yaw, const float3& local );
        static float3 toLocal( const float3& position, float32 yaw, const float3& absolute );

    private:
        bool isOver( const float3& position, float32 yaw, const float3& feet ) const;
    };
} // namespace sw
