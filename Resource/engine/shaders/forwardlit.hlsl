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
	float3 nrm : TEXCOORD1;
	nointerpolation uint materialIndex : TEXCOORD2; // GPUScene 인스턴스가 준 머티리얼 원소
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

PSInput VSMain(VSInput input, uint iid : SV_InstanceID, uint vid : SV_VertexID)
{
	PSInput output;
	// GPU 가 변형한 정점이 있으면 그걸 쓴다(모프 안 하면 입력 스트림 그대로).
	const float3 localPos = SwLoadMorphPosition(vid, input.pos);
	SwInstanceData inst = SwLoadInstance(iid);
	float4 worldPos = mul(float4(localPos, 1.0f), inst.world);
	output.pos = mul(worldPos, g_ViewProj);
	output.col = input.col;
	output.uv  = localPos.xy * float2(0.5f, -0.5f) + 0.5f;
	float3 n = DemoCubeNormal(localPos);
	output.nrm = normalize(mul(float4(n, 0.0f), inst.world).xyz);
	output.materialIndex = inst.materialIndex;
	return output;
}

// 반환 타입이 **패스에 따라 갈린다** — 포워드는 SV_TARGET 하나, G버퍼 패스는 알베도·노멀 둘.
// 머티리얼이 셰이더 경로를 정하므로 디퍼드의 G버퍼 패스도 이 파일로 그린다. 예전엔 여기가 늘
// float4 하나였고, 그래서 **G버퍼의 노멀 타깃이 클리어 값 그대로** 남아 디퍼드 조명이 화면 전체를
// 같은 노멀로 계산했다. 자세한 사연은 binding.hlsli 4 절.
SW_SURFACE_OUTPUT PSMain(PSInput input)
{
	float3 N = normalize(input.nrm);

	// 정점 색 x 머티리얼 색 x 알베도 텍스처. 머티리얼은 인스턴스의 materialIndex 로 버퍼에서 읽는다 — 드로우별 바인딩 없음.
	// 텍스처 인덱스가 없거나(SW_INVALID_INDEX) 백엔드가 bindless 에뮬(DX11/GL)이면 흰색이 곱해진다 —
	// 자세한 이유는 binding.hlsli 의 SW_SampleMaterialTexture 주석 참고.
	SwMaterialData_t material = SW_MATERIAL(input.materialIndex);
	float4 albedo = input.col * material.color * SW_SampleMaterialTexture(material.albedoMap, input.uv);

#if defined( SW_PASS_GBUFFER )
	// G버퍼 패스는 **조명을 계산하지 않는다** — 표면만 적어 두고 셰이딩은 디퍼드 조명 패스가 한다.
	// 조명 코드가 통째로 컴파일 아웃되므로 런타임 분기가 아니다(언리얼의 베이스 패스와 같은 구성).
	return SwStoreSurface(float4(0, 0, 0, 0), albedo, N);
#else
	float3 L = normalize(-g_KeyLightDirIntensity.xyz);
	float  ndotl = saturate(dot(N, L));

	float2 shadowUV = saturate(input.uv + g_ShadowParams.zw);
	float  shadowSample = SampleShadow(shadowUV).r;
	float  shadow = lerp(1.0f - g_ShadowParams.y, 1.0f, saturate(shadowSample + g_ShadowParams.x));

	float3 ambient = g_KeyLightColor.rgb * g_KeyLightColor.a;
	float3 lit = albedo.rgb * (ambient + ndotl * g_KeyLightDirIntensity.w * g_KeyLightColor.rgb) * shadow;
	float rim = pow(1.0f - saturate(dot(N, float3(0, 0, 1))), 2.0f) * 0.15f;
	lit += rim * g_KeyLightColor.rgb;

	// 알파를 쓰는지가 **퍼뮤테이션으로 갈린다**. 반투명 머티리얼만 MATERIAL_BLEND_TRANSLUCENT 를 always-define
	// 으로 들고 있고(glassmaterial.material), 불투명 변형은 알파 경로가 아예 컴파일되지 않는다 — 불투명
	// 패스에서 머티리얼 알파가 새어 나오는 일이 없다. 언리얼도 블렌드 모드가 머티리얼의 퍼뮤테이션이다.
#if defined( MATERIAL_BLEND_TRANSLUCENT )
	return SwStoreSurface(float4(lit, albedo.a), albedo, N);
#else
	return SwStoreSurface(float4(lit, 1.0f), albedo, N);
#endif
#endif
}
