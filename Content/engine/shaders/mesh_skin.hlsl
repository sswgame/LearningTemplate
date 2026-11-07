#include "common.hlsli"
#include "octahedral.hlsli"

/**
 * mesh_skin.hlsl — GPU 스키닝 (컴퓨트). 언리얼 GPU Skin Cache 의 자리다.
 *
 * 모프 풀(GPUMeshMorphPool)의 **스킨 구간**을 맡는다. 스킨 데이터는 원본(스킨 데이터 번호가 같은 메시들)마다 한 벌(레스트 구간 · 가중치 구간 · 모프 차이 구간 — 풀의 SpanAllocator 가 따로 내준다)이고,
 * 그리는 메시마다의 인스턴스 표(결과 시작 · 가중치 구간 시작 · 정점 수 · 팔레트 시작 · 모프 가중치 시작 · 타깃 수 · 레스트 구간 시작 · 차이 구간 시작)이 둘을 잇는다.
 * 가중치 안의 차이 시작 번호는 그 원본의 차이 구간 기준 상대 번호다. 스레드 하나 = 스킨 구간의
 * 결과 정점 하나 — 인스턴스를 이분 탐색으로 찾아 원본 레스트 정점에 모프 차이를 가중치만큼 더하고(블렌드 셰이프 — 언리얼 모프 타깃과 같은 순서:
 * 모프 다음 스키닝), 본 넷으로 섞어 결과 버퍼에 쓴다. 정점 셰이더는 모프와 같은 길(MorphPool::loadVertex)로 결과를 읽는다.
 * 섞는 방식은 인스턴스마다 프레임 값 머리가 고른다(SkinningMethod) — 선형(팔레트 행렬 가중 합)과 듀얼 쿼터니언(회전 · 이동을 DQ 로 섞고 스케일은 따로 선형,
 * Kavan 2007 DLB). 본 행의 뜻도 그 방식을 따른다(binding_slots.hlsli 10 절).
 *
 * 바인딩 계약(binding_slots.hlsli): 컴퓨트 CB 는 b0, 읽기 t0..t3, 쓰기 u0.
 * C++: bindComputeConstantBuffer( cb, 0 ) / bindComputeShaderResource( rest, 0 ) / ( weights, 1 ) / ( palette, 2 ) / ( instances, 3 ) / bindComputeUAV( morph, 0 ).
 */

// 결과 · 레스트 · 가중치 · 팔레트 · 인스턴스 표의 원소 배치(SW_MORPH_FLOAT4_PER_VERTEX · SW_SKIN_*)는 binding_slots.hlsli 10 절 — C++ 와 같은 정의다.
// 팔레트 시작은 인스턴스 표가 더한다.
// 이분 탐색 걸음 상한 — 인스턴스 2^16 개까지. 고정 횟수라 루프가 셰이더 컴파일러에 펼쳐진다.
#define SW_SKIN_SEARCH_STEPS 16u
// 정점 하나에 걸리는 모프 차이 상한 — 데이터가 깨져도 루프가 끝나게.
#define SW_SKIN_MAX_DELTAS_PER_VERTEX 64u

SW_DECLARE_CBUFFER( SkinParams, SW_SLOT_COMPUTE_CB )
{
    uint g_SkinVertexBase;    // 풀 안에서 스킨 구간이 시작하는 정점
    uint g_SkinVertexCount;   // 스킨 구간의 정점 수(모든 인스턴스의 합)
    uint g_SkinBoneCount;     // 팔레트의 본 수 — 범위 밖 번호를 막는다
    uint g_SkinInstanceCount; // 인스턴스 표의 줄 수
    uint g_SkinPad0;
    uint g_SkinPad1;
    uint g_SkinPad2;
    uint g_SkinPad3;
};

SW_DECLARE_STRUCTURED_BUFFER( float4, g_RestVertices, 0 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SkinWeights, 1 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_SkinPalette, 2 );
SW_DECLARE_STRUCTURED_BUFFER( uint4, g_SkinInstances, 3 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_MorphVerticesRW, 0 );

/** @brief 본 하나의 행렬로 점을 옮긴다(행벡터 규약). */
float3 transformPoint( uint bone, float3 position )
{
    const float4 homogeneous = float4( position, 1.0f );
    const uint   row         = bone * SW_SKIN_FLOAT4_PER_BONE;
    return float3( dot( homogeneous, g_SkinPalette[row] ), dot( homogeneous, g_SkinPalette[row + 1u] ), dot( homogeneous, g_SkinPalette[row + 2u] ) );
}

/** @brief 본 하나의 행렬로 방향을 옮긴다(이동 없음). 균등 스케일 · 강체 팔레트를 가정한다 — 결과는 정규화한다. */
float3 transformDirection( uint bone, float3 direction )
{
    const uint row = bone * SW_SKIN_FLOAT4_PER_BONE;
    return float3( dot( direction, g_SkinPalette[row].xyz ), dot( direction, g_SkinPalette[row + 1u].xyz ), dot( direction, g_SkinPalette[row + 2u].xyz ) );
}

/** @brief 팔레트 버퍼를 float 배열로 볼 때 @p index 번째 값 — 프레임 값(스키닝 방식 · 모프 가중치)이 본 행 뒤에 float4 로 담겨 있다. */
float loadFrameValue( uint index )
{
    const float4 packed    = g_SkinPalette[index / 4u];
    const uint   component = index % 4u;
    return component == 0u ? packed.x : ( component == 1u ? packed.y : ( component == 2u ? packed.z : packed.w ) );
}

/** @brief 단위 쿼터니언 @p rotation 으로 방향을 돌린다 — C++ DualQuaternion::transformDirection(행벡터 makeFromQuaternion 과 같다)과 같은 식이다. */
float3 rotateByQuaternion( float4 rotation, float3 direction )
{
    const float3 inner = cross( rotation.xyz, direction ) + direction * rotation.w;
    return direction + cross( rotation.xyz, inner ) * 2.0f;
}

/** @brief 섞은 듀얼 쿼터니언(실수부 @p real · 허수부 @p dual, 정규화 전)과 스케일로 점을 옮긴다 — 스케일 → 회전 → 이동(행렬 `S * R * T` 와 같은 순서). */
float3 transformPointDualQuaternion( float4 real, float4 dual, float3 scale, float3 position )
{
    const float  inverseLength = rsqrt( max( dot( real, real ), 1e-12f ) );
    const float4 unitReal      = real * inverseLength;
    const float4 unitDual      = dual * inverseLength;
    // 이동 = 2 · (허수부 ⊗ 켤레 실수부).xyz — DualQuaternion::getTranslation 과 같다.
    const float3 translation = 2.0f * ( unitReal.w * unitDual.xyz - unitDual.w * unitReal.xyz + cross( unitReal.xyz, unitDual.xyz ) );
    return rotateByQuaternion( unitReal, position * scale ) + translation;
}

/** @brief 스킨 구간의 결과 번호가 속한 인스턴스 — 결과 시작이 그 번호 이하인 마지막 줄(줄은 결과 시작 순이다). */
uint findInstance( uint skinIndex )
{
    uint low   = 0u;
    uint count = g_SkinInstanceCount;
    SW_UNROLL for ( uint step = 0u; step < SW_SKIN_SEARCH_STEPS; ++step )
    {
        const uint halfCount = count / 2u;
        const uint middle    = low + halfCount;
        const bool bRight    = ( halfCount > 0u ) && ( g_SkinInstances[middle * SW_SKIN_UINT4_PER_INSTANCE].x <= skinIndex );
        low                  = bRight ? middle : low;
        count                = ( halfCount > 0u ) ? ( bRight ? count - halfCount : halfCount ) : count;
    }
    return low;
}

SW_NUMTHREADS( 64, 1, 1 )
void CSMain( uint3 dispatchThreadID : SV_DispatchThreadID )
{
    const uint skinIndex = dispatchThreadID.x;
    if ( skinIndex >= g_SkinVertexCount )
        return;

    const uint  instanceIndex = findInstance( skinIndex );
    const uint4 instance      = g_SkinInstances[instanceIndex * SW_SKIN_UINT4_PER_INSTANCE];
    const uint4 morph         = g_SkinInstances[instanceIndex * SW_SKIN_UINT4_PER_INSTANCE + 1u];
    const uint  local         = min( skinIndex - instance.x, instance.z - 1u );
    const uint  source        = instance.y + local; // 가중치 구간 안 정점
    const uint  restSource    = morph.z + local;    // 레스트 구간 안 정점
    const uint  paletteBase   = instance.w;

    float3       position   = g_RestVertices[restSource * SW_MORPH_FLOAT4_PER_VERTEX].xyz;
    float3       normal     = g_RestVertices[restSource * SW_MORPH_FLOAT4_PER_VERTEX + 1u].xyz;
    const float4 tangent    = g_RestVertices[restSource * SW_MORPH_FLOAT4_PER_VERTEX + 2u];
    const float4 weights    = g_SkinWeights[source * SW_SKIN_FLOAT4_PER_VERTEX];
    const float4 joints     = g_SkinWeights[source * SW_SKIN_FLOAT4_PER_VERTEX + 1u];
    const float4 deltaRange = g_SkinWeights[source * SW_SKIN_FLOAT4_PER_VERTEX + 2u];

    // 모프 타깃 — 이 정점을 옮기는 차이마다 그 타깃의 가중치만큼 더한다(가중치가 0 이면 레스트 그대로).
    const uint deltaStart = (uint)deltaRange.x;
    const uint deltaCount = ( morph.y > 0u ) ? min( (uint)deltaRange.y, SW_SKIN_MAX_DELTAS_PER_VERTEX ) : 0u;
    for ( uint deltaIndex = 0u; deltaIndex < deltaCount; ++deltaIndex )
    {
        const uint   element     = ( morph.w + deltaStart + deltaIndex ) * SW_MORPH_FLOAT4_PER_VERTEX; // 차이 구간 시작 + 이 원본 기준 상대 번호
        const float4 delta       = g_RestVertices[element];
        const float3 deltaNormal = g_RestVertices[element + 1u].xyz;
        const uint   target      = (uint)delta.w;
        const float  weight      = ( target < morph.y ) ? loadFrameValue( morph.x + SW_SKIN_FRAME_HEADER_FLOATS + target ) : 0.0f;
        position += delta.xyz * weight;
        normal += deltaNormal * weight;
    }

    float3 skinnedPosition = float3( 0.0f, 0.0f, 0.0f );
    float3 skinnedNormal   = float3( 0.0f, 0.0f, 0.0f );
    float3 skinnedTangent  = float3( 0.0f, 0.0f, 0.0f );
    // 방식은 인스턴스마다 하나라 같은 스레드 묶음(이웃 정점)은 대개 같은 갈래를 탄다.
    const bool bDualQuaternion = loadFrameValue( morph.x ) > 0.5f * ( SW_SKIN_METHOD_LINEAR + SW_SKIN_METHOD_DUAL_QUATERNION );
    if ( bDualQuaternion )
    {
        // 본 행 = 실수부 · 허수부 · 스케일. 실수부의 부호를 첫 본에 맞춰(대척점) 최단 경로로 섞는다 — DualQuaternion::dlb 와 같은 규칙.
        const uint   firstBone = min( paletteBase + (uint)joints[0], g_SkinBoneCount - 1u );
        const float4 pivot     = g_SkinPalette[firstBone * SW_SKIN_FLOAT4_PER_BONE];
        float4       real      = float4( 0.0f, 0.0f, 0.0f, 0.0f );
        float4       dual      = float4( 0.0f, 0.0f, 0.0f, 0.0f );
        float3       scale     = float3( 0.0f, 0.0f, 0.0f );
        SW_UNROLL for ( uint influence = 0u; influence < 4u; ++influence )
        {
            const uint   bone     = min( paletteBase + (uint)joints[influence], g_SkinBoneCount - 1u );
            const uint   row      = bone * SW_SKIN_FLOAT4_PER_BONE;
            const float4 boneReal = g_SkinPalette[row];
            const float  weight   = weights[influence] * ( dot( boneReal, pivot ) < 0.0f ? -1.0f : 1.0f );
            real += boneReal * weight;
            dual += g_SkinPalette[row + 1u] * weight;
            scale += g_SkinPalette[row + 2u].xyz * weights[influence];
        }
        const float4 unitReal = real * rsqrt( max( dot( real, real ), 1e-12f ) );
        skinnedPosition       = transformPointDualQuaternion( real, dual, scale, position );
        // 노멀 · 탄젠트는 회전만 — 선형 경로와 같이 균등 스케일을 가정하고 아래에서 정규화한다.
        skinnedNormal  = rotateByQuaternion( unitReal, normal );
        skinnedTangent = rotateByQuaternion( unitReal, tangent.xyz );
    }
    else
    {
        SW_UNROLL for ( uint influence = 0u; influence < 4u; ++influence )
        {
            const uint  bone   = min( paletteBase + (uint)joints[influence], g_SkinBoneCount - 1u );
            const float weight = weights[influence];
            skinnedPosition += transformPoint( bone, position ) * weight;
            skinnedNormal += transformDirection( bone, normal ) * weight;
            skinnedTangent += transformDirection( bone, tangent.xyz ) * weight;
        }
    }

    const uint   word         = ( g_SkinVertexBase + skinIndex ) * SW_MORPH_RESULT_WORDS_PER_VERTEX; // 결과 배치는 binding_slots.hlsli 10 절
    const float  normalLength = length( skinnedNormal );
    const float3 outNormal    = normalLength > 1e-6f ? skinnedNormal / normalLength : normal;
    // 탄젠트는 노멀과 같은 행렬로 돌린다. 모프 차이가 노멀만 바꾸므로 변형된 노멀에 다시 직교화한다(픽셀도 한 번 더 한다 — toon.hlsl applyNormalMap). 부호는 레스트 그대로라 싣지 않는다.
    const float3 orthogonal      = skinnedTangent - outNormal * dot( outNormal, skinnedTangent );
    const float  tangentLength   = length( orthogonal );
    g_MorphVerticesRW[word]      = asuint( skinnedPosition.x );
    g_MorphVerticesRW[word + 1u] = asuint( skinnedPosition.y );
    g_MorphVerticesRW[word + 2u] = asuint( skinnedPosition.z );
    g_MorphVerticesRW[word + 3u] = Octahedral::pack( outNormal );
    g_MorphVerticesRW[word + 4u] = Octahedral::pack( tangentLength > 1e-6f ? orthogonal / tangentLength : tangent.xyz );
}
