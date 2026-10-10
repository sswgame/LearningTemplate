/**
 * @file CameraPose.h
 * @brief 카메라 한 장면의 값 — 자리 · 회전 · 렌즈(시야각 · 직교 높이 · 투영 · 근/원평면)입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
    /**
     * @struct CameraPose
     * @brief 카메라가 놓일 월드 자리 · 회전과 렌즈입니다. 프리셋 계산 · 블렌드 · 감쇠가 이 값 하나를 주고받습니다(컴포넌트를 모른다).
     * @details 회전은 앞이 +Z, 위가 +Y 인 엔진 규칙(`quaternion::makeFromYawPitchRoll`)입니다. 각은 라디안, 거리는 m 입니다.
     */
    struct CameraPose
    {
        quaternion _rotation{};
        float3     _position{};
        float32    _fieldOfViewY{ 0.70f }; ///< 수직 시야각(rad) — 원근일 때
        float32    _orthoHeight{ 10.0f };  ///< 화면 높이(m) — 직교일 때
        float32    _nearPlane{ 0.1f };
        float32    _farPlane{ 100.0f };
        uint8      _bOrthographic{ SW_FALSE };
    };
} // namespace sw
