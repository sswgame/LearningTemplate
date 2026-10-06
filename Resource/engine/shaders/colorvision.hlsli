/**
 * @file colorvision.hlsli
 * @brief 색각 보정(Daltonization)을 **함수 하나**로 내놓는다 — 캔버스(UI, 톤맵 뒤)가 쓰고, 톤맵 쪽도 같은 함수를 쓴다.
 * @details 방식(`gv_colorVisionMode`): 0 끔 · 1 적색약(protanopia) · 2 녹색약(deuteranopia) · 3 청색약(tritanopia).
 *          그 눈이 보는 색을 Machado 2009(결핍 정도 1.0)의 행렬로 흉내 내고, 보이지 않는 차이(원래 − 흉내)를 보이는 채널로 옮겨 더한다
 *          (Fidaner 2005 의 오차 재분배 — 언리얼 `r.Color.Deficiency` 의 보정과 같은 갈래). 적 · 녹색약은 빨강 쪽 오차를 초록 · 파랑으로,
 *          청색약은 파랑 쪽 오차를 빨강 · 초록으로 옮긴다. 엔진은 감마 인코딩 없이 화면 값을 다루므로(톤맵이 UNORM 에 바로 쓴다) 받은 값에 그대로 건다.
 */
#ifndef SW_ENGINE_COLORVISION_HLSLI
#define SW_ENGINE_COLORVISION_HLSLI

static const uint kColorVisionOff          = 0u;
static const uint kColorVisionProtanopia   = 1u;
static const uint kColorVisionDeuteranopia = 2u;
static const uint kColorVisionTritanopia   = 3u;

/** @brief 결핍 눈이 보는 색으로 옮기는 행렬입니다(행 우선 — mul( 행렬, 색 )). 모르는 방식은 단위 행렬입니다. */
float3x3 swGetColorVisionSimulation( uint mode )
{
	if ( mode == kColorVisionProtanopia )
		return float3x3( 0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f );
	if ( mode == kColorVisionDeuteranopia )
		return float3x3( 0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f );
	if ( mode == kColorVisionTritanopia )
		return float3x3( 1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f );
	return float3x3( 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f );
}

/**
 * @brief 색 @p color 를 방식 @p mode 로 보정해 돌려줍니다(0..1 로 자른다). 끔 · 모르는 방식이면 그대로입니다.
 * @details 고르기는 early-return 이 아니라 값 선택이다(GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다 — binding.hlsli swComputeMorphElement).
 */
float3 swApplyColorVisionCorrection( float3 color, uint mode )
{
	const float3 simulated = mul( swGetColorVisionSimulation( mode ), color );
	const float3 error     = color - simulated;
	const float3 redShift  = float3( 0.0f, 0.7f * error.r + error.g, 0.7f * error.r + error.b );
	const float3 blueShift = float3( error.r + 0.7f * error.b, error.g + 0.7f * error.b, 0.0f );
	const float3 corrected = saturate( color + ( ( mode == kColorVisionTritanopia ) ? blueShift : redShift ) );
	const bool   bActive   = mode >= kColorVisionProtanopia && mode <= kColorVisionTritanopia;
	return bActive ? corrected : color;
}

#endif // SW_ENGINE_COLORVISION_HLSLI
