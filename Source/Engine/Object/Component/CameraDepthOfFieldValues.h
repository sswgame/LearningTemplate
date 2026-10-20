/**
 * @file CameraDepthOfFieldValues.h
 * @brief 카메라 하나의 피사계 심도 렌즈 값(초점 거리 · 조리개)입니다. 카메라 컴포넌트가 갖고, 렌더 뷰가 렌더러로 나릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct CameraDepthOfFieldValues
     * @brief 카메라 하나의 피사계 심도 렌즈 값입니다 — 초점이 맞는 거리와 조리개 f 값.
     * @details 언리얼 CineCameraComponent 의 FocusSettings · CurrentAperture 에 해당한다.
     *          값 타입을 엔진(Object 티어)에 두어 렌더러가 아래로 읽는다(CheckEngineLayers). 게임 프레임워크의 프리셋 `<Lens focusDistance · aperture>` 와
     *          포즈 블렌드가 이 값을 쓰고, `CameraPoseUtil::applyLensToCamera` 가 카메라 컴포넌트(`CameraComponent::setDepthOfField`)에,
     *          수집기가 `RenderViewSettings::_depthOfField` 에 싣는다. 읽는 패스(피사계 심도)는 아직 없다(계획 CameraRendererIntegration S2).
     */
    REFLECT()
    struct SW_API CameraDepthOfFieldValues
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Distance the lens focuses at", Units = m )
        float32 _focusDistance{ 10.0f };
        PROPERTY( Min = 0.5, Tooltip = "Aperture f-number; smaller is a shallower depth of field" )
        float32 _aperture{ 2.8f };
    };
} // namespace sw
