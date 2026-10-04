#include "common.hlsli"

/**
 * meshskin.hlsl — GPU 스키닝 (컴퓨트). 언리얼 GPU Skin Cache 의 자리다.
 *
 * 모프 풀(GpuMeshMorphPool)의 **스킨 구간**을 맡는다. 스킨 데이터는 원본(스킨 데이터 번호가 같은 메시들)마다 한 벌(레스트 · 가중치)이고,
 * 그리는 메시마다의 인스턴스 표(결과 시작 · 원본 시작 · 정점 수 · 팔레트 시작)가 둘을 잇는다. 스레드 하나 = 스킨 구간의 결과 정점 하나 —
 * 인스턴스를 이분 탐색으로 찾아 원본 레스트 정점을 본 넷의 팔레트 행렬로 섞고 결과 버퍼에 쓴다. 정점 셰이더는 모프와 같은 길
 * (swLoadMorphedVertex)로 결과를 읽으므로 그리기 쪽에는 새 코드가 없다.
 *
 * 바인딩 계약(bindingslots.hlsli): 컴퓨트 CB 는 b0, 읽기 t0..t3, 쓰기 u0.
 * C++: bindComputeConstantBuffer( cb, 0 ) / bindComputeShaderResource( rest, 0 ) / ( weights, 1 ) / ( palette, 2 ) / ( instances, 3 ) / bindComputeUav( morph, 0 ).
 */

// 결과 · 레스트 버퍼의 원소 배치는 meshmorph.hlsl 과 같다 — 정점 하나 = float4 둘([2i] 위치, [2i+1] 노멀).
#define SW_MORPH_FLOAT4_PER_VERTEX 2u
// 가중치 버퍼 — 원본 정점 하나 = float4 둘([2i] 가중치 넷, [2i+1] 원본 스켈레톤 본 번호 넷을 float 로). 팔레트 시작은 인스턴스 표가 더한다.
#define SW_SKIN_FLOAT4_PER_VERTEX 2u
// 팔레트 — 본 하나 = float4 셋(행벡터 규약 4x4 행렬의 0 · 1 · 2 열). 위치 = dot( float4( p, 1 ), 열 ).
#define SW_SKIN_FLOAT4_PER_BONE 3u
// 인스턴스 표 — 인스턴스 하나 = uint4 둘([2i] 결과 시작(스킨 구간 기준) · 원본 시작 · 정점 수 · 팔레트 시작, [2i+1] 예약). C++ GpuSkinInstanceRow.
#define SW_SKIN_UINT4_PER_INSTANCE 2u
// 이분 탐색 걸음 상한 — 인스턴스 2^16 개까지. 고정 횟수라 루프가 셰이더 컴파일러에 펼쳐진다.
#define SW_SKIN_SEARCH_STEPS 16u

SW_DECLARE_CBUFFER( SkinParams, SW_SLOT_COMPUTE_CB )
{
	uint g_SkinVertexBase;    // 풀 안에서 스킨 구간이 시작하는 정점
	uint g_SkinVertexCount;   // 스킨 구간의 정점 수(모든 인스턴스의 합)
	uint g_SkinBoneCount;     // 팔레트의 본 수 — 범위 밖 번호를 막는다
	uint g_SkinInstanceCount; // 인스턴스 표의 줄 수
};

SW_DECLARE_STRUCTURED_BUFFER( float4, g_RestVertices, 0 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SkinWeights, 1 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SkinPalette, 2 );
SW_DECLARE_STRUCTURED_BUFFER( uint4, g_SkinInstances, 3 );
SW_DECLARE_RW_STRUCTURED_BUFFER( float4, g_MorphVerticesRW, 0 );

/** @brief 본 하나의 행렬로 점을 옮긴다(행벡터 규약). */
float3 transformPoint( uint bone, float3 position )
{
	const float4 homogeneous = float4( position, 1.0f );
	const uint   row   = bone * SW_SKIN_FLOAT4_PER_BONE;
	return float3( dot( homogeneous, g_SkinPalette[row] ), dot( homogeneous, g_SkinPalette[row + 1u] ), dot( homogeneous, g_SkinPalette[row + 2u] ) );
}

/** @brief 본 하나의 행렬로 방향을 옮긴다(이동 없음). 균등 스케일 · 강체 팔레트를 가정한다 — 결과는 정규화한다. */
float3 transformDirection( uint bone, float3 direction )
{
	const uint row = bone * SW_SKIN_FLOAT4_PER_BONE;
	return float3( dot( direction, g_SkinPalette[row].xyz ), dot( direction, g_SkinPalette[row + 1u].xyz ), dot( direction, g_SkinPalette[row + 2u].xyz ) );
}

/** @brief 스킨 구간의 결과 번호가 속한 인스턴스 — 결과 시작이 그 번호 이하인 마지막 줄(줄은 결과 시작 순이다). */
uint findInstance( uint skinIndex )
{
	uint low   = 0u;
	uint count = g_SkinInstanceCount;
	[unroll]
	for ( uint step = 0u; step < SW_SKIN_SEARCH_STEPS; ++step )
	{
		const uint halfCount = count / 2u;
		const uint middle    = low + halfCount;
		const bool bRight    = ( halfCount > 0u ) && ( g_SkinInstances[middle * SW_SKIN_UINT4_PER_INSTANCE].x <= skinIndex );
		low                 = bRight ? middle : low;
		count               = ( halfCount > 0u ) ? ( bRight ? count - halfCount : halfCount ) : count;
	}
	return low;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const uint skinIndex = dispatchThreadId.x;
	if (skinIndex >= g_SkinVertexCount)
		return;

	const uint  instanceIndex = findInstance( skinIndex );
	const uint4 instance      = g_SkinInstances[instanceIndex * SW_SKIN_UINT4_PER_INSTANCE];
	const uint  local         = min( skinIndex - instance.x, instance.z - 1u );
	const uint  source        = instance.y + local;
	const uint  paletteBase   = instance.w;

	const float3 position = g_RestVertices[source * SW_MORPH_FLOAT4_PER_VERTEX].xyz;
	const float3 normal   = g_RestVertices[source * SW_MORPH_FLOAT4_PER_VERTEX + 1u].xyz;
	const float4 weights  = g_SkinWeights[source * SW_SKIN_FLOAT4_PER_VERTEX];
	const float4 joints   = g_SkinWeights[source * SW_SKIN_FLOAT4_PER_VERTEX + 1u];

	float3 skinnedPosition = float3( 0.0f, 0.0f, 0.0f );
	float3 skinnedNormal   = float3( 0.0f, 0.0f, 0.0f );
	[unroll]
	for ( uint influence = 0u; influence < 4u; ++influence )
	{
		const uint  bone   = min( paletteBase + (uint)joints[influence], g_SkinBoneCount - 1u );
		const float weight = weights[influence];
		skinnedPosition += transformPoint( bone, position ) * weight;
		skinnedNormal   += transformDirection( bone, normal ) * weight;
	}

	const uint  element      = ( g_SkinVertexBase + skinIndex ) * SW_MORPH_FLOAT4_PER_VERTEX;
	const float normalLength = length( skinnedNormal );
	g_MorphVerticesRW[element]      = float4( skinnedPosition, 1.0f );
	g_MorphVerticesRW[element + 1u] = float4( normalLength > 1e-6f ? skinnedNormal / normalLength : normal, 0.0f );
}
