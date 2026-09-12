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
	uint     g_SwVisibleInstanceIdsIndex; // 컬링이 만든 가시 ID 목록이 걸려 있으면 유효, 아니면 SW_INVALID_INDEX
	uint     g_SwMorphVerticesIndex;      // GPU 가 변형한 정점 풀이 걸려 있으면 유효, 아니면 SW_INVALID_INDEX
	uint     g_SwMorphVertexCount;        // 그 풀의 원소 수 — 범위 밖 인덱스를 막는다
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
	uint g_MorphVertexBase; // 모프 정점 풀에서 이 배치 메시의 시작 오프셋. 0xFFFFFFFF = 모프 안 함
SW_ROOT_CONSTANTS_END
#define SW_DRAW_INSTANCE_BASE  SW_ROOT( g_InstanceBase )
#define SW_DRAW_MATERIAL_COUNT SW_ROOT( g_SwMaterialCount )
#define SW_DRAW_MORPH_BASE     SW_ROOT( g_MorphVertexBase )
#else
// 컴퓨트에는 이 블록이 없다 — 그래픽스 전용 헬퍼(SwLoadInstance / SW_MATERIAL)가 컴파일만 되게 0 으로 둔다.
#define SW_DRAW_INSTANCE_BASE  0u
#define SW_DRAW_MATERIAL_COUNT 0u
#define SW_DRAW_MORPH_BASE     0xFFFFFFFFu
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

// ------------------------------------------------------------------------------
// 1-2) GPU 가 변형한 정점 (메시 모프). C++ RHIVertex 와 레이아웃 일치 (float3 + float4 = 28 바이트).
//      메시마다 버퍼를 따로 두지 않고 **풀 하나에 구간을 나눠 쓴다** — 언리얼 GPU Skin Cache 가 캐시
//      버퍼 하나를 할당해 나눠 쓰는 것과 같다. 그래야 드로우 사이에 바인딩이 바뀌지 않는다(이 엔진의 규약).
// ------------------------------------------------------------------------------
// **float4 두 개다.** float3 뒤에 float4 를 두면 원소가 28 바이트인데, std430 은 vec4 를 16 바이트
// 경계에 맞춘다 — DX/Vulkan 은 DXC 가 명시 오프셋을 적어 그대로 읽지만 OpenGL(ARB_gl_spirv)에서는
// 어긋나 기하가 무너진다(실제로 GL 만 그랬다). 풀 원소는 엔진 내부 형식이라 RHIVertex 와 같을 이유가
// 없으므로 정렬이 안전한 모양으로 둔다. pos.w · col 은 지금 쓰지 않지만 자리를 비워 두지 않는다.
struct SwVertexData
{
	float4 pos;
	float4 col;
};

SW_DECLARE_STRUCTURED_BUFFER( SwVertexData, g_SwMorphVertices, SW_SLOT_MORPH_VERTEX_SRV );

/**
 * @brief 이 정점의 위치 — 모프 대상이면 GPU 가 변형한 값을, 아니면 입력 스트림 값을 돌려준다.
 * @details 폴백이 조건 셋인 이유: (1) 이 배치가 모프 대상이 아니거나, (2) 풀이 안 걸렸거나,
 *          (3) 예산이 모자라 이 메시가 풀에 못 들어갔을 수 있다. 셋 다 "레스트 포즈로 그린다" 로
 *          끝나야 한다 — 언리얼도 스킨 캐시가 차면 일반 경로로 되돌아간다.
 */
float3 SwLoadMorphPosition( uint vertexId, float3 restPosition )
{
	if ( SW_DRAW_MORPH_BASE == SW_INVALID_INDEX || g_SwMorphVerticesIndex == SW_INVALID_INDEX )
		return restPosition;
	const uint element = SW_DRAW_MORPH_BASE + vertexId;
	if ( element >= g_SwMorphVertexCount )
		return restPosition;
	return g_SwMorphVertices[element].pos.xyz;
}

// GPU 컬링이 압축해 넣은 가시 인스턴스 번호 목록. 컬링이 꺼져 있거나 못 만들면 안 걸린다.
SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwVisibleInstanceIds, SW_SLOT_VISIBLE_INSTANCE_SRV );

/**
 * @brief 드로우의 인스턴스 서수를 **실제 인스턴스 번호**로 바꾼다.
 * @details GPU 컬링이 켜져 있으면 드로우가 그리는 것은 "배치의 n 번째 인스턴스"가 아니라 "배치에서
 *          살아남은 n 번째 인스턴스"다. 그 대응이 g_SwVisibleInstanceIds 에 들어 있다.
 *          목록이 없으면(컬링 없음) 예전과 같이 배치 시작 + 서수를 쓴다.
 */
uint SwResolveInstanceId( uint drawInstanceId )
{
	const uint slot = SW_DRAW_INSTANCE_BASE + drawInstanceId;
	if ( g_SwVisibleInstanceIdsIndex != SW_INVALID_INDEX && slot < g_SwInstanceCount )
		return g_SwVisibleInstanceIds[slot];
	return slot;
}

/**
 * @brief 이 드로우의 인스턴스 데이터 — 씬 메시는 전부 인스턴스 버퍼에서 읽는다(경로 하나).
 * @details 인스턴스 버퍼가 안 걸린 드로우(풀스크린·픽스처)나 범위 밖 인덱스는 PassCB 의 g_World 와 머티리얼 원소 0 으로 만든다.
 *          범위 검사는 백엔드마다 다른 OOB 결과(DX11/GL 0, Vulkan robustBufferAccess 0, DX12 루트 SRV 는 정의되지 않음)를
 *          하나로 맞추기 위한 것이다.
 */
SwInstanceData SwLoadInstance( uint instanceId )
{
	const uint element = SwResolveInstanceId( instanceId );
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

float4 SampleShadow( float2 uv )      { return SW_SampleIndex( g_ShadowMapIndex, uv ); }
float4 SampleAlbedo( float2 uv )      { return SW_SampleIndex( g_GBufferAlbedoIndex, uv ); }
float4 SampleNormal( float2 uv )      { return SW_SampleIndex( g_GBufferNormalIndex, uv ); }
float4 SampleDepth( float2 uv )       { return SW_SampleIndex( g_SceneDepthIndex, uv ); }
float4 SampleSource( float2 uv )      { return SW_SampleIndex( g_SourceColorIndex, uv ); }
float4 SampleSourceDepth( float2 uv ) { return SW_SampleIndex( g_SourceDepthIndex, uv ); }

#endif // SW_NATIVE_BINDLESS || !SW_STAGE_COMPUTE

// ------------------------------------------------------------------------------
// 4) 픽셀 출력 — **같은 머티리얼 셰이더가 포워드와 G버퍼 양쪽에 쓰인다.**
//    머티리얼이 셰이더 경로를 정하므로(usesMaterialShader) G버퍼 패스도 머티리얼의 .hlsl 로 그린다.
//    그 셰이더가 SV_TARGET 하나만 내면 **G버퍼의 노멀 타깃이 클리어 값 그대로 남는다** — 실제로
//    그랬고, 디퍼드 조명은 모든 픽셀을 같은 노멀로 계산하고 있었다(오류도 경고도 없이).
//    언리얼이 같은 머티리얼을 패스별 셰이더 **타입**으로 감싸는 자리다. 여기서는 패스가 define 을
//    얹고(SW_PASS_GBUFFER), 출력 서명이 그 define 을 따라간다.
// ------------------------------------------------------------------------------
// 양쪽 다 **구조체**다. 포워드 쪽을 `float4` 로 두면 `SW_SURFACE_OUTPUT PSMain(...)` 에 반환
// 시맨틱이 사라져 DXC 가 "Semantic must be defined for all outputs" 로 거절한다 — 그러면 머티리얼
// 셰이더가 통째로 컴파일되지 않아 화면이 빈다(실제로 한 번 그랬다).
#if defined( SW_PASS_GBUFFER )
struct SwSurfaceOutput
{
	float4 albedo : SV_TARGET0;
	float4 normal : SV_TARGET1;
};
#else
struct SwSurfaceOutput
{
	float4 color : SV_TARGET0;
};
#endif
#define SW_SURFACE_OUTPUT SwSurfaceOutput

/**
 * @brief 표면을 **패스가 원하는 모양**으로 내보낸다.
 * @details 포워드는 셰이딩한 색 하나, G버퍼는 알베도와 월드 노멀 둘. 노멀 인코딩은 한 군데뿐이어야
 *          한다 — 굽는 쪽(여기)과 읽는 쪽(deferredlighting)이 어긋나면 조명이 조용히 틀린다.
 */
SW_SURFACE_OUTPUT SwStoreSurface( float4 litColor, float4 albedo, float3 worldNormal )
{
#if defined( SW_PASS_GBUFFER )
	SwSurfaceOutput output;
	output.albedo = float4( albedo.rgb, 1.0f );
	output.normal = float4( saturate( normalize( worldNormal ) * 0.5f + 0.5f ), 1.0f );
	return output;
#else
	SwSurfaceOutput output;
	output.color = litColor;
	return output;
#endif
}

// 축 정렬 데모 큐브 노멀 — createUnitCube 가 만드는 큐브에만 맞는다(정점에 노멀이 없어서 위치로 만든다).
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
