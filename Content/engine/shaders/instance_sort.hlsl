#include "common.hlsli"
#include "batch_data.hlsli"

/**
 * instance_sort.hlsl — 배치 안의 **가시 인스턴스를 CPU 정렬 순서로 되돌린다** (컴퓨트).
 *
 * 컬링이 압축을 하면 자리 번호가 원자 연산의 **완료 순서**로 정해진다. 불투명은 상관없지만 투명은 그
 * 순서가 곧 블렌딩 순서라 그림이 틀린다. 투명을 압축하지 않고 CPU 정렬 순서를 쓰면 순서는 지켜지지만 **투명은 컬링
 * 이득을 통째로 포기**한다.
 *
 * 여기서는 압축을 그대로 두고 **순서를 GPU 에서 되돌린다**. 투명 순서는 CPU 가 이미 정했다(GPUSceneBuilder::sortTransparent —
 * 정렬 레이어 키 → 깊이 → 후보 번호의 전순서)이고, 배치 안의 인스턴스는 그 순서대로 연속으로 놓인다. 그래서 **인스턴스 번호가
 * 곧 그리는 순서**다. 컬링 뒤에 배치마다 워크그룹 하나가 그 배치의 가시 목록을 인스턴스 번호 오름차순으로 정렬한다.
 * 깊이를 여기서 다시 재면 (1) 정렬 레이어를 모르고 (2) 같은 깊이를 불안정하게 가르며 (3) 직교 카메라의 시선 축 깊이를 모른다 —
 * CPU 와 GPU 가 다른 순서를 낸다. 정렬 기준은 CPU 한 곳뿐이다. 추가 뷰(CCTV · PiP)는 그 뷰의 눈으로 CPU 가 다시 정한 순번(t2,
 * GPUSceneBuilder::computeViewTransparentOrders)으로 정렬한다 — 키는 (순번 << 9) | 배치 안 번호.
 *
 * 정렬은 **바이토닉 정렬**이다. 워크그룹 하나에 담기는 배치(SW_SORT_MAX_ELEMENTS 이하)는 그룹공유 메모리 안에서 한 디스패치로 끝낸다(sortMode 1).
 * 그보다 큰 배치(sortMode 2)는 같은 파일의 `SW_SORT_MULTI_PASS` 커널이 배치마다 여러 디스패치로 정렬한다 — 512 칸 블록을 그룹공유로 정렬한 뒤(블록 정렬) 블록을 두 배씩
 * 합친다(뒤집기 합치기 → 펼치기, 거리 256 이하는 블록 안 펼치기 하나). 비교가 모두 오름차순이라 개수 뒤의 빈 칸은 무한대로 보고 건너뛰기만 하면 된다(2 의 거듭제곱 채움이 없다).
 * 디스패치 차례는 C++ 이 배치 크기로 정한다(`GPUScene::collectSortStages`) — GPU 가 줄인 개수를 CPU 가 모르므로 배치 크기까지 잡고, 개수 밖의 쌍은 셰이더가 건너뛴다.
 *
 * 바인딩 계약(binding_slots.hlsli): CB b0, 인스턴스 t0, 배치 구간 t1, 뷰 순번 t2(추가 뷰 — 다른 뷰는 자리표), 차례 → 칸 t3, 간접 인자 u0, 가시 ID u1.
 * 정렬하는 배치는 컬링이 **차례**를 적어 두었다(인스턴스 버퍼가 칸 순이라 칸 번호는 그리는 순서가 아니다). 차례로 정렬한 뒤 t3 으로 칸을 찾아 되쓴다.
 * (컬링과 같은 자리라 바인딩을 갈아 끼우지 않고 PSO 만 바꿔 디스패치한다. 다단 커널은 배치 · 단계를 루트 상수로 더 받는다.)
 */

/// @brief 한 배치에서 GPU 정렬로 다룰 수 있는 최대 인스턴스 수. 바이토닉이라 2의 거듭제곱이어야 한다.
#define SW_SORT_MAX_ELEMENTS 512
#define SW_SORT_THREADS      256 // = SW_SORT_MAX_ELEMENTS / 2 (스레드마다 비교·교환 한 쌍)
// 바이토닉 정렬 본체는 GPU 입자 정렬(vfx_particle_sort.hlsl)과 함께 쓴다.
#include "bitonic_sort.hlsli"

// 인스턴스 원소(InstanceData)는 그래픽스와 같은 정의 하나를 쓴다(베낀 구조체는 계약 검사가 보지 않는다).
#include "instance_data.hlsli"

// 간접 인자 원소(SceneDrawArgs)도 같은 정의 하나다 — C++ RHIDrawIndexedIndirectCommand 와 바이트 배치가 같다.
#include "scene_draw_args.hlsli"

// 배치 표 원소는 binding.hlsli 와 같은 BatchData(batch_data.hlsli) 다.

SW_DECLARE_CBUFFER( SortParams, SW_SLOT_COMPUTE_CB )
{
    float4 g_CameraPos; // xyz = 월드 카메라 위치
    uint   g_InstanceCount;
    uint   g_BatchCount;
    uint   g_UseViewRank;         // 1 = 추가 뷰 — t2 의 뷰 순번으로 정렬한다, 0 = 인스턴스 번호
    uint   g_TransparentTailBase; // 순번 표 0 번의 인스턴스 번호
};

SW_DECLARE_STRUCTURED_BUFFER( InstanceData, g_Instances, 0 );
SW_DECLARE_STRUCTURED_BUFFER( BatchData, g_BatchInfo, 1 );
SW_DECLARE_STRUCTURED_BUFFER( uint, g_ViewRank, 2 );
SW_DECLARE_STRUCTURED_BUFFER( uint, g_InstanceOrder, 3 ); // 그리는 차례 → GPUScene 칸(gpu_cull.hlsl 과 같은 목록)
SW_DECLARE_RW_STRUCTURED_BUFFER( SceneDrawArgs, g_IndirectArgs, 0 );
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VisibleInstanceIDs, 1 );

#if defined( SW_SORT_MULTI_PASS )
SW_ROOT_CONSTANTS_BEGIN
{
    uint g_SortBatch; // 정렬할 배치
    uint g_SortStage; // 아래 kSortStage*
    uint g_SortSpan;  // 뒤집기 합치기의 블록 칸 수 · 펼치기의 거리
};
SW_ROOT_CONSTANTS_END

/// 단계 — C++ `GPUSortStageKind` 와 같은 값이다.
static const uint kSortStageBlock    = 0u; // 512 칸 블록을 그룹공유로 정렬한다(그룹 하나가 블록 하나)
static const uint kSortStageFlip     = 1u; // 블록 g_SortSpan 의 앞 절반과 뒤집은 뒤 절반을 견준다(스레드 하나가 쌍 하나)
static const uint kSortStageDistance = 2u; // 거리 g_SortSpan 의 쌍을 견준다(스레드 하나가 쌍 하나, 거리 512 이상)
static const uint kSortStageMerge    = 3u; // 거리 256 부터 1 까지의 펼치기를 블록 안에서 한 번에 한다
static const uint kSortStageResolve  = 4u; // 정렬한 차례를 칸으로 바꿔 쓴다(스레드 하나가 칸 하나)

/// 개수 밖의 칸 — 어떤 차례보다 뒤다.
static const uint kSortPastEnd = 0xFFFFFFFFu;

groupshared uint s_arrOrder[SW_SORT_MAX_ELEMENTS];
groupshared uint s_arrKey[SW_SORT_MAX_ELEMENTS];

/** @brief 차례의 정렬 키입니다 — 주 시점은 차례 그대로, 추가 뷰는 그 뷰의 순번(t2)이다. */
uint computeSortKey( uint orderIndex )
{
    if ( g_UseViewRank == 0u )
        return orderIndex;
    return orderIndex >= g_TransparentTailBase ? g_ViewRank[orderIndex - g_TransparentTailBase] : 0u;
}

/** @brief 첫째가 둘째보다 뒤면 true 입니다. 키가 같으면 차례로 가른다(전순서). */
bool isSortedAfter( uint keyA, uint orderA, uint keyB, uint orderB )
{
    return keyA > keyB || ( keyA == keyB && orderA > orderB );
}

/** @brief 그룹공유의 두 칸을 오름차순으로 맞춥니다(low < high). */
void exchangeLocal( uint low, uint high )
{
    const uint keyLow    = s_arrKey[low];
    const uint keyHigh   = s_arrKey[high];
    const uint orderLow  = s_arrOrder[low];
    const uint orderHigh = s_arrOrder[high];
    if ( isSortedAfter( keyLow, orderLow, keyHigh, orderHigh ) )
    {
        s_arrKey[low]    = keyHigh;
        s_arrKey[high]   = keyLow;
        s_arrOrder[low]  = orderHigh;
        s_arrOrder[high] = orderLow;
    }
}

/** @brief 쌍 번호 @p pairIndex 의 낮은 칸입니다(블록 2 × half 안의 앞 절반). */
uint computePairLow( uint pairIndex, uint halfSpan )
{
    return ( ( pairIndex & ~( halfSpan - 1u ) ) << 1u ) + ( pairIndex & ( halfSpan - 1u ) );
}

SW_NUMTHREADS( SW_SORT_THREADS, 1, 1 )
void CSMain( uint3 groupId : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID )
{
    const uint      stage      = SW_ROOT( g_SortStage );
    const uint      batchIndex = SW_ROOT( g_SortBatch );
    const uint      span       = SW_ROOT( g_SortSpan );
    const BatchData info       = g_BatchInfo[batchIndex];
    const uint      slots      = info._visibleBase;
    const uint      count      = g_IndirectArgs[batchIndex]._instanceCount;

    if ( stage == kSortStageFlip || stage == kSortStageDistance )
    {
        const uint halfSpan = stage == kSortStageFlip ? span >> 1u : span;
        const uint low      = computePairLow( dispatchThreadID.x, halfSpan );
        const uint high     = stage == kSortStageFlip ? low + ( span - 1u ) - 2u * ( dispatchThreadID.x & ( halfSpan - 1u ) ) : low + halfSpan;
        // 높은 칸이 개수 밖이면 무한대다 — 바꿀 것이 없다.
        if ( high < count )
        {
            const uint orderLow  = g_VisibleInstanceIDs[slots + low];
            const uint orderHigh = g_VisibleInstanceIDs[slots + high];
            if ( isSortedAfter( computeSortKey( orderLow ), orderLow, computeSortKey( orderHigh ), orderHigh ) )
            {
                g_VisibleInstanceIDs[slots + low]  = orderHigh;
                g_VisibleInstanceIDs[slots + high] = orderLow;
            }
        }
        return;
    }
    if ( stage == kSortStageResolve )
    {
        if ( dispatchThreadID.x < count )
        {
            const uint orderIndex                            = g_VisibleInstanceIDs[slots + dispatchThreadID.x];
            g_VisibleInstanceIDs[slots + dispatchThreadID.x] = ( orderIndex < g_InstanceCount ) ? g_InstanceOrder[orderIndex] : orderIndex;
        }
        return;
    }

    const uint blockBase   = groupId.x * (uint)SW_SORT_MAX_ELEMENTS;
    const uint threadIndex = groupThreadID.x;
    SW_UNROLL for ( uint loadIndex = 0u; loadIndex < 2u; ++loadIndex )
    {
        const uint local  = threadIndex + loadIndex * (uint)SW_SORT_THREADS;
        const bool bValid = blockBase + local < count;
        uint       order  = kSortPastEnd;
        uint       key    = kSortPastEnd;
        if ( bValid )
        {
            order = g_VisibleInstanceIDs[slots + blockBase + local];
            key   = computeSortKey( order );
        }
        s_arrOrder[local] = order;
        s_arrKey[local]   = key;
    }
    GroupMemoryBarrierWithGroupSync();

    if ( stage == kSortStageBlock )
    {
        // 블록 2 는 뒤집기 하나뿐이다(펼칠 거리가 없다) — 반복 앞에 둔다.
        exchangeLocal( threadIndex << 1u, ( threadIndex << 1u ) + 1u );
        GroupMemoryBarrierWithGroupSync();
        for ( uint blockSize = 4u; blockSize <= (uint)SW_SORT_MAX_ELEMENTS; blockSize <<= 1u )
        {
            const uint flipHalf = blockSize >> 1u;
            const uint flipLow  = computePairLow( threadIndex, flipHalf );
            exchangeLocal( flipLow, flipLow + ( blockSize - 1u ) - 2u * ( threadIndex & ( flipHalf - 1u ) ) );
            GroupMemoryBarrierWithGroupSync();
            for ( uint distance = blockSize >> 2u; distance > 0u; distance >>= 1u )
            {
                const uint low = computePairLow( threadIndex, distance );
                exchangeLocal( low, low + distance );
                GroupMemoryBarrierWithGroupSync();
            }
        }
    }
    else
    {
        for ( uint distance = (uint)SW_SORT_THREADS; distance > 0u; distance >>= 1u )
        {
            const uint low = computePairLow( threadIndex, distance );
            exchangeLocal( low, low + distance );
            GroupMemoryBarrierWithGroupSync();
        }
    }

    SW_UNROLL for ( uint storeIndex = 0u; storeIndex < 2u; ++storeIndex )
    {
        const uint local = threadIndex + storeIndex * (uint)SW_SORT_THREADS;
        if ( blockBase + local < count )
            g_VisibleInstanceIDs[slots + blockBase + local] = s_arrOrder[local];
    }
}
#else
// 정렬할 값은 인스턴스 번호 하나다 — 번호가 곧 CPU 가 정한 그리는 순서다(위 주석).
groupshared uint s_arrID[SW_SORT_MAX_ELEMENTS];

SW_NUMTHREADS( SW_SORT_THREADS, 1, 1 )
void CSMain( uint3 groupId : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID )
{
    const uint batchIndex = groupId.x;
    if ( batchIndex >= g_BatchCount )
        return;

    const BatchData info = g_BatchInfo[batchIndex];
    if ( info._sortMode != 1u ) // 이 배치는 여기서 정렬하지 않는다
        return;

    const uint count = min( g_IndirectArgs[batchIndex]._instanceCount, (uint)SW_SORT_MAX_ELEMENTS );
    const uint base  = info._instanceBase; // 인스턴스 구간의 시작 — 키의 배치 안 번호와 되쓰는 인스턴스 번호가 이 축이다
    const uint slots = info._visibleBase;  // 가시 구간의 시작 — 가시 목록을 읽고 쓰는 축이다

    // **실제 개수의 다음 2의 거듭제곱까지만 일한다.** 늘 512 칸을 채우고 비교하면 인스턴스가 열 개인 배치도 512 칸
    // 분량이고, 작은 씬에서 컬링·정렬 프리패스 비용의 대부분이 이 패딩이 된다.
    // (바이토닉은 2의 거듭제곱 길이를 요구하므로 count 까지가 아니라 그 위 거듭제곱까지다.)
    //
    // **루프 경계는 그대로 상수다.** `count` 는 UAV 에서 읽은 값이라 컴파일러(FXC)에게는 "스레드마다
    // 다를 수 있는 값" 이고, 그 값에 걸린 흐름 안의 배리어는 X4026 으로 거부된다 — 실제로는 그룹 안에서
    // 같은 값이지만 증명할 수 없다. 그래서 배리어는 상수 루프에 두고 **비교·교환만** sortLength 안으로 줄인다.
    // 배리어 45 번은 남지만 그것은 싸고, 비싼 것은 칸마다의 그룹공유 읽기·쓰기다.
    const uint sortLength = computeBitonicSortLength( count );

    // 그룹공유로 올린다. count 밖의 자리는 가장 큰 값으로 둬 뒤로 밀어 두고, 되쓸 때 count 까지만 쓴다.
    // 인스턴스 번호가 범위를 벗어나도(있어선 안 되지만) 값 그대로 정렬된다 — 그리는 쪽이 g_SwInstanceCount 로 막는다.
    // 추가 뷰는 키 = (그 뷰의 순번 << 9) | 배치 안 번호다(배치 안 번호는 SW_SORT_MAX_ELEMENTS 미만 — 9 비트). 되쓸 때 아래 9 비트로 번호를 되찾는다.
    // 정렬하는 배치(sortMode 1)는 투명 배치뿐이고 그 인스턴스는 모두 꼬리라 순번이 있다.
    for ( uint loadIndex = groupThreadID.x; loadIndex < (uint)SW_SORT_MAX_ELEMENTS; loadIndex += SW_SORT_THREADS )
    {
        uint key = 0xFFFFFFFFu;
        if ( loadIndex < count )
        {
            const uint instanceId = g_VisibleInstanceIDs[slots + loadIndex];
            if ( g_UseViewRank != 0u )
            {
                const uint viewRank = ( instanceId >= g_TransparentTailBase ) ? min( g_ViewRank[instanceId - g_TransparentTailBase], 0x7FFFFFu ) : 0x7FFFFFu;
                key                 = ( viewRank << 9u ) | ( ( instanceId - base ) & 0x1FFu );
            }
            else
            {
                key = instanceId;
            }
        }
        s_arrID[loadIndex] = key;
    }
    GroupMemoryBarrierWithGroupSync();

    // 바이토닉 정렬 — **오름차순**(번호가 작은 것 = CPU 순서에서 먼저 그릴 것이 앞). sortLength 를 넘는 단계는 할 일이 없다(bitonic_sort.hlsli).
    SW_BITONIC_SORT_ASCENDING( s_arrID, sortLength, groupThreadID.x )

    for ( uint storeIndex = groupThreadID.x; storeIndex < count; storeIndex += SW_SORT_THREADS )
    {
        const uint key        = s_arrID[storeIndex];
        const uint orderIndex = ( g_UseViewRank != 0u ) ? ( base + ( key & 0x1FFu ) ) : key;
        // 차례를 칸으로 바꿔 쓴다 — 정점 셰이더는 가시 목록에서 칸을 읽는다. 차례가 범위 밖이면(있어선 안 된다) 칸 0 을 그리지 않게 그대로 둔다(그리는 쪽이 g_SwInstanceCount 로 막는다).
        g_VisibleInstanceIDs[slots + storeIndex] = ( orderIndex < g_InstanceCount ) ? g_InstanceOrder[orderIndex] : orderIndex;
    }
}
#endif // SW_SORT_MULTI_PASS
