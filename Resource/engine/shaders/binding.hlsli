/**
 * binding.hlsli — 리플렉션 구동 바인딩용 셰이더 헬퍼 (bindless.hlsli 대체).
 *
 * - 셰이더는 `#include "binding.hlsli"` 하나만 하고, `g_ViewProj` / `g_World` 등 PassCB 필드와
 *   `SampleShadow(uv)` / `SampleSource(uv)` 등 헬퍼를 바로 쓴다. (예전 `GetPassCB()` 인다이렉션 없음)
 * - 엔진(C++ ShaderBindingBinder)이 ShaderReflection 으로 PassCB 멤버 이름을 읽어 값을 채운다.
 *   따라서 이 파일의 PassCB 를 고치면 C++ 는 자동으로 따라온다 (미러 없음).
 * - 텍스처는 이름 규약: `uint g_<Name>Index` (PassCB) ↔ 엔진 리소스 `"<Name>"`.
 *   네이티브 bindless(DX12 SM6.6 / Vulkan): 인덱스로 힙 직접 샘플.
 *   에뮬(DX11 / OpenGL): 엔진이 리플렉션 t# 슬롯에 SRV 를 바인딩, 값 비교로 멀티플렉싱.
 * - GPUScene 인스턴스드 드로우: VS 는 `SwLoadInstanceWorld( SV_InstanceID )` 로 월드 행렬을 얻는다
 *   (언리얼 GPUScene 방식 — per-instance world/material 을 영속 구조버퍼에서 읽음).
 */

#ifndef SW_ENGINE_BINDING_HLSLI
#define SW_ENGINE_BINDING_HLSLI

#include "common.hlsli"
#include "bindingslots.hlsli"

static const uint SW_INVALID_INDEX = 0xFFFFFFFFu;
static const uint kInvalidBindlessIndex = 0xFFFFFFFFu; // 하위호환 별칭

// ------------------------------------------------------------------------------
// 1) PassCB — b0. 셰이더가 실제 쓰는 필드만. 엔진이 이름으로 채운다.
// ------------------------------------------------------------------------------
SW_DECLARE_CBUFFER( PassCB, 0 )
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
	uint     g_InstanceBase;     // GPUScene 인스턴스 버퍼에서 이 배치의 시작 오프셋
	uint     g_SwInstancesIndex; // 인스턴스 구조버퍼 bindless SRV 인덱스 (DX12). SW_INVALID_INDEX 면 g_World 폴백
};

// ------------------------------------------------------------------------------
// 1-1) GPUScene 인스턴스 (per-instance world/material). C++ GpuInstance 와 레이아웃 일치.
//      VS 는 SwLoadInstanceWorld( SV_InstanceID ) 로 월드 행렬을 얻는다.
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

#if defined( SW_BINDLESS ) && defined( DX12 )

float4x4 SwLoadInstanceWorld( uint instanceId )
{
	if ( g_SwInstancesIndex == SW_INVALID_INDEX )
		return g_World;
	StructuredBuffer<SwInstanceData> instances = ResourceDescriptorHeap[NonUniformResourceIndex( g_SwInstancesIndex )];
	return instances[g_InstanceBase + instanceId].world;
}

#elif defined( __spirv__ ) && defined( VULKAN )

// Vulkan 그래픽스 storage buffer 바인딩은 파이프라인 레이아웃 set 6 (STORAGE_BUFFER) 를 통해 이뤄진다.
SW_DECLARE_STRUCTURED_BUFFER_SPACE( SwInstanceData, g_SwInstances, 0, 6 );
float4x4 SwLoadInstanceWorld( uint instanceId )
{
	if ( g_SwInstancesIndex == SW_INVALID_INDEX )
		return g_World;
	return g_SwInstances[g_InstanceBase + instanceId].world;
}

#else

// DX11 / OpenGL : 엔진이 리플렉션 t 슬롯에 인스턴스 SRV/SSBO 를 바인딩.
// 슬롯 번호는 리터럴이어야 한다 (register( t##slot ) 토큰 페이스트가 매크로 확장보다 먼저). = SW_SLOT_INSTANCE_SRV.
SW_DECLARE_STRUCTURED_BUFFER( SwInstanceData, g_SwInstances, 4 );
float4x4 SwLoadInstanceWorld( uint instanceId )
{
	if ( g_SwInstancesIndex == SW_INVALID_INDEX )
		return g_World;
	return g_SwInstances[g_InstanceBase + instanceId].world;
}

#endif

// ------------------------------------------------------------------------------
// 2) 샘플러
// ------------------------------------------------------------------------------
#if defined( __spirv__ )
[[vk::binding( SW_SAMPLER_LINEAR_WRAP, 4 )]] SamplerState g_SwSamplerLinearWrap : register( s0, space4 );
#else
SamplerState g_SwSamplerLinearWrap : register( s0 );
#endif

// ------------------------------------------------------------------------------
// 3) 텍스처 샘플 — 이름 기반
// ------------------------------------------------------------------------------
#if defined( SW_BINDLESS ) && defined( DX12 )

float4 SW_SampleIndex( uint index, float2 uv )
{
	if ( index == SW_INVALID_INDEX )
		return float4( 0, 0, 0, 1 );
	Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex( index )];
	return tex.Sample( g_SwSamplerLinearWrap, uv );
}

#elif defined( SW_BINDLESS ) && defined( VULKAN )

SW_DECLARE_TEXTURE2D_ARRAY_UNBOUNDED( g_SwBindlessTex2D, 0, 1 );
float4 SW_SampleIndex( uint index, float2 uv )
{
	if ( index == SW_INVALID_INDEX )
		return float4( 0, 0, 0, 1 );
	return g_SwBindlessTex2D[NonUniformResourceIndex( index )].Sample( g_SwSamplerLinearWrap, uv );
}

#else

// DX11 / OpenGL : 엔진이 t0..t3 에 SRV 바인딩. 값 비교로 어느 논리 텍스처인지 판별.
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot0, g_SwSlot0Sampler, 0, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot1, g_SwSlot1Sampler, 1, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot2, g_SwSlot2Sampler, 2, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwSlot3, g_SwSlot3Sampler, 3, 0 );

// 머티리얼 텍스처 고정 슬롯 t5..t8 — 레지스터 번호는 SW_SLOT_MATERIAL_TEX0(=5) 부터다.
// 토큰 붙이기라 매크로 산술을 쓸 수 없어 리터럴로 적는다(둘이 어긋나면 엉뚱한 슬롯을 읽는다).
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex0, g_SwMaterialTex0Sampler, 5, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex1, g_SwMaterialTex1Sampler, 6, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex2, g_SwMaterialTex2Sampler, 7, 0 );
SW_DECLARE_TEXTURE2D_SAMPLER( g_SwMaterialTex3, g_SwMaterialTex3Sampler, 8, 0 );

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

#endif

/**
 * @brief 머티리얼이 준 텍스처 인덱스를 샘플링합니다 (MaterialCB 의 uint 슬롯).
 * @details SW_SampleIndex 와 나누는 이유: 그쪽은 **엔진이 아는 인덱스**(그림자·G버퍼 등) 전용이다.
 *          DX11/OpenGL 은 bindless 가 없어 t0..t3 에 걸린 엔진 텍스처를 인덱스 값 비교로 되짚는
 *          에뮬 경로라, 머티리얼이 준 임의 인덱스는 풀 수 없다 — 그런데 그 경로의 마지막 폴백은
 *          t0(그림자맵)을 샘플링하므로, 그대로 두면 큐브에 그림자맵이 입혀진다. 조용히 엉뚱한
 *          텍스처를 입히느니 흰색(=텍스처 없음)을 돌려준다. DX12/Vulkan 은 네이티브 bindless 라
 *          정상 동작한다. DX11/GL 을 제대로 지원하려면 머티리얼 텍스처를 실제 슬롯에 바인딩하는
 *          경로가 필요하다(아직 없음).
 */
#if defined( SW_BINDLESS ) && ( defined( DX12 ) || defined( VULKAN ) )
float4 SW_SampleMaterialTexture( uint index, float2 uv )
{
	// 네이티브 bindless: index 는 힙/배열 전역 인덱스다.
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
