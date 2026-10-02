#include "binding.hlsli"

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

PSInput VSMain(SwVertexInput input)
{
	PSInput output;

	SwInstanceData inst = SwLoadInstance(input.instanceSlot);
	float4 worldPos = mul(float4(input.pos, 1.0f), inst.world);
	output.pos = mul(worldPos, g_ViewProj);

	// 사각형의 UV 는 메시의 것이다(MeshUtil::createSpriteQuad — 양면이고 면마다 u 의 방향이 달라 어느 쪽에서 봐도 뒤집히지 않는다).
	// 예전에는 위치에서 지어냈고(u = x + 0.5) 그 u 는 이 사각형이 보이는 쪽 카메라에서 화면 왼쪽으로 늘어 모든 스프라이트가 좌우로 뒤집혔다.
	// 인스턴스의 UV 사각형(아틀라스 프레임)은 여기서, 머티리얼의 uvRect 는 픽셀 단계에서 그 위에 적용한다 — 머티리얼은 픽셀 단계에서만 읽는다
	// (binding.hlsli SW_MATERIAL: 두 단계가 읽으면 GL 이 링크를 거절한다). 둘 다 아핀이라 정점에서 한 것과 픽셀에서 한 것이 같다.
	// 합성은 "머티리얼 사각형 안의 인스턴스 사각형" 이다.
	// 인스턴스 색은 정점 색에 곱해 넘긴다 — 프레임 · 색은 머티리얼 인스턴스가 아니라 인스턴스에 실려, 같은 텍스처의 스프라이트가 한 배치로 남는다.
	const float2 quadUv = input.uv;
	const float4 frame  = SwInstanceUvRectOf(inst);
	output.uv = quadUv * frame.zw + frame.xy;
	output.col = input.col * SwInstanceTintOf(inst);
	output.materialIndex = inst.materialIndex;

	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	SwMaterialData_t material = SW_MATERIAL(input.materialIndex);
	// 머티리얼 uvRect (x=u, y=v, z=폭, w=높이) — 인스턴스 사각형이 이미 들어간 UV 위에 얹는다
	float2 uv = input.uv * material.uvRect.zw + material.uvRect.xy;
	float4 texColor = float4(1,1,1,1);
	if (material.albedoMap != SW_INVALID_INDEX)
	{
		texColor = SW_SampleMaterialTexture(material.albedoMap, uv);
	}

	float4 finalColor = texColor * material.color * input.col;

#if defined(ALPHA_TEST)
	if (finalColor.a < 0.1f)
		discard;
#endif

	return finalColor;
}
