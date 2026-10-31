/**
 * @file post_grade.hlsli
 * @brief 카메라별 후처리 값(CameraPostProcessValues)을 톤맵 앞뒤에서 적용합니다 — 노출은 앞, 채도 · 대비 · 비네트 · 레터박스는 뒤.
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
    static float3 apply( float2 uv, float3 color );
};

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
