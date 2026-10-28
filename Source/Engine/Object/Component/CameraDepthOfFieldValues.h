/**
 * @file CameraDepthOfFieldValues.h
 * @brief 카메라 하나의 피사계 심도 렌즈 값(초점 거리 · 조리개 · 센서 높이)입니다. 카메라 컴포넌트가 갖고, 렌더 뷰가 렌더러로 나릅니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct CameraDepthOfFieldValues
     * @brief 카메라 하나의 피사계 심도 렌즈 값입니다 — 초점이 맞는 거리, 조리개 f 값, 착란원을 화면 크기로 바꾸는 센서 높이.
     * @details 언리얼 CineCameraComponent 의 FocusSettings · CurrentAperture · Filmback 에 해당한다.
     *          값 타입을 엔진(Object 티어)에 두어 렌더러가 아래로 읽는다. 게임 프레임워크의 프리셋 `<Lens focusDistance · aperture · sensorSize>` 와
     *          포즈 블렌드가 이 값을 쓰고, `CameraPoseUtil::applyLensToCamera` 가 카메라 컴포넌트(`CameraComponent::setDepthOfField`)에,
     *          수집기가 `RenderViewSettings::_depthOfField` 에 싣고, `FrameRenderer::applyViewPassConstants` 가 `g_CameraLens` 로 바꿔 피사계 심도 패스(depthoffield.hlsl)가 읽는다.
     *          조리개 0 은 핀홀로 보아 피사계 심도가 없다 — 기본값이라 렌즈를 적지 않은 카메라는 흐리지 않는다.
     *          섞을 때는 f 값이 아니라 그 역수(구경에 비례)를 섞는다(`blend`). f 값을 그대로 섞으면 0(없음)에서 2.8 로 가는 중간이 f/1.4 라 가장 흐리다.
     */
    REFLECT()
    struct SW_API CameraDepthOfFieldValues
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Distance the lens focuses at", Units = m )
        float32 _focusDistance{ 10.0f };
        PROPERTY( Min = 0.0, Tooltip = "Aperture f-number; smaller is a shallower depth of field, 0 is a pinhole (no depth of field)" )
        float32 _aperture{ 0.0f };
        PROPERTY( Min = 1.0, Tooltip = "Sensor (filmback) height; with the field of view it sets the focal length and so the blur size", Units = mm )
        float32 _sensorHeight{ 24.0f };

        /** @brief 피사계 심도가 있는 렌즈인지 나타냅니다(조리개 · 초점 거리 · 센서 높이가 모두 0 보다 크다). */
        bool hasDepthOfField() const { return _aperture > 0.0f && _focusDistance > 0.0f && _sensorHeight > 0.0f; }

        /**
         * @brief 두 렌즈 값을 섞습니다. 초점 거리 · 센서 높이는 그대로, 조리개는 f 값의 역수로 섞습니다(0 은 역수 0 — 구경이 닫힌 핀홀).
         * @param weight 0 이면 from, 1 이면 to
         */
        static CameraDepthOfFieldValues blend( const CameraDepthOfFieldValues& from, const CameraDepthOfFieldValues& to, float32 weight );
    };
} // namespace sw
