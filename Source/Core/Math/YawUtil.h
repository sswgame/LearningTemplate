/**
 * @file YawUtil.h
 * @brief 회전 · 월드 행렬이 +Z 를 돌린 방향의 요(라디안)를 구하는 도우미입니다.
 */
#pragma once
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct YawUtil
     * @brief 앞(+Z)의 요를 `atan2( x, z )` 로 구하는 static 도우미입니다. 요 규칙이 한 곳에 있어 호출처마다 같은 식을 다시 적지 않습니다.
     */
    struct YawUtil
    {
        /** @brief 회전이 +Z 를 돌린 방향의 요(라디안)입니다. */
        [[nodiscard]] static float32 fromRotation( const quaternion& rotation )
        {
            const float3 forward = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, rotation );
            return MathUtil::atan2( forward._x, forward._z );
        }

        /** @brief 월드 행렬의 앞(+Z)의 요(라디안)입니다. */
        [[nodiscard]] static float32 fromWorld( const float4x4& world )
        {
            const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
            return MathUtil::atan2( forward._x, forward._z );
        }
    };
} // namespace sw
