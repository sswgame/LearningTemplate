/**
 * @file PostProcessValues.h
 * @brief 화면 후처리 값과 칸마다의 덮어쓰기 표시입니다. 카메라 · 후처리 볼륨이 갖고, 렌더 뷰가 섞은 값을 렌더러로 나릅니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Serializer/DataPath.h"

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

        /**
         * @brief 컬러 그레이딩(언리얼 Color Grading · White Balance)입니다. 노출 뒤 · 톤맵 앞의 선형 HDR 색에 화이트 밸런스, 범위별 채도 · 대비 · 감마 · 게인 · 오프셋을 걸고,
         *        톤맵 뒤에 LUT 를 섞는다(`post_grade.hlsli` 의 `CameraGrade::applyColorGrading` · `applyLut`). 범위 값은 rgb 와 w(모든 채널에 곱하거나 더하는 값)이고
         *        전역(`_color*`)이 범위(`_shadows*` · `_midtones*` · `_highlights*`)에 곱해진다(오프셋은 더해진다). 모든 기본값은 중립이다.
         */
        PROPERTY( Min = 1500.0, Max = 15000.0, Tooltip = "White balance temperature of the scene light; 6500 is neutral, lower makes the image cooler", Units = K )
        float32 _whiteTemperature{ 6500.0f };
        PROPERTY( Min = -1.0, Max = 1.0, Tooltip = "White balance tint; 0 is neutral, positive compensates a green cast (more magenta)" )
        float32 _whiteTint{ 0.0f };
        PROPERTY( Tooltip = "Global Saturation (rgb per channel, w all channels); 1 keeps the colour" )
        float4 _colorSaturation{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Global Contrast around mid grey 0.18 (rgb, w); 1 keeps the contrast" )
        float4 _colorContrast{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Global Gamma (rgb, w); 1 keeps the curve" )
        float4 _colorGamma{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Global Gain multiplier (rgb, w); 1 keeps the colour" )
        float4 _colorGain{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Global Offset added (rgb, w added to all channels); 0 keeps the colour" )
        float4 _colorOffset{ 0.0f, 0.0f, 0.0f, 0.0f };
        PROPERTY( Tooltip = "Shadows Saturation (rgb per channel, w all channels); 1 keeps the colour" )
        float4 _shadowsSaturation{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Shadows Contrast around mid grey 0.18 (rgb, w); 1 keeps the contrast" )
        float4 _shadowsContrast{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Shadows Gamma (rgb, w); 1 keeps the curve" )
        float4 _shadowsGamma{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Shadows Gain multiplier (rgb, w); 1 keeps the colour" )
        float4 _shadowsGain{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Shadows Offset added (rgb, w added to all channels); 0 keeps the colour" )
        float4 _shadowsOffset{ 0.0f, 0.0f, 0.0f, 0.0f };
        PROPERTY( Tooltip = "Midtones Saturation (rgb per channel, w all channels); 1 keeps the colour" )
        float4 _midtonesSaturation{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Midtones Contrast around mid grey 0.18 (rgb, w); 1 keeps the contrast" )
        float4 _midtonesContrast{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Midtones Gamma (rgb, w); 1 keeps the curve" )
        float4 _midtonesGamma{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Midtones Gain multiplier (rgb, w); 1 keeps the colour" )
        float4 _midtonesGain{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Midtones Offset added (rgb, w added to all channels); 0 keeps the colour" )
        float4 _midtonesOffset{ 0.0f, 0.0f, 0.0f, 0.0f };
        PROPERTY( Tooltip = "Highlights Saturation (rgb per channel, w all channels); 1 keeps the colour" )
        float4 _highlightsSaturation{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Highlights Contrast around mid grey 0.18 (rgb, w); 1 keeps the contrast" )
        float4 _highlightsContrast{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Highlights Gamma (rgb, w); 1 keeps the curve" )
        float4 _highlightsGamma{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Highlights Gain multiplier (rgb, w); 1 keeps the colour" )
        float4 _highlightsGain{ 1.0f, 1.0f, 1.0f, 1.0f };
        PROPERTY( Tooltip = "Highlights Offset added (rgb, w added to all channels); 0 keeps the colour" )
        float4 _highlightsOffset{ 0.0f, 0.0f, 0.0f, 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Scene luminance where the shadow range has faded out (linear, before tonemapping)" )
        float32 _shadowsMax{ 0.09f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Scene luminance where the highlight range starts (linear, before tonemapping)" )
        float32 _highlightsMin{ 0.5f };
        /// @brief 컬러 그레이딩 LUT 텍스처(가로 N² × 세로 N 띠, 언리얼 256 × 16 과 같은 배치)입니다. 비면 LUT 가 없다. 톤맵한 [0,1] 색을 주소로 읽는다.
        PROPERTY( Tooltip = "Colour grading LUT texture (N*N wide, N tall strip, e.g. 256 x 16); empty is none" )
        DataPath _colorGradingLut;
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "How much of the LUT result replaces the colour; 0 is off" )
        float32 _colorGradingLutIntensity{ 0.0f };

        /**
         * @brief 렌즈 · 필름 효과입니다(언리얼 Lens · Film Grain 칸). 모두 톤맵하는 셰이더 안에서 돈다(`CameraGrade::applyLens` · `applyFilmGrain`) — 원본을 읽는 자리에서
         *        왜곡 · 색수차 · 선명화를 하고, 톤맵 뒤에 그레인을 더한다. 따로 패스를 두면 720p 왕복 하나(약 57 us)가 늘어난다. 세기가 모두 0 이면 셰이더가 건너뛴다.
         */
        PROPERTY( Min = 0.0, Max = 5.0, Tooltip = "Chromatic aberration (colour fringing towards the screen edges); 0 is off" )
        float32 _chromaticAberrationIntensity{ 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Distance from the centre (0 centre, 1 corner) where chromatic aberration starts" )
        float32 _chromaticAberrationStart{ 0.0f };
        PROPERTY( Min = -1.0, Max = 1.0, Tooltip = "Lens distortion; positive is barrel, negative is pincushion, 0 is off" )
        float32 _lensDistortion{ 0.0f };
        PROPERTY( Min = 0.0, Max = 2.0, Tooltip = "Sharpen (unsharp mask on the source colour); 0 is off" )
        float32 _sharpen{ 0.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Film grain intensity; 0 is off" )
        float32 _filmGrainIntensity{ 0.0f };
        PROPERTY( Min = 1.0, Max = 8.0, Tooltip = "Film grain size in pixels" )
        float32 _filmGrainSize{ 1.0f };

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
        PROPERTY()
        uint8 _bWhiteTemperature : 1;
        PROPERTY()
        uint8 _bWhiteTint : 1;
        PROPERTY()
        uint8 _bColorSaturation : 1;
        PROPERTY()
        uint8 _bColorContrast : 1;
        PROPERTY()
        uint8 _bColorGamma : 1;
        PROPERTY()
        uint8 _bColorGain : 1;
        PROPERTY()
        uint8 _bColorOffset : 1;
        PROPERTY()
        uint8 _bShadowsSaturation : 1;
        PROPERTY()
        uint8 _bShadowsContrast : 1;
        PROPERTY()
        uint8 _bShadowsGamma : 1;
        PROPERTY()
        uint8 _bShadowsGain : 1;
        PROPERTY()
        uint8 _bShadowsOffset : 1;
        PROPERTY()
        uint8 _bMidtonesSaturation : 1;
        PROPERTY()
        uint8 _bMidtonesContrast : 1;
        PROPERTY()
        uint8 _bMidtonesGamma : 1;
        PROPERTY()
        uint8 _bMidtonesGain : 1;
        PROPERTY()
        uint8 _bMidtonesOffset : 1;
        PROPERTY()
        uint8 _bHighlightsSaturation : 1;
        PROPERTY()
        uint8 _bHighlightsContrast : 1;
        PROPERTY()
        uint8 _bHighlightsGamma : 1;
        PROPERTY()
        uint8 _bHighlightsGain : 1;
        PROPERTY()
        uint8 _bHighlightsOffset : 1;
        PROPERTY()
        uint8 _bShadowsMax : 1;
        PROPERTY()
        uint8 _bHighlightsMin : 1;
        PROPERTY()
        uint8 _bColorGradingLut : 1;
        PROPERTY()
        uint8 _bColorGradingLutIntensity : 1;
        PROPERTY()
        uint8 _bChromaticAberrationIntensity : 1;
        PROPERTY()
        uint8 _bChromaticAberrationStart : 1;
        PROPERTY()
        uint8 _bLensDistortion : 1;
        PROPERTY()
        uint8 _bSharpen : 1;
        PROPERTY()
        uint8 _bFilmGrainIntensity : 1;
        PROPERTY()
        uint8 _bFilmGrainSize : 1;
    };
} // namespace sw
