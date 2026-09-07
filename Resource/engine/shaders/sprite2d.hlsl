#include "binding.hlsli"

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
	nointerpolation uint materialIndex : TEXCOORD1;
};

SW_MATERIAL_BEGIN
{
	float4 color;
	float4 uvRect;
	uint albedoMap;
}
SW_MATERIAL_END

PSInput VSMain(VSInput input, uint iid : SV_InstanceID)
{
	PSInput output;

	SwInstanceData inst = SwLoadInstance(iid);
	float4 worldPos = mul(float4(input.pos, 1.0f), inst.world);
	output.pos = mul(worldPos, g_ViewProj);

	// Default UV from position [-0.5, 0.5] mapped to [0, 1]
	float2 baseUv = input.pos.xy * float2(1.0f, -1.0f) + 0.5f;

	// Apply UVRect (x=u, y=v, z=width, w=height)
	float4 uvRect = SW_MATERIAL(inst.materialIndex).uvRect;
	output.uv = baseUv * uvRect.zw + uvRect.xy;
	output.col = input.col;
	output.materialIndex = inst.materialIndex;

	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	SwMaterialData_t material = SW_MATERIAL(input.materialIndex);
	float4 texColor = float4(1,1,1,1);
	if (material.albedoMap != SW_INVALID_INDEX)
	{
		texColor = SW_SampleMaterialTexture(material.albedoMap, input.uv);
	}

	float4 finalColor = texColor * material.color * input.col;

#if defined(ALPHA_TEST)
	if (finalColor.a < 0.1f)
		discard;
#endif

	return finalColor;
}
