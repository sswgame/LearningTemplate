/**
 * gpu_message.hlsli — 셰이더 메시지 채널: 출력 · 로그 · assert (언리얼 ShaderPrint · ShaderDebug, 유니티 셰이더 printf 선과 같은 자리).
 *
 * 셰이더가 (메시지 id, 픽셀 · 스레드, 인자 넷)을 UAV 링에 원자적으로 적는다. CPU 가 몇 프레임 뒤 비동기 되읽기로 읽어 id 의 서식으로 풀어 로그로 낸다
 * (C++: Source/Engine/Graphics/Debug/GPUMessage.h `GPUMessageChannel`). GPU 에는 문자열이 없다 — 서식 · 심각도 · 인자 종류는 CPU 표(`SW_GPU_MESSAGE_FORMAT`)가 id 로 정한다.
 * 버퍼 배치와 id 는 gpu_message_ids.hlsli.
 *
 * 쓰는 법(컴퓨트 · 픽셀 셰이더 — 슬롯 u4):
 *   #include "gpu_message.hlsli"                           // common.hlsli(또는 binding.hlsli) 뒤
 *   SW_GPU_MESSAGE_THREAD( dispatchThreadID.x );          // 컴퓨트: 진입점 처음에 한 번 — 기록의 스레드 · `gpu.thread` 선택과 비교하는 값
 *   SW_GPU_MESSAGE_PIXEL( input._position );              // 픽셀: 진입점 처음에 한 번 — SV_Position 의 정수 좌표 · `gpu.inspect x y` 선택과 비교하는 값
 *   SW_GPU_PRINT( SW_GPU_MESSAGE_ID_X, a, b, c, 0 );      // 고른 픽셀 · 스레드 하나만 적는다(인자 넷 — 쓰지 않는 칸은 0)
 *   SW_GPU_LOG( SW_GPU_MESSAGE_ID_X, a, b, c, d );        // 누구든 적되 id 당 프레임 예산(`-gv_gpuMessageLogBudget`)까지만
 *   SW_GPU_ASSERT( level < levelCount, SW_GPU_ASSERT_ID_CULL_LOD_LEVEL, level );                 // 틀리면 적는다(값 하나)
 *   SW_GPU_ASSERT_ARGS( i < count, SW_GPU_ASSERT_ID_X, i, count, base, 0 );                      // 틀리면 문맥 인자 넷과 함께 적는다
 *
 * **퍼뮤테이션이다.** 셰이더에 `SW_GPU_MESSAGES_ENABLED` 가 정의될 때만 버퍼를 선언하고 기록을 낸다. 아니면 매크로가 모두 비어 코드도 바인딩(u4)도 없다 — Shipping 은 그 define 을
 * 붙이지 않는다(`GPUMessageChannel::isRequested`). 조건 · 인자 식도 평가하지 않으므로 부작용 있는 식을 넣지 않는다.
 */

#ifndef SW_ENGINE_GPUMESSAGE_HLSLI
#define SW_ENGINE_GPUMESSAGE_HLSLI

#include "gpu_message_ids.hlsli"

#if defined( SW_GPU_MESSAGES_ENABLED )

SW_DECLARE_RW_STRUCTURED_BUFFER( uint, g_GPUMessageRW, SW_SLOT_GPU_MESSAGE_UAV );

static uint g_GPUMessageX = SW_GPU_MESSAGE_NO_COORD; // 이 호출의 픽셀 x 또는 스레드(SW_GPU_MESSAGE_PIXEL · SW_GPU_MESSAGE_THREAD 가 정한다)
static uint g_GPUMessageY = SW_GPU_MESSAGE_NO_COORD; // 픽셀 y — 스레드 기록이면 NO_COORD

struct GPUMessage
{
    static uint toBits( uint value );
    static uint toBits( int value );
    static uint toBits( float value );
    static uint toBits( bool value );
    static void setThread( uint thread );
    static void setPixel( float4 svPosition );
    static bool isSelected();
    static bool takeLogBudget( uint messageID );
    static void write( uint messageID, uint4 argBits );
    static bool isFinite( float value );
    static bool isFinite( float3 value );
    static bool isFinite( float4 value );
};

static uint GPUMessage::toBits( uint value )
{
    return value;
}
static uint GPUMessage::toBits( int value )
{
    return asuint( value );
}
static uint GPUMessage::toBits( float value )
{
    return asuint( value );
}
static uint GPUMessage::toBits( bool value )
{
    return value ? 1u : 0u;
}

static void GPUMessage::setThread( uint thread )
{
    g_GPUMessageX = thread;
    g_GPUMessageY = SW_GPU_MESSAGE_NO_COORD;
}

static void GPUMessage::setPixel( float4 svPosition )
{
    g_GPUMessageX = (uint)svPosition.x;
    g_GPUMessageY = (uint)svPosition.y;
}

/** @brief 이 호출이 CPU 가 고른 픽셀(`gpu.inspect x y`) 또는 스레드인지입니다. 고른 것이 없으면(NO_COORD) 늘 거짓이다. */
static bool GPUMessage::isSelected()
{
    if ( g_GPUMessageX == SW_GPU_MESSAGE_NO_COORD )
        return false;
    if ( g_GPUMessageY == SW_GPU_MESSAGE_NO_COORD )
        return g_GPUMessageX == g_GPUMessageRW[SW_GPU_MESSAGE_HEADER_SELECT_THREAD];
    return g_GPUMessageX == g_GPUMessageRW[SW_GPU_MESSAGE_HEADER_SELECT_X] && g_GPUMessageY == g_GPUMessageRW[SW_GPU_MESSAGE_HEADER_SELECT_Y];
}

/** @brief id 의 이번 프레임 예산이 남았으면 한 칸 쓰고 참입니다. 예산 칸은 id % 칸 수라 다른 id 와 칸을 나눌 수 있다 — 나누면 둘이 예산을 함께 쓴다. */
static bool GPUMessage::takeLogBudget( uint messageID )
{
    uint previous = 0u;
    InterlockedAdd( g_GPUMessageRW[SW_GPU_MESSAGE_HEADER_UINTS + ( messageID % SW_GPU_MESSAGE_BUDGET_SLOTS )], 1u, previous );
    return previous < g_GPUMessageRW[SW_GPU_MESSAGE_HEADER_LOG_BUDGET];
}

/**
 * @brief 기록 하나를 링에 적습니다. 순번은 누적 카운터의 원자 증가가 정한다 — 순번의 칸이 이미 쓰였으면 덮는다(한 바퀴 넘은 만큼은 CPU 가 버려진 수로 센다).
 * @details 순번 + 1 을 **맨 나중에** 쓴다. CPU 는 순번이 맞는 칸만 믿는다(쓰다 만 칸 · 덮인 칸을 거른다).
 */
static void GPUMessage::write( uint messageID, uint4 argBits )
{
    uint sequence = 0u;
    InterlockedAdd( g_GPUMessageRW[SW_GPU_MESSAGE_HEADER_COUNTER], 1u, sequence );
    const uint base                                        = SW_GPU_MESSAGE_HEADER_UINTS + SW_GPU_MESSAGE_BUDGET_SLOTS + ( sequence % SW_GPU_MESSAGE_RING_CAPACITY ) * SW_GPU_MESSAGE_RECORD_UINTS;
    g_GPUMessageRW[base + SW_GPU_MESSAGE_RECORD_ID]        = messageID;
    g_GPUMessageRW[base + SW_GPU_MESSAGE_RECORD_X]         = g_GPUMessageX;
    g_GPUMessageRW[base + SW_GPU_MESSAGE_RECORD_Y]         = g_GPUMessageY;
    g_GPUMessageRW[base + SW_GPU_MESSAGE_RECORD_ARG0]      = argBits.x;
    g_GPUMessageRW[base + SW_GPU_MESSAGE_RECORD_ARG0 + 1u] = argBits.y;
    g_GPUMessageRW[base + SW_GPU_MESSAGE_RECORD_ARG0 + 2u] = argBits.z;
    g_GPUMessageRW[base + SW_GPU_MESSAGE_RECORD_ARG0 + 3u] = argBits.w;
    g_GPUMessageRW[base + SW_GPU_MESSAGE_RECORD_SEQUENCE]  = sequence + 1u;
}

static bool GPUMessage::isFinite( float value )
{
    return isnan( value ) == false && isinf( value ) == false;
}
static bool GPUMessage::isFinite( float3 value )
{
    return GPUMessage::isFinite( value.x ) && GPUMessage::isFinite( value.y ) && GPUMessage::isFinite( value.z );
}
static bool GPUMessage::isFinite( float4 value )
{
    return GPUMessage::isFinite( value.xyz ) && GPUMessage::isFinite( value.w );
}

    #define SW_GPU_MESSAGE_ARGS( a0, a1, a2, a3 ) uint4( GPUMessage::toBits( a0 ), GPUMessage::toBits( a1 ), GPUMessage::toBits( a2 ), GPUMessage::toBits( a3 ) )
    #define SW_GPU_MESSAGE_THREAD( thread )       GPUMessage::setThread( thread )
    #define SW_GPU_MESSAGE_PIXEL( svPosition )    GPUMessage::setPixel( svPosition )
    #define SW_GPU_PRINT( messageID, a0, a1, a2, a3 )                              \
        if ( GPUMessage::isSelected() )                                            \
        {                                                                          \
            GPUMessage::write( messageID, SW_GPU_MESSAGE_ARGS( a0, a1, a2, a3 ) ); \
        }
    #define SW_GPU_LOG( messageID, a0, a1, a2, a3 )                                \
        if ( GPUMessage::takeLogBudget( messageID ) )                              \
        {                                                                          \
            GPUMessage::write( messageID, SW_GPU_MESSAGE_ARGS( a0, a1, a2, a3 ) ); \
        }
    #define SW_GPU_ASSERT_ARGS( condition, conditionID, a0, a1, a2, a3 )             \
        if ( ( condition ) == false )                                                \
        {                                                                            \
            GPUMessage::write( conditionID, SW_GPU_MESSAGE_ARGS( a0, a1, a2, a3 ) ); \
        }
    #define SW_GPU_ASSERT( condition, conditionID, value ) SW_GPU_ASSERT_ARGS( condition, conditionID, value, 0u, 0u, 0u )

#else // !SW_GPU_MESSAGES_ENABLED

    #define SW_GPU_MESSAGE_THREAD( thread )
    #define SW_GPU_MESSAGE_PIXEL( svPosition )
    #define SW_GPU_PRINT( messageID, a0, a1, a2, a3 )
    #define SW_GPU_LOG( messageID, a0, a1, a2, a3 )
    #define SW_GPU_ASSERT_ARGS( condition, conditionID, a0, a1, a2, a3 )
    #define SW_GPU_ASSERT( condition, conditionID, value )

#endif // SW_GPU_MESSAGES_ENABLED

#endif // SW_ENGINE_GPUMESSAGE_HLSLI
