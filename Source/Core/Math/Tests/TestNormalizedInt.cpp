#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/NormalizedInt.h"
#include "Core/Math/XorShift32.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief 정규화 정수 테스트의 도우미입니다. */
        struct NormalizedIntTestInternal
        {
            static constexpr uint32 kSampleCount = 20000;
            static constexpr uint32 kSeed        = 0x51ED270Bu;
            /** @brief float32 곱셈 · 나눗셈 오차(1 ulp 몇 배)를 받아 주는 여유입니다. */
            static constexpr float32 kFloatSlack = 2.0e-6f;

            /** @brief [@p minValue, @p maxValue] 의 결정적 난수 실수입니다. */
            static float32 nextValue( uint32& inoutState, const float32 minValue, const float32 maxValue )
            {
                inoutState = XorShift32::next( inoutState );
                return minValue + ( maxValue - minValue ) * XorShift32::toUnit24( inoutState );
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [NormalizedIntTest] unorm8 — 0 · 1 · 가운데 · 범위 밖 자르기 · NaN · 무한대
 */
SW_TEST_CASE( NormalizedIntTest, Unorm8Boundaries )
{
    SW_EXPECT_EQUAL( 0, sw::unorm8::fromFloat( 0.0f ).getRaw() );
    SW_EXPECT_EQUAL( 255, sw::unorm8::fromFloat( 1.0f ).getRaw() );
    SW_EXPECT_EQUAL( 128, sw::unorm8::fromFloat( 0.5f ).getRaw() ); // 127.5 → 128 (가운데는 위로)
    SW_EXPECT_EQUAL( 64, sw::unorm8::fromFloat( 0.25f ).getRaw() ); // 63.75 → 64
    SW_EXPECT_EQUAL( 0, sw::unorm8::fromFloat( -3.0f ).getRaw() );
    SW_EXPECT_EQUAL( 255, sw::unorm8::fromFloat( 3.0f ).getRaw() );
    SW_EXPECT_EQUAL( 0, sw::unorm8::fromFloat( sw::BitUtil::bitsToFloat( 0x7FC00000u ) ).getRaw() ); // NaN → 0
    SW_EXPECT_EQUAL( 255, sw::unorm8::fromFloat( sw::BitUtil::bitsToFloat( 0x7F800000u ) ).getRaw() );
    SW_EXPECT_EQUAL( 0, sw::unorm8::fromFloat( sw::BitUtil::bitsToFloat( 0xFF800000u ) ).getRaw() );
    SW_EXPECT_EQUAL( 0.0f, sw::unorm8::fromRaw( 0 ).toFloat() );
    SW_EXPECT_EQUAL( 1.0f, sw::unorm8::fromRaw( 255 ).toFloat() );
    SW_EXPECT_EQUAL( 65535, sw::unorm16::fromFloat( 1.0f ).getRaw() );
    SW_EXPECT_EQUAL( 1.0f, sw::unorm16::fromRaw( 65535 ).toFloat() );
}

/**
 * @brief [NormalizedIntTest] snorm — 0 은 정확히 0, ±1 은 ±최대, 정수 최솟값은 만들지 않고 풀면 -1, 범위 밖 자르기 · NaN
 */
SW_TEST_CASE( NormalizedIntTest, SnormBoundaries )
{
    SW_EXPECT_EQUAL( 0, sw::snorm8::fromFloat( 0.0f ).getRaw() );
    SW_EXPECT_EQUAL( 127, sw::snorm8::fromFloat( 1.0f ).getRaw() );
    SW_EXPECT_EQUAL( -127, sw::snorm8::fromFloat( -1.0f ).getRaw() );
    SW_EXPECT_EQUAL( 64, sw::snorm8::fromFloat( 0.5f ).getRaw() );   // 63.5 → 64 (가운데는 0 에서 먼 쪽)
    SW_EXPECT_EQUAL( -64, sw::snorm8::fromFloat( -0.5f ).getRaw() ); // 대칭
    SW_EXPECT_EQUAL( 127, sw::snorm8::fromFloat( 9.0f ).getRaw() );
    SW_EXPECT_EQUAL( -127, sw::snorm8::fromFloat( -9.0f ).getRaw() );
    SW_EXPECT_EQUAL( 0, sw::snorm8::fromFloat( sw::BitUtil::bitsToFloat( 0x7FC00000u ) ).getRaw() );
    SW_EXPECT_EQUAL( 32767, sw::snorm16::fromFloat( 1.0f ).getRaw() );
    SW_EXPECT_EQUAL( -32767, sw::snorm16::fromFloat( -1.0f ).getRaw() );
    SW_EXPECT_EQUAL( 0, sw::snorm16::fromFloat( -0.0f ).getRaw() );

    SW_EXPECT_EQUAL( 1.0f, sw::snorm8::fromRaw( 127 ).toFloat() );
    SW_EXPECT_EQUAL( -1.0f, sw::snorm8::fromRaw( -127 ).toFloat() );
    SW_EXPECT_EQUAL( -1.0f, sw::snorm8::fromRaw( -128 ).toFloat() ); // 정수 최솟값도 -1 로 풀린다(D3D 규칙)
    SW_EXPECT_EQUAL( -1.0f, sw::snorm16::fromRaw( -32768 ).toFloat() );
    SW_EXPECT_EQUAL( 0.0f, sw::snorm16::fromRaw( 0 ).toFloat() );
}

/**
 * @brief [NormalizedIntTest] 가능한 모든 정수는 풀었다 다시 담으면 같은 정수다(unorm8 · unorm16 · snorm8 · snorm16), snorm 은 정수 최솟값을 만들지 않는다
 */
SW_TEST_CASE( NormalizedIntTest, EveryRawValueRoundTrips )
{
    uint32 mismatchCount = 0;
    for ( uint32 raw = 0; raw <= 255u; ++raw )
    {
        if ( sw::unorm8::fromFloat( sw::unorm8::fromRaw( static_cast<uint8>( raw ) ).toFloat() ).getRaw() != raw )
            ++mismatchCount;
    }
    for ( uint32 raw = 0; raw <= 65535u; ++raw )
    {
        if ( sw::unorm16::fromFloat( sw::unorm16::fromRaw( static_cast<uint16>( raw ) ).toFloat() ).getRaw() != raw )
            ++mismatchCount;
    }
    for ( int32 raw = -127; raw <= 127; ++raw )
    {
        if ( sw::snorm8::fromFloat( sw::snorm8::fromRaw( static_cast<int8>( raw ) ).toFloat() ).getRaw() != raw )
            ++mismatchCount;
    }
    for ( int32 raw = -32767; raw <= 32767; ++raw )
    {
        if ( sw::snorm16::fromFloat( sw::snorm16::fromRaw( static_cast<int16>( raw ) ).toFloat() ).getRaw() != raw )
            ++mismatchCount;
    }
    SW_EXPECT_EQUAL( 0u, mismatchCount );

    uint32 state = sw::NormalizedIntTestInternal::kSeed;
    for ( uint32 sampleIndex = 0; sampleIndex < sw::NormalizedIntTestInternal::kSampleCount; ++sampleIndex )
    {
        const float32 value = sw::NormalizedIntTestInternal::nextValue( state, -3.0f, 3.0f );
        SW_EXPECT_TRUE( sw::snorm8::fromFloat( value ).getRaw() != -128 );
        SW_EXPECT_TRUE( sw::snorm16::fromFloat( value ).getRaw() != -32768 );
    }
}

/**
 * @brief [NormalizedIntTest] 양자화 오차는 반 단계 이하이고(범위 안), snorm 은 부호에 대해 대칭이다
 */
SW_TEST_CASE( NormalizedIntTest, QuantizationErrorIsHalfAStep )
{
    using sw::NormalizedIntTestInternal;
    uint32  state        = NormalizedIntTestInternal::kSeed;
    float32 worstUnorm8  = 0.0f;
    float32 worstUnorm16 = 0.0f;
    float32 worstSnorm8  = 0.0f;
    float32 worstSnorm16 = 0.0f;
    uint32  asymmetric   = 0;
    for ( uint32 sampleIndex = 0; sampleIndex < NormalizedIntTestInternal::kSampleCount; ++sampleIndex )
    {
        const float32 unit    = NormalizedIntTestInternal::nextValue( state, 0.0f, 1.0f );
        const float32 signed1 = NormalizedIntTestInternal::nextValue( state, -1.0f, 1.0f );
        worstUnorm8           = sw::MathUtil::max( worstUnorm8, sw::MathUtil::abs( sw::unorm8::fromFloat( unit ).toFloat() - unit ) );
        worstUnorm16          = sw::MathUtil::max( worstUnorm16, sw::MathUtil::abs( sw::unorm16::fromFloat( unit ).toFloat() - unit ) );
        worstSnorm8           = sw::MathUtil::max( worstSnorm8, sw::MathUtil::abs( sw::snorm8::fromFloat( signed1 ).toFloat() - signed1 ) );
        worstSnorm16          = sw::MathUtil::max( worstSnorm16, sw::MathUtil::abs( sw::snorm16::fromFloat( signed1 ).toFloat() - signed1 ) );
        if ( sw::snorm16::fromFloat( -signed1 ).getRaw() != -sw::snorm16::fromFloat( signed1 ).getRaw() )
            ++asymmetric;
    }
    SW_EXPECT_TRUE( worstUnorm8 <= 0.5f / 255.0f + NormalizedIntTestInternal::kFloatSlack );
    SW_EXPECT_TRUE( worstUnorm16 <= 0.5f / 65535.0f + NormalizedIntTestInternal::kFloatSlack );
    SW_EXPECT_TRUE( worstSnorm8 <= 0.5f / 127.0f + NormalizedIntTestInternal::kFloatSlack );
    SW_EXPECT_TRUE( worstSnorm16 <= 0.5f / 32767.0f + NormalizedIntTestInternal::kFloatSlack );
    SW_EXPECT_EQUAL( 0u, asymmetric );
}

/**
 * @brief [NormalizedIntTest] 묶음 — 성분 순서가 메모리 순서이고(x, y, z, w 의 순서), float4 · float2 로 왕복한다
 */
SW_TEST_CASE( NormalizedIntTest, VectorsKeepComponentOrderInMemory )
{
    const sw::unorm8x4 color = sw::unorm8x4::fromFloat4( sw::float4{ 1.0f, 0.5f, 0.0f, 0.25f } );
    uint8              arrByte[4]{};
    sw::Memory::copy( arrByte, &color, sizeof( color ) );
    SW_EXPECT_EQUAL( 255, arrByte[0] );
    SW_EXPECT_EQUAL( 128, arrByte[1] );
    SW_EXPECT_EQUAL( 0, arrByte[2] );
    SW_EXPECT_EQUAL( 64, arrByte[3] );
    const sw::float4 backColor = color.toFloat4();
    SW_EXPECT_EQUAL( 1.0f, backColor._x );
    SW_EXPECT_EQUAL( 0.0f, backColor._z );
    SW_EXPECT_NEAR( 0.5f, backColor._y, 0.5f / 255.0f + 1.0e-6f );

    const sw::snorm16x2 octant = sw::snorm16x2::fromFloat2( sw::float2{ -1.0f, 0.5f } );
    int16               arrWord[2]{};
    sw::Memory::copy( arrWord, &octant, sizeof( octant ) );
    SW_EXPECT_EQUAL( -32767, arrWord[0] );
    SW_EXPECT_EQUAL( 16384, arrWord[1] ); // 16383.5 → 16384
    const sw::float2 backOctant = octant.toFloat2();
    SW_EXPECT_EQUAL( -1.0f, backOctant._x );

    const sw::snorm16x4 position = sw::snorm16x4::fromFloat4( sw::float4{ 1.0f, -1.0f, 0.0f, 2.0f } );
    float32             arrFloat[4]{};
    position.toFloats( arrFloat );
    SW_EXPECT_EQUAL( 1.0f, arrFloat[0] );
    SW_EXPECT_EQUAL( -1.0f, arrFloat[1] );
    SW_EXPECT_EQUAL( 0.0f, arrFloat[2] );
    SW_EXPECT_EQUAL( 1.0f, arrFloat[3] ); // 범위 밖은 잘린다

    const sw::unorm16x2 uv = sw::unorm16x2::fromFloat2( sw::float2{ 0.25f, 0.75f } );
    SW_EXPECT_NEAR( 0.25f, uv.toFloat2()._x, 0.5f / 65535.0f + 1.0e-6f );
    SW_EXPECT_NEAR( 0.75f, uv.toFloat2()._y, 0.5f / 65535.0f + 1.0e-6f );
}

/**
 * @brief [NormalizedIntTest] 결정성 — 같은 입력은 같은 정수이고, constexpr 결과가 런타임 결과와 같다
 */
SW_TEST_CASE( NormalizedIntTest, QuantizationIsDeterministicAndConstexpr )
{
    constexpr sw::snorm16 kCompileTime = sw::snorm16::fromFloat( 0.3f );
    static_assert( kCompileTime.getRaw() == 9830, "0.3 * 32767 = 9830.1 → 9830" );
    volatile float32 runtimeValue = 0.3f;
    SW_EXPECT_EQUAL( 9830, sw::snorm16::fromFloat( runtimeValue ).getRaw() );
    SW_EXPECT_EQUAL( -9830, sw::snorm16::fromFloat( -runtimeValue ).getRaw() );
    constexpr sw::unorm8 kUnorm = sw::unorm8::fromFloat( 0.3f );
    static_assert( kUnorm.getRaw() == 77, "0.3 * 255 = 76.5 → 77 (부동소수 76.5 는 정확히 가운데라 위로)" );
}
