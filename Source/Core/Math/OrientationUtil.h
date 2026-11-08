/**
 * @file OrientationUtil.h
 * @brief 회전 ↔ 방향 변환: 앞 방향과 위 방향으로 엔진의 오일러 회전(피치, 요, 롤, 라디안)을 구하고, 회전 · 월드 행렬의 앞(+Z) 요를 구합니다.
 * @details 옛 GameFramework OrientationUtil 과 Core YawUtil 을 합쳤다 — 같은 일(요 규칙 atan2( x, z ))이 두 타깃에 있었다(언리얼 FRotator · FRotationMatrix 가 한 자리).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct OrientationUtil
     * @brief 방향 벡터를 오일러 회전으로 바꿉니다. `CameraComponent::lookAt` 은 요와 피치만 구해 롤이 없습니다. 루프를 도는 코스터 차량, 탑승 카메라,
     *        총처럼 **위 방향까지** 맞춰야 하는 것이 씁니다.
     * @details 엔진 규칙(`quaternion::makeFromYawPitchRoll`: 롤, 피치, 요 순, +Z 앞, +Y 위)의 역입니다. 요 = atan2( f.x, f.z ), 피치 = -asin( f.y ),
     *          롤 = 롤 없는 위 · 오른쪽에 대한 @p up 의 각도. 앞이 거의 수직이면 요가 정해지지 않으므로 위 방향에서 요를 고르고 롤을 0 으로 둡니다.
     *          돌려준 값의 `_x` 가 피치, `_y` 가 요, `_z` 가 롤입니다(`quaternion::getEulerAngles` 와 같은 배치). 부모가 없는(루트) 컴포넌트에 씁니다.
     */
    struct SW_API OrientationUtil
    {
        static float3 computeEulerFromForwardUp( const float3& forward, const float3& up );
        /** @brief @p current 를 @p target 쪽으로 짧은 쪽으로 최대 @p maxStep(라디안, 0 이상)만큼 돌린 각입니다. 요가 한 프레임에 튀지 않게 돌릴 때 씁니다. */
        static float32 turnTowardAngle( float32 current, float32 target, float32 maxStep );

        /** @brief 요와 피치(라디안, 피치는 +가 위)가 가리키는 단위 방향입니다. */
        static float3 computeLookDirection( float32 yaw, float32 pitch );
        /**
         * @brief @p point 가 @p origin 에서 @p forward 쪽으로 반각 @p halfAngleDegree(도), 거리 @p range 의 원뿔 안이면 true 입니다. 손전등, 감시 카메라, 화염 방사에 씁니다.
         * @details 3D 각이다. @p forward 는 길이가 0 이 아니면 되며 단위가 아니어도 된다. 꼭짓점과 같은 위치는 안으로 친다.
         */
        static bool isInCone( const float3& origin, const float3& forward, float32 halfAngleDegree, float32 range, const float3& point );
        /** @brief `isInCone` 을 XZ 평면에서 봅니다. 높이를 무시하며 탑다운 시야와 바닥 손전등에 씁니다. */
        static bool isInFlatCone( const float3& origin, const float3& forward, float32 halfAngleDegree, float32 range, const float3& point );

        /** @brief 회전이 +Z 를 돌린 방향의 요(라디안)입니다(요 규칙 `atan2( x, z )`). */
        [[nodiscard]] static float32 computeYawFromRotation( const quaternion& rotation )
        {
            const float3 forward = float3::transform( float3{ 0.0f, 0.0f, 1.0f }, rotation );
            return MathUtil::atan2( forward._x, forward._z );
        }

        /** @brief 월드 행렬의 앞(+Z)의 요(라디안)입니다. */
        [[nodiscard]] static float32 computeYawFromWorld( const float4x4& world )
        {
            const float3 forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
            return MathUtil::atan2( forward._x, forward._z );
        }
    };
} // namespace sw
