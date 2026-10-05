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
        /**
         * @brief `classify` 에 히스테리시스를 겁니다 — 지금이 옆걸음이면 앞뒤로 돌아가는 데 옆 성분이 앞뒤 성분의 1 / @p sideBias 배 아래로 내려가야 하고,
         *        지금이 앞뒤면 @p sideBias 배를 넘어야 옆걸음입니다. 대각선 근처에서 둘을 오가지 않습니다.
         */
        static LocomotionDirection classifyFrom( LocomotionDirection current, const float3& velocity, float32 yaw, bool bOnGround, float32 idleSpeed, float32 sideBias = 1.3f );
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocomotionDirectionFilter
     * @brief 이동 방향이 바뀐 것을 @p minHoldSeconds 동안 이어져야 받아들입니다(공중 · 착지는 바로). 애니메이터의 이동 상태가 문턱 근처에서
     *        깜빡이며 클립을 처음부터 다시 트는 것을 막습니다.
     */
    class SW_GF_API LocomotionDirectionFilter
    {
    public:
        explicit LocomotionDirectionFilter( float32 minHoldSeconds = 0.25f );

        /** @brief 이번 프레임의 후보를 넣고 받아들인 방향을 돌려줍니다. */
        LocomotionDirection update( LocomotionDirection candidate, float32 deltaSeconds );
        LocomotionDirection getDirection() const { return _direction; }
        /** @brief 처음 상태(서기)로 되돌립니다. */
        void reset();

    private:
        float32             _minHoldSeconds;
        float32             _pendingSeconds; ///< 후보가 지금과 다르게 이어진 시간
        LocomotionDirection _direction;
        LocomotionDirection _pending;
    };
} // namespace sw
