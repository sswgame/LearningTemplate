/**
 * @file PostProcessValues.h
 * @brief 화면 후처리 값과 칸마다의 덮어쓰기 표시입니다. 카메라 · 후처리 볼륨이 갖고, 렌더 뷰가 섞은 값을 렌더러로 나릅니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct PostProcessOverrides;

    /**
     * @struct PostProcessValues
     * @brief 뷰 하나가 화면 후처리에 적용하는 값입니다. 모든 값의 기본은 "바꾸지 않음"(중립)입니다.
     * @details 언리얼 FPostProcessSettings 의 값 칸, Cinemachine Volume Settings 에 해당한다. 카메라 컴포넌트(`CameraComponent::setPostProcess`)의 값이 바탕이고,
     *          카메라가 선 자리의 후처리 볼륨(`PostProcessVolumeComponent`)들이 우선순위 순으로 덮어쓴 칸만 섞인다(`blendOverrides`).
     *          게임 프레임워크의 프리셋 `<PostProcess>` 섹션과 포즈 블렌드(`blend`)도 이 값을 쓴다.
     *          렌더러는 뷰마다(`RenderViewSettings::_postProcess`) 읽는다 — 노출은 톤맵 앞, 채도 · 대비 · 비네트 · 레터박스는 톤맵 뒤(`post_grade.hlsli`),
     *          블룸 배율은 블룸 세기(`g_BloomParams.y`)에 곱한다. 후처리를 끈 뷰와 표면 값을 보이는 보기 모드는 적용하지 않는다.
     *          칸을 더하면 `PostProcessOverrides` 에 같은 차례로 `_b<칸 이름>` 비트를 하나 더한다 — 섞기는 리플렉션으로 두 목록을 나란히 돈다(단위 테스트가 짝을 대조한다).
     */
    REFLECT()
    struct SW_API PostProcessValues
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
         *          카메라의 화면이 그대로다. 두 값 사이를 섞으면 범위가 직선으로 섞인다. 측정 · 적응은 렌더러가 뷰마다 한다(`FramePostProcess::prepareAutoExposure`).
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

        /**
         * @brief 두 값 사이를 모든 칸에서 섞습니다(카메라 포즈 블렌드). 섞을 수 없는 칸(경로)은 절반에서 바꿉니다.
         * @param weight 0 이면 from, 1 이면 to
         */
        static PostProcessValues blend( const PostProcessValues& from, const PostProcessValues& to, float32 weight );
        /**
         * @brief @p inoutValues 의 칸 가운데 @p overrides 가 켠 칸만 @p target 쪽으로 @p weight 만큼 섞습니다(언리얼 볼륨 블렌드의 bOverride_* 칸).
         * @details 섞을 수 없는 칸은 @p weight 가 0.5 이상일 때 바꾼다. 레터박스는 양쪽에 있을 때만 비율을 섞고, 한쪽에만 있으면 절반에서 바꾼다(0 은 없음이라 섞을 비율이 없다).
         */
        static void blendOverrides( PostProcessValues& inoutValues, const PostProcessValues& target, const PostProcessOverrides& overrides, float32 weight );
    };

    /**
     * @struct PostProcessOverrides
     * @brief `PostProcessValues` 의 칸마다 "이 볼륨이 덮어쓴다" 표시입니다. 칸 하나에 비트 하나, 이름은 `_b` + 값 칸 이름(첫 글자 대문자), 차례는 값 칸과 같습니다.
     * @details 언리얼 FPostProcessSettings 의 bOverride_* 와 같다 — 볼륨은 켠 칸만 바꾸고 나머지는 아래(카메라 · 낮은 우선순위 볼륨)의 값이 남는다.
     *          기본은 모두 꺼짐이라 칸을 켜지 않은 볼륨은 화면을 바꾸지 않는다.
     */
    REFLECT()
    struct SW_API PostProcessOverrides
    {
        REFLECT_BODY();

        /** @brief 모든 칸을 끈 표시로 만듭니다. */
        PostProcessOverrides();
        /** @brief 모든 칸을 켠 표시입니다(포즈 블렌드처럼 모든 칸을 섞을 때). */
        static PostProcessOverrides makeAll();

        PROPERTY()
        uint8 _bExposureCompensation : 1;
        PROPERTY()
        uint8 _bBloomScale : 1;
        PROPERTY()
        uint8 _bVignetteIntensity : 1;
        PROPERTY()
        uint8 _bSaturation : 1;
        PROPERTY()
        uint8 _bContrast : 1;
        PROPERTY()
        uint8 _bLetterboxAspectRatio : 1;
        PROPERTY()
        uint8 _bAutoExposureMinEv : 1;
        PROPERTY()
        uint8 _bAutoExposureMaxEv : 1;
        PROPERTY()
        uint8 _bAutoExposureSpeedUp : 1;
        PROPERTY()
        uint8 _bAutoExposureSpeedDown : 1;
    };
} // namespace sw
