#include "lighting.hlsli"

struct PSInput
{
	float4 position                    : SV_POSITION;
	float4 color                       : COLOR;
	float2 uv                          : TEXCOORD0;
	float3 normal                      : TEXCOORD1;
	nointerpolation uint materialIndex : TEXCOORD2; // GPUScene 인스턴스가 준 머티리얼 원소
	// 월드 위치를 넘긴다 — 점광의 거리 감쇠와 그림자 투영이 둘 다 이걸 쓴다(로컬 좌표로 만든 UV 로 그림자를 읽으면
	// 그림자가 아니라 무늬다).
	float3 worldPosition : TEXCOORD3;
};

// defaultmaterial.material 의 프로퍼티 순서·타입과 1:1 (color/roughness/albedoMap). 머티리얼 패커는
// 리플렉션 오프셋(StructuredBuffer 원소 레이아웃)으로 채우므로 여기 순서를 바꾸면 에셋도 같이 바꿔야 한다.
SW_MATERIAL_BEGIN
{
	float4 color;
	float  roughness;
	uint   albedoMap;
}
SW_MATERIAL_END

PSInput VSMain(SwVertexInput input, uint vertexId : SV_VertexID)
{
	PSInput output;
	// 인스턴스는 슬롯 스트림이 준 자리에서, 배치는 인스턴스에서 — 모프 풀 시작·정점 풀 시작이 배치 표에 있다.
	SwInstanceData instance = swLoadInstance(input.instanceSlot);
	// GPU 가 변형한 정점(VAT · 모프 · 스킨)이 있으면 그걸 쓴다(아니면 입력 스트림 그대로). 위치와 노멀이 같이 온다.
	float3 localPosition;
	float3 localNormal;
	swLoadAnimatedVertex(instance, vertexId, input.position, input.normal, localPosition, localNormal);
	// 위치는 깊이 프리패스와 **같은 함수**로 만든다 — 프리패스가 쓴 깊이와 비트까지 같아야 LessEqual 을 통과한다(binding.hlsli).
	float4 worldPosition = swComputeWorldPosition(localPosition, instance.world);
	output.position = swComputeClipPosition(worldPosition, g_ViewProj);
	output.worldPosition = worldPosition.xyz;
	output.color = input.color;
	// UV 는 정점 속성이다. 주의: `localPosition.xy * 0.5 + 0.5` 로 **지어내면** — 노멀과 같은 함정이다 — 원점 중심
	// 단위 도형이 아닐 때 텍스처가 엉뚱하게 붙고, 도형의 옆면과 뚜껑이 같은 자리를 문다(XY 평면 투영이라 앞뒤가 겹친다).
	output.uv = input.uv;
	// 월드 노멀은 월드 행렬이 아니라 그 3x3 의 여인수 행렬로 옮긴다(binding.hlsli swComputeWorldNormal). 트랜스폼은
	// 비균등 스케일을 받으므로 월드 행렬을 그대로 곱하면 늘린 메시의 조명이 틀린다.
	output.normal = swComputeWorldNormal(localNormal, instance.world);
	output.materialIndex = instance.materialIndex;
	return output;
}

// 반환 타입이 **패스에 따라 갈린다** — 포워드는 SV_TARGET 하나, G버퍼 패스는 알베도·노멀 둘.
// 머티리얼이 셰이더 경로를 정하므로 디퍼드의 G버퍼 패스도 이 파일로 그린다. float4 하나로 고정하면 **G버퍼의 노멀
// 타깃이 클리어 값 그대로** 남는다(binding.hlsli 4 절).
SW_SURFACE_OUTPUT PSMain(PSInput input)
{
	float3 normal = normalize(input.normal);

	// 정점 색 x 머티리얼 색 x 알베도 텍스처. 머티리얼은 인스턴스의 materialIndex 로 버퍼에서 읽는다 — 드로우별 바인딩 없음.
	// 텍스처 인덱스가 없거나(kInvalidIndex) 백엔드가 bindless 에뮬(DX11/GL)이면 흰색이 곱해진다 —
	// 자세한 이유는 binding.hlsli 의 swSampleMaterialTexture 주석 참고.
	SwMaterialData material = SW_MATERIAL(input.materialIndex);
	float4 albedo = input.color * material.color * swSampleMaterialTexture(material.albedoMap, input.uv);

#if defined( SW_PASS_GBUFFER )
	// G버퍼 패스는 **조명을 계산하지 않는다** — 표면만 적어 두고 셰이딩은 디퍼드 조명 패스가 한다.
	// 조명 코드가 통째로 컴파일 아웃되므로 런타임 분기가 아니다(언리얼의 베이스 패스와 같은 구성).
	return swStoreSurface(float4(0, 0, 0, 0), albedo, normal);
#else
#if SW_VIEWMODE_SKIPS_LIGHTING
	// 보기 모드 Unlit — 조명 · 그림자 · 림 없이 알베도 그대로.
	float3 lit = albedo.rgb;
#else
	// 조명 식은 디퍼드와 **같은 함수**다(lighting.hlsli). 두 벌로 두면 두 경로의 그림이 갈라진다.
	float  shadow = swSampleShadowAtWorld(input.worldPosition, normal);
	float3 lit = swShadeLights(albedo.rgb, input.worldPosition, normal, shadow);
	float rim = pow(1.0f - saturate(dot(normal, float3(0, 0, 1))), 2.0f) * 0.15f;
	lit += rim * g_KeyLightColor.rgb;
#endif

	// 알파를 쓰는지가 **퍼뮤테이션으로 갈린다**. 반투명 머티리얼만 MATERIAL_BLEND_TRANSLUCENT 를 always-define
	// 으로 들고 있고(glassmaterial.material), 불투명 변형은 알파 경로가 아예 컴파일되지 않는다 — 불투명
	// 패스에서 머티리얼 알파가 새어 나오는 일이 없다. 언리얼도 블렌드 모드가 머티리얼의 퍼뮤테이션이다.
#if defined( MATERIAL_BLEND_TRANSLUCENT )
	return swStoreSurface(float4(lit, albedo.a), albedo, normal);
#else
	return swStoreSurface(float4(lit, 1.0f), albedo, normal);
#endif
#endif
}
