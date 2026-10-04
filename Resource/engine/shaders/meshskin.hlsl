#include "common.hlsli"

/**
 * meshskin.hlsl — GPU 스키닝 (컴퓨트). 언리얼 GPU Skin Cache 의 자리다.
 *
 * 모프 풀(GpuMeshMorphPool)의 **스킨 구간**을 맡는다. 레스트 정점(바인드 포즈)을 읽어 본 넷의 팔레트 행렬로 섞은 위치 · 노멀을
 * 결과 버퍼의 같은 원소에 쓴다. 정점 셰이더는 모프와 같은 길(swLoadMorphedVertex)로 결과를 읽으므로 그리기 쪽에는 새 코드가 없다.
 *
 * 바인딩 계약(bindingslots.hlsli): 컴퓨트 CB 는 b0, 읽기 t0..t2, 쓰기 u0.
 * C++: bindComputeConstantBuffer( cb, 0 ) / bindComputeShaderResource( rest, 0 ) / ( skin, 1 ) / ( palette, 2 ) / bindComputeUav( morph, 0 ).
 */

// 결과 · 레스트 버퍼의 원소 배치는 meshmorph.hlsl 과 같다 — 정점 하나 = float4 둘([2i] 위치, [2i+1] 노멀).
#define SW_MORPH_FLOAT4_PER_VERTEX 2u
// 스킨 버퍼 — 스킨 정점 하나 = float4 둘([2i] 가중치 넷, [2i+1] 팔레트 행 번호 넷을 float 로). 행 번호는 풀이 미리 팔레트 시작을 더해 둔다.
#define SW_SKIN_FLOAT4_PER_VERTEX 2u
// 팔레트 — 본 하나 = float4 셋(행벡터 규약 4x4 행렬의 0 · 1 · 2 열). 위치 = dot( float4( p, 1 ), 열 ).
#define SW_SKIN_FLOAT4_PER_BONE 3u

SW_DECLARE_CBUFFER( SkinParams, SW_SLOT_COMPUTE_CB )
{
	uint g_SkinVertexBase;  // 풀 안에서 스킨 구간이 시작하는 정점
	uint g_SkinVertexCount; // 스킨 구간의 정점 수
	uint g_SkinBoneCount;   // 팔레트의 본 수 — 범위 밖 번호를 막는다
	uint g_SkinPad;
};

SW_DECLARE_STRUCTURED_BUFFER( float4, g_RestVertices, 0 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SkinWeights, 1 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SkinPalette, 2 );
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

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	const uint skinIndex = dispatchThreadId.x;
	if (skinIndex >= g_SkinVertexCount)
		return;

	const uint   element  = ( g_SkinVertexBase + skinIndex ) * SW_MORPH_FLOAT4_PER_VERTEX;
	const float3 position = g_RestVertices[element].xyz;
	const float3 normal   = g_RestVertices[element + 1u].xyz;
	const float4 weights  = g_SkinWeights[skinIndex * SW_SKIN_FLOAT4_PER_VERTEX];
	const float4 joints   = g_SkinWeights[skinIndex * SW_SKIN_FLOAT4_PER_VERTEX + 1u];

	float3 skinnedPosition = float3( 0.0f, 0.0f, 0.0f );
	float3 skinnedNormal   = float3( 0.0f, 0.0f, 0.0f );
	[unroll]
	for ( uint influence = 0u; influence < 4u; ++influence )
	{
		const uint  bone   = min( (uint)joints[influence], g_SkinBoneCount - 1u );
		const float weight = weights[influence];
		skinnedPosition += transformPoint( bone, position ) * weight;
		skinnedNormal   += transformDirection( bone, normal ) * weight;
	}

	const float normalLength = length( skinnedNormal );
	g_MorphVerticesRW[element]      = float4( skinnedPosition, 1.0f );
	g_MorphVerticesRW[element + 1u] = float4( normalLength > 1e-6f ? skinnedNormal / normalLength : normal, 0.0f );
}
