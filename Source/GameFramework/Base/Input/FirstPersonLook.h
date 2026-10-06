/**
 * @file FirstPersonLook.h
 * @brief 마우스로 요 · 피치를 돌리는 1인칭 시점 — 슈터 · 복셀 샌드박스 · 걷는 시뮬레이터가 함께 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Utility/RayMath.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class FirstPersonLook
     * @brief 요 · 피치 시점입니다. 피치는 ±최대각에서 자르고(뒤로 넘어가지 않는다) 요는 [-π, π) 로 감습니다.
     * @details 엔진 오일러(`SceneComponent::setLocalRotation`)는 피치가 아래로 + 입니다. 카메라에 넣을 값은 `computeCameraEuler` 가 돌려줍니다.
     *          장르의 규칙이 아니라 기반이다 — 1 인칭 시점이 필요한 키트 · 게임은 슈터 키트를 링크하지 않고 이것을 쓴다.
     */
    class SW_GF_API FirstPersonLook
    {
    public:
        FirstPersonLook();

        /** @brief 마우스 이동(픽셀)을 감도(라디안/픽셀)로 돌립니다. 화면 아래(+y)로 끌면 아래를 본다. */
        void addMouseDelta( float32 deltaX, float32 deltaY, float32 sensitivity );
        /** @brief 반동 — 피치를 올리고(위로 튄다) 요를 조금 흔듭니다(라디안). */
        void addRecoil( float32 pitchKick, float32 yawKick );
        void setAngles( float32 yaw, float32 pitch );
        /** @brief 피치 한계(라디안)입니다. 기본 85°. */
        void setMaxPitch( float32 maxPitch );

        float32 getYaw() const { return _yaw; }
        float32 getPitch() const { return _pitch; }
        float3  getForward() const { return RayMath::computeLookDirection( _yaw, _pitch ); }
        /** @brief 수평 앞(이동용, 피치 없음)입니다. */
        float3 getFlatForward() const { return RayMath::computeLookDirection( _yaw, 0.0f ); }
        /** @brief 수평 오른쪽입니다. */
        float3 getFlatRight() const;
        /** @brief 카메라 · 무기에 넣을 엔진 오일러(피치 · 요 · 롤 0)입니다. */
        float3 computeCameraEuler() const { return float3{ -_pitch, _yaw, 0.0f }; }
        /** @brief 앞 · 오른쪽 축 입력(-1..1)을 수평 이동 방향으로 바꿉니다(길이 1 이하). */
        float3 computeMoveDirection( float32 forwardAxis, float32 rightAxis ) const;

    private:
        float32 _yaw;
        float32 _pitch;
        float32 _maxPitch;
    };
} // namespace sw
