/**
 * @file postbloom.hlsli
 * @brief 블룸을 **함수 하나**로 내놓는다 — 전용 패스(postbloom.hlsl)와 합친 체인(postchain.hlsl)이 같은 코드를 쓴다.
 * @details 효과를 패스가 아니라 함수로 두는 것이 합치기의 전제다. 전체화면 패스는 픽셀마다
 *          "읽고 · 계산하고 · 쓴다" 인데, 쓰고 다시 읽는 왕복이 계산보다 비싸다(720p 한 왕복이
 *          약 57 us). 함수로 두면 파이프라인 XML 이 퍼뮤테이션으로 켜고 끄면서도 왕복은 한 번이다.
 *          밝은 부분을 반해상도로 나누는 파이프라인(deferredpipeline.xml)은 BloomDownsample 패스가 swComputeBloomBrightDownsampled 를,
 *          Bloom 패스가 swApplyBloomBright 를 쓴다(언리얼 블룸 셋업 → 다운샘플 → 합성과 같은 순서).
 */
#ifndef SW_ENGINE_POSTBLOOM_HLSLI
#define SW_ENGINE_POSTBLOOM_HLSLI

/**
 * @brief 흐린 색에서 문턱(g_BloomParams.x)을 넘는 밝은 부분을 부드러운 무릎(z)과 세기(y)로 냅니다.
 */
float3 swComputeBloomBrightFromBlur( float3 blur )
{
	float peakChannel = max( max( blur.r, blur.g ), blur.b );
	float soft        = saturate( ( peakChannel - g_BloomParams.x + g_BloomParams.z ) / max( g_BloomParams.z, 1e-4 ) );
	return blur * soft * soft * g_BloomParams.y;
}

/**
 * @brief 원본과 같은 해상도에서 밝은 부분을 구합니다(블러 두 탭).
 */
float3 swComputeBloomBright( float2 uv, float2 texel )
{
	// 블러는 **바이리니어 두 번**으로 끝낸다. 텍셀 중심에서 반 칸 비낀 곳을 찍으면 샘플러가 이웃 네
	// 텍셀을 공짜로 평균해 준다 — 두 번이면 여덟 텍셀이다. 여기만은 `swSampleSource`(선형)여야 한다:
	// 점 샘플러로 바꾸면 섞이지 않아 블러가 아니게 된다.
	float2 halfTexel = texel * 0.5f;
	float3 blur      = swSampleSource( uv - halfTexel ).rgb;
	blur += swSampleSource( uv + halfTexel ).rgb;
	blur *= 0.5f;
	return swComputeBloomBrightFromBlur( blur );
}

/**
 * @brief 반해상도 출력 픽셀 하나의 밝은 부분을 구합니다(원본 4x4 텍셀을 바이리니어 네 탭으로 평균).
 * @param sourceTexel 원본(SourceColor) 텍셀 크기 — `g_SourceTexel.xy`.
 * @details 반해상도 픽셀의 중심은 원본 2x2 의 모서리다. 거기서 대각으로 한 텍셀 비킨 네 곳은 각각 다른 2x2 의 모서리라
 *          바이리니어 한 번이 2x2 를 고르게 섞는다 — 네 탭이 원본 4x4 를 덮는다.
 */
float3 swComputeBloomBrightDownsampled( float2 uv, float2 sourceTexel )
{
	float3 blur = swSampleSource( uv + float2( -sourceTexel.x, -sourceTexel.y ) ).rgb;
	blur += swSampleSource( uv + float2( sourceTexel.x, -sourceTexel.y ) ).rgb;
	blur += swSampleSource( uv + float2( -sourceTexel.x, sourceTexel.y ) ).rgb;
	blur += swSampleSource( uv + float2( sourceTexel.x, sourceTexel.y ) ).rgb;
	blur *= 0.25f;
	return swComputeBloomBrightFromBlur( blur );
}

/**
 * @brief 원본 색에 블룸을 더해 돌려줍니다.
 * @param color 이미 읽어 둔 이 픽셀의 원본 색 — 부르는 쪽이 한 번만 읽게 하려고 인자로 받는다.
 */
float3 swApplyBloom( float2 uv, float2 texel, float3 color )
{
	// SSAO 가 있으면 조명 결과를 가린다(파이프라인 XML 이 AOColor 를 입력으로 선언한다). 없으면 1.
	float ambientOcclusion = swSampleAmbientOcclusion( uv );
	return color * ambientOcclusion + swComputeBloomBright( uv, texel );
}

/**
 * @brief 원본 색에 반해상도로 구해 둔 밝은 부분(g_BloomBrightIndex)을 업샘플해 더합니다. 바이리니어 한 번이 업샘플이다.
 */
float3 swApplyBloomBright( float2 uv, float3 color )
{
	float ambientOcclusion = swSampleAmbientOcclusion( uv );
	return color * ambientOcclusion + swSampleIndex( g_BloomBrightIndex, uv ).rgb;
}

#endif // SW_ENGINE_POSTBLOOM_HLSLI
