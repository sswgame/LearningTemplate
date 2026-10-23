#include "common.hlsli"
#include "lightdata.hlsli"
#include "lightgridcommon.hlsli"

/**
 * lightgrid.hlsl — 라이트 그리드의 GPU 생산자 (컴퓨트). 클러스터드 라이팅의 빛 목록을 뷰마다 만든다(언리얼 LightGridInjection, 유니티 Forward+ 의 라이트 컬링 컴퓨트).
 *
 * CPU 생산자(C++ `LightGrid::populate`)와 **같은 형식 · 같은 범위 식**이다. 셰이더(lighting.hlsli 의 커서)는 어느 쪽이 만들었는지 모른다.
 * 클러스터 안의 라이트 순서는 원자 연산이 정하므로 CPU 와 다를 수 있다(집합은 같다).
 *
 * 그룹 하나 = 뷰 하나(SV_GroupID.x). 라이트 상한(SW_MAX_FRAME_LIGHT)을 그룹의 스레드가 나눠 돌고, 세 단계를 그룹 동기화로 잇는다.
 *   1) 세기  — 라이트마다 범위를 구해 클러스터마다의 개수를 그룹 공유 메모리에 원자적으로 더한다. 방향광은 전역 구간의 수에 더한다.
 *   2) 접두사 합 — 스레드마다 클러스터 구간의 합을 내고 스레드 0 이 그 합들을 이어 붙인다. 클러스터 단어(시작, 개수)와 헤더를 쓰고 개수를 채우기 커서로 바꾼다.
 *   3) 채우기 — 같은 범위를 다시 구해 커서를 원자적으로 올린 자리에 라이트 번호를 쓴다.
 * 디스패치를 셋으로 나누지 않은 까닭: 라이트 상한이 그룹 하나가 돌 수 있는 수이고, 개수 배열이 그룹 공유 메모리(32KB)에 든다(ShaderBindingSlots.h 가 확인한다).
 * 디스패치 사이에 전역 버퍼로 개수를 넘기면 "다음 프레임 전에 0 으로 되돌린다" 는 약속이 하나 더 생긴다.
 *
 * 목록은 뷰마다 SW_LIGHT_GRID_GPU_MAX_ENTRY_COUNT 항목까지다. 넘친 항목은 쓰지 않고 그 클러스터의 개수를 자른다(읽는 쪽은 버퍼 밖을 읽지 않는다).
 * 헤더의 SW_LIGHT_GRID_WORD_ENTRY_COUNT 가 담아야 했던 수다.
 *
 * 바인딩 계약(bindingslots.hlsli): 읽기 t0 라이트(g_SwLights 와 같은 버퍼) · t1 뷰 정보, 쓰기 u0 그리드. 상수 버퍼는 없다(뷰마다의 값은 t1 에 있다).
 * C++: bindComputeShaderResource( lights, 0 ) / ( views, 1 ) / bindComputeUav( grid, 0 ) / dispatchCompute( 뷰 수, 1, 1 ) — FrameRendererCompute.cpp dispatchLightGrid.
 */

// 씬 라이트 한 개는 lighting.hlsli 와 같은 SwLightData(lightdata.hlsli) 다 — 원소 간격이 같아야 번호가 맞는다.

/** @brief 뷰 하나의 생산 입력입니다(g_LightGridViews 의 float4 여섯 개, 배치는 bindingslots.hlsli 10 절). */
struct GridView
{
    float4x4 viewProj;
    float    nearDepth;
    float    sliceScale;
    bool     bPerspective;
    uint     lightCount;
    uint     gridBase;
};

SW_DECLARE_STRUCTURED_BUFFER( SwLightData, g_LightGridLights, 0 );
SW_DECLARE_STRUCTURED_BUFFER( float4, g_LightGridViews, 1 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_LightGridRW, 0 );

// 그룹 공유 배열의 크기는 FXC(DX11) 가 상수식으로 읽어야 해서 매크로로 둔다.
#define SW_LIGHT_GRID_CLUSTER_COUNT ( SW_LIGHT_GRID_DIM_X * SW_LIGHT_GRID_DIM_Y * SW_LIGHT_GRID_DIM_Z )
static const uint kClusterCount = SW_LIGHT_GRID_CLUSTER_COUNT;
static const uint kIndexBase    = SW_LIGHT_GRID_HEADER_WORD_COUNT + kClusterCount * SW_LIGHT_GRID_WORDS_PER_CLUSTER;
static const uint kChunkSize    = ( kClusterCount + SW_LIGHT_GRID_GPU_THREAD_COUNT - 1u ) / SW_LIGHT_GRID_GPU_THREAD_COUNT;

groupshared uint s_arrClusterCount[SW_LIGHT_GRID_CLUSTER_COUNT]; // 1) 개수, 2) 뒤로는 채우기 커서
groupshared uint s_arrThreadSum[SW_LIGHT_GRID_GPU_THREAD_COUNT];
groupshared uint s_globalCount;
groupshared uint s_globalCursor;

/** @brief 뷰 @p viewIndex 의 생산 입력을 읽습니다. */
GridView loadView( uint viewIndex )
{
    const uint   element = viewIndex * SW_LIGHT_GRID_VIEW_FLOAT4_COUNT;
    const float4 depth   = g_LightGridViews[element + 4u];
    const uint4  counts  = asuint( g_LightGridViews[element + 5u] );
    GridView     view;
    view.viewProj     = float4x4( g_LightGridViews[element], g_LightGridViews[element + 1u], g_LightGridViews[element + 2u], g_LightGridViews[element + 3u] );
    view.nearDepth    = depth.x;
    view.sliceScale   = depth.y;
    view.bPerspective = depth.z > 0.5f;
    view.lightCount   = counts.x;
    view.gridBase     = counts.y;
    return view;
}

/** @brief 깊이 조각 위치를 조각 번호로 자릅니다(음수는 0, 끝은 마지막 조각). */
uint clampSlice( float slicePosition )
{
    return min( (uint)max( slicePosition, 0.0f ), SW_LIGHT_GRID_DIM_Z - 1u );
}

/**
 * @brief 점광 · 스폿의 경계 구가 덮는 클러스터 범위(양 끝 포함)입니다. 화면 밖이거나 깊이 범위 밖이면 false 입니다.
 * @details C++ `LightGrid::computeLightRange` 와 같은 식이다 — 깊이 범위는 중심 깊이 ± 반경 × 깊이 기울기(깊이는 월드 위치의 일차 함수라 정확하다),
 *          XY 범위는 월드 AABB 모서리 여덟 개의 NDC 범위다. 원근에서 모서리 하나라도 카메라 면을 넘으면 화면 전체다.
 * @note early-return 이 없다 — GL 드라이버가 early-return 모양을 잘못 컴파일한 적이 있다(binding.hlsli swComputeMorphElement).
 */
bool computeLightRange( GridView view, SwLightData light, out uint3 outMinimum, out uint3 outMaximum )
{
    const float3 center        = light.positionRadius.xyz;
    const float  radius        = light.positionRadius.w;
    const float4 centerClip    = mul( float4( center, 1.0f ), view.viewProj );
    const float3 depthGradient = view.bPerspective ? float3( view.viewProj[0][3], view.viewProj[1][3], view.viewProj[2][3] )
                                                   : float3( view.viewProj[0][2], view.viewProj[1][2], view.viewProj[2][2] );
    const float  centerDepth   = view.bPerspective ? centerClip.w : centerClip.z;
    const float  depthReach    = radius * length( depthGradient );
    const float  minDepth      = centerDepth - depthReach;
    const float  maxDepth      = centerDepth + depthReach;
    const bool   bBehind       = view.bPerspective ? ( maxDepth < view.nearDepth ) : ( maxDepth < 0.0f );
    const bool   bBeyond       = ( view.bPerspective == false ) && ( minDepth > 1.0f );

    // 처음 값은 빈 범위(±최대)다 — 화면 끝(±1)에서 시작하면 모서리가 모두 화면 밖 한쪽에 있어도 범위가 화면 끝에 걸쳐 남는다.
    const float maxFloat    = asfloat( 0x7f7fffffu );
    bool        bFullScreen = false;
    float2      minNdc      = float2( maxFloat, maxFloat );
    float2      maxNdc      = float2( -maxFloat, -maxFloat );
    [unroll] for ( uint corner = 0u; corner < 8u; ++corner )
    {
        const float3 offset        = float3( ( corner & 1u ) != 0u ? radius : -radius, ( corner & 2u ) != 0u ? radius : -radius, ( corner & 4u ) != 0u ? radius : -radius );
        const float4 clip          = mul( float4( center + offset, 1.0f ), view.viewProj );
        const bool   bBehindCamera = view.bPerspective && ( clip.w < SW_LIGHT_GRID_MIN_CORNER_CLIP_W );
        const float2 ndc           = clip.xy / ( bBehindCamera ? 1.0f : clip.w );
        bFullScreen                = bFullScreen || bBehindCamera;
        minNdc                     = bBehindCamera ? minNdc : min( minNdc, ndc );
        maxNdc                     = bBehindCamera ? maxNdc : max( maxNdc, ndc );
    }
    minNdc                = bFullScreen ? float2( -1.0f, -1.0f ) : minNdc;
    maxNdc                = bFullScreen ? float2( 1.0f, 1.0f ) : maxNdc;
    const bool bOffScreen = maxNdc.x < -1.0f || 1.0f < minNdc.x || maxNdc.y < -1.0f || 1.0f < minNdc.y;

    const float minSlice = swComputeLightGridSlicePosition( minDepth, view.bPerspective, view.nearDepth, view.sliceScale ) - SW_LIGHT_GRID_SLICE_PAD;
    const float maxSlice = swComputeLightGridSlicePosition( maxDepth, view.bPerspective, view.nearDepth, view.sliceScale ) + SW_LIGHT_GRID_SLICE_PAD;
    outMinimum           = uint3( swComputeLightGridTile( minNdc.x - SW_LIGHT_GRID_NDC_PAD, SW_LIGHT_GRID_DIM_X ), swComputeLightGridTile( minNdc.y - SW_LIGHT_GRID_NDC_PAD, SW_LIGHT_GRID_DIM_Y ),
                                  clampSlice( minSlice ) );
    outMaximum           = uint3( swComputeLightGridTile( maxNdc.x + SW_LIGHT_GRID_NDC_PAD, SW_LIGHT_GRID_DIM_X ), swComputeLightGridTile( maxNdc.y + SW_LIGHT_GRID_NDC_PAD, SW_LIGHT_GRID_DIM_Y ),
                                  clampSlice( maxSlice ) );
    return radius > 0.0f && bBehind == false && bBeyond == false && bOffScreen == false;
}

/** @brief 라이트 하나의 생산 방식입니다 — 0 은 그리드에 넣지 않음(2D 라이트 · 2D 그림자 세그먼트 · 화면 밖), 1 은 전역 구간(방향광), 2 는 클러스터 범위(SW_LIGHT_GRID_LOCAL_TYPE_MASK 의 타입 — 점광 · 스폿 · 반사 프로브 · 라이트 프로브 볼륨)입니다. */
uint classifyLight( GridView view, SwLightData light, out uint3 outMinimum, out uint3 outMaximum )
{
    const uint lightType = (uint)( light.directionType.w + 0.5f );
    const bool bLocal    = lightType < 32u && ( ( SW_LIGHT_GRID_LOCAL_TYPE_MASK >> lightType ) & 1u ) != 0u;
    const bool bInRange  = computeLightRange( view, light, outMinimum, outMaximum );
    return ( lightType == SW_LIGHT_TYPE_DIRECTIONAL ) ? 1u : ( ( bLocal && bInRange ) ? 2u : 0u );
}

/** @brief 클러스터 번호입니다. 셰이더 swComputeLightGridCluster · C++ 와 같은 순서((조각 × Y + y) × X + x)다. */
uint computeClusterIndex( uint3 cell )
{
    return ( cell.z * SW_LIGHT_GRID_DIM_Y + cell.y ) * SW_LIGHT_GRID_DIM_X + cell.x;
}

SW_NUMTHREADS( SW_LIGHT_GRID_GPU_THREAD_COUNT, 1, 1 )
void CSMain( uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID )
{
    const uint     thread = groupThreadId.x;
    const GridView view   = loadView( groupId.x );

    for ( uint clearCluster = thread; clearCluster < kClusterCount; clearCluster += SW_LIGHT_GRID_GPU_THREAD_COUNT )
    {
        s_arrClusterCount[clearCluster] = 0u;
    }
    if ( thread == 0u )
    {
        s_globalCount  = 0u;
        s_globalCursor = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    // 1) 세기
    for ( uint countLight = thread; countLight < view.lightCount; countLight += SW_LIGHT_GRID_GPU_THREAD_COUNT )
    {
        uint3      minimum;
        uint3      maximum;
        const uint kind = classifyLight( view, g_LightGridLights[countLight], minimum, maximum );
        if ( kind == 1u )
            InterlockedAdd( s_globalCount, 1u );
        if ( kind == 2u )
        {
            for ( uint countZ = minimum.z; countZ <= maximum.z; ++countZ )
            {
                for ( uint countY = minimum.y; countY <= maximum.y; ++countY )
                {
                    for ( uint countX = minimum.x; countX <= maximum.x; ++countX )
                    {
                        InterlockedAdd( s_arrClusterCount[computeClusterIndex( uint3( countX, countY, countZ ) )], 1u );
                    }
                }
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // 2) 접두사 합 — 스레드마다 연속한 클러스터 구간 하나를 맡는다.
    const uint chunkFirst = min( thread * kChunkSize, kClusterCount );
    const uint chunkLast  = min( chunkFirst + kChunkSize, kClusterCount );
    uint       chunkSum   = 0u;
    for ( uint sumCluster = chunkFirst; sumCluster < chunkLast; ++sumCluster )
    {
        chunkSum += s_arrClusterCount[sumCluster];
    }
    s_arrThreadSum[thread] = chunkSum;
    GroupMemoryBarrierWithGroupSync();
    if ( thread == 0u )
    {
        // 전역 구간(방향광)이 목록 맨 앞이다. 스레드마다의 합을 그 뒤로 이어 붙여 각 구간의 시작으로 바꾼다.
        uint running = s_globalCount;
        for ( uint sumThread = 0u; sumThread < SW_LIGHT_GRID_GPU_THREAD_COUNT; ++sumThread )
        {
            const uint threadSum      = s_arrThreadSum[sumThread];
            s_arrThreadSum[sumThread] = running;
            running += threadSum;
        }
        const uint base                                       = view.gridBase;
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_DIM_X]        = SW_LIGHT_GRID_DIM_X;
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_DIM_Y]        = SW_LIGHT_GRID_DIM_Y;
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_DIM_Z]        = SW_LIGHT_GRID_DIM_Z;
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_PERSPECTIVE]  = view.bPerspective ? 1u : 0u;
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_NEAR_DEPTH]   = asuint( view.nearDepth );
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_SLICE_SCALE]  = asuint( view.sliceScale );
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_GLOBAL_COUNT] = s_globalCount;
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_CLUSTER_BASE] = SW_LIGHT_GRID_HEADER_WORD_COUNT;
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_INDEX_BASE]   = kIndexBase;
        g_LightGridRW[base + SW_LIGHT_GRID_WORD_ENTRY_COUNT]  = running;
    }
    GroupMemoryBarrierWithGroupSync();
    uint offset = s_arrThreadSum[thread];
    for ( uint startCluster = chunkFirst; startCluster < chunkLast; ++startCluster )
    {
        // 넘친 클러스터는 개수를 상한까지 자른다 — 그 뒤 클러스터의 시작은 모두 상한 밖이라 개수가 0 이다.
        const uint count                = s_arrClusterCount[startCluster];
        const uint kept                 = min( count, SW_LIGHT_GRID_GPU_MAX_ENTRY_COUNT - min( offset, SW_LIGHT_GRID_GPU_MAX_ENTRY_COUNT ) );
        const uint wordIndex            = view.gridBase + SW_LIGHT_GRID_HEADER_WORD_COUNT + startCluster * SW_LIGHT_GRID_WORDS_PER_CLUSTER;
        g_LightGridRW[wordIndex]        = offset;
        g_LightGridRW[wordIndex + 1u]   = kept;
        s_arrClusterCount[startCluster] = offset;
        offset += count;
    }
    GroupMemoryBarrierWithGroupSync();

    // 3) 채우기 — 상한 밖 자리는 쓰지 않는다(위에서 잘린 개수 밖이다).
    const uint indexWord = view.gridBase + kIndexBase;
    for ( uint fillLight = thread; fillLight < view.lightCount; fillLight += SW_LIGHT_GRID_GPU_THREAD_COUNT )
    {
        uint3      minimum;
        uint3      maximum;
        const uint kind = classifyLight( view, g_LightGridLights[fillLight], minimum, maximum );
        if ( kind == 1u )
        {
            uint globalSlot = 0u;
            InterlockedAdd( s_globalCursor, 1u, globalSlot );
            if ( globalSlot < SW_LIGHT_GRID_GPU_MAX_ENTRY_COUNT )
                g_LightGridRW[indexWord + globalSlot] = fillLight;
        }
        if ( kind == 2u )
        {
            for ( uint fillZ = minimum.z; fillZ <= maximum.z; ++fillZ )
            {
                for ( uint fillY = minimum.y; fillY <= maximum.y; ++fillY )
                {
                    for ( uint fillX = minimum.x; fillX <= maximum.x; ++fillX )
                    {
                        uint slot = 0u;
                        InterlockedAdd( s_arrClusterCount[computeClusterIndex( uint3( fillX, fillY, fillZ ) )], 1u, slot );
                        if ( slot < SW_LIGHT_GRID_GPU_MAX_ENTRY_COUNT )
                            g_LightGridRW[indexWord + slot] = fillLight;
                    }
                }
            }
        }
    }
}
