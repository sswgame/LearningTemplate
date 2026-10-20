/**
 * @file CameraPose.h
 * @brief 카메라 한 장면의 값입니다. 위치, 회전, 렌즈, 카메라별 후처리 값(엔진의 `CameraPostProcessValues`)을 담습니다.
 * @details 렌즈는 시야각, 직교 높이, 투영, 근평면과 원평면, 초점, 조리개, 시프트입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/CameraDepthOfFieldValues.h"
#include "Engine/Object/Component/CameraPostProcessValues.h"

namespace sw
{
    /**
     * @struct CameraPose
     * @brief 카메라가 놓일 월드 위치, 회전, 렌즈입니다. 프리셋 계산, 블렌드, 감쇠가 이 값 하나를 주고받으며 컴포넌트는 모릅니다.
     * @details 회전은 앞이 +Z, 위가 +Y 인 엔진 규칙(`quaternion::makeFromYawPitchRoll`)입니다. 각은 라디안, 거리는 m 입니다.
     *          후처리 값(`_postProcess`)은 프리셋의 `<PostProcess>` 섹션이고, 포즈를 카메라에 쓰면 카메라 컴포넌트로 가서 렌더러가 뷰마다 읽습니다.
     *          피사계 심도 렌즈 값(`_depthOfField`, 엔진)도 같은 길로 카메라 컴포넌트와 렌더 뷰까지 간다(읽는 패스는 아직 없다).
     *          렌즈 시프트는 비대칭 투영이 읽을 값이라 카메라에 쓰지 않는다.
     */
    struct CameraPose
    {
        quaternion               _rotation{};
        float3                   _position{};
        CameraPostProcessValues  _postProcess{};
        CameraDepthOfFieldValues _depthOfField{};        ///< 초점 거리(m)와 조리개 f 값
        float2                   _lensShift{};           ///< 투영 중심을 화면 크기 비율만큼 옮긴다(건축 사진의 시프트 렌즈)
        float32                  _fieldOfViewY{ 0.70f }; ///< 수직 시야각(rad) — 원근일 때
        float32                  _orthoHeight{ 10.0f };  ///< 화면 높이(m) — 직교일 때
        float32                  _nearPlane{ 0.1f };
        float32                  _farPlane{ 100.0f };
        uint8                    _bOrthographic{ SW_FALSE };
    };
} // namespace sw
