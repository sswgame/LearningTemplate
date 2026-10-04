#include "binding.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
};

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;
	SwInstanceData instance = swLoadInstance(input.instanceSlot);
	// 그림자도 같은 정점을 봐야 한다 — 모프를 여기서 빼면 물체와 그림자의 모양이 어긋난다.
	float4 worldPosition = swComputeWorldPosition(swLoadAnimatedPosition(instance, vertexId, input.position), instance.world);
#if defined(SW_PASS_DEPTH_PREPASS)
	// 깊이 프리패스는 **카메라** 깊이를 쓴다 — 기본 패스(forwardlit · gbuffer)가 같은 함수로 같은 깊이를 내 LessEqual 을 통과한다. 이 분기가
	// 없으면 프리패스가 그림자와 같은 광원 행렬로 그려 장면 깊이에 광원 공간의 깊이가 들어간다.
	output.position = swComputeClipPosition(worldPosition, g_ViewProj);
#else
	output.position = swComputeClipPosition(worldPosition, g_LightViewProj);
#endif
	// 머티리얼이 빠지겠다고 한 패스 — 정점을 클립 공간 밖 한 점으로 모아 아무것도 그리지 않는다(드로우는 그대로 나가지만 래스터가 없다).
	// 그림자를 드리우지 않는 머티리얼(풀): MATERIAL_SHADOW_CAST_OFF. 정점 셰이더가 정점을 옮기는 머티리얼(바람)은 이 셰이더가 같은 자리를 못 만들므로
	// 깊이 프리패스에서 빠진다(MATERIAL_VERTEX_DEFORM) — 남으면 기본 패스의 흔들린 정점이 프리패스 깊이에 가려 구멍이 난다.
#if defined(SW_PASS_DEPTH_PREPASS) && defined(MATERIAL_VERTEX_DEFORM)
	output.position = float4(0.0f, 0.0f, -1.0f, 1.0f);
#elif !defined(SW_PASS_DEPTH_PREPASS) && defined(MATERIAL_SHADOW_CAST_OFF)
	output.position = float4(0.0f, 0.0f, -1.0f, 1.0f);
#endif
	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return float4(input.position.z, 0, 0, 1);
}
