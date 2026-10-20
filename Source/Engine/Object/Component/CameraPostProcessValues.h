/**
 * @file CameraPostProcessValues.h
 * @brief 카메라 하나가 화면 후처리에 적용하는 값입니다. 카메라 컴포넌트가 갖고, 렌더 뷰가 렌더러로 나릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

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
    };
} // namespace sw
