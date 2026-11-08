/**
 * @file post_grade.hlsli
 * @brief 뷰의 후처리 값(PostProcessValues)을 톤맵 앞뒤에서 적용합니다 — 원본을 읽는 자리에서 렌즈(왜곡 · 색수차 · 선명화), 톤맵 앞 노출 · 컬러 그레이딩,
 *        뒤에 LUT · 필름 그레인 · 채도 · 대비 · 비네트 · 레터박스.
 * @details 값은 PassCB 의 g_CameraGrade · g_CameraFrame 이고 엔진이 뷰마다 채운다(FramePassExecutor::applyViewPassConstants).
 *          중립 값(노출 배율 1, 채도 1, 대비 1, 비네트 0, 레터박스 0)이면 두 함수가 색을 바꾸지 않는다 — 카메라 값을 쓰지 않는 게임의 화면이 그대로다.
 *          톤맵을 하는 두 셰이더(tonemap.hlsl, post_chain.hlsl 의 SW_POST_TONEMAP)가 같은 함수를 부른다.
 */
#ifndef SW_ENGINE_POSTGRADE_HLSLI
#define SW_ENGINE_POSTGRADE_HLSLI

#include "color_vision.hlsli"

struct CameraGrade
{
    static float3 applyExposure( float3 hdrColor );
    static float3 correctRange( float3 color, float4 saturation, float4 contrast, float4 gamma, float4 gain, float4 offset );
    static float3 applyColorGrading( float3 hdrColor );
    static float2 computeLutUv( float3 color, float size, float slice );
    static float3 applyLut( float3 color );
    static float computeLensRadius( float2 uv );
    static float3 applyLens( float2 uv, float2 texel, float3 sourceColor );
    static float3 applyFilmGrain( float2 pixelPosition, float3 color );
    static float3 apply( float2 uv, float3 color );
};

/// @brief 휘도 가중치(Rec.709 선형)입니다. 그레이딩의 범위 무게와 채도가 쓴다.
static const float3 kGradeLumaWeights = float3( 0.2126f, 0.7152f, 0.0722f );

/**
 * @brief 톤맵 앞의 HDR 색에 카메라 노출 배율과 자동 노출을 곱합니다.
 * @details 자동 노출은 이번 EV(auto_exposure.hlsl 이 쓴 1x1)의 2^-EV 다. 재지 않는 뷰는 g_AutoExposureIndex 가 무효라 EV 0(배율 1)이다 —
 *          Bindless::sampleIndexWith 가 무효 번호에 0 을 돌려준다. 고정 노출(카메라 범위가 한 점)은 C++ 가 g_CameraGrade.x 에 이미 곱했다.
 *          점 샘플로 읽는다 — R32 실수의 선형 필터는 Vulkan 에서 선택 기능이다(텍셀 하나라 값은 같다).
 */
static float3 CameraGrade::applyExposure( float3 hdrColor )
{
    const float autoExposureEv = Bindless::sampleIndexWith( g_AutoExposureIndex, SW_SAMPLER_POINT_CLAMP, float2( 0.5f, 0.5f ) ).r;
    return hdrColor * ( g_CameraGrade.x * exp2( -autoExposureEv ) );
}

/**
 * @brief 범위 하나의 보정입니다(언리얼 PostProcessCombineLUTs 의 ColorCorrect 와 같은 순서) — 채도, 중간 회색 0.18 을 축으로 한 대비, 감마, 게인, 오프셋.
 * @details 인자는 C++ 가 전역 · 범위 값과 w 를 미리 합친 rgb 다(`PostGradeConstants::make`). 모두 중립(1 · 1 · 1 · 1 · 0)이면 색이 그대로다.
 */
static float3 CameraGrade::correctRange( float3 color, float4 saturation, float4 contrast, float4 gamma, float4 gain, float4 offset )
{
    const float luminance = dot( color, kGradeLumaWeights );
    color                 = max( 0.0f, lerp( luminance.xxx, color, saturation.rgb ) );
    color                 = pow( color * ( 1.0f / 0.18f ), contrast.rgb ) * 0.18f;
    color                 = pow( color, 1.0f / max( gamma.rgb, 1e-4f ) );
    return color * gain.rgb + offset.rgb;
}

/**
 * @brief 노출 뒤 · 톤맵 앞의 선형 HDR 색에 화이트 밸런스와 범위별 보정(그림자 · 중간 · 밝은 부분)을 겁니다.
 * @details 범위 무게는 화이트 밸런스 뒤 휘도로 정한다 — 그림자 1 - smoothstep( 0, 그림자 끝 ), 밝은 부분 smoothstep( 시작, 1 ), 중간은 나머지(언리얼 ColorCorrectAll).
 *          값이 모두 중립이면 C++ 가 g_GradeParams.z 를 0 으로 두어 계산하지 않는다(뷰마다 같은 값이라 분기가 흩어지지 않는다).
 */
static float3 CameraGrade::applyColorGrading( float3 hdrColor )
{
    float3 graded = hdrColor;
    SW_BRANCH if ( g_GradeParams.z > 0.5f )
    {
        const float3 balanced         = max( 0.0f, float3( dot( g_GradeWhiteBalance0.xyz, hdrColor ), dot( g_GradeWhiteBalance1.xyz, hdrColor ), dot( g_GradeWhiteBalance2.xyz, hdrColor ) ) );
        const float  luminance        = dot( balanced, kGradeLumaWeights );
        const float  shadowsWeight    = 1.0f - smoothstep( 0.0f, max( g_GradeParams.x, 1e-4f ), luminance );
        const float  highlightsWeight = smoothstep( min( g_GradeParams.y, 0.9999f ), 1.0f, luminance );
        const float  midtonesWeight   = 1.0f - shadowsWeight - highlightsWeight;
        const float3 shadows          = CameraGrade::correctRange( balanced, g_GradeShadowsSaturation, g_GradeShadowsContrast, g_GradeShadowsGamma, g_GradeShadowsGain, g_GradeShadowsOffset );
        const float3 midtones         = CameraGrade::correctRange( balanced, g_GradeMidtonesSaturation, g_GradeMidtonesContrast, g_GradeMidtonesGamma, g_GradeMidtonesGain, g_GradeMidtonesOffset );
        const float3 highlights       = CameraGrade::correctRange( balanced, g_GradeHighlightsSaturation, g_GradeHighlightsContrast, g_GradeHighlightsGamma, g_GradeHighlightsGain, g_GradeHighlightsOffset );
        graded                        = shadows * shadowsWeight + midtones * midtonesWeight + highlights * highlightsWeight;
    }
    return graded;
}

/** @brief LUT 띠(가로 N² × 세로 N)에서 색의 파랑 조각 @p slice 안 텍셀 중심 UV 입니다. C++ `PostGradeConstants::computeLutUv` 와 같은 식이다(단위 테스트가 그쪽을 본다). */
static float2 CameraGrade::computeLutUv( float3 color, float size, float slice )
{
    const float red   = saturate( color.r ) * ( size - 1.0f );
    const float green = saturate( color.g ) * ( size - 1.0f );
    return float2( ( slice * size + red + 0.5f ) / ( size * size ), ( green + 0.5f ) / size );
}

/**
 * @brief 톤맵한 [0,1] 색을 컬러 그레이딩 LUT(g_ColorGradingLutIndex)로 바꿔 세기만큼 섞습니다. 파랑은 이웃 두 조각을 읽어 섞는다(조각 사이는 필터가 섞지 않는다).
 * @details LUT 가 없거나(읽는 중 · 경로 없음) 세기 0 이면 g_GradeLut.x 가 0 이라 읽지 않는다.
 */
static float3 CameraGrade::applyLut( float3 color )
{
    float3 result = color;
    SW_BRANCH if ( g_GradeLut.x > 0.5f )
    {
        const float  size    = g_GradeLut.x;
        const float3 clamped = saturate( color );
        const float  blue    = clamped.b * ( size - 1.0f );
        const float  slice0  = floor( blue );
        const float  slice1  = min( slice0 + 1.0f, size - 1.0f );
        const float3 lower   = Bindless::sampleIndex( g_ColorGradingLutIndex, CameraGrade::computeLutUv( clamped, size, slice0 ) ).rgb;
        const float3 upper   = Bindless::sampleIndex( g_ColorGradingLutIndex, CameraGrade::computeLutUv( clamped, size, slice1 ) ).rgb;
        result               = lerp( color, lerp( lower, upper, blue - slice0 ), g_GradeLut.y );
    }
    return result;
}

/** @brief 화면 가운데에서 @p uv 까지의 거리 제곱입니다 — 가로/세로 비율을 보정해 원이고, 모서리가 1 이다. */
static float CameraGrade::computeLensRadius( float2 uv )
{
    const float  aspect   = max( g_CameraFrame.y, 1e-4f );
    const float2 centered = ( uv - 0.5f ) * float2( aspect, 1.0f );
    return dot( centered, centered ) / ( 0.25f * ( aspect * aspect + 1.0f ) );
}

/**
 * @brief 원본을 읽는 자리의 렌즈 효과입니다 — 렌즈 왜곡(UV 를 반경으로 옮긴다), 색수차(빨강 · 파랑을 반경 방향으로 비켜 읽는다), 선명화(이웃 넷과의 차이를 더한다).
 * @param sourceColor 부르는 쪽이 @p uv 에서 이미 읽은 원본 색 — 세 효과가 모두 0 이면 그대로 돌려준다(값 선택, 원본을 다시 읽지 않는다)
 * @details 언리얼은 색수차(SceneColorFringe) · 그레인 · 선명화를 톤맵 패스 안에서 한다 — 같은 자리다. 왜곡은 양수가 술통형이고, 모서리가 모서리를 짚도록 줄여 화면 밖을 읽지 않는다.
 */
static float3 CameraGrade::applyLens( float2 uv, float2 texel, float3 sourceColor )
{
    float3 color = sourceColor;
    SW_BRANCH if ( any( g_LensParams != 0.0f ) )
    {
        const float  radius2    = CameraGrade::computeLensRadius( uv );
        const float  distortion = g_LensParams.z;
        const float2 centered   = ( uv - 0.5f ) * ( 1.0f + distortion * radius2 ) / ( 1.0f + max( distortion, 0.0f ) );
        const float2 lensUv     = 0.5f + centered;
        const float3 center     = PassTexture::sampleSource( lensUv ).rgb;
        color                   = center;
        const float startRadius = g_LensParams.y;
        const float fringe      = g_LensParams.x * 0.01f * saturate( ( sqrt( radius2 ) - startRadius ) / max( 1.0f - startRadius, 1e-4f ) );
        SW_BRANCH if ( fringe > 0.0f )
        {
            color.r = PassTexture::sampleSource( 0.5f + centered * ( 1.0f - fringe ) ).r;
            color.b = PassTexture::sampleSource( 0.5f + centered * ( 1.0f + fringe ) ).b;
        }
        SW_BRANCH if ( g_LensParams.w > 0.0f )
        {
            const float3 neighbors = ( PassTexture::sampleSource( lensUv + float2( texel.x, 0.0f ) ).rgb + PassTexture::sampleSource( lensUv - float2( texel.x, 0.0f ) ).rgb +
                                       PassTexture::sampleSource( lensUv + float2( 0.0f, texel.y ) ).rgb + PassTexture::sampleSource( lensUv - float2( 0.0f, texel.y ) ).rgb ) *
                                     0.25f;
            color = max( 0.0f, color + ( center - neighbors ) * g_LensParams.w );
        }
    }
    return color;
}

/**
 * @brief 톤맵한 [0,1] 색에 필름 그레인을 더합니다. 그레인은 `g_GrainParams.y` 픽셀 칸마다 하나이고 프레임마다 바뀐다(머티리얼 시계 — 고정 프레임 시간이면 같은 프레임이 같은 그레인).
 * @details 정수 해시라 백엔드마다 같은 값이다(sin 해시는 GPU 마다 정밀도가 달라 골든이 갈린다). 중간 밝기에서 가장 세고 검정 · 흰색에서 사라진다(언리얼 그레인의 범위 응답).
 */
static float3 CameraGrade::applyFilmGrain( float2 pixelPosition, float3 color )
{
    float3 result = color;
    SW_BRANCH if ( g_GrainParams.x > 0.0f )
    {
        const uint2 cell  = uint2( max( pixelPosition / max( g_GrainParams.y, 1.0f ), 0.0f ) );
        const uint  frame = uint( max( g_SwMaterialTime, 0.0f ) * 60.0f );
        uint        hash  = cell.x * 1973u + cell.y * 9277u + frame * 26699u;
        hash              = ( hash ^ 61u ) ^ ( hash >> 16 );
        hash *= 9u;
        hash ^= hash >> 4;
        hash *= 0x27d4eb2du;
        hash ^= hash >> 15;
        const float noise     = float( hash & 0xFFFFu ) / 65535.0f - 0.5f;
        const float luminance = dot( saturate( color ), kGradeLumaWeights );
        const float response  = 4.0f * luminance * ( 1.0f - luminance );
        result                = saturate( color + noise * g_GrainParams.x * response );
    }
    return result;
}

/** @brief 톤맵 뒤의 [0,1] 색에 채도 · 대비 · 비네트 · 색각 보정 · 레터박스를 적용합니다. */
static float3 CameraGrade::apply( float2 uv, float3 color )
{
    // 채도: 휘도(Rec.709)에서 멀어지는 정도를 곱한다. 대비: 화면 중간 회색(0.5)을 축으로 늘린다.
    const float luminance = dot( color, float3( 0.2126f, 0.7152f, 0.0722f ) );
    color                 = lerp( luminance.xxx, color, g_CameraGrade.y );
    color                 = saturate( ( color - 0.5f ) * g_CameraGrade.z + 0.5f );

    // 비네트: 화면 가운데에서 모서리로 갈수록 어둡게 한다(가로/세로 비율을 보정해 원으로 만든다). 세기 0 이면 1 을 곱한다.
    const float  aspect   = max( g_CameraFrame.y, 1e-4f );
    const float2 centered = ( uv - 0.5f ) * float2( aspect, 1.0f ) / max( aspect, 1.0f );
    const float  falloff  = saturate( dot( centered, centered ) * 2.0f );
    color *= 1.0f - g_CameraGrade.w * falloff;

    // 색각 보정(gv_colorVisionMode) — 화면 · 호스트 출력 뷰만 z 에 방식이 실리고 렌더 텍스처 뷰는 0 이다(주 화면에서 그 텍스처가 다시 보정되므로).
    color = ColorVision::applyCorrection( color, (uint)g_CameraFrame.z );

    // 레터박스: 남길 비율이 화면보다 넓으면 위아래, 좁으면 좌우를 검게 지운다. 0 이면 끈다. 고르기는 early-return 이 아니라 값 선택이다.
    const float targetAspect = g_CameraFrame.x;
    const bool  bLetterbox   = targetAspect > 0.0f && g_CameraFrame.y > 0.0f;
    const float visibleY     = bLetterbox ? saturate( g_CameraFrame.y / targetAspect ) : 1.0f;
    const float visibleX     = bLetterbox ? saturate( targetAspect / g_CameraFrame.y ) : 1.0f;
    const bool  bInsideY     = abs( uv.y - 0.5f ) <= visibleY * 0.5f;
    const bool  bInsideX     = abs( uv.x - 0.5f ) <= visibleX * 0.5f;
    return ( bInsideY && bInsideX ) ? color : float3( 0.0f, 0.0f, 0.0f );
}

#endif // SW_ENGINE_POSTGRADE_HLSLI
