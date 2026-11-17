/**
 * @file bitonic_sort.hlsli
 * @brief 워크그룹 하나의 그룹공유 배열을 오름차순으로 정렬하는 바이토닉 정렬입니다. instance_sort.hlsl(투명 인스턴스) · vfx_particle_sort.hlsl(GPU 입자)이 함께 쓴다.
 * @details include 앞에 `SW_SORT_MAX_ELEMENTS`(2 의 거듭제곱 — 배열 크기)와 `SW_SORT_THREADS`(= SW_SORT_MAX_ELEMENTS / 2)를 정한다.
 *          HLSL 은 그룹공유 배열을 함수 인자로 넘길 수 없어 매크로로 둔다(FXC · DXC · SPIR-V 공통).
 *          **루프 경계는 상수다** — 정렬할 길이는 UAV 에서 읽은 값이라 FXC 는 그 값에 걸린 흐름 안의 배리어를 X4026 으로 거부한다.
 *          배리어는 상수 루프에 두고 비교 · 교환만 @p sortLength(실제 개수의 다음 2 의 거듭제곱) 안으로 줄인다.
 */
#ifndef SW_ENGINE_BITONIC_SORT_HLSLI
#define SW_ENGINE_BITONIC_SORT_HLSLI

#if !defined( SW_SORT_MAX_ELEMENTS ) || !defined( SW_SORT_THREADS )
    #error "define SW_SORT_MAX_ELEMENTS and SW_SORT_THREADS before including bitonic_sort.hlsli"
#endif

/**
 * @brief @p count 이상인 가장 작은 2 의 거듭제곱(최소 2)입니다 — 정렬이 일하는 길이다.
 */
uint computeBitonicSortLength( uint count )
{
    uint sortLength = 2u;
    while ( sortLength < count )
        sortLength <<= 1u;
    return sortLength;
}

/**
 * 그룹공유 배열 @p keyArray 의 앞 @p sortLength 칸을 오름차순으로 정렬한다(길이 밖 칸은 부르는 쪽이 가장 큰 값으로 채워 둔다).
 * @p threadIndex 는 SV_GroupThreadID.x 다. 앞에 배열을 채운 뒤의 배리어, 뒤의 마지막 배리어가 들어 있다.
 */
#define SW_BITONIC_SORT_ASCENDING( keyArray, sortLength, threadIndex )                                                                                        \
    for ( uint bitonicBlockSize = 2u; bitonicBlockSize <= (uint)SW_SORT_MAX_ELEMENTS; bitonicBlockSize <<= 1u )                                               \
    {                                                                                                                                                         \
        for ( uint bitonicDistance = bitonicBlockSize >> 1u; bitonicDistance > 0u; bitonicDistance >>= 1u )                                                   \
        {                                                                                                                                                     \
            for ( uint bitonicIndex = ( threadIndex ); bitonicIndex < ( sortLength ) && bitonicBlockSize <= ( sortLength ); bitonicIndex += SW_SORT_THREADS ) \
            {                                                                                                                                                 \
                const uint bitonicPartner = bitonicIndex ^ bitonicDistance;                                                                                   \
                if ( bitonicPartner > bitonicIndex )                                                                                                          \
                {                                                                                                                                             \
                    const bool bitonicAscending = ( ( bitonicIndex & bitonicBlockSize ) == 0u );                                                              \
                    const bool bitonicSwap      = bitonicAscending ? ( keyArray[bitonicIndex] > keyArray[bitonicPartner] )                                    \
                                                                   : ( keyArray[bitonicIndex] < keyArray[bitonicPartner] );                                   \
                    if ( bitonicSwap )                                                                                                                        \
                    {                                                                                                                                         \
                        const uint bitonicValue  = keyArray[bitonicIndex];                                                                                    \
                        keyArray[bitonicIndex]   = keyArray[bitonicPartner];                                                                                  \
                        keyArray[bitonicPartner] = bitonicValue;                                                                                              \
                    }                                                                                                                                         \
                }                                                                                                                                             \
            }                                                                                                                                                 \
            GroupMemoryBarrierWithGroupSync();                                                                                                                \
        }                                                                                                                                                     \
    }

#endif // SW_ENGINE_BITONIC_SORT_HLSLI
