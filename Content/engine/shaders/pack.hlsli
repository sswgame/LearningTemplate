/**
 * pack.hlsli — 바이트 · 비트 패킹: 32 비트 단어 하나와 실수 묶음 사이를 오가는 정적 함수 `PackUtil`.
 *
 * **짝**: C++ `PackUtil`(Source/Core/Math/PackUtil.h)과 같은 이름 · 같은 인자 순서 · 같은 비트 배치다. 한쪽을 고치면 다른 쪽도 고친다.
 * 단어 안 순서는 모두 **x(첫 성분)가 아래 비트**다(DXGI R8G8B8A8 · R16G16 · R10G10B10A2 · R9G9B9E5 와 같은 배치, 리틀 엔디언 메모리의 바이트 순서).
 * 언리얼 BitPacking.ush · PackUnitVector 와 C++ FPackedNormal 짝, DirectXMath XMConvertFloatToHalf 와 같은 자리다.
 *
 * 양쪽이 같게 지키는 규칙:
 *   - unorm(8 · 16 · RGB10A2): [0, 1] 로 자르고 가장 가까운 단계, 가운데는 위로(0 에서 먼 쪽). NaN 은 0, 음의 0 은 0.
 *   - snorm(8 · 16): [-1, 1] 로 자르고 가장 가까운 단계, 가운데는 0 에서 먼 쪽. NaN 은 0, 음의 0 은 0. 정수 최솟값(-128 · -32768)은 만들지 않고 풀면 -1.
 *   - half: IEEE 754 binary16, 가장 가까운 값이고 가운데는 짝수 쪽. 65520 이상은 무한대, 2^-25 이하는 0(부호 유지 — 음의 0 은 0x8000),
 *     그 사이는 비정규수. NaN 은 조용한 NaN(위쪽 10 비트 페이로드 유지). f32tof16 대신 비트 연산으로 하는 것은 내장 함수의 반올림 · 비정규수 처리가
 *     백엔드(DXIL · SPIR-V · GLSL · WGSL)마다 정해지지 않아 CPU 와 같은 비트를 보장할 수 없어서다.
 *   - RGB9E5: 푸는 쪽만 둔다(담기는 CPU 베이크 — C++ `PackUtil::packRGB9E5`).
 *   - 담기(실수 → 정수)는 입력이 같으면 CPU 와 비트까지 같다: 곱셈 · 덧셈을 precise 로 나눠 mad 융합을 막고, saturate · clamp 대신 비교로 잘라
 *     NaN 처리를 백엔드에 맡기지 않는다. 풀기(정수 → 실수)는 CPU 와 같은 식(정수 / 최댓값)이지만 GPU 나눗셈은 2.5 ulp 까지 허용되어 ulp 안에서 다를 수 있다.
 *   - extractBits · insertBits 의 count 는 1..32, offset + count <= 32 다(언리얼 BitFieldExtractU32 와 같은 자리, 시프트 32 를 피하는 꼴).
 */

#ifndef SW_ENGINE_PACK_HLSLI
#define SW_ENGINE_PACK_HLSLI

/** @brief 32 비트 단어 하나와 실수 묶음 사이의 패킹입니다(C++ `PackUtil` 짝). */
struct PackUtil
{
    static uint extractBits( uint value, uint offset, uint count );
    static uint insertBits( uint value, uint field, uint offset, uint count );
    static uint quantizeUnorm( float value, float maxValue ); // C++ NormalizedMath::quantizeUnorm
    static int quantizeSnorm( float value, float maxValue );  // C++ NormalizedMath::quantizeSnorm
    static uint packUnorm8x4( float4 value );
    static float4 unpackUnorm8x4( uint packed );
    static uint packSnorm8x4( float4 value );
    static float4 unpackSnorm8x4( uint packed );
    static uint packUnorm16x2( float2 value );
    static float2 unpackUnorm16x2( uint packed );
    static uint packSnorm16x2( float2 value );
    static float2 unpackSnorm16x2( uint packed );
    static uint packRGB10A2( float4 value );
    static float4 unpackRGB10A2( uint packed );
    static uint packHalf( float value );  // C++ half::fromFloat
    static float unpackHalf( uint bits ); // C++ half::toFloat
    static uint packHalf2x16( float2 value );
    static float2 unpackHalf2x16( uint packed );
    static float3 unpackRGB9E5( uint packed );
    static uint shiftRightRoundEven( uint value, uint shift ); // C++ half::shiftRightRoundEven
};

/** @brief @p value 의 @p offset 비트부터 @p count 비트를 꺼낸다. */
static uint PackUtil::extractBits( uint value, uint offset, uint count )
{
    return ( value >> offset ) & ( 0xFFFFFFFFu >> ( 32u - count ) );
}

/** @brief @p value 의 @p offset 비트부터 @p count 비트를 @p field 로 바꾼다(넘치는 위쪽 비트는 버린다). */
static uint PackUtil::insertBits( uint value, uint field, uint offset, uint count )
{
    const uint mask = ( 0xFFFFFFFFu >> ( 32u - count ) ) << offset;
    return ( value & ~mask ) | ( ( field << offset ) & mask );
}

/** @brief [0, 1] 을 0..@p maxValue 로 양자화한다. NaN 은 두 비교가 모두 거짓이라 0 이다. */
static uint PackUtil::quantizeUnorm( float value, float maxValue )
{
    const float   clamped = value >= 1.0f ? 1.0f : ( value > 0.0f ? value : 0.0f );
    precise float scaled  = clamped * maxValue;
    precise float rounded = scaled + 0.5f;
    return (uint)rounded;
}

/** @brief [-1, 1] 을 -@p maxValue..@p maxValue 로 양자화한다(크기를 unorm 으로 둥글리고 부호를 붙인다 — 가운데는 0 에서 먼 쪽). */
static int PackUtil::quantizeSnorm( float value, float maxValue )
{
    const int magnitude = (int)PackUtil::quantizeUnorm( abs( value ), maxValue );
    return value < 0.0f ? -magnitude : magnitude;
}

/** @brief 네 성분을 unorm8 넷으로 묶는다(x 가 아래 바이트 — R8G8B8A8_UNORM). */
static uint PackUtil::packUnorm8x4( float4 value )
{
    return PackUtil::quantizeUnorm( value.x, 255.0f ) | ( PackUtil::quantizeUnorm( value.y, 255.0f ) << 8u ) | ( PackUtil::quantizeUnorm( value.z, 255.0f ) << 16u ) |
           ( PackUtil::quantizeUnorm( value.w, 255.0f ) << 24u );
}

/** @brief `packUnorm8x4` 의 역. */
static float4 PackUtil::unpackUnorm8x4( uint packed )
{
    return float4( packed & 0xFFu, ( packed >> 8u ) & 0xFFu, ( packed >> 16u ) & 0xFFu, packed >> 24u ) / 255.0f;
}

/** @brief 네 성분을 snorm8 넷으로 묶는다(x 가 아래 바이트 — R8G8B8A8_SNORM). */
static uint PackUtil::packSnorm8x4( float4 value )
{
    return ( (uint)PackUtil::quantizeSnorm( value.x, 127.0f ) & 0xFFu ) | ( ( (uint)PackUtil::quantizeSnorm( value.y, 127.0f ) & 0xFFu ) << 8u ) |
           ( ( (uint)PackUtil::quantizeSnorm( value.z, 127.0f ) & 0xFFu ) << 16u ) | ( (uint)PackUtil::quantizeSnorm( value.w, 127.0f ) << 24u );
}

/** @brief `packSnorm8x4` 의 역 — 바이트마다 부호를 넓혀 127 로 나누고 -1 아래는 자른다. */
static float4 PackUtil::unpackSnorm8x4( uint packed )
{
    const int4 raw = int4( (int)( packed << 24u ) >> 24, (int)( packed << 16u ) >> 24, (int)( packed << 8u ) >> 24, (int)packed >> 24 );
    return max( float4( raw ) / 127.0f, -1.0f );
}

/** @brief 두 성분을 unorm16 둘로 묶는다(x 가 아래 16 비트 — R16G16_UNORM). */
static uint PackUtil::packUnorm16x2( float2 value )
{
    return PackUtil::quantizeUnorm( value.x, 65535.0f ) | ( PackUtil::quantizeUnorm( value.y, 65535.0f ) << 16u );
}

/** @brief `packUnorm16x2` 의 역. */
static float2 PackUtil::unpackUnorm16x2( uint packed )
{
    return float2( packed & 0xFFFFu, packed >> 16u ) / 65535.0f;
}

/** @brief 두 성분을 snorm16 둘로 묶는다(x 가 아래 16 비트 — R16G16_SNORM). */
static uint PackUtil::packSnorm16x2( float2 value )
{
    return ( (uint)PackUtil::quantizeSnorm( value.x, 32767.0f ) & 0xFFFFu ) | ( (uint)PackUtil::quantizeSnorm( value.y, 32767.0f ) << 16u );
}

/** @brief `packSnorm16x2` 의 역 — 반마다 부호를 넓혀 32767 로 나누고 -1 아래는 자른다. */
static float2 PackUtil::unpackSnorm16x2( uint packed )
{
    const int2 raw = int2( (int)( packed << 16u ) >> 16, (int)packed >> 16 );
    return max( float2( raw ) / 32767.0f, -1.0f );
}

/** @brief RGB 10 비트 셋 + A 2 비트(R 이 아래 비트 — R10G10B10A2_UNORM). */
static uint PackUtil::packRGB10A2( float4 value )
{
    return PackUtil::quantizeUnorm( value.x, 1023.0f ) | ( PackUtil::quantizeUnorm( value.y, 1023.0f ) << 10u ) | ( PackUtil::quantizeUnorm( value.z, 1023.0f ) << 20u ) |
           ( PackUtil::quantizeUnorm( value.w, 3.0f ) << 30u );
}

/** @brief `packRGB10A2` 의 역. */
static float4 PackUtil::unpackRGB10A2( uint packed )
{
    return float4( packed & 0x3FFu, ( packed >> 10u ) & 0x3FFu, ( packed >> 20u ) & 0x3FFu, packed >> 30u ) / float4( 1023.0f, 1023.0f, 1023.0f, 3.0f );
}

/** @brief @p value 를 @p shift 칸 오른쪽으로 밀고 버린 부분이 절반을 넘으면(가운데면 짝수 쪽으로) 올린다. 올림이 지수로 넘어가는 것은 의도한 동작이다. */
static uint PackUtil::shiftRightRoundEven( uint value, uint shift )
{
    const uint truncated = value >> shift;
    const uint remainder = value & ( ( 1u << shift ) - 1u );
    const uint halfway   = 1u << ( shift - 1u );
    const bool bRoundUp  = ( remainder > halfway ) || ( ( remainder == halfway ) && ( ( truncated & 1u ) != 0u ) );
    return bRoundUp ? truncated + 1u : truncated;
}

/** @brief float32 → half 비트(아래 16 비트). C++ `half::fromFloat` 와 같은 분기 · 같은 경계다. */
static uint PackUtil::packHalf( float value )
{
    const uint floatBits    = asuint( value );
    const uint absoluteBits = floatBits & 0x7FFFFFFFu;
    const uint signBits     = ( floatBits >> 16u ) & 0x8000u;
    uint       resultBits   = 0u;
    if ( absoluteBits > 0x7F800000u )
    {
        resultBits = 0x7C00u | 0x0200u | ( ( absoluteBits >> 13u ) & 0x03FFu ); // NaN — 조용한 NaN, 위쪽 10 비트 페이로드
    }
    else if ( absoluteBits >= 0x477FF000u )
    {
        resultBits = 0x7C00u; // 65520 이상 — 무한대
    }
    else if ( absoluteBits >= 0x38800000u )
    {
        resultBits = PackUtil::shiftRightRoundEven( absoluteBits - 0x38000000u, 13u ); // 정규수 — 지수 편향 127 → 15
    }
    else if ( absoluteBits > 0x33000000u )
    {
        resultBits = PackUtil::shiftRightRoundEven( ( absoluteBits & 0x007FFFFFu ) | 0x00800000u, 126u - ( absoluteBits >> 23u ) ); // 비정규수
    }
    return signBits | resultBits;
}

/** @brief half 비트(아래 16 비트) → float32. 모든 half 값이 정확히 나온다. C++ `half::toFloat` 와 같은 식이다. */
static float PackUtil::unpackHalf( uint bits )
{
    const uint signBits   = ( bits & 0x8000u ) << 16u;
    const uint exponent   = ( bits & 0x7C00u ) >> 10u;
    const uint mantissa   = bits & 0x03FFu;
    uint       resultBits = 0u;
    if ( exponent == 31u )
    {
        resultBits = 0x7F800000u | ( mantissa << 13u );
    }
    else if ( exponent != 0u )
    {
        resultBits = ( ( exponent + 112u ) << 23u ) | ( mantissa << 13u );
    }
    else
    {
        resultBits = asuint( (float)mantissa * 5.9604644775390625e-8f ); // 0 · 비정규수: 가수 × 2^-24 는 float32 정규수라 정확하다
    }
    return asfloat( signBits | resultBits );
}

/** @brief 두 성분을 half 둘로 묶는다(x 가 아래 16 비트 — R16G16_FLOAT). */
static uint PackUtil::packHalf2x16( float2 value )
{
    return PackUtil::packHalf( value.x ) | ( PackUtil::packHalf( value.y ) << 16u );
}

/** @brief `packHalf2x16` 의 역. */
static float2 PackUtil::unpackHalf2x16( uint packed )
{
    return float2( PackUtil::unpackHalf( packed & 0xFFFFu ), PackUtil::unpackHalf( packed >> 16u ) );
}

/** @brief 공유 지수 32 비트(R9G9B9E5_SHAREDEXP — 가수 9 비트 셋, 위 5 비트 지수 · 편향 15)를 RGB 로 푼다. C++ `PackUtil::unpackRGB9E5` 와 같은 식이다. */
static float3 PackUtil::unpackRGB9E5( uint packed )
{
    const float scale = exp2( (float)( (int)( packed >> 27u ) - 15 - 9 ) );
    return float3( (float)( packed & 511u ), (float)( ( packed >> 9u ) & 511u ), (float)( ( packed >> 18u ) & 511u ) ) * scale;
}

#endif // SW_ENGINE_PACK_HLSLI
