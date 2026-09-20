#include "binding.hlsli"

#include "postbloom.hlsli"

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
	// 본문은 postbloom.hlsli 하나다 — 합친 체인(postchain.hlsl)과 **같은 코드**를 쓴다.
	float2 texel = g_OutlineParams.yz;
	float3 color = SampleSourcePoint(input.uv).rgb;
	return float4(SwApplyBloom(input.uv, texel, color), 1.0f);
}
