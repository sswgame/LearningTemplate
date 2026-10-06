#include "binding.hlsli"

struct PSInput
{
	float4 position                    : SV_POSITION;
	float4 color                       : COLOR;
	float2 uv                          : TEXCOORD0;
	nointerpolation uint materialIndex : TEXCOORD1;
};

SW_MATERIAL_BEGIN
{
	float4 color;
	float4 uvRect;
	uint albedoMap;
	uint pointFilter;
	uint premultipliedTexture;
}
SW_MATERIAL_END

PSInput VSMain(SwVertexInput input)
{
	PSInput output;

	SwInstanceData instance = swLoadInstance(input.instanceSlot);
	float4 worldPosition = mul(float4(input.position, 1.0f), instance.world);
	// 픽셀 스냅: 인스턴스 원점의 X · Y 를 자산 픽셀 격자(1 / PPU)에 붙인다(정점마다가 아니라 원점을 — 사각형이 찌그러지지 않는다).
	// 그리는 눈은 화면 픽셀 격자에 붙어 있고 배율이 정수라(PixelPerfectCameraComponent) 자산 픽셀이 화면 픽셀 배율 칸에 딱 맞고, 스프라이트끼리 아트 픽셀이 어긋나지 않는다.
	if (instance.pixelSnap > 0.0f)
	{
		const float2 origin  = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), instance.world).xy;
		const float2 snapped = floor(origin / instance.pixelSnap + 0.5f) * instance.pixelSnap;
		worldPosition.xy += snapped - origin;
	}
	output.position = mul(worldPosition, g_ViewProj);

	// 사각형의 UV 는 메시의 것이다(MeshUtil::createSpriteQuad — 양면이고 면마다 u 의 방향이 달라 어느 쪽에서 봐도 뒤집히지 않는다).
	// 위치에서 지어내면(u = x + 0.5) 그 u 는 이 사각형이 보이는 쪽 카메라에서 화면 왼쪽으로 늘어 모든 스프라이트가 좌우로 뒤집힌다.
	// 인스턴스의 UV 사각형(아틀라스 프레임)은 여기서, 머티리얼의 uvRect 는 픽셀 단계에서 그 위에 적용한다 — 머티리얼은 픽셀 단계에서만 읽는다
	// (binding.hlsli SW_MATERIAL: 두 단계가 읽으면 GL 이 링크를 거절한다). 둘 다 아핀이라 정점에서 한 것과 픽셀에서 한 것이 같다.
	// 합성은 "머티리얼 사각형 안의 인스턴스 사각형" 이다.
	// 인스턴스 색은 정점 색에 곱해 넘긴다 — 프레임 · 색은 머티리얼 인스턴스가 아니라 인스턴스에 실려, 같은 텍스처의 스프라이트가 한 배치로 남는다.
	const float2 quadUv = input.uv;
	const float4 frame  = swComputeInstanceUvRect(instance);
	output.uv = quadUv * frame.zw + frame.xy;
	output.color = input.color * swComputeInstanceTint(instance);
	output.materialIndex = instance.materialIndex;

	return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	SwMaterialData material = SW_MATERIAL(input.materialIndex);
	// 머티리얼 uvRect (x=u, y=v, z=폭, w=높이) — 인스턴스 사각형이 이미 들어간 UV 위에 얹는다
	float2 uv = input.uv * material.uvRect.zw + material.uvRect.xy;
	float4 textureColor = float4(1,1,1,1);
	if (material.albedoMap != kInvalidIndex)
	{
		// 점 필터(픽셀 아트)면 텍셀 중심에 붙여 읽는다 — 확대해도 텍셀 경계가 번지지 않는다(유니티 Filter Mode Point · Godot TEXTURE_FILTER_NEAREST).
		textureColor = (material.pointFilter != 0u) ? swSampleMaterialTexturePoint(material.albedoMap, uv) : swSampleMaterialTexture(material.albedoMap, uv);
		// 프리멀티플라이 텍스처(월드 공간 UI 의 렌더 텍스처 — 캔버스가 프리멀티플라이로 그린다)는 곧은 알파로 되돌린다 — 투명 블렌드가 SrcAlpha 를 다시 곱한다.
		if (material.premultipliedTexture != 0u && textureColor.a > 0.0f)
			textureColor.rgb /= textureColor.a;
	}

	float4 finalColor = textureColor * material.color * input.color;

#if defined(ALPHA_TEST)
	if (finalColor.a < 0.1f)
		discard;
#endif

	return finalColor;
}
