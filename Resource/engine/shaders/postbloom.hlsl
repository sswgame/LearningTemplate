#include "binding.hlsli"

#include "postbloom.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv       : TEXCOORD0;
};

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;
	input.position = input.position;
	float2 clipPosition = float2((vertexId == 1) ? 3.0f : -1.0f, (vertexId == 2) ? 3.0f : -1.0f);
	output.position = float4(clipPosition, 0.0f, 1.0f);
	output.uv       = clipPosition * float2(0.5f, -0.5f) + 0.5f;
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	// 본문은 postbloom.hlsli 하나다 — 합친 체인(postchain.hlsl)과 **같은 코드**를 쓴다.
	// 블룸은 원본을 텍셀 반 칸 비켜 읽는다 — 원본(반해상도일 수 있다)의 텍셀이다(g_SourceTexel).
	float2 texel = g_SourceTexel.xy;
	float3 color = swSampleSourcePoint(input.uv).rgb;
	return float4(swApplyBloom(input.uv, texel, color), 1.0f);
}
