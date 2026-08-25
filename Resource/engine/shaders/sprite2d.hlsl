#include "bindless.hlsli"

struct VSInput
{
	float3 pos : POSITION;
	float4 col : COLOR;
};

struct PSInput
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
	float2 uv  : TEXCOORD0;
};

SW_DECLARE_CBUFFER( MaterialCB, 1 )
{
	float4 color;
	float4 uvRect;
	uint albedoMap;
};

PSInput VSMain(VSInput input)
{
	PSInput output;
	PassCBData passCb = GetPassCB();

	float4 worldPos = mul(float4(input.pos, 1.0f), passCb.g_World);
	output.pos = mul(worldPos, passCb.g_ViewProj);
	
	// Default UV from position [-0.5, 0.5] mapped to [0, 1]
	float2 baseUv = input.pos.xy * float2(1.0f, -1.0f) + 0.5f;
	
	// Apply UVRect (x=u, y=v, z=width, w=height)
	output.uv = baseUv * uvRect.zw + uvRect.xy;
	output.col = input.col;

	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 texColor = float4(1,1,1,1);
	if (albedoMap != kInvalidBindlessIndex)
	{
		texColor = SampleBindlessIndex(albedoMap, input.uv);
	}
	
	float4 finalColor = texColor * color * input.col;
	
#if defined(ALPHA_TEST)
	if (finalColor.a < 0.1f)
		discard;
#endif

	return finalColor;
}
