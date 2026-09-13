#include "binding.hlsli"

struct PSInput
{
	float4 pos : SV_POSITION;
};

PSInput VSMain(SwVertexInput input, uint vid : SV_VertexID)
{
	PSInput output;
	SwInstanceData inst = SwLoadInstance(input.instanceSlot);
	// 그림자도 같은 정점을 봐야 한다 — 모프를 여기서 빼면 물체와 그림자의 모양이 어긋난다.
	float4 worldPos = mul(float4(SwLoadMorphPosition(inst.meshBatchIndex, vid, input.pos), 1.0f), inst.world);
	output.pos = mul(worldPos, g_LightViewProj);
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.pos.z, 0, 0, 1);
}
