/**
 * @file LocomotionMath.h
 * @brief 캐릭터가 어느 쪽으로 움직이는지(서기 · 앞 · 뒤 · 왼쪽 · 오른쪽 · 공중)를 보는 쪽 기준으로 가립니다 — 애니메이터의 이동 상태를 고르는 입력입니다.
 * @details 3인칭 슈터처럼 몸이 시점을 따라 돌고 다리는 이동 방향을 따르는 캐릭터(옆걸음 · 뒷걸음)가 씁니다. 좌표는 엔진과 같습니다(+Y 위, +Z 앞, 요는 +Z 에서 +X 쪽).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 보는 쪽 기준의 이동 방향입니다. */
    enum class LocomotionDirection : uint8
    {
        Idle = 0, ///< 서 있다(수평 속도가 문턱 아래)
        Forward,
        Backward,
        Left,
        Right,
        Airborne, ///< 발이 땅에 없다
    };
} // namespace sw

namespace sw
{
    /** @brief 이동 방향 가르기 · 보는 쪽 성분 계산입니다(전부 static, 상태 없음). */
    struct SW_GF_API LocomotionMath
    {
        /** @brief 수평 속도를 보는 쪽(@p yaw)의 앞 성분(+) · 오른쪽 성분(+)으로 나눕니다. */
        static float2 computeLocalVelocity( const float3& velocity, float32 yaw );
        /**
         * @brief 수평 속도 · 보는 요로 이동 방향을 고릅니다. 옆 성분이 앞뒤 성분의 @p sideBias 배를 넘어야 옆걸음입니다(대각선은 앞 · 뒤로).
         * @param idleSpeed 이보다 느리면 `Idle` 입니다(m/s).
         */
        static LocomotionDirection classify( const float3& velocity, float32 yaw, bool bOnGround, float32 idleSpeed, float32 sideBias = 1.2f );
    };
} // namespace sw
