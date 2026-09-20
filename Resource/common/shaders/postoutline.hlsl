#include "binding.hlsli"

struct PSInput
{
	float4 pos : SV_POSITION;
	float2 uv  : TEXCOORD0;
};

PSInput VSMain(SwVertexInput input, uint vid : SV_VertexID)
{
	PSInput output;
	input.pos = input.pos;
	float2 p = float2((vid == 1) ? 3.0f : -1.0f, (vid == 2) ? 3.0f : -1.0f);
	output.pos = float4(p, 0.0f, 1.0f);
	output.uv  = p * float2(0.5f, -0.5f) + 0.5f;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	// 화면과 1:1 이라 UV 가 텍셀 중심에 정확히 떨어진다 — 선형 필터는 섞을 것이 없으면서 값만 비싸다.
	float2 texel = g_OutlineParams.yz;
	float  center;
	float4 listNeighbor;
	SampleDepthCross(input.uv, texel, center, listNeighbor);

	// 네 이웃과의 차이 합. **합이라 이웃 순서는 상관없다** — 게더 성분 순서가 백엔드마다 달라도 같은 값이다.
	float4 diff = abs(listNeighbor - center.xxxx);
	float  edge = saturate((diff.x + diff.y + diff.z + diff.w) * 4.0f - g_OutlineParams.x);
	edge *= g_OutlineColor.a;

	float3 color = SampleSourcePoint(input.uv).rgb;
	return float4(lerp(color, g_OutlineColor.rgb, edge), 1.0f);
}
