/**
 * @file CameraPose.h
 * @brief 카메라 한 장면의 값 — 자리 · 회전 · 렌즈(시야각 · 직교 높이 · 투영 · 근/원평면 · 초점 · 조리개 · 시프트)와 카메라별 후처리 값입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @struct CameraPostProcessValues
     * @brief 카메라 하나가 화면 후처리에 적용하는 값입니다. 프리셋의 `<PostProcess>` 섹션이고 포즈가 섞어 나릅니다. 모든 값의 기본은 "바꾸지 않음"(중립)입니다.
     * @details 언리얼 CineCameraComponent 의 PostProcessSettings 덮어쓰기와 Cinemachine Volume Settings 에 해당한다.
     *          값이 중립과 같으면 덮어쓰지 않는 것이라 따로 켜고 끄는 표시를 두지 않는다. 그래서 두 카메라 사이를 그냥 직선으로 섞을 수 있다.
     *          렌더러는 아직 카메라별로 읽지 않는다(톤맵 · 블룸 · 비네트 상수가 전역이다). 읽는 쪽이 생기면 렌더 뷰가 이 값을 싣는다.
     */
    REFLECT()
    struct SW_GF_API CameraPostProcessValues
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Exposure compensation; 0 keeps the global exposure", Meta = "Units=EV" )
        float32 _exposureCompensation{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Bloom intensity multiplier; 1 keeps the global bloom" )
        float32 _bloomScale{ 1.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Vignette intensity; 0 is none" )
        float32 _vignetteIntensity{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Saturation multiplier; 1 keeps the colour" )
        float32 _saturation{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Contrast multiplier; 1 keeps the contrast" )
        float32 _contrast{ 1.0f };
        PROPERTY( Min = 0.0, Tooltip = "Aspect ratio left visible between letterbox bars (e.g. 2.39); 0 is no letterbox" )
        float32 _letterboxAspectRatio{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct CameraPose
     * @brief 카메라가 놓일 월드 자리 · 회전과 렌즈입니다. 프리셋 계산 · 블렌드 · 감쇠가 이 값 하나를 주고받습니다(컴포넌트를 모른다).
     * @details 회전은 앞이 +Z, 위가 +Y 인 엔진 규칙(`quaternion::makeFromYawPitchRoll`)입니다. 각은 라디안, 거리는 m 입니다.
     *          초점 거리 · 조리개 · 렌즈 시프트는 피사계 심도와 비대칭 투영이 읽을 값입니다. 렌더러에는 아직 그 패스가 없어 카메라에 쓰지 않습니다.
     */
    struct CameraPose
    {
        quaternion              _rotation{};
        float3                  _position{};
        CameraPostProcessValues _postProcess{};
        float2                  _lensShift{};           ///< 투영 중심을 화면 크기 비율만큼 옮긴다(건축 사진의 시프트 렌즈)
        float32                 _fieldOfViewY{ 0.70f }; ///< 수직 시야각(rad) — 원근일 때
        float32                 _orthoHeight{ 10.0f };  ///< 화면 높이(m) — 직교일 때
        float32                 _nearPlane{ 0.1f };
        float32                 _farPlane{ 100.0f };
        float32                 _focusDistance{ 10.0f }; ///< 초점이 맞는 거리(m)
        float32                 _aperture{ 2.8f };       ///< 조리개 f 값. 작을수록 심도가 얕다
        uint8                   _bOrthographic{ SW_FALSE };
    };
} // namespace sw
