/**
 * @file OrientationUtil.h
 * @brief 앞 · 위 방향으로 엔진의 오일러 회전(피치 · 요 · 롤, 라디안)을 구합니다 — `SceneComponent::setLocalRotation` 에 그대로 넣습니다.
 */
#pragma once
#include "Core/Math/Math.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct OrientationUtil
     * @brief 방향 벡터 → 오일러 회전입니다. `CameraComponent::lookAt` 은 요 · 피치만 구해 롤이 없습니다 — 루프를 도는 코스터 차량 · 탑승 카메라 ·
     *        총처럼 **위 방향까지** 맞춰야 하는 것이 씁니다.
     * @details 엔진 규칙(`quaternion::createFromYawPitchRoll` — 롤 → 피치 → 요, +Z 앞 · +Y 위)의 역입니다: 요 = atan2( f.x, f.z ), 피치 = −asin( f.y ),
     *          롤 = 롤 없는 위 · 오른쪽에 대한 @p up 의 각도. 앞이 거의 수직이면 요가 정해지지 않으므로 위 방향에서 요를 고르고 롤을 0 으로 둡니다.
     *          돌려준 값의 `_x` 가 피치, `_y` 가 요, `_z` 가 롤입니다(`quaternion::getEulerAngles` 와 같은 배치). 부모가 없는(루트) 컴포넌트에 씁니다.
     */
    struct SW_GF_API OrientationUtil
    {
        static float3 computeEulerFromForwardUp( const float3& forward, const float3& up );
        /** @brief 각(라디안)을 [-π, π] 로 맞춥니다. */
        static float32 wrapAngle( float32 angle );
        /** @brief @p current 를 @p target 쪽으로 짧은 길로 최대 @p maxStep(라디안, 0 이상)만큼 돌린 각입니다 — 요가 한 프레임에 튀지 않게 돌릴 때. */
        static float32 turnTowardAngle( float32 current, float32 target, float32 maxStep );
    };
} // namespace sw
