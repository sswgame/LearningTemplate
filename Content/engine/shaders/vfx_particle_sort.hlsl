#include "common.hlsli"
#include "vfx_simulate.hlsli"

/**
 * vfx_particle_sort.hlsl — GPU 방출기 안 입자의 그리는 순서를 만든다(컴퓨트, VFXSortMode). 언리얼 나이아가라 GPU 정렬(`FGPUSortManager`)의 작은 판이다.
 *
 * 워크그룹 하나 = 정렬 일 하나(ViewDepth 는 방출기 × 뷰, 나이 순서는 방출기). 살아 있는 입자(렌더 입력의 앞 alive 칸)마다 키를 만들어 그룹공유 바이토닉 정렬로
 * 오름차순 정렬하고, 입자 번호를 순서 버퍼의 그 일 구간에 쓴다. 입자 그리기(particle.hlsl)가 그 구간으로 입자를 고른다 — 입자 데이터는 옮기지 않는다.
 * 키 = (정렬 값을 순서 보존 uint 로 바꾼 위 23 비트 << 9) | 입자 번호(9 비트) — 같은 값은 번호 순이다(CPU VFXParticleOrder 의 안정 정렬과 같은 규칙, 값은 23 비트로 양자화).
 * 깊이 규칙은 CPU 와 같다(GPUTransparentSorter::makeTransparentSortKey — 축이 있으면 축 위 거리, 없으면 눈까지의 거리 제곱).
 * 한도는 워크그룹 하나에 담기는 SW_SORT_MAX_ELEMENTS(512)다 — 넘는 풀은 로드 검사가 막는다(C++ VFXEmitterDef::kMaxGPUSortedParticles 와 계약 검사로 묶었다).
 *
 * 바인딩 계약: 읽기 t0 매개변수(g_VFXParams), t1 렌더 입력(g_VFXParticles), t2 간접 인자(살아 있는 수, DX11 은 RAW), 쓰기 u0 순서(g_VFXOrderRW).
 * 루트 상수 g_VFXSortParamBase. 매개변수 행: 0 (순서 첫 칸, 모드, -, -) · 1..3 방출기 행렬의 열 0..2 · 4 (눈 xyz, 축 사용 1/0) · 5 (축 xyz, -).
 * C++: VFXGPUSimulation::recordDispatches — 정렬 일마다 dispatchCompute( 1, 1, 1 ).
 */

#define SW_SORT_MAX_ELEMENTS 512
#define SW_SORT_THREADS      256 // = SW_SORT_MAX_ELEMENTS / 2
#include "bitonic_sort.hlsli"

SW_DECLARE_STRUCTURED_BUFFER( uint4, g_VFXParams, 0 );
SW_DECLARE_STRUCTURED_BUFFER( GPUParticle, g_VFXParticles, 1 );
#if defined( DX11 )
SW_DECLARE_BYTE_ADDRESS_BUFFER( g_VFXArgs, 2 );
#else
SW_DECLARE_STRUCTURED_BUFFER( uint, g_VFXArgs, 2 );
#endif
SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_VFXOrderRW, 0 );

SW_ROOT_CONSTANTS_BEGIN
{
    uint g_VFXSortParamBase; // 이 정렬 일 매개변수의 첫 행
};
SW_ROOT_CONSTANTS_END

// 모드 — C++ VFXSortMode 의 값과 같다(None 은 일이 없다).
static const uint kVFXSortViewDepth     = 1u;
static const uint kVFXSortOldestFirst   = 2u;
static const uint kVFXSortYoungestFirst = 3u;
static const uint kVFXSortIndexBits     = 9u; // 입자 번호 칸 — SW_SORT_MAX_ELEMENTS(512) 미만
static const uint kVFXSortIndexMask     = ( 1u << kVFXSortIndexBits ) - 1u;

groupshared uint s_arrKey[SW_SORT_MAX_ELEMENTS];

uint loadArgsWord( uint word )
{
#if defined( DX11 )
    return g_VFXArgs.Load( word * 4u );
#else
    return g_VFXArgs[word];
#endif
}

uint4 loadSortRow( uint row )
{
    return g_VFXParams[SW_ROOT( g_VFXSortParamBase ) + row];
}

/** @brief 부동소수를 크기 순서가 같은 uint 로 바꿉니다(음수 포함 — 부호 비트를 뒤집거나 전체를 뒤집는다). */
uint toOrderedBits( float value )
{
    const uint bits = asuint( value );
    return ( bits & 0x80000000u ) != 0u ? ~bits : ( bits | 0x80000000u );
}

/** @brief 입자 하나의 정렬 값(오름차순으로 그릴 순서)입니다. 먼 것부터 · 오래된 것부터는 값을 뒤집는다. */
uint computeSortValue( GPUParticle particle, uint mode )
{
    if ( mode == kVFXSortViewDepth )
    {
        const float4 local  = float4( particle._positionSize.xyz, 1.0f );
        const float3 world  = float3( dot( local, asfloat( loadSortRow( 1u ) ) ), dot( local, asfloat( loadSortRow( 2u ) ) ), dot( local, asfloat( loadSortRow( 3u ) ) ) );
        const uint4  eyeRow = loadSortRow( 4u );
        const float3 eye    = asfloat( eyeRow.xyz );
        const float3 axis   = asfloat( loadSortRow( 5u ).xyz );
        const float3 offset = world - eye;
        const float  depth  = eyeRow.w != 0u ? dot( offset, axis ) : dot( offset, offset );
        return ~toOrderedBits( depth ); // 먼 것이 먼저
    }
    const uint life = toOrderedBits( particle._rotationLife.y );
    return mode == kVFXSortOldestFirst ? ~life : life;
}

SW_NUMTHREADS( SW_SORT_THREADS, 1, 1 )
void CSMain( uint3 groupThreadID : SV_GroupThreadID )
{
    const uint4 header     = loadSortRow( 0u );
    const uint  orderFirst = header.x;
    const uint  mode       = header.y;
    const uint  count      = min( loadArgsWord( SW_VFX_ARGS_WORD_ALIVE ), (uint)SW_SORT_MAX_ELEMENTS );
    const uint  sortLength = computeBitonicSortLength( count );

    // 길이 밖 칸은 가장 큰 값으로 뒤로 민다(되쓸 때 count 까지만 쓴다).
    for ( uint loadIndex = groupThreadID.x; loadIndex < (uint)SW_SORT_MAX_ELEMENTS; loadIndex += SW_SORT_THREADS )
    {
        uint key = 0xFFFFFFFFu;
        if ( loadIndex < count )
            key = ( ( computeSortValue( g_VFXParticles[loadIndex], mode ) >> kVFXSortIndexBits ) << kVFXSortIndexBits ) | loadIndex;
        s_arrKey[loadIndex] = key;
    }
    GroupMemoryBarrierWithGroupSync();

    SW_BITONIC_SORT_ASCENDING( s_arrKey, sortLength, groupThreadID.x )

    for ( uint storeIndex = groupThreadID.x; storeIndex < count; storeIndex += SW_SORT_THREADS )
    {
        g_VFXOrderRW[orderFirst + storeIndex] = s_arrKey[storeIndex] & kVFXSortIndexMask;
    }
}
