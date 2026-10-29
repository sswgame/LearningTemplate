#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/RGB10A2.h"
#include "Core/Math/XorShift32.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

/**
 * @brief [RGB10A2Test] 비트 배치 — R 은 낮은 10 비트, G 는 10..19, B 는 20..29, A 는 위쪽 2 비트(DXGI R10G10B10A2_UNORM)
 */
SW_TEST_CASE( RGB10A2Test, BitLayoutFollowsDxgi )
{
    SW_EXPECT_EQUAL( 0x000003FFu, sw::RGB10A2::fromFloat4( sw::float4{ 1.0f, 0.0f, 0.0f, 0.0f } ).getPacked() );
    SW_EXPECT_EQUAL( 0x000FFC00u, sw::RGB10A2::fromFloat4( sw::float4{ 0.0f, 1.0f, 0.0f, 0.0f } ).getPacked() );
    SW_EXPECT_EQUAL( 0x3FF00000u, sw::RGB10A2::fromFloat4( sw::float4{ 0.0f, 0.0f, 1.0f, 0.0f } ).getPacked() );
    SW_EXPECT_EQUAL( 0xC0000000u, sw::RGB10A2::fromFloat4( sw::float4{ 0.0f, 0.0f, 0.0f, 1.0f } ).getPacked() );
    SW_EXPECT_EQUAL( 0xFFFFFFFFu, sw::RGB10A2::fromFloat4( sw::float4{ 1.0f, 1.0f, 1.0f, 1.0f } ).getPacked() );
    SW_EXPECT_EQUAL( 0x00000000u, sw::RGB10A2::fromFloat4( sw::float4{ 0.0f, 0.0f, 0.0f, 0.0f } ).getPacked() );

    const sw::RGB10A2 raw = sw::RGB10A2::fromRaw( 1u, 2u, 3u, 2u );
    SW_EXPECT_EQUAL( 1u, raw.getRed() );
    SW_EXPECT_EQUAL( 2u, raw.getGreen() );
    SW_EXPECT_EQUAL( 3u, raw.getBlue() );
    SW_EXPECT_EQUAL( 2u, raw.getAlpha() );

    uint32 memoryWord = 0;
    sw::Memory::copy( &memoryWord, &raw, sizeof( raw ) );
    SW_EXPECT_EQUAL( raw.getPacked(), memoryWord );
}

/**
 * @brief [RGB10A2Test] 양자화 — 범위 밖 자르기 · NaN 은 0 · 알파는 네 단계 · 가운데는 위로 반올림
 */
SW_TEST_CASE( RGB10A2Test, QuantizationClampsAndRounds )
{
    const sw::RGB10A2 clamped = sw::RGB10A2::fromFloat4( sw::float4{ 5.0f, -5.0f, sw::BitUtil::bitsToFloat( 0x7FC00000u ), 9.0f } );
    SW_EXPECT_EQUAL( 1023u, clamped.getRed() );
    SW_EXPECT_EQUAL( 0u, clamped.getGreen() );
    SW_EXPECT_EQUAL( 0u, clamped.getBlue() );
    SW_EXPECT_EQUAL( 3u, clamped.getAlpha() );

    const sw::RGB10A2 middle = sw::RGB10A2::fromFloat4( sw::float4{ 0.5f, 0.25f, 0.75f, 0.5f } );
    SW_EXPECT_EQUAL( 512u, middle.getRed() );   // 511.5 → 512
    SW_EXPECT_EQUAL( 256u, middle.getGreen() ); // 255.75 → 256
    SW_EXPECT_EQUAL( 767u, middle.getBlue() );  // 767.25 → 767
    SW_EXPECT_EQUAL( 2u, middle.getAlpha() );   // 1.5 → 2

    const sw::float4 alphaSteps[4]{
        sw::float4{0.0f, 0.0f, 0.0f,  0.0f},
        sw::float4{0.0f, 0.0f, 0.0f, 0.33f},
        sw::float4{0.0f, 0.0f, 0.0f, 0.66f},
        sw::float4{0.0f, 0.0f, 0.0f,  1.0f}
    };
    for ( uint32 step = 0; step < 4u; ++step )
    {
        SW_EXPECT_EQUAL( step, sw::RGB10A2::fromFloat4( alphaSteps[step] ).getAlpha() );
    }
}

/**
 * @brief [RGB10A2Test] 모든 10 비트 정수는 풀었다 다시 담으면 같은 정수이고, 최대 정수는 정확히 1.0 이다
 */
SW_TEST_CASE( RGB10A2Test, EveryChannelValueRoundTrips )
{
    uint32 mismatchCount = 0;
    for ( uint32 raw = 0; raw <= 1023u; ++raw )
    {
        const sw::RGB10A2 source = sw::RGB10A2::fromRaw( raw, 1023u - raw, raw, raw & 3u );
        const sw::RGB10A2 back   = sw::RGB10A2::fromFloat4( source.toFloat4() );
        if ( back.getPacked() != source.getPacked() )
            ++mismatchCount;
    }
    SW_EXPECT_EQUAL( 0u, mismatchCount );
    const sw::float4 white = sw::RGB10A2::fromRaw( 1023u, 1023u, 1023u, 3u ).toFloat4();
    SW_EXPECT_EQUAL( 1.0f, white._x );
    SW_EXPECT_EQUAL( 1.0f, white._w );
}

/**
 * @brief [RGB10A2Test] 양자화 오차는 색 채널 반 단계(1/2046) 이하, 같은 입력은 같은 비트다(결정적 난수 표본)
 */
SW_TEST_CASE( RGB10A2Test, ErrorIsHalfAStepAndDeterministic )
{
    uint32  state    = 0x6A09E667u;
    float32 worst    = 0.0f;
    uint32  mismatch = 0;
    for ( uint32 sampleIndex = 0; sampleIndex < 20000u; ++sampleIndex )
    {
        float32 arrChannel[3]{};
        for ( float32& channel : arrChannel )
        {
            state   = sw::XorShift32::next( state );
            channel = sw::XorShift32::toUnit24( state );
        }
        const sw::float4  source{ arrChannel[0], arrChannel[1], arrChannel[2], 1.0f };
        const sw::RGB10A2 packed = sw::RGB10A2::fromFloat4( source );
        const sw::float4  back   = packed.toFloat4();
        worst                    = sw::MathUtil::max( worst, sw::MathUtil::max( sw::MathUtil::abs( back._x - source._x ),
                                                                                sw::MathUtil::max( sw::MathUtil::abs( back._y - source._y ), sw::MathUtil::abs( back._z - source._z ) ) ) );
        if ( sw::RGB10A2::fromFloat4( source ).getPacked() != packed.getPacked() )
            ++mismatch;
    }
    SW_EXPECT_TRUE( worst <= 0.5f / 1023.0f + 2.0e-6f );
    SW_EXPECT_EQUAL( 0u, mismatch );

    constexpr sw::RGB10A2 kCompileTime = sw::RGB10A2::fromFloat4( sw::float4{ 0.5f, 0.25f, 0.75f, 0.5f } );
    static_assert( kCompileTime.getPacked() == ( 512u | ( 256u << 10 ) | ( 767u << 20 ) | ( 2u << 30 ) ), "constexpr 양자화가 런타임과 같은 비트여야 한다" );
}
