/**
 * @file CameraPostProcessValues.h
 * @brief 카메라 하나가 화면 후처리에 적용하는 값입니다. 카메라 컴포넌트가 갖고, 렌더 뷰가 렌더러로 나릅니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @struct CameraPostProcessValues
     * @brief 카메라 하나가 화면 후처리에 적용하는 값입니다. 모든 값의 기본은 "바꾸지 않음"(중립)입니다.
     * @details 언리얼 CineCameraComponent 의 PostProcessSettings 덮어쓰기와 Cinemachine Volume Settings 에 해당한다.
     *          값이 중립과 같으면 덮어쓰지 않는 것이라 따로 켜고 끄는 표시를 두지 않는다. 그래서 두 카메라 사이를 그냥 직선으로 섞을 수 있다.
     *          카메라 컴포넌트(`CameraComponent::setPostProcess`)가 갖고, 게임 프레임워크의 프리셋 `<PostProcess>` 섹션과 포즈 블렌드가 이 값을 쓴다.
     *          렌더러는 뷰마다(`RenderViewSettings::_postProcess`) 읽는다 — 노출은 톤맵 앞, 채도 · 대비 · 비네트 · 레터박스는 톤맵 뒤(`postgrade.hlsli`),
     *          블룸 배율은 블룸 세기(`g_BloomParams.y`)에 곱한다. 후처리를 끈 뷰와 표면 값을 보이는 보기 모드는 적용하지 않는다.
     */
    REFLECT()
    struct SW_API CameraPostProcessValues
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
        /**
         * @brief 자동 노출이 고를 수 있는 가장 어두운 장면 밝기(EV, 0 = 평균 휘도가 중간 회색 0.18)입니다. 최대와 같으면 측정하지 않는 고정 노출 2^-EV 입니다.
         * @details 언리얼 PostProcessSettings 의 AutoExposureMinBrightness · MaxBrightness(EV100)에 해당한다. 기본 0 · 0 은 노출 1(중립)이라 자동 노출을 쓰지 않는
         *          카메라의 화면이 그대로다. 두 카메라 사이를 섞으면 범위가 직선으로 섞인다. 측정 · 적응은 렌더러가 뷰마다 한다(`FrameRenderer::prepareAutoExposure`).
         */
        PROPERTY( Tooltip = "Darkest scene brightness auto exposure adapts to (EV, 0 = mean luminance 0.18); equal to the maximum is a fixed exposure", Meta = "Units=EV" )
        float32 _autoExposureMinEv{ 0.0f };
        PROPERTY( Tooltip = "Brightest scene brightness auto exposure adapts to (EV, 0 = mean luminance 0.18)", Meta = "Units=EV" )
        float32 _autoExposureMaxEv{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Adaptation speed when the scene gets brighter (1/s, exponential)" )
        float32 _autoExposureSpeedUp{ 3.0f };
        PROPERTY( Min = 0.0, Tooltip = "Adaptation speed when the scene gets darker (1/s, exponential)" )
        float32 _autoExposureSpeedDown{ 1.0f };

        /** @brief 자동 노출이 장면을 측정하는지(최소 EV 가 최대보다 작다) 나타냅니다. 같으면 고정 노출 2^-최소 EV 입니다. */
        bool usesAutoExposure() const { return _autoExposureMinEv < _autoExposureMaxEv; }
    };
} // namespace sw
