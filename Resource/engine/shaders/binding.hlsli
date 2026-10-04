/**
 * binding.hlsli — 리플렉션 구동 바인딩용 셰이더 헬퍼.
 *
 * - 셰이더는 `#include "binding.hlsli"` 하나만 하고, `g_ViewProj` / `g_World` 등 PassCB 필드와
 *   `swSampleShadow(uv)` / `swSampleSource(uv)` 등 헬퍼를 바로 쓴다(접근 함수 인다이렉션 없음).
 * - 엔진(C++ ShaderParameterBinder)이 ShaderReflection 으로 PassCB 멤버 이름을 읽어 값을 채운다.
 *   따라서 이 파일의 PassCB 를 고치면 C++ 는 자동으로 따라온다 (미러 없음).
 * - 텍스처는 이름 규약: `uint g_<Name>Index` (PassCB) ↔ 엔진 리소스 `"<Name>"`.
 *   네이티브 bindless(DX12 / Vulkan): 인덱스로 무제한 텍스처 배열을 직접 샘플 (SM6.6 힙 인덱싱 아님).
 *   에뮬(DX11 / OpenGL): 엔진이 리플렉션 t# 슬롯에 SRV 를 바인딩, 값 비교로 멀티플렉싱.
 * - GPUScene(언리얼 방식): 드로우별 데이터는 바인딩이 아니라 버퍼에서 읽는다.
 *     VS: `SwInstanceData instance = swLoadInstance( input.instanceSlot )` → instance.world / instance.materialIndex
 *     PS: `SW_MATERIAL( materialIndex ).color`  — VS 가 materialIndex 를 nointerpolation 으로 넘긴다.
 *   머티리얼 구조체는 셰이더가 SW_MATERIAL_BEGIN/END 로 선언하고, 엔진이 그 셰이더 타입의 머티리얼 데이터를
 *   StructuredBuffer(g_SwMaterials, t9)에 원소로 쌓아 인스턴스에 materialIndex 를 매긴다.
 */

#ifndef SW_ENGINE_BINDING_HLSLI
#define SW_ENGINE_BINDING_HLSLI

#include "common.hlsli"
#include "instancedata.hlsli"

static const uint kInvalidIndex = 0xFFFFFFFFu;

// ------------------------------------------------------------------------------
// 1) PassCB — b0. 셰이더가 실제 쓰는 필드만. 엔진이 이름으로 채운다. 패스(배치)마다 한 번 걸리는 진짜 상수버퍼.
// ------------------------------------------------------------------------------
SW_DECLARE_CBUFFER( PassCB, SW_SLOT_PASS_CB )
{
	float4x4 g_LightViewProj;
	float4x4 g_ViewProj;
	// 뷰-투영의 역행렬 — 디퍼드 조명이 **깊이에서 월드 위치를 복원**하는 데 쓴다(G버퍼에 위치를
	// 따로 굽지 않는다: 첨부 하나를 통째로 아끼고, 언리얼도 깊이에서 복원한다). 점광은 위치가
	// 있어야 거리 감쇠를 계산할 수 있으므로 이게 없으면 디퍼드에 점광을 넣을 수 없다.
	float4x4 g_InvViewProj;
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
	uint     g_AmbientOcclusionIndex; // SSAO 결과(AOColor)가 걸려 있으면 유효, 아니면 kInvalidIndex (AO = 1 로 폴백)
	uint     g_Flags;
	uint     g_SwInstancesIndex; // 인스턴스 구조버퍼가 걸려 있으면 유효, kInvalidIndex 면 g_World 폴백 (풀스크린·픽스처)
	uint     g_SwInstanceCount;  // 인스턴스 버퍼 원소 수 — 범위 밖 인덱스를 막는다 (DX12 루트 SRV 는 경계 검사가 없다)
	uint     g_SwVisibleInstanceIdsIndex; // 컬링이 만든 가시 ID 목록이 걸려 있으면 유효, 아니면 kInvalidIndex
	uint     g_SwMorphVerticesIndex;      // GPU 가 변형한 정점 풀이 걸려 있으면 유효, 아니면 kInvalidIndex
	uint     g_SwMorphVertexCount;        // 그 풀의 원소 수 — 범위 밖 인덱스를 막는다
	uint     g_SwLightsIndex;             // 씬 라이트 버퍼가 걸려 있으면 유효, 아니면 kInvalidIndex
	uint     g_SwLightCount;              // 그 버퍼의 원소 수 (0 이면 PassCB 키라이트 폴백)
	uint     g_SwBatchesIndex;            // 씬 배치 표가 걸려 있으면 유효, 아니면 kInvalidIndex (풀스크린·픽스처)
	uint     g_SwBatchCount;              // 그 표의 원소 수 — 범위 밖 배치 번호를 막는다
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
	// 이 드로우 그룹의 머티리얼 데이터 버퍼(g_SwMaterials) 원소 수 — SW_MATERIAL 이 클램프한다. 그룹(같은 PSO·머티리얼 버퍼) 안에서
	// 같다. 배치마다 다른 값(인스턴스 시작·모프 풀 시작·정점 풀 시작)은 루트 상수가 아니라 배치 표(g_SwBatches)에 있고, 정점은
	// 인스턴스 슬롯 스트림(SwVertexInput.instanceSlot)으로 자기 인스턴스를, 인스턴스로 자기 배치를 찾는다 — 그래서 같은 PSO 의
	// 배치들이 루트 상수를 바꾸지 않고 멀티 드로우 하나로 나간다.
	uint g_SwMaterialCount;
SW_ROOT_CONSTANTS_END
#define SW_DRAW_MATERIAL_COUNT SW_ROOT( g_SwMaterialCount )
#else
// 컴퓨트에는 이 블록이 없다 — 그래픽스 전용 헬퍼(SW_MATERIAL)가 컴파일만 되게 0 으로 둔다.
#define SW_DRAW_MATERIAL_COUNT 0u
#endif

// ------------------------------------------------------------------------------
// 1-1) GPUScene 인스턴스 (per-instance world/material/스프라이트 프레임 · 색). 원소 정의는 instancedata.hlsli 하나다(컴퓨트도 그것을 쓴다).
//      네 백엔드 공통 — 엔진이 리플렉션 슬롯 t4 에 인스턴스 버퍼를 건다 (레지스트리 이름 "SwInstances").
// ------------------------------------------------------------------------------
SW_DECLARE_STRUCTURED_BUFFER( SwInstanceData, g_SwInstances, SW_SLOT_INSTANCE_SRV );

// ------------------------------------------------------------------------------
// 1-1a) 씬 배치 표 — C++ GpuBatchInfo · gpucull.hlsl GpuBatchInfo 와 레이아웃 일치(uint 여덟, 32바이트).
//       배치마다 다른 값은 전부 여기 있다. 정점은 인스턴스(swLoadInstance)의 meshBatchIndex 로 자기 배치를 찾는다.
// ------------------------------------------------------------------------------
struct SwBatchData
{
	uint instanceBase;    // 인스턴스 버퍼(또는 가시 목록)에서 이 배치의 시작
	uint instanceCount;   // CPU 가 센 개수 (컬링은 간접 인자에 자기 개수를 따로 만든다)
	uint sortMode;        // 컬링 전용 (GpuBatchSortMode)
	uint morphVertexBase; // 모프 정점 풀에서 이 메시의 시작. kInvalidIndex = 모프 안 함
	uint firstVertex;     // 정점 풀에서 이 메시의 시작 (간접 인자의 startVertex). SV_VertexID 가 이 값을 포함하는지는 API 마다 다르다 — swComputeMorphElement 참고
	uint pad0;
	uint pad1;
	uint pad2;
};

SW_DECLARE_STRUCTURED_BUFFER( SwBatchData, g_SwBatches, SW_SLOT_BATCH_SRV );

/**
 * @brief 배치 표의 원소. 표가 안 걸렸거나(풀스크린·픽스처) 범위 밖이면 "인스턴스 0 부터 · 모프 없음 · 정점 풀 없음" 이다.
 * @note 분기는 있지만 early-return 은 없다 — GL 드라이버가 early-return 을 잘못 컴파일한 사연은 swComputeMorphElement 참고.
 */
SwBatchData swLoadBatch( uint batchIndex )
{
	SwBatchData batch;
	batch.instanceBase    = 0u;
	batch.instanceCount   = 0u;
	batch.sortMode        = 0u;
	batch.morphVertexBase = kInvalidIndex;
	batch.firstVertex     = 0u;
	batch.pad0            = 0u;
	batch.pad1            = 0u;
	batch.pad2            = 0u;
	const bool bValid = ( g_SwBatchesIndex != kInvalidIndex ) && ( batchIndex < g_SwBatchCount );
	if ( bValid )
		batch = g_SwBatches[batchIndex];
	return batch;
}

// ------------------------------------------------------------------------------
// 1-2) GPU 가 변형한 정점 (메시 모프). C++ `GpuMorphVertex` 와 바이트 배치 일치(float4 둘).
//      메시마다 버퍼를 따로 두지 않고 **풀 하나에 구간을 나눠 쓴다** — 언리얼 GPU Skin Cache 가 캐시
//      버퍼 하나를 할당해 나눠 쓰는 것과 같다. 그래야 드로우 사이에 바인딩이 바뀌지 않는다(이 엔진의 규약).
// ------------------------------------------------------------------------------
// **구조체가 아니라 평면 float4 배열이다.** 정점 하나가 원소 둘 — [2i] 위치, [2i+1] 노멀(w 는 안 쓴다).
// 주의: `struct { float4 pos; float4 nrm; }` 로 두면 레이아웃이 네 백엔드에서 같아도(ArrayStride 32 · 오프셋 0/16)
// OpenGL 만 **같은 원소의 두 멤버를 다른 원소에서 읽는다**(셰이더 모양에 따라 달라진다 — 드라이버의 SPIR-V 경로가
// 구조체 멤버 로드를 다루는 방식). 평면 배열은 멤버가 없으니 그 자리가 아예 없다.
// 색은 담지 않는다 — 정점 셰이더가 색·UV 는 **입력 스트림에서** 읽고 풀에서는 위치와 노멀만 가져간다.
#define SW_MORPH_FLOAT4_PER_VERTEX 2u
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SwMorphVertices, SW_SLOT_MORPH_VERTEX_SRV );

/**
 * @brief 이 정점의 풀 원소 번호 — 모프 대상이 아니면 `kInvalidIndex`.
 * @details 폴백이 조건 셋인 이유: (1) 이 배치가 모프 대상이 아니거나, (2) 풀이 안 걸렸거나,
 *          (3) 예산이 모자라 이 메시가 풀에 못 들어갔을 수 있다. 셋 다 "레스트 포즈로 그린다" 로
 *          끝나야 한다 — 언리얼도 스킨 캐시가 차면 일반 경로로 되돌아간다.
 */
// **분기 없는 한 식이어야 한다.** 주의: `if (base == INVALID || …) return INVALID;` 같은 early-return 을 DXC 는
// SPIR-V 의 `OpSwitch(0){ default: … }` 구조로 내는데, OpenGL 드라이버가 그 모양을 잘못 컴파일해 **같은 인보케이션에서
// 같은 UBO 멤버를 두 번 읽어 다른 값**(0 과 -1)을 낸다 — 결과는 정점마다 한 칸 앞 원소를 읽는 것(DX12·DX11·Vulkan 은
// 같은 소스로 멀쩡하다). 회귀는 RenderPassGpuTest.MorphPoolIdentityMatchesRest 가 픽셀로 잡는다.
uint swComputeMorphElement( uint batchIndex, uint vertexId )
{
	const SwBatchData batch = swLoadBatch( batchIndex );
	// **API 차이 — 이 메시의 로컬 정점 번호.** 간접 드로우의 startVertex 를 SV_VertexID 가 포함하는지가 백엔드마다 다르다:
	// Vulkan(VertexIndex)·OpenGL(gl_VertexID)은 포함하고, D3D11·D3D12 는 드로우 안의 0 기반 번호다.
	// RHIDeviceTest.SceneDrawVertexIdStartsAtZeroOnlyOnD3D 가 네 백엔드에서 이 기대를 실측한다 — 넷 다 포함한다고 보고
	// 빼기만 하면 DX 에서 정점 풀 첫 메시만 모프된다(RenderPassGpuTest.MorphPoolIdentityMatchesRest 가 DX 에서만 진다).
	// 셰이더 파일에 백엔드 분기는 없다 — 이 헤더가 흡수한다.
#if defined( DX11 ) || defined( DX12 )
	const uint local = vertexId;
#else
	const uint local = vertexId - batch.firstVertex; // 풀 밖 메시는 firstVertex 가 0 이라 그대로다
#endif
	const uint base    = batch.morphVertexBase;
	const uint element = base + local;
	// 잘못된 번호(밑돎으로 커진 local)는 element 가 풀 크기를 넘어 여기서 걸러진다.
	const bool bValid = ( base != kInvalidIndex ) && ( g_SwMorphVerticesIndex != kInvalidIndex ) && ( element < g_SwMorphVertexCount );
	return bValid ? element : kInvalidIndex;
}

/**
 * @brief 이 정점의 위치·노멀 — 모프 대상이면 GPU 가 변형한 값을, 아니면 입력 스트림 값을 돌려준다.
 * @details 위치와 노멀을 **함께** 돌려주는 이유는 둘이 같이 변하기 때문이다. 노멀은 정점 속성이므로 위치만 바꾸면
 *          **레스트 포즈의 노멀**이 남아 변형된 표면이 원래 모양대로 칠해진다.
 */
void swLoadMorphedVertex( uint batchIndex, uint vertexId, float3 restPosition, float3 restNormal, out float3 outPosition, out float3 outNormal )
{
	outPosition        = restPosition;
	outNormal          = restNormal;
	const uint element = swComputeMorphElement( batchIndex, vertexId );
	if ( element == kInvalidIndex )
		return;
	outPosition = g_SwMorphVertices[element * SW_MORPH_FLOAT4_PER_VERTEX].xyz;
	outNormal   = g_SwMorphVertices[element * SW_MORPH_FLOAT4_PER_VERTEX + 1u].xyz;
}

/**
 * @brief 위치만 필요한 패스(그림자·뎁스 프리패스)를 위한 짧은 형태.
 * @note 노멀을 읽지 않으므로 그 로드가 통째로 빠진다 — 뎁스 전용 패스는 그게 비용의 전부다.
 */
float3 swLoadMorphPosition( uint batchIndex, uint vertexId, float3 restPosition )
{
	const uint element = swComputeMorphElement( batchIndex, vertexId );
	return ( element == kInvalidIndex ) ? restPosition : g_SwMorphVertices[element * SW_MORPH_FLOAT4_PER_VERTEX].xyz;
}

/**
 * @brief 로컬 위치를 월드로 옮긴다. 정점 위치를 만드는 **모든** 지오메트리 패스가 이것과 swComputeClipPosition 를 쓴다.
 * @details 깊이 프리패스와 기본 패스가 같은 정점에서 **비트까지 같은 깊이**를 내야 기본 패스의 LessEqual 이 통과한다(언리얼은 depth-only 패스와
 *          base pass 가 같은 정점 팩토리 코드를 쓴다). 셰이더마다 같은 식을 따로 적으면 컴파일러가 곱셈 · 덧셈을 다르게 묶을 수 있어(FMA)
 *          `precise` 로 묶음을 고정하고 한 함수로 모은다.
 */
float4 swComputeWorldPosition( float3 localPosition, float4x4 world )
{
	precise float4 worldPosition = mul( float4( localPosition, 1.0f ), world );
	return worldPosition;
}

/** @brief 월드 위치를 클립 공간으로 옮긴다(카메라는 g_ViewProj, 그림자는 g_LightViewProj). swComputeWorldPosition 와 같은 이유로 precise 다. */
float4 swComputeClipPosition( float4 worldPosition, float4x4 viewProj )
{
	precise float4 clipPosition = mul( worldPosition, viewProj );
	return clipPosition;
}

/**
 * @brief 로컬 노멀을 월드로 옮겨 정규화한다. 노멀을 내보내는 **모든** 지오메트리 패스(forwardlit · gbuffer · gbuffernormal)가 이것을 쓴다.
 * @details 노멀은 위치처럼 월드 행렬로 옮기면 안 된다. 행 벡터 규약(`p' = p · M`)에서 접선은 `t · M` 으로 가므로, 수직을 지키는 노멀은
 *          `n · (M⁻¹)ᵀ` 다. 균등 스케일 · 회전뿐이면 두 식의 방향이 같아 드러나지 않지만, 비균등 스케일(스케일한 부모 아래 회전한 자식도)에서는
 *          노멀이 늘어난 축 **쪽으로** 기울어 조명이 틀린다(`mul( float4( n, 0 ), world )` 가 그렇다).
 *          역행렬은 필요 없다. 3x3 의 `(M⁻¹)ᵀ = cof(M) / det(M)` 이고 여인수 행렬의 행은 월드 행 셋의 외적 셋(r1×r2, r2×r0, r0×r1)이다.
 *          정규화하므로 행렬식은 **부호만** 남긴다 — 거울 변환(det < 0)에서 노멀이 안쪽을 보지 않게 한다. 한 축이 0 인 납작한 물체도
 *          보이는 면의 노멀이 남는다(월드 행렬을 곱하면 그 면의 노멀이 0 이 되어 NaN 이다). 언리얼의 로컬 정점 팩토리도 노멀을
 *          역스케일 · 행렬식 부호로 옮긴다. 회귀는 RenderPassGpuTest.NormalsStayPerpendicularUnderNonUniformScale 가 G버퍼 노멀로 잡는다.
 */
float3 swComputeWorldNormal( float3 localNormal, float4x4 world )
{
	const float3 row0        = world[0].xyz;
	const float3 row1        = world[1].xyz;
	const float3 row2        = world[2].xyz;
	const float3 cofactor0   = cross( row1, row2 );
	const float3 cofactor1   = cross( row2, row0 );
	const float3 cofactor2   = cross( row0, row1 );
	const float  handedness  = ( dot( row0, cofactor0 ) < 0.0f ) ? -1.0f : 1.0f;
	const float3 worldNormal = localNormal.x * cofactor0 + localNormal.y * cofactor1 + localNormal.z * cofactor2;
	return normalize( worldNormal * handedness );
}

// GPU 컬링이 압축해 넣은 가시 인스턴스 번호 목록. 컬링이 꺼져 있거나 못 만들면 안 걸린다.
SW_DECLARE_STRUCTURED_BUFFER( uint, g_SwVisibleInstanceIds, SW_SLOT_VISIBLE_INSTANCE_SRV );

/**
 * @brief 인스턴스 슬롯(전역 자리)을 **실제 인스턴스 번호**로 바꾼다.
 * @details GPU 컬링이 켜져 있으면 드로우가 그리는 것은 "배치의 n 번째 인스턴스"가 아니라 "배치에서
 *          살아남은 n 번째 인스턴스"다. 그 대응이 g_SwVisibleInstanceIds 에 들어 있다.
 *          목록이 없으면(컬링 없음) 슬롯이 곧 인스턴스 번호다.
 */
uint swResolveInstanceId( uint instanceSlot )
{
	// 슬롯은 입력 어셈블러가 인스턴스 슬롯 스트림에서 준 전역 자리다(간접 인자의 startInstance + 인스턴스 서수).
	const uint slot = instanceSlot;
	if ( g_SwVisibleInstanceIdsIndex != kInvalidIndex && slot < g_SwInstanceCount )
		return g_SwVisibleInstanceIds[slot];
	return slot;
}

/**
 * @brief 이 드로우의 인스턴스 데이터 — 씬 메시는 전부 인스턴스 버퍼에서 읽는다(경로 하나).
 * @details 인스턴스 버퍼가 안 걸린 드로우(풀스크린·픽스처)나 범위 밖 인덱스는 PassCB 의 g_World 와 머티리얼 원소 0 으로 만든다.
 *          범위 검사는 백엔드마다 다른 OOB 결과(DX11/GL 0, Vulkan robustBufferAccess 0, DX12 루트 SRV 는 정의되지 않음)를
 *          하나로 맞추기 위한 것이다.
 */
SwInstanceData swLoadInstance( uint instanceSlot )
{
	const uint element = swResolveInstanceId( instanceSlot );
	if ( g_SwInstancesIndex != kInvalidIndex && element < g_SwInstanceCount )
		return g_SwInstances[element];
	SwInstanceData instance;
	instance.world          = g_World;
	instance.boundsCenter   = float3( 0, 0, 0 );
	instance.boundsRadius   = 0;
	instance.meshBatchIndex = 0;
	instance.materialIndex  = 0;
	instance.blendMode      = 0;
	instance.spinSeed       = 0;
	instance.uvStart        = 0u;          // (0, 0)
	instance.uvEnd          = 0xFFFFFFFFu; // (1, 1) — 텍스처 전체
	instance.tint           = 0xFFFFFFFFu; // 흰색 불투명
	instance.reserved       = 0u;
	return instance;
}

float4x4 swLoadInstanceWorld( uint instanceSlot )
{
	return swLoadInstance( instanceSlot ).world;
}

// ------------------------------------------------------------------------------
// 1-2) 머티리얼 데이터 — 셰이더가 구조체를 선언하면 엔진이 그 타입의 머티리얼들을 StructuredBuffer 원소로 쌓는다.
//      SW_MATERIAL_BEGIN { float4 color; uint albedoMap; } SW_MATERIAL_END
//      ... SW_MATERIAL( instance.materialIndex ).color
//      리플렉션 이름 g_SwMaterials(t9) ↔ 레지스트리 "SwMaterials" (배치마다 그 셰이더 타입의 버퍼를 등록한다).
//      원소 레이아웃은 네 백엔드가 같다 — SPIR-V 도 DX 패킹(-fvk-use-dx-layout, ShaderCompiler.cpp)으로 굽고
//      ShaderBindingValidatorTest.ReflectionNamesAreUniformAcrossBackends 가 구운 바이너리로 확인한다.
//      인덱스는 g_SwMaterialCount 로 클램프한다 — 잘못된 인덱스가 백엔드마다 다른 OOB 결과를 내지 않도록.
//      **머티리얼은 픽셀 단계에서만 읽는다.** 정점 · 픽셀 두 단계가 g_SwMaterials 를 함께 읽으면 GL(ARB_gl_spirv) 드라이버가 구조 버퍼의
//      이름 없는 멤버를 단계마다 다른 SPIR-V id 로 이름 짓고("_struct14_member0" · "_struct19_member0") 링크를 거절한다 — 그 배치는 패스
//      셰이더로 물러나 그려진다. 정점에서 필요한 값(sprite2d 의 uvRect)은 픽셀에서 적용한다(아핀이면 결과가 같다).
//      RenderPassGpuTest.SpriteDrawsWithTheSpriteShader 가 GL 에서 확인한다.
// ------------------------------------------------------------------------------
uint swClampMaterialIndex( uint index )
{
	const uint materialCount = SW_DRAW_MATERIAL_COUNT;
	return ( materialCount == 0 ) ? 0 : min( index, materialCount - 1 );
}
#define SW_MATERIAL_BEGIN struct SwMaterialData
#define SW_MATERIAL_END   ; SW_DECLARE_STRUCTURED_BUFFER( SwMaterialData, g_SwMaterials, SW_SLOT_MATERIAL_BUFFER );
#define SW_MATERIAL( index ) g_SwMaterials[swClampMaterialIndex( index )]

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
#endif
#endif

// ------------------------------------------------------------------------------
// 3) 텍스처 샘플 — 이름 기반. 컴퓨트 RW 텍스처는 swStoreRwTexture2D / swLoadRwTexture2D (컴퓨트 스테이지에서만 선언된다).
// ------------------------------------------------------------------------------
#if defined( SW_NATIVE_BINDLESS )

// 무제한 텍스처 배열 — DX12 t0 space1 (루트 시그니처 테이블), Vulkan set 1 binding 0. SM5.1 무제한 배열이지 6.6 힙 인덱싱이 아니다.
#if defined( __spirv__ )
[[vk::binding( SW_VK_TEXTURE_BINDING, SW_VK_TEXTURE_SET )]] Texture2D g_SwBindlessTex2D[] : register( t0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
#else
Texture2D g_SwBindlessTex2D[] : register( t0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
#endif
/** @brief 인덱스의 텍스처를 샘플러 세트의 samplerId(SW_SAMPLER_*, 비교 샘플러 제외)로 샘플링합니다. */
float4 swSampleIndexWith( uint index, uint samplerId, float2 uv )
{
	if ( index == kInvalidIndex )
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
/** @brief 엔진 텍스처(풀스크린 입력 · G버퍼 · 깊이 · 그림자 맵)를 엔진 텍스처 샘플러(선형 · 클램프)로 읽습니다 — 에뮬 백엔드가 t0..t3 에 거는 것과 같다. */
float4 swSampleIndex( uint index, float2 uv )
{
	return swSampleIndexWith( index, SW_ENGINE_TEXTURE_SAMPLER, uv );
}
/**
 * @brief 씬 깊이의 2x2 이웃을 **텍스처 연산 한 번**으로 읽습니다 (`GatherRed`).
 * @details 게더는 필터를 하지 않고 텍셀 넷을 그대로 돌려준다 — 값은 바이리니어 샘플 하나와 비슷한데
 *          텍셀은 넷이다. 그래서 십자 다섯 칸이 샘플 다섯 번이 아니라 게더 두 번이면 된다.
 * @note 샘플러 3 번은 `SW_SAMPLER_POINT_CLAMP` 다 (bindingslots.hlsli 4 의 표).
 *       게더는 필터 모드를 안 보지만 **주소 모드는 본다** — 화면 가장자리에서 WRAP 이면 반대편을 읽는다.
 */
float4 swGatherDepthRed( float2 uv )
{
	if ( g_SceneDepthIndex == kInvalidIndex )
		return float4( 1, 1, 1, 1 );
#if defined( __spirv__ )
	return g_SwBindlessTex2D[NonUniformResourceIndex( g_SceneDepthIndex )].GatherRed( SW_SAMPLER_STATE( SW_SAMPLER_POINT_CLAMP ), uv );
#else
	return g_SwBindlessTex2D[NonUniformResourceIndex( g_SceneDepthIndex )].GatherRed( g_SwSampler3, uv );
#endif
}
/** @brief 깊이 텍스처를 비교 샘플러(LESS_EQUAL)로 읽습니다 — 1 이면 depth 가 저장값 이하(빛 받음). */
float swSampleShadowComparison( uint index, float2 uv, float depth )
{
	if ( index == kInvalidIndex )
		return 1.0f;
	return g_SwBindlessTex2D[NonUniformResourceIndex( index )].SampleCmpLevelZero( g_SwSamplerShadowCmp, uv, depth );
}

#if defined( SW_STAGE_COMPUTE )
// 컴퓨트 RW 텍스처 배열 — DX12 u0 space1 (텍스처 테이블의 두 번째 범위), Vulkan set 1 binding 3 (STORAGE_IMAGE[]).
// 인덱스는 registerBindlessTextureUav 가 준다. 컴퓨트에서만 선언한다 — 그래픽스 스테이지에 UAV 배열을 두면
// DX12 가 PS UAV 슬롯을, Vulkan 이 vertexPipelineStores 기능을 요구한다.
#if defined( __spirv__ )
[[vk::binding( SW_VK_RWTEXTURE_BINDING, SW_VK_TEXTURE_SET )]] RWTexture2D<float4> g_SwBindlessRWTex2D[] : register( u0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
#else
RWTexture2D<float4> g_SwBindlessRWTex2D[] : register( u0, SW_CAT( space, SW_SPACE_BINDLESS_TEX ) );
#endif
void swStoreRwTexture2D( uint index, uint2 texelPosition, float4 value )
{
	if ( index == kInvalidIndex )
		return;
	g_SwBindlessRWTex2D[NonUniformResourceIndex( index )][texelPosition] = value;
}
float4 swLoadRwTexture2D( uint index, uint2 texelPosition )
{
	if ( index == kInvalidIndex )
		return float4( 0, 0, 0, 0 );
	return g_SwBindlessRWTex2D[NonUniformResourceIndex( index )][texelPosition];
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
float4 swSampleSlotWith( Texture2D slotTexture, uint samplerId, float2 uv )
{
	switch ( samplerId )
	{
		case 1: return slotTexture.Sample( g_SwSampler1, uv );
		case 2: return slotTexture.Sample( g_SwSampler2, uv );
		case 3: return slotTexture.Sample( g_SwSampler3, uv );
		case 4: return slotTexture.Sample( g_SwSampler4, uv );
		case 5: return slotTexture.Sample( g_SwSampler5, uv );
		case 6: return slotTexture.Sample( g_SwSampler6, uv );
		default: return slotTexture.Sample( g_SwSampler0, uv );
	}
}
#endif

float4 swSampleIndex( uint index, float2 uv )
{
	if ( index == kInvalidIndex )
		return float4( 0, 0, 0, 1 );
	// FrameRenderer 가 [shadow/source, albedo/ao, normal, depth] 순으로 t0..t3 에 바인딩 (commitBindlessTextureBindings 와 같은 표).
	if ( index == g_ShadowMapIndex || index == g_SourceColorIndex )
		return g_SwSlot0.Sample( g_SwSlot0Sampler, uv );
	if ( index == g_GBufferAlbedoIndex || index == g_AmbientOcclusionIndex )
		return g_SwSlot1.Sample( g_SwSlot1Sampler, uv );
	if ( index == g_GBufferNormalIndex )
		return g_SwSlot2.Sample( g_SwSlot2Sampler, uv );
	if ( index == g_SceneDepthIndex )
		return g_SwSlot3.Sample( g_SwSlot3Sampler, uv );
	return g_SwSlot0.Sample( g_SwSlot0Sampler, uv );
}
/**
 * @brief 씬 깊이의 2x2 이웃을 **텍스처 연산 한 번**으로 읽습니다 (`GatherRed`).
 * @details 에뮬 백엔드는 깊이가 늘 슬롯 3 이다(위 표와 같은 순서) — 인덱스로 되짚을 필요가 없다.
 *          샘플러는 슬롯에 걸린 것을 그대로 쓴다. 게더는 필터 모드를 보지 않는다.
 */
float4 swGatherDepthRed( float2 uv )
{
	if ( g_SceneDepthIndex == kInvalidIndex )
		return float4( 1, 1, 1, 1 );
	return g_SwSlot3.GatherRed( g_SwSlot3Sampler, uv );
}
#if defined( DX11 )
/** @brief DX11: 텍스처는 슬롯 멀티플렉싱(swSampleIndex 와 같은 표), 샘플러는 정적 세트에서 samplerId 로 고른다. */
float4 swSampleIndexWith( uint index, uint samplerId, float2 uv )
{
	if ( index == kInvalidIndex )
		return float4( 0, 0, 0, 1 );
	if ( index == g_ShadowMapIndex || index == g_SourceColorIndex )
		return swSampleSlotWith( g_SwSlot0, samplerId, uv );
	if ( index == g_GBufferAlbedoIndex || index == g_AmbientOcclusionIndex )
		return swSampleSlotWith( g_SwSlot1, samplerId, uv );
	if ( index == g_GBufferNormalIndex )
		return swSampleSlotWith( g_SwSlot2, samplerId, uv );
	if ( index == g_SceneDepthIndex )
		return swSampleSlotWith( g_SwSlot3, samplerId, uv );
	return swSampleSlotWith( g_SwSlot0, samplerId, uv );
}
#else
/** @brief OpenGL 은 결합 샘플러뿐(ARB_gl_spirv 는 분리 샘플러를 못 쓴다)이라 samplerId 를 무시한다 — 슬롯의 샘플러 상태는 엔진이 정한다. */
float4 swSampleIndexWith( uint index, uint samplerId, float2 uv )
{
	return swSampleIndex( index, uv ); // samplerId 는 쓰지 않는다
}
#endif
/** @brief 에뮬 백엔드: 비교 샘플러가 없어 저장된 깊이를 읽어 직접 비교한다 (필터링 없는 하드 섀도). */
float swSampleShadowComparison( uint index, float2 uv, float depth )
{
	if ( index == kInvalidIndex )
		return 1.0f;
	return ( depth <= swSampleIndex( index, uv ).r ) ? 1.0f : 0.0f;
}

#endif // !SW_STAGE_COMPUTE

#if defined( SW_STAGE_COMPUTE )
// 컴퓨트 RW 텍스처 고정 슬롯 u4..u7 — 엔진이 bindComputeUav( index, SW_SLOT_COMPUTE_TEXUAV0 + 서수 ) 로 건다. index = 서수.
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot0, SW_SLOT_COMPUTE_TEXUAV0, SW_GL_IMAGE_UNIT0 );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot1, SW_SLOT_COMPUTE_TEXUAV1, SW_GL_IMAGE_UNIT1 );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot2, SW_SLOT_COMPUTE_TEXUAV2, SW_GL_IMAGE_UNIT2 );
SW_DECLARE_RW_TEXTURE2D( g_SwRWSlot3, SW_SLOT_COMPUTE_TEXUAV3, SW_GL_IMAGE_UNIT3 );
void swStoreRwTexture2D( uint index, uint2 texelPosition, float4 value )
{
	if ( index == 0 ) g_SwRWSlot0[texelPosition] = value;
	else if ( index == 1 ) g_SwRWSlot1[texelPosition] = value;
	else if ( index == 2 ) g_SwRWSlot2[texelPosition] = value;
	else if ( index == 3 ) g_SwRWSlot3[texelPosition] = value;
}
float4 swLoadRwTexture2D( uint index, uint2 texelPosition )
{
	if ( index == 0 ) return g_SwRWSlot0[texelPosition];
	if ( index == 1 ) return g_SwRWSlot1[texelPosition];
	if ( index == 2 ) return g_SwRWSlot2[texelPosition];
	if ( index == 3 ) return g_SwRWSlot3[texelPosition];
	return float4( 0, 0, 0, 0 );
}
#endif // SW_STAGE_COMPUTE

#endif

#if defined( SW_NATIVE_BINDLESS ) || !defined( SW_STAGE_COMPUTE )
/**
 * @brief 머티리얼이 준 텍스처 인덱스를 샘플링합니다 (머티리얼 데이터의 uint 슬롯).
 * @details swSampleIndex 와 나누는 이유: 그쪽은 **엔진이 아는 인덱스**(그림자·G버퍼 등) 전용이다.
 *          DX11/OpenGL 은 bindless 가 없어 t0..t3 에 걸린 엔진 텍스처를 인덱스 값 비교로 되짚는
 *          에뮬 경로라, 머티리얼이 준 임의 인덱스는 풀 수 없다 — 그래서 엔진이 머티리얼 텍스처를
 *          t5..t8 에 서수 순서로 걸고 머티리얼 데이터에는 서수를 넣는다. DX12/Vulkan 은 전역 인덱스 그대로다.
 */
#if defined( SW_NATIVE_BINDLESS )
float4 swSampleMaterialTexture( uint index, float2 uv )
{
	// 네이티브 bindless: index 는 배열 전역 인덱스다. 샘플러는 머티리얼 텍스처 샘플러(선형 · 랩)다.
	if ( index == kInvalidIndex )
		return float4( 1, 1, 1, 1 );
	return swSampleIndexWith( index, SW_MATERIAL_TEXTURE_SAMPLER, uv );
}
#else
float4 swSampleMaterialTexture( uint index, float2 uv )
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

float4 swSampleShadow( float2 uv )      { return swSampleIndex( g_ShadowMapIndex, uv ); }
float4 swSampleAlbedo( float2 uv )      { return swSampleIndex( g_GBufferAlbedoIndex, uv ); }
float4 swSampleNormal( float2 uv )      { return swSampleIndex( g_GBufferNormalIndex, uv ); }
float4 swSampleDepth( float2 uv )       { return swSampleIndex( g_SceneDepthIndex, uv ); }
float4 swSampleSource( float2 uv )      { return swSampleIndex( g_SourceColorIndex, uv ); }
/**
 * @brief 화면과 1:1 인 전체화면 패스 전용 — 텍셀 중심에 정확히 떨어지므로 선형 필터가 **할 일이 없다**.
 * @details 그런데도 값은 공짜가 아니다: 선형 샘플은 텍셀 넷을 읽어 섞고, 깊이 같은 포맷은 그 경로가
 *          더 비싸다. 배율이 1 이 아닌 패스(블룸의 바이리니어 2탭 같은 것)에는 쓰면 안 된다 —
 *          거기서는 섞는 것이 목적이다.
 * @note CLAMP 인 것도 의미가 있다. 화면 가장자리에서 한 텍셀 비낀 샘플이 WRAP 이면 반대편을 읽는다.
 */
float4 swSampleDepthPoint( float2 uv )  { return swSampleIndexWith( g_SceneDepthIndex, SW_SAMPLER_POINT_CLAMP, uv ); }
float4 swSampleSourcePoint( float2 uv ) { return swSampleIndexWith( g_SourceColorIndex, SW_SAMPLER_POINT_CLAMP, uv ); }
/**
 * @brief 깊이의 십자 다섯 칸(중심 + 상하좌우 한 텍셀)을 읽습니다 — 게더가 있으면 **두 번**에 끝낸다.
 * @details 반 텍셀 비낀 두 게더의 2x2 가 십자 다섯 칸을 정확히 덮는다(중심은 둘 다에 들어 있다).
 *          같은 텍셀을 읽으므로 점 샘플 다섯 번과 **값이 같다** — 근사가 아니다.
 * @note `outNeighborDepth` 의 **순서는 약속하지 않는다.** 게더 성분 순서는 백엔드마다 다를 수 있고,
 *       쓰는 쪽(외곽선)이 네 이웃의 **합**만 보므로 순서가 결과를 바꾸지 않는다. 방향이 필요한
 *       계산에는 쓰지 말 것.
 */
void swSampleDepthCross( float2 uv, float2 texel, out float outCenterDepth, out float4 outNeighborDepth )
{
	// **반 텍셀이 아니라 1/4 텍셀이다.** 정확히 텍셀 모서리에 찍으면 어느 2x2 를 잡을지가 반올림에
	// 달려 버려 백엔드·드라이버마다 갈릴 수 있다. 1/4 은 의도한 2x2 안쪽이라 어디서든 같은 넷이다.
	float4 gatherHi = swGatherDepthRed( uv + texel * 0.25f );
	float4 gatherLo = swGatherDepthRed( uv - texel * 0.25f );
	outCenterDepth   = gatherHi.w;
	outNeighborDepth = float4( gatherHi.z, gatherHi.x, gatherLo.x, gatherLo.z );
}
/** @brief SSAO 결과. 파이프라인에 SSAO 가 없으면(인덱스 무효) 가림 없음(1)이다 — 0 으로 폴백하면 화면이 검게 된다. */
float swSampleAmbientOcclusion( float2 uv )
{
	if ( g_AmbientOcclusionIndex == kInvalidIndex )
		return 1.0f;
	return swSampleIndex( g_AmbientOcclusionIndex, uv ).r;
}

#endif // SW_NATIVE_BINDLESS || !SW_STAGE_COMPUTE

// ------------------------------------------------------------------------------
// 4) 픽셀 출력 — **같은 머티리얼 셰이더가 포워드와 G버퍼 양쪽에 쓰인다.**
//    머티리얼이 셰이더 경로를 정하므로(usesMaterialShader) G버퍼 패스도 머티리얼의 .hlsl 로 그린다.
//    그 셰이더가 SV_TARGET 하나만 내면 **G버퍼의 노멀 타깃이 클리어 값 그대로 남아**, 디퍼드 조명이 모든 픽셀을
//    같은 노멀로 계산한다(오류도 경고도 없이).
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
SW_SURFACE_OUTPUT swStoreSurface( float4 litColor, float4 albedo, float3 worldNormal )
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

#endif // SW_ENGINE_BINDING_HLSLI
