/**
 * @file InertiaSettings.h
 * @brief 관성 이동의 몸 수치(질량 · 중력 · 경사 · 구름 저항 · 공기 저항)와 진행 방향 바깥 가속도 계산 한 벌입니다.
 * @details 바디 없는 탈것(`KinematicVehicleMotor`), 말(`MountMovementComponent`), 롤러코스터 열차(`CoasterTrain`)가 같은 식을 쓴다 — 경사에서 빨라지고 느려지는 규칙이 한 곳이다.
 *          구동(페달 · 다리)의 출력 한계(`_maxPower`, 일률 P = F·v)도 여기 있다 — 가속 한계가 경사 당김보다 커도 출력이 모자라면 오르막에서 처진다
 *          (언리얼 Chaos Vehicle 의 엔진 토크 곡선 · 유니티 WheelCollider motorTorque 와 같은 자리를 바디 없는 모델에서는 일률 하나로 둔다).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 관성 이동의 몸 수치입니다. 탈것 · 말 · 썰매가 같은 계산(`computeAlongAcceleration`)을 씁니다.
     * @details 무거운 썰매는 `_mass` 한 칸이다 — 경사 중력과 구름 저항은 질량과 상관없고 공기 저항 · 구동력 · 브레이크만 질량으로 나뉘므로 무거울수록 내리막 끝속도가 높다.
     */
    REFLECT()
    struct SW_GF_API InertiaSettings
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.01, Units = kg, Tooltip = "Mass; drive force, brake force and air drag divide by it" )
        float32 _mass{ 200.0f };
        PROPERTY( Min = 0.0, Units = "m/s2", Tooltip = "Gravity for the slope pull and the rolling resistance" )
        float32 _gravity{ 9.81f };
        PROPERTY( Min = 0.0, Tooltip = "Slope pull scale (1 = the full tangent part of gravity, 0 = slopes do not pull)" )
        float32 _slopeScale{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Scale on the surface rolling resistance (wheels or runners)" )
        float32 _rollingResistanceScale{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Air drag coefficient; deceleration = coefficient * speed^2 / mass" )
        float32 _airDrag{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Drive power limit in watts (power = force * speed); drive acceleration cannot exceed power / (mass * speed), so steep climbs slow the vehicle. 0 = no limit" )
        float32 _maxPower{ 0.0f };

        /** @brief 출력 한계를 볼 때 이 속도(m/s) 아래는 이 속도로 봅니다 — 서 있을 때(속도 0) 한계가 무한대가 되지 않게(출발은 가속 한계가 정한다). */
        static constexpr float32 kPowerMinSpeed = 1.0f;

        /**
         * @brief 진행 방향 속도 @p speed 에 걸리는 바깥 가속도(m/s²)입니다. 경사 중력 − 구름 저항 − 공기 저항이며 구동은 빠져 있습니다.
         * @param slopeSin 진행 방향의 오르막 사인(+ 오르막, − 내리막)입니다.
         * @param rollingResistance 디딘 표면의 구름 저항 계수입니다. 땅에 닿지 않았으면 0 을 넘깁니다.
         * @param deltaTime 구름 저항이 속도의 부호를 뒤집지 않게 한 걸음 안에서 자를 때 씁니다(정지 마찰).
         */
        float32 computeAlongAcceleration( float32 speed, float32 slopeSin, float32 rollingResistance, float32 deltaTime ) const;
        /**
         * @brief 구동 가속도 @p driveAcceleration(m/s²)을 출력 한계로 자릅니다 — 속도 @p speed 에서 낼 수 있는 가속도는 `_maxPower / (_mass · |speed|)` 까지입니다.
         * @details `_maxPower` 가 0 이면 그대로 돌려준다. 오르막의 평형 속도는 `_maxPower / (_mass · _gravity · 사인)` 이다(말이 비탈에서 걸음새 속도를 잃는 자리).
         */
        float32 limitDriveAcceleration( float32 driveAcceleration, float32 speed ) const;
    };
} // namespace sw
