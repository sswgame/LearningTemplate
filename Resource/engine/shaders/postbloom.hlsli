/**
 * @file postbloom.hlsli
 * @brief 블룸을 **함수 하나**로 내놓는다 — 전용 패스(postbloom.hlsl)와 합친 체인(postchain.hlsl)이 같은 코드를 쓴다.
 * @details 효과를 패스가 아니라 함수로 두는 것이 합치기의 전제다. 전체화면 패스는 픽셀마다
 *          "읽고 · 계산하고 · 쓴다" 인데, 쓰고 다시 읽는 왕복이 계산보다 비싸다(720p 한 왕복이
 *          약 57 us). 함수로 두면 파이프라인 XML 이 퍼뮤테이션으로 켜고 끄면서도 왕복은 한 번이다.
 */
#ifndef SW_POSTBLOOM_HLSLI
#define SW_POSTBLOOM_HLSLI

/**
 * @brief 원본 색에 블룸을 더해 돌려줍니다.
 * @param color 이미 읽어 둔 이 픽셀의 원본 색 — 부르는 쪽이 한 번만 읽게 하려고 인자로 받는다.
 */
float3 SwApplyBloom( float2 uv, float2 texel, float3 color )
{
	// 블러는 **바이리니어 두 번**으로 끝낸다. 텍셀 중심에서 반 칸 비낀 곳을 찍으면 샘플러가 이웃 네
	// 텍셀을 공짜로 평균해 준다 — 두 번이면 여덟 텍셀이다. 여기만은 `SampleSource`(선형)여야 한다:
	// 점 샘플러로 바꾸면 섞이지 않아 블러가 아니게 된다.
	float2 halfTexel = texel * 0.5f;
	float3 blur      = SampleSource( uv - halfTexel ).rgb;
	blur += SampleSource( uv + halfTexel ).rgb;
	blur *= 0.5f;

	float  lum    = max( max( blur.r, blur.g ), blur.b );
	float  soft   = saturate( ( lum - g_BloomParams.x + g_BloomParams.z ) / max( g_BloomParams.z, 1e-4 ) );
	float3 bright = blur * soft * soft * g_BloomParams.y;
	// SSAO 가 있으면 조명 결과를 가린다(파이프라인 XML 이 AOColor 를 입력으로 선언한다). 없으면 1.
	float ao = SampleAmbientOcclusion( uv );
	return color * ao + bright;
}

#endif // SW_POSTBLOOM_HLSLI
