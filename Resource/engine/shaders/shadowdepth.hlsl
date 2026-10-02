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
	float4 worldPos = SwWorldPositionOf(SwLoadMorphPosition(inst.meshBatchIndex, vid, input.pos), inst.world);
#if defined(SW_PASS_DEPTH_PREPASS)
	// 깊이 프리패스는 **카메라** 깊이를 쓴다 — 기본 패스(forwardlit · gbuffer)가 같은 함수로 같은 깊이를 내 LessEqual 을 통과한다. 예전에는 이 분기가
	// 없어 프리패스가 그림자와 같은 광원 행렬로 그렸고, 장면 깊이에 광원 공간의 깊이가 들어갔다.
	output.pos = SwClipPositionOf(worldPos, g_ViewProj);
#else
	output.pos = SwClipPositionOf(worldPos, g_LightViewProj);
#endif
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.pos.z, 0, 0, 1);
}
