#include "binding.hlsli"

struct PSInput
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
	float3 nrm : TEXCOORD0;
};

struct PSOutput
{
	float4 albedo : SV_TARGET0;
	float4 normal : SV_TARGET1;
};

PSInput VSMain(SwVertexInput input, uint vid : SV_VertexID)
{
	PSInput output;
	SwInstanceData inst = SwLoadInstance(input.instanceSlot);
	float3 localPos;
	float3 localNormal;
	SwLoadMorphedVertex(inst.meshBatchIndex, vid, input.pos, input.nrm, localPos, localNormal);
	float4x4 world = inst.world;
	float4 worldPos = mul(float4(localPos, 1.0f), world);
	output.pos = mul(worldPos, g_ViewProj);
	output.col = input.col;
	output.nrm = normalize(mul(float4(localNormal, 0.0f), world).xyz);
	return output;
}

PSOutput PSMain(PSInput input)
{
	PSOutput output;
	float3 nEnc = saturate(normalize(input.nrm) * 0.5f + 0.5f);
	output.albedo = float4(input.col.rgb, 1.0f);
	output.normal = float4(nEnc, 1.0f);
	return output;
}
