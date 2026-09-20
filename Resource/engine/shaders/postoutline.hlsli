/**
 * @file postoutline.hlsli
 * @brief 외곽선을 **함수 하나**로 내놓는다 — 전용 패스(postoutline.hlsl)와 합친 체인(postchain.hlsl)이 같은 코드를 쓴다.
 */
#ifndef SW_POSTOUTLINE_HLSLI
#define SW_POSTOUTLINE_HLSLI

/**
 * @brief 깊이 불연속에 외곽선 색을 섞어 돌려줍니다.
 * @param color 이미 읽어 둔 이 픽셀의 색(블룸을 거쳤으면 그 결과) — 부르는 쪽이 한 번만 읽는다.
 */
float3 SwApplyOutline( float2 uv, float2 texel, float3 color )
{
	float  center;
	float4 listNeighbor;
	SampleDepthCross( uv, texel, center, listNeighbor );

	// 네 이웃과의 차이 합. **합이라 이웃 순서는 상관없다** — 게더 성분 순서가 백엔드마다 달라도 같은 값이다.
	float4 diff = abs( listNeighbor - center.xxxx );
	float  edge = saturate( ( diff.x + diff.y + diff.z + diff.w ) * 4.0f - g_OutlineParams.x );
	edge *= g_OutlineColor.a;
	return lerp( color, g_OutlineColor.rgb, edge );
}

#endif // SW_POSTOUTLINE_HLSLI
