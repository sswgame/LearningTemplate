/**
 * binding.hlsli — 리플렉션 구동 바인딩용 셰이더 헬퍼 (bindless.hlsli 대체).
 *
 * - 셰이더는 `#include "binding.hlsli"` 하나만 하고, `g_ViewProj` / `g_World` 등 PassCB 필드와
 *   `SampleShadow(uv)` / `SampleSource(uv)` 등 헬퍼를 바로 쓴다. (예전 `GetPassCB()` 인다이렉션 없음)
 * - 엔진(C++ ShaderBindingBinder)이 ShaderReflection 으로 PassCB 멤버 이름을 읽어 값을 채운다.
 *   따라서 이 파일의 PassCB 를 고치면 C++ 는 자동으로 따라온다 (미러 없음).
 * - 텍스처는 이름 규약: `uint g_<Name>Index` (PassCB) ↔ 엔진 리소스 `"<Name>"`.
 *   네이티브 bindless(DX12 / Vulkan): 인덱스로 무제한 텍스처 배열을 직접 샘플 (SM6.6 힙 인덱싱 아님).
 *   에뮬(DX11 / OpenGL): 엔진이 리플렉션 t# 슬롯에 SRV 를 바인딩, 값 비교로 멀티플렉싱.
 * - GPUScene(언리얼 방식): 드로우별 데이터는 바인딩이 아니라 버퍼에서 읽는다.
 *     VS: `SwInstanceData inst = SwLoadInstance( SV_InstanceID )` → inst.world / inst.materialIndex
 *     PS: `SW_MATERIAL( materialIndex ).color`  — VS 가 materialIndex 를 nointerpolation 으로 넘긴다.
 *   머티리얼 구조체는 셰이더가 SW_MATERIAL_BEGIN/END 로 선언하고, 엔진이 그 셰이더 타입의 머티리얼 데이터를
 *   StructuredBuffer(g_SwMaterials, t9)에 원소로 쌓아 인스턴스에 materialIndex 를 매긴다.
 */

#ifndef SW_ENGINE_BINDING_HLSLI
#define SW_ENGINE_BINDING_HLSLI

#include "common.hlsli"

static const uint SW_INVALID_INDEX = 0xFFFFFFFFu;
static const uint kInvalidBindlessIndex = 0xFFFFFFFFu; // 하위호환 별칭

// ------------------------------------------------------------------------------
// 1) PassCB — b0. 셰이더가 실제 쓰는 필드만. 엔진이 이름으로 채운다. 패스(배치)마다 한 번 걸리는 진짜 상수버퍼.
// ------------------------------------------------------------------------------
SW_DECLARE_CBUFFER( PassCB, SW_SLOT_PASS_CB )
{
	float4x4 g_LightViewProj;
	float4x4 g_ViewProj;
	float4x4 g_World;
	float4   g_KeyLightDirIntensity;
	float4   g_KeyLightColor;
	float4   g_ShadowParams;
	float4   g_BloomParams;
	float4   g_OutlineColor;
	float4   g_OutlineParams;
	uint     g_ShadowMapIndex;
	uint     g_GBufferAlbedoIndex;
	uint     g_GBufferNormalIndex;
	uint     g_SceneDepthIndex;
	uint     g_SourceColorIndex;
	uint     g_SourceDepthIndex;
	uint     g_Flags;
	uint     g_SwInstancesIndex; // 인스턴스 구조버퍼가 걸려 있으면 유효, SW_INVALID_INDEX 면 g_World 폴백 (풀스크린·픽스처)
	uint     g_SwInstanceCount;  // 인스턴스 버퍼 원소 수 — 범위 밖 인덱스를 막는다 (DX12 루트 SRV 는 경계 검사가 없다)
};

// ------------------------------------------------------------------------------
// 1-0) 드로우별 루트/푸시 상수 — **배치마다 달라지는 값만** 여기 둔다.
//      PassCB 는 패스당 한 번 올리는 버퍼라, 배치마다 바뀌는 값을 거기 넣으면 한 패스의 드로우들이 서로를
//      덮어써 전부 마지막 값을 읽는다(GPU 는 제출 뒤에 읽는다). 언리얼이 FMeshDrawCommand 의 느슨한
//      파라미터를 드로우별로 싣는 자리와 같다. DX12 루트 상수 / Vulkan 푸시 상수 / DX11·GL 은 b2 에뮬.
// ------------------------------------------------------------------------------
//      컴퓨트 셰이더는 자기 루트 상수 블록을 직접 선언하므로(예: computetexturewrite.hlsl) 여기서는 빼 둔다 —
//      한 셰이더에 블록이 둘이면 재정의다.
#if !defined( SW_STAGE_COMPUTE )
SW_ROOT_CONSTANTS_BEGIN
	uint g_InstanceBase;    // GPUScene 인스턴스 버퍼에서 이 배치의 시작 오프셋
	uint g_SwMaterialCount; // 이 배치의 머티리얼 데이터 버퍼(g_SwMaterials) 원소 수 — SW_MATERIAL 이 클램프한다
SW_ROOT_CONSTANTS_END
#define SW_DRAW_INSTANCE_BASE  SW_ROOT( g_InstanceBase )
#define SW_DRAW_MATERIAL_COUNT SW_ROOT( g_SwMaterialCount )
#else
// 컴퓨트에는 이 블록이 없다 — 그래픽스 전용 헬퍼(SwLoadInstance / SW_MATERIAL)가 컴파일만 되게 0 으로 둔다.
#define SW_DRAW_INSTANCE_BASE  0u
#define SW_DRAW_MATERIAL_COUNT 0u
#endif

// ------------------------------------------------------------------------------
// 1-1) GPUScene 인스턴스 (per-instance world/material). C++ GpuInstance 와 레이아웃 일치.
//      네 백엔드 공통 — 엔진이 리플렉션 슬롯 t4 에 인스턴스 버퍼를 건다 (레지스트리 이름 "SwInstances").
// ------------------------------------------------------------------------------
struct SwInstanceData
{
	float4x4 world;
	float3   boundsCenter;
	float    boundsRadius;
	uint     meshBatchIndex;
	uint     materialIndex;
	uint     blendMode;
	uint     pad;
};

SW_DECLARE_STRUCTURED_BUFFER( SwInstanceData, g_SwInstances, SW_SLOT_INSTANCE_SRV );

/**
 * @brief 이 드로우의 인스턴스 데이터 — 씬 메시는 전부 인스턴스 버퍼에서 읽는다(경로 하나).
 * @details 인스턴스 버퍼가 안 걸린 드로우(풀스크린·픽스처)나 범위 밖 인덱스는 PassCB 의 g_World 와 머티리얼 원소 0 으로 만든다.
 *          범위 검사는 백엔드마다 다른 OOB 결과(DX11/GL 0, Vulkan robustBufferAccess 0, DX12 루트 SRV 는 정의되지 않음)를
 *          하나로 맞추기 위한 것이다.
 */
SwInstanceData SwLoadInstance( uint instanceId )
{
	const uint element = SW_DRAW_INSTANCE_BASE + instanceId;
	if ( g_SwInstancesIndex != SW_INVALID_INDEX && element < g_SwInstanceCount )
		return g_SwInstances[element];
	SwInstanceData inst;
	inst.world          = g_World;
	inst.boundsCenter   = float3( 0, 0, 0 );
	inst.boundsRadius   = 0;
	inst.meshBatchIndex = 0;
	inst.materialIndex  = 0;
	inst.blendMode      = 0;
	inst.pad            = 0;
	return inst;
}

float4x4 SwLoadInstanceWorld( uint instanceId )
{
	return SwLoadInstance( instanceId ).world;
}

// ------------------------------------------------------------------------------
// 1-2) 머티리얼 데이터 — 셰이더가 구조체를 선언하면 엔진이 그 타입의 머티리얼들을 StructuredBuffer 원소로 쌓는다.
//      SW_MATERIAL_BEGIN { float4 color; uint albedoMap; } SW_MATERIAL_END
//      ... SW_MATERIAL( inst.materialIndex ).color
//      리플렉션 이름 g_SwMaterials(t9) ↔ 레지스트리 "SwMaterials" (배치마다 그 셰이더 타입의 버퍼를 등록한다).
//      원소 레이아웃은 네 백엔드가 같다 — SPIR-V 도 DX 패킹(-fvk-use-dx-layout, ShaderCompiler.cpp)으로 굽고
//      ShaderBindingContractTest.ReflectionNamesAreUniformAcrossBackends 가 구운 바이너리로 확인한다.
//      인덱스는 g_SwMaterialCount 로 클램프한다 — 잘못된 인덱스가 백엔드마다 다른 OOB 결과를 내지 않도록.
// ------------------------------------------------------------------------------
uint SwClampMaterialIndex( uint index )
{
	const uint materialCount = SW_DRAW_MATERIAL_COUNT;
	return ( materialCount == 0 ) ? 0 : min( index, materialCount - 1 );
}
#define SW_MATERIAL_BEGIN struct SwMaterialData_t
#define SW_MATERIAL_END   ; SW_DECLARE_STRUCTURED_BUFFER( SwMaterialData_t, g_SwMaterials, SW_SLOT_MATERIAL_BUFFER );
#define SW_MATERIAL( index ) g_SwMaterials[SwClampMaterialIndex( index )]

// ------------------------------------------------------------------------------
// 2) 샘플러 세트 — 네이티브 bindless 백엔드(DX12/Vulkan)만 쓴다. DX11/GL 은 슬롯 결합 샘플러(g_SwSlot#Sampler, s#)뿐이라
//    여기서 s0 를 또 선언하면 같은 레지스터를 두 샘플러가 나눠 갖는다(FXC 는 안 쓰면 조용히 버려 드러나지 않았다).
//    DX12 는 루트 시그니처 정적 샘플러 s0..s7, Vulkan 은 set SW_VK_TEXTURE_SET 의 immutable sampler 배열(binding 1) + 비교 샘플러(binding 2).
//    번호는 bindingslots.hlsli 4 의 SW_SAMPLER_* 다 — 언리얼의 정적 샘플러 세트와 같은 자리.
// ------------------------------------------------------------------------------
#if defined( SW_NATIVE_BINDLESS )
#if defined( __spirv__ )
// Vulkan: set 1 binding 1 의 immutable sampler 배열 — 배열 그대로 인덱싱한다.
[[vk::binding( SW_VK_SAMPLER_BINDING, SW_VK_TEXTURE_SET )]] SamplerState g_SwSamplers[SW_STATIC_SAMPLER_ARRAY_COUNT] : register( s0 );
[[vk::binding( SW_VK_SHADOW_SAMPLER_BINDING, SW_VK_TEXTURE_SET )]] SamplerComparisonState g_SwSamplerShadowCmp : register( SW_CAT( s, SW_SAMPLER_SHADOW_CMP ) );
#define SW_SAMPLER_STATE( samplerId ) g_SwSamplers[samplerId]
#define g_SwSamplerLinearWrap g_SwSamplers[SW_SAMPLER_LINEAR_WRAP]
#else
// DX12: 정적 샘플러는 배열 선언(s0..s6 범위)을 못 채운다 — 루트 시그니처가 범위를 디스크립터 테이블로 요구해 PSO 생성이
// "sampler descriptor range not fully bound" 로 실패한다. 하나씩 선언하고 리터럴 분기로 고른다(언리얼도 정적 샘플러를 개별 선언).
SamplerState g_SwSampler0 : register( s0 );
SamplerState g_SwSampler1 : register( s1 );
SamplerState g_SwSampler2 : register( s2 );
SamplerState g_SwSampler3 : register( s3 );
SamplerState g_SwSampler4 : register( s4 );
SamplerState g_SwSampler5 : register( s5 );
SamplerState g_SwSampler6 : register( s6 );
SamplerComparisonState g_SwSamplerShadowCmp : register( SW_CAT( s, SW_SAMPLER_SHADOW_CMP ) );
#define g_SwSamplerLinearWrap g_SwSampler0
#endif
#endif

// ------------------------------------------------------------------------------
// 3) 텍스처 샘플 — 이름 기반. 컴퓨트 RW 텍스처는 SW_StoreTex2D / SW_LoadRWTex2D (컴퓨트 스테이지에서만 선언된다).
// ------------------------------------------------------------------------------
#if defined( SW_NATIVE_BINDLESS )

// 무제한 텍스처 배열 — DX12 t0 space1 (루트 시그니처 테이블), Vulkan set 1 binding 0. SM5.1 무제한 배열이지 6.6 힙 인덱싱이 아니다.
#if defined( __spirv__ )
[[vk::binding( SW_VK_TEXTURE_BINDING, SW_VK_TEXTURE_SET )]] Texture2D g_SwBindlessTex2D[] : register( t0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
#else
Texture2D g_SwBindlessTex2D[] : register( t0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
#endif
/** @brief 인덱스의 텍스처를 샘플러 세트의 samplerId(SW_SAMPLER_*, 비교 샘플러 제외)로 샘플링합니다. */
float4 SW_SampleIndexWith( uint index, uint samplerId, float2 uv )
{
	if ( index == SW_INVALID_INDEX )
		return float4( 0, 0, 0, 1 );
#if defined( __spirv__ )
	return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( SW_SAMPLER_STATE( samplerId ), uv );
#else
	switch ( samplerId )
	{
		case 1: return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( g_SwSampler1, uv );
		case 2: return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( g_SwSampler2, uv );
		case 3: return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( g_SwSampler3, uv );
		case 4: return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( g_SwSampler4, uv );
		case 5: return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( g_SwSampler5, uv );
		case 6: return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( g_SwSampler6, uv );
		default: return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( g_SwSampler0, uv );
	}
#endif
}
float4 SW_SampleIndex( uint index, float2 uv )
{
	return SW_SampleIndexWith( index, SW_SAMPLER_LINEAR_WRAP, uv );
}
/** @brief 깊이 텍스처를 비교 샘플러(LESS_EQUAL)로 읽습니다 — 1 이면 depth 가 저장값 이하(빛 받음). */
float SW_SampleShadowCmp( uint index, float2 uv, float depth )
{
	if ( index == SW_INVALID_INDEX )
		return 1.0f;
	return g_SwBindlessTex2D[NonUniformResourceIndex( index )].SampleCmpLevelZero( g_SwSamplerShadowCmp, uv, depth );
}

#if defined( SW_STAGE_COMPUTE )
// 컴퓨트 RW 텍스처 배열 — DX12 u0 space1 (텍스처 테이블의 두 번째 범위), Vulkan set 1 binding 3 (STORAGE_IMAGE[]).
// 인덱스는 registerBindlessTextureUAV 가 준다. 컴퓨트에서만 선언한다 — 그래픽스 스테이지에 UAV 배열을 두면
// DX12 가 PS UAV 슬롯을, Vulkan 이 vertexPipelineStores 기능을 요구한다.
#if defined( __spirv__ )
[[vk::binding( SW_VK_RWTEXTURE_BINDING, SW_VK_TEXTURE_SET )]] RWTexture2D<float4> g_SwBindlessRWTex2D[] : register( u0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
#else
RWTexture2D<float4> g_SwBindlessRWTex2D[] : register( u0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
#endif
void SW_StoreTex2D( uint index, uint2 coord, float4 value )
{
	if ( index == SW_INVALID_INDEX )
		return;
	g_SwBindlessRWTex2D[NonUniformResourceIndex( index )][coord] = value;
}
float4 SW_LoadRWTex2D( uint index, uint2 coord )
{
	if ( index == SW_INVALID_INDEX )
		return float4( 0, 0, 0, 0 );
	return g_SwBindlessRWTex2D[NonUniformResourceIndex( index )][coord];
}
#endif // SW_STAGE_COMPUTE

#else

#if !defined( SW_STAGE_COMPUTE )
// 에뮬 백엔드의 샘플 슬롯은 그래픽스 스테이지 전용이다 — GL 은 텍스처 유닛과 이미지 유닛이 SPIR-V 에서 같은 binding 번호를
// 쓰므로 컴퓨트에서 둘을 함께 선언하면 DXC 가 RW 텍스처를 결합 샘플러와 합쳐 버린다(엔진도 컴퓨트에는 t0..t3 을 걸지 않는다).
// DX11 / OpenGL : 엔진이 t0..t3 에 SRV 바인딩. 값 비교로 어느 논리 텍스처인지 판별.
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot0, g_SwSlot0Sampler, SW_SLOT_ENGINE_TEX0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot1, g_SwSlot1Sampler, SW_SLOT_ENGINE_TEX1 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot2, g_SwSlot2Sampler, SW_SLOT_ENGINE_TEX2 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot3, g_SwSlot3Sampler, SW_SLOT_ENGINE_TEX3 );

// 머티리얼 텍스처 고정 슬롯 t5..t8 — 번호는 bindingslots.hlsli 가 정한다 (C++ shaderslot::kMaterialTexture0 와 같은 파일).
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex0, g_SwMaterialTex0Sampler, SW_SLOT_MATERIAL_TEX0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex1, g_SwMaterialTex1Sampler, SW_SLOT_MATERIAL_TEX1 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex2, g_SwMaterialTex2Sampler, SW_SLOT_MATERIAL_TEX2 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex3, g_SwMaterialTex3Sampler, SW_SLOT_MATERIAL_TEX3 );

#if defined( DX11 )
// DX11 정적 샘플러 세트 s9..s15 (bindingslots.hlsli 4) — 엔진이 디바이스 초기화 때 건다(D3D11RHIDevice::bindStaticSamplers).
// DX12 와 같은 표(SW_SAMPLER_*)라 셰이더가 고른 samplerId 가 DX11 에서도 존중된다.
SamplerState g_SwSampler0 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER0 ) );
SamplerState g_SwSampler1 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER1 ) );
SamplerState g_SwSampler2 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER2 ) );
SamplerState g_SwSampler3 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER3 ) );
SamplerState g_SwSampler4 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER4 ) );
SamplerState g_SwSampler5 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER5 ) );
SamplerState g_SwSampler6 : register( SW_CAT( s, SW_DX11_STATIC_SAMPLER6 ) );
/** @brief 슬롯 텍스처를 정적 샘플러 세트의 samplerId 로 샘플링합니다 — SM5.0 은 샘플러 배열 동적 인덱싱이 없어 리터럴 분기. */
float4 SwSampleSlotWith( Texture2D tex, uint samplerId, float2 uv )
{
	switch ( samplerId )
	{
		case 1: return tex.Sample( g_SwSampler1, uv );
		case 2: return tex.Sample( g_SwSampler2, uv );
		case 3: return tex.Sample( g_SwSampler3, uv );
		case 4: return tex.Sample( g_SwSampler4, uv );
		case 5: return tex.Sample( g_SwSampler5, uv );
		case 6: return tex.Sample( g_SwSampler6, uv );
		default: return tex.Sample( g_SwSampler0, uv );
	}
}
#endif

float4 SW_SampleIndex( uint index, float2 uv )
{
	if ( index == SW_INVALID_INDEX )
		return float4( 0, 0, 0, 1 );
	// FrameRenderer 가 [shadow/source, albedo/srcDepth, normal, depth] 순으로 t0..t3 에 바인딩.
	if ( index == g_ShadowMapIndex || index == g_SourceColorIndex )
		return g_SwSlot0.Sample( g_SwSlot0Sampler, uv );
	if ( index == g_GBufferAlbedoIndex || index == g_SourceDepthIndex )
		return g_SwSlot1.Sample( g_SwSlot1Sampler, uv );
	if ( index == g_GBufferNormalIndex )
		return g_SwSlot2.Sample( g_SwSlot2Sampler, uv );
	if ( index == g_SceneDepthIndex )
		return g_SwSlot3.Sample( g_SwSlot3Sampler, uv );
	return g_SwSlot0.Sample( g_SwSlot0Sampler, uv );
}
#if defined( DX11 )
/** @brief DX11: 텍스처는 슬롯 멀티플렉싱(SW_SampleIndex 와 같은 표), 샘플러는 정적 세트에서 samplerId 로 고른다. */
float4 SW_SampleIndexWith( uint index, uint samplerId, float2 uv )
{
	if ( index == SW_INVALID_INDEX )
		return float4( 0, 0, 0, 1 );
	if ( index == g_ShadowMapIndex || index == g_SourceColorIndex )
		return SwSampleSlotWith( g_SwSlot0, samplerId, uv );
	if ( index == g_GBufferAlbedoIndex || index == g_SourceDepthIndex )
		return SwSampleSlotWith( g_SwSlot1, samplerId, uv );
	if ( index == g_GBufferNormalIndex )
		return SwSampleSlotWith( g_SwSlot2, samplerId, uv );
	if ( index == g_SceneDepthIndex )
		return SwSampleSlotWith( g_SwSlot3, samplerId, uv );
	return SwSampleSlotWith( g_SwSlot0, samplerId, uv );
}
#else
/** @brief OpenGL 은 결합 샘플러뿐(ARB_gl_spirv 는 분리 샘플러를 못 쓴다)이라 samplerId 를 무시한다 — 슬롯의 샘플러 상태는 엔진이 정한다. */
float4 SW_SampleIndexWith( uint index, uint samplerId, float2 uv )
{
	return SW_SampleIndex( index, uv ); // samplerId 는 쓰지 않는다
}
#endif
/** @brief 에뮬 백엔드: 비교 샘플러가 없어 저장된 깊이를 읽어 직접 비교한다 (필터링 없는 하드 섀도). */
float SW_SampleShadowCmp( uint index, float2 uv, float depth )
{
	if ( index == SW_INVALID_INDEX )
		return 1.0f;
	return ( depth <= SW_SampleIndex( index, uv ).r ) ? 1.0f : 0.0f;
}

#endif // !SW_STAGE_COMPUTE

#if defined( SW_STAGE_COMPUTE )
// 컴퓨트 RW 텍스처 고정 슬롯 u4..u7 — 엔진이 bindComputeUAV( index, SW_SLOT_COMPUTE_TEXUAV0 + 서수 ) 로 건다. index = 서수.
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot0, SW_SLOT_COMPUTE_TEXUAV0, SW_GL_IMAGE_UNIT0 );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot1, SW_SLOT_COMPUTE_TEXUAV1, SW_GL_IMAGE_UNIT1 );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot2, SW_SLOT_COMPUTE_TEXUAV2, SW_GL_IMAGE_UNIT2 );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot3, SW_SLOT_COMPUTE_TEXUAV3, SW_GL_IMAGE_UNIT3 );
void SW_StoreTex2D( uint index, uint2 coord, float4 value )
{
	if ( index == 0 ) g_SwRWSlot0[coord] = value;
	else if ( index == 1 ) g_SwRWSlot1[coord] = value;
	else if ( index == 2 ) g_SwRWSlot2[coord] = value;
	else if ( index == 3 ) g_SwRWSlot3[coord] = value;
}
float4 SW_LoadRWTex2D( uint index, uint2 coord )
{
	if ( index == 0 ) return g_SwRWSlot0[coord];
	if ( index == 1 ) return g_SwRWSlot1[coord];
	if ( index == 2 ) return g_SwRWSlot2[coord];
	if ( index == 3 ) return g_SwRWSlot3[coord];
	return float4( 0, 0, 0, 0 );
}
#endif // SW_STAGE_COMPUTE

#endif

#if defined( SW_NATIVE_BINDLESS ) || !defined( SW_STAGE_COMPUTE )
/**
 * @brief 머티리얼이 준 텍스처 인덱스를 샘플링합니다 (머티리얼 데이터의 uint 슬롯).
 * @details SW_SampleIndex 와 나누는 이유: 그쪽은 **엔진이 아는 인덱스**(그림자·G버퍼 등) 전용이다.
 *          DX11/OpenGL 은 bindless 가 없어 t0..t3 에 걸린 엔진 텍스처를 인덱스 값 비교로 되짚는
 *          에뮬 경로라, 머티리얼이 준 임의 인덱스는 풀 수 없다 — 그래서 엔진이 머티리얼 텍스처를
 *          t5..t8 에 서수 순서로 걸고 머티리얼 데이터에는 서수를 넣는다. DX12/Vulkan 은 전역 인덱스 그대로다.
 */
#if defined( SW_NATIVE_BINDLESS )
float4 SW_SampleMaterialTexture( uint index, float2 uv )
{
	// 네이티브 bindless: index 는 배열 전역 인덱스다.
	if ( index == SW_INVALID_INDEX )
		return float4( 1, 1, 1, 1 );
	return SW_SampleIndex( index, uv );
}
#else
float4 SW_SampleMaterialTexture( uint index, float2 uv )
{
	// 에뮬 백엔드: index 는 전역 인덱스가 아니라 **머티리얼 텍스처 서수**(0..N-1)다.
	// 엔진이 그 서수 순서대로 t5..t8 에 바인딩해 둔다. SM5.0 은 리소스 배열 동적 인덱싱이 안 되므로
	// (그건 SM5.1 = D3D12) 리터럴 분기로 고른다 — 슬롯 수가 4 라 분기도 4 개다.
	if ( index == 0 ) return g_SwMaterialTex0.Sample( g_SwMaterialTex0Sampler, uv );
	if ( index == 1 ) return g_SwMaterialTex1.Sample( g_SwMaterialTex1Sampler, uv );
	if ( index == 2 ) return g_SwMaterialTex2.Sample( g_SwMaterialTex2Sampler, uv );
	if ( index == 3 ) return g_SwMaterialTex3.Sample( g_SwMaterialTex3Sampler, uv );
	return float4( 1, 1, 1, 1 );
}
#endif

/** @brief 하위호환: 예전 SampleBindlessIndex 이름. */
float4 SampleBindlessIndex( uint index, float2 uv ) { return SW_SampleIndex( index, uv ); }

float4 SampleShadow( float2 uv )      { return SW_SampleIndex( g_ShadowMapIndex, uv ); }
float4 SampleAlbedo( float2 uv )      { return SW_SampleIndex( g_GBufferAlbedoIndex, uv ); }
float4 SampleNormal( float2 uv )      { return SW_SampleIndex( g_GBufferNormalIndex, uv ); }
float4 SampleDepth( float2 uv )       { return SW_SampleIndex( g_SceneDepthIndex, uv ); }
float4 SampleSource( float2 uv )      { return SW_SampleIndex( g_SourceColorIndex, uv ); }
float4 SampleSourceDepth( float2 uv ) { return SW_SampleIndex( g_SourceDepthIndex, uv ); }

#endif // SW_NATIVE_BINDLESS || !SW_STAGE_COMPUTE

// 축 정렬 데모 큐브 노멀 (bindless.hlsli 하위호환).
float3 DemoCubeNormal( float3 pos )
{
	float3 a = abs( pos );
	if ( a.x >= a.y && a.x >= a.z )
		return float3( sign( pos.x ), 0.0f, 0.0f );
	if ( a.y >= a.x && a.y >= a.z )
		return float3( 0.0f, sign( pos.y ), 0.0f );
	return float3( 0.0f, 0.0f, sign( pos.z ) );
}

#endif // SW_ENGINE_BINDING_HLSLI
