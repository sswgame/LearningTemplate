/**
 * gpuassert.hlsli — 셰이더 assert / printf (언리얼 ShaderPrint · ShaderDebug, 유니티 셰이더 printf 선과 같은 자리).
 *
 * 컴퓨트 셰이더가 조건이 틀리면 (조건 id, 값 하나, 스레드 식별)을 UAV 버퍼에 원자적으로 적는다. CPU 가 몇 프레임 뒤 비동기 되읽기로 읽어 로그로 낸다
 * (C++: Source/Engine/Graphics/Debug/GPUAssert.h `GPUAssertMonitor`). 버퍼 배치와 id 표는 gpuassertids.hlsli.
 *
 * 쓰는 법(컴퓨트 셰이더에서만 — 슬롯 u4 를 쓴다):
 *   #include "gpuassert.hlsli"                       // common.hlsli 뒤
 *   SW_GPU_ASSERT_THREAD( dispatchThreadID.x );      // 진입점 처음에 한 번 — 기록에 실리는 스레드 식별
 *   SW_GPU_ASSERT( level < levelCount, SW_GPU_ASSERT_ID_CULL_LOD_LEVEL, level );
 *
 * **퍼뮤테이션이다.** 셰이더에 `SW_GPU_ASSERT_ENABLED` 가 정의될 때만 버퍼를 선언하고 검사를 낸다. 아니면 두 매크로가 비어 코드도 바인딩(u4)도 없다 — Shipping 은 그 define 을
 * 붙이지 않는다(`GPUAssertMonitor::isRequested`). 조건 식도 평가하지 않으므로 부작용 있는 식을 넣지 않는다.
 */

#ifndef SW_ENGINE_GPUASSERT_HLSLI
#define SW_ENGINE_GPUASSERT_HLSLI

#include "gpuassertids.hlsli"

#if defined( SW_GPU_ASSERT_ENABLED )

SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_GPUAssertRW, SW_SLOT_COMPUTE_ASSERT_UAV );

static uint g_GPUAssertThread = 0u; // 이 스레드의 식별(SW_GPU_ASSERT_THREAD 가 정한다) — 스레드마다 따로 갖는 정적 값이다

struct GPUAssert
{
    static uint toBits( uint value );
    static uint toBits( int value );
    static uint toBits( float value );
    static void report( uint conditionID, uint valueBits );
    static void check( bool bHolds, uint conditionID, uint valueBits );
    static bool isFinite( float value );
    static bool isFinite( float3 value );
    static bool isFinite( float4 value );
};

static bool GPUAssert::isFinite( float value )
{
    return isnan( value ) == false && isinf( value ) == false;
}
static bool GPUAssert::isFinite( float3 value )
{
    return GPUAssert::isFinite( value.x ) && GPUAssert::isFinite( value.y ) && GPUAssert::isFinite( value.z );
}
static bool GPUAssert::isFinite( float4 value )
{
    return GPUAssert::isFinite( value.xyz ) && GPUAssert::isFinite( value.w );
}

static uint GPUAssert::toBits( uint value )
{
    return value;
}
static uint GPUAssert::toBits( int value )
{
    return asuint( value );
}
static uint GPUAssert::toBits( float value )
{
    return asuint( value );
}

/**
 * @brief 기록 하나를 링에 적습니다. 순번은 누적 카운터의 원자 증가가 정한다 — 순번의 칸이 이미 쓰였으면 덮는다(한 바퀴 넘은 만큼은 CPU 가 버려진 수로 센다).
 * @details 순번 + 1 을 **맨 나중에** 쓴다. CPU 는 순번이 맞는 칸만 믿는다(쓰다 만 칸 · 덮인 칸을 거른다).
 */
static void GPUAssert::report( uint conditionID, uint valueBits )
{
    uint sequence = 0u;
    InterlockedAdd( g_GPUAssertRW[0], 1u, sequence );
    const uint base          = SW_GPU_ASSERT_HEADER_UINTS + ( sequence % SW_GPU_ASSERT_RING_CAPACITY ) * SW_GPU_ASSERT_RECORD_UINTS;
    g_GPUAssertRW[base]      = conditionID;
    g_GPUAssertRW[base + 1u] = g_GPUAssertThread;
    g_GPUAssertRW[base + 2u] = valueBits;
    g_GPUAssertRW[base + 3u] = sequence + 1u;
}

    #define SW_GPU_ASSERT_THREAD( thread ) g_GPUAssertThread = ( thread )
    #define SW_GPU_ASSERT( condition, conditionID, value )                \
        if ( ( condition ) == false )                                     \
        {                                                                 \
            GPUAssert::report( conditionID, GPUAssert::toBits( value ) ); \
        }

#else // !SW_GPU_ASSERT_ENABLED

    #define SW_GPU_ASSERT_THREAD( thread )
    #define SW_GPU_ASSERT( condition, conditionID, value )

#endif // SW_GPU_ASSERT_ENABLED

#endif // SW_ENGINE_GPUASSERT_HLSLI
