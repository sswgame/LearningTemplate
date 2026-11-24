#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/PackUtil.h"
#include "Core/Testing/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief 패킹 테스트의 도우미입니다. */
        struct PackUtilTestInternal
        {
            static constexpr uint32 kQuietNaNBits = 0x7FC00000u;

            /** @brief 셰이더 짝(`Content/engine/shaders/pack.hlsli`)의 본문입니다. 프로젝트 루트가 없으면 빈 글입니다. */
            static string readShaderPair()
            {
                const AbsolutePath projectRoot = ProcessRoots::get()._project;
                string             text;
                if ( projectRoot.empty() == false )
                    std::ignore = FileUtil::readTextFile( projectRoot.join( "Content/engine/shaders" ).join( "pack.hlsli" ), text );
                return text;
            }
        };
    } // namespace
} // namespace sw

using namespace sw;
using Internal = PackUtilTestInternal;

/**
 * @brief [PackUtilTest] 비트 배치 — 모든 묶음에서 x(첫 성분)가 아래 비트다(DXGI R8G8B8A8 · R16G16 · R10G10B10A2 와 같은 배치)
 */
SW_TEST_CASE( PackUtilTest, BitLayoutPutsFirstComponentLow )
{
    SW_EXPECT_EQUAL( 0x0000FF80u, PackUtil::packUnorm8x4( float4{ 0.5f, 1.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( 0x40007F81u, PackUtil::packSnorm8x4( float4{ -1.0f, 1.0f, 0.0f, 0.5f } ) );
    SW_EXPECT_EQUAL( 0xFFFF8000u, PackUtil::packUnorm16x2( float2{ 0.5f, 1.0f } ) );
    SW_EXPECT_EQUAL( 0xC0008001u, PackUtil::packSnorm16x2( float2{ -1.0f, -0.5f } ) ); // -0.5 → -16383.5 → -16384(0 에서 먼 쪽)
    SW_EXPECT_EQUAL( 0x3800BC00u, PackUtil::packHalf2x16( float2{ -1.0f, 0.5f } ) );
    SW_EXPECT_EQUAL( 0xC00003FFu, PackUtil::packRGB10A2( float4{ 1.0f, 0.0f, 0.0f, 1.0f } ) );
    SW_EXPECT_EQUAL( RGB10A2::fromFloat4( float4{ 0.25f, 0.5f, 0.75f, 0.5f } ).getPacked(), PackUtil::packRGB10A2( float4{ 0.25f, 0.5f, 0.75f, 0.5f } ) );
}

/**
 * @brief [PackUtilTest] 반올림 — unorm 의 가운데는 위로, snorm 의 가운데는 0 에서 먼 쪽, half 의 가운데는 짝수 쪽
 */
SW_TEST_CASE( PackUtilTest, RoundingFollowsHeaderRules )
{
    SW_EXPECT_EQUAL( 128u, PackUtil::packUnorm8x4( float4{ 0.5f, 0.0f, 0.0f, 0.0f } ) );           // 127.5 → 128
    SW_EXPECT_EQUAL( 0xC0u, PackUtil::packSnorm8x4( float4{ -0.5f, 0.0f, 0.0f, 0.0f } ) & 0xFFu ); // -63.5 → -64
    SW_EXPECT_EQUAL( 0x3C00u, PackUtil::packHalf( BitUtil::bitsToFloat( 0x3F801000u ) ) );         // 1 + 2^-11: 1.0 과 다음 값의 가운데 → 짝수(1.0)
    SW_EXPECT_EQUAL( 0x3C02u, PackUtil::packHalf( BitUtil::bitsToFloat( 0x3F803000u ) ) );         // 1 + 3·2^-11 → 짝수(0x3C02)
    SW_EXPECT_EQUAL( 0x3C01u, PackUtil::packHalf( BitUtil::bitsToFloat( 0x3F801001u ) ) );         // 가운데를 넘으면 위로
}

/**
 * @brief [PackUtilTest] 범위 밖 · NaN · 음의 0 — unorm · snorm 은 자르고 NaN · 음의 0 은 0, half 는 무한대 · 0 · 조용한 NaN · 부호 유지
 */
SW_TEST_CASE( PackUtilTest, OutOfRangeNaNAndNegativeZero )
{
    const float32 nan          = BitUtil::bitsToFloat( Internal::kQuietNaNBits );
    const float32 negativeZero = BitUtil::bitsToFloat( 0x80000000u );
    SW_EXPECT_EQUAL( 0xFF0000FFu, PackUtil::packUnorm8x4( float4{ 7.0f, -3.0f, nan, 2.0f } ) );
    SW_EXPECT_EQUAL( 0u, PackUtil::packUnorm16x2( float2{ negativeZero, nan } ) );
    SW_EXPECT_EQUAL( 0x00007F81u, PackUtil::packSnorm8x4( float4{ -9.0f, 9.0f, nan, negativeZero } ) );
    SW_EXPECT_EQUAL( 0u, PackUtil::packSnorm16x2( float2{ negativeZero, nan } ) );
    SW_EXPECT_EQUAL( 0x8000u, PackUtil::packHalf( negativeZero ) );
    SW_EXPECT_EQUAL( 0x7E00u, PackUtil::packHalf( nan ) );
    SW_EXPECT_EQUAL( 0x7C00u, PackUtil::packHalf( 65520.0f ) );                            // 65504 와 65536 의 가운데 → 짝수(무한대)
    SW_EXPECT_EQUAL( 0x7BFFu, PackUtil::packHalf( BitUtil::bitsToFloat( 0x477FEFFFu ) ) ); // 65520 바로 아래 → 65504
    SW_EXPECT_EQUAL( 0x0000u, PackUtil::packHalf( BitUtil::bitsToFloat( 0x33000000u ) ) ); // 2^-25 → 0(가운데 → 짝수)
    SW_EXPECT_EQUAL( 0x0001u, PackUtil::packHalf( BitUtil::bitsToFloat( 0x33000001u ) ) ); // 그 위 → 가장 작은 비정규수
    SW_EXPECT_EQUAL( 0x7FFF8001u, PackUtil::packSnorm16x2( float2{ -2.0f, 2.0f } ) );
    // snorm 정수 최솟값은 만들지 않고, 풀면 -1 이다.
    SW_EXPECT_EQUAL( -1.0f, PackUtil::unpackSnorm16x2( 0x00008000u )._x );
    SW_EXPECT_EQUAL( -1.0f, PackUtil::unpackSnorm8x4( 0x00000080u )._x );
}

/**
 * @brief [PackUtilTest] 왕복 — 모든 8 · 16 비트 정수는 풀고 다시 담으면 같은 비트다(snorm 정수 최솟값은 -최대로, half NaN 은 조용한 NaN 으로)
 */
SW_TEST_CASE( PackUtilTest, EveryRawValueRoundTrips )
{
    for ( uint32 raw = 0; raw < 256u; ++raw )
    {
        const uint32 word = raw | ( ( 255u - raw ) << 8 ) | ( raw << 16 ) | ( ( raw ^ 0x5Au ) << 24 );
        SW_EXPECT_EQUAL( word, PackUtil::packUnorm8x4( PackUtil::unpackUnorm8x4( word ) ) );
        if ( raw != 0x80u && ( 255u - raw ) != 0x80u && ( raw ^ 0x5Au ) != 0x80u )
            SW_EXPECT_EQUAL( word, PackUtil::packSnorm8x4( PackUtil::unpackSnorm8x4( word ) ) );
    }
    uint32 failedUnorm16 = 0;
    uint32 failedSnorm16 = 0;
    uint32 failedHalf    = 0;
    for ( uint32 raw = 0; raw < 65536u; ++raw )
    {
        const uint32 word = raw | ( ( 65535u - raw ) << 16 );
        failedUnorm16 += PackUtil::packUnorm16x2( PackUtil::unpackUnorm16x2( word ) ) != word ? 1u : 0u;
        if ( raw != 0x8000u && raw != 0x7FFFu )
            failedSnorm16 += PackUtil::packSnorm16x2( PackUtil::unpackSnorm16x2( word ) ) != word ? 1u : 0u;
        const bool bNaN = ( raw & 0x7C00u ) == 0x7C00u && ( raw & 0x03FFu ) != 0u;
        if ( bNaN == false )
            failedHalf += PackUtil::packHalf( PackUtil::unpackHalf( raw ) ) != raw ? 1u : 0u;
    }
    SW_EXPECT_EQUAL( 0u, failedUnorm16 );
    SW_EXPECT_EQUAL( 0u, failedSnorm16 );
    SW_EXPECT_EQUAL( 0u, failedHalf );
    SW_EXPECT_EQUAL( 0x7E00u, PackUtil::packHalf( PackUtil::unpackHalf( 0x7C01u ) ) & 0x7E00u );
}

/**
 * @brief [PackUtilTest] 풀기 — 정수 / 최댓값이고 끝값은 정확히 0 · 1 · -1 이다
 */
SW_TEST_CASE( PackUtilTest, UnpackHitsExactEndpoints )
{
    const float4 color = PackUtil::unpackUnorm8x4( 0xFF00FF00u );
    SW_EXPECT_EQUAL( 0.0f, color._x );
    SW_EXPECT_EQUAL( 1.0f, color._y );
    SW_EXPECT_EQUAL( 0.0f, color._z );
    SW_EXPECT_EQUAL( 1.0f, color._w );
    SW_EXPECT_EQUAL( 128.0f / 255.0f, PackUtil::unpackUnorm8x4( 0x80u )._x );
    const float2 signedPair = PackUtil::unpackSnorm16x2( 0x80017FFFu );
    SW_EXPECT_EQUAL( 1.0f, signedPair._x );
    SW_EXPECT_EQUAL( -1.0f, signedPair._y );
    const float2 halfPair = PackUtil::unpackHalf2x16( 0xC0003C00u );
    SW_EXPECT_EQUAL( 1.0f, halfPair._x );
    SW_EXPECT_EQUAL( -2.0f, halfPair._y );
    const float4 rgb10a2 = PackUtil::unpackRGB10A2( 0xFFFFFFFFu );
    SW_EXPECT_EQUAL( 1.0f, rgb10a2._x );
    SW_EXPECT_EQUAL( 1.0f, rgb10a2._w );
}

/**
 * @brief [PackUtilTest] 비트필드 — 꺼내기 · 바꾸기가 offset 부터 count 비트만 건드리고, count 32 도 시프트 32 없이 된다
 */
SW_TEST_CASE( PackUtilTest, BitFieldExtractAndInsert )
{
    SW_EXPECT_EQUAL( 0x12u, PackUtil::extractBits( 0xABCD1234u, 8u, 8u ) );
    SW_EXPECT_EQUAL( 0xAu, PackUtil::extractBits( 0xABCD1234u, 28u, 4u ) );
    SW_EXPECT_EQUAL( 1u, PackUtil::extractBits( 0x80000000u, 31u, 1u ) );
    SW_EXPECT_EQUAL( 0xABCD1234u, PackUtil::extractBits( 0xABCD1234u, 0u, 32u ) );
    SW_EXPECT_EQUAL( 0xAB5D1234u, PackUtil::insertBits( 0xABCD1234u, 0x5u, 20u, 4u ) );
    SW_EXPECT_EQUAL( 0xABCD12F4u, PackUtil::insertBits( 0xABCD1234u, 0x1Fu, 4u, 4u ) ); // 넘치는 위쪽 비트는 버린다
    SW_EXPECT_EQUAL( 0x02FFFFFFu, PackUtil::insertBits( 0xFFFFFFFFu, 2u, 24u, 8u ) );
    SW_EXPECT_EQUAL( 0x12345678u, PackUtil::insertBits( 0xFFFFFFFFu, 0x12345678u, 0u, 32u ) );
}

/**
 * @brief [PackUtilTest] RGB9E5 — 공유 지수 배치(가수 9 비트 셋, 위 5 비트 지수 · 편향 15), 상한 65408, 0 · 음수는 0
 */
SW_TEST_CASE( PackUtilTest, RGB9E5KnownWords )
{
    SW_EXPECT_EQUAL( 0x80000100u, PackUtil::packRGB9E5( float3{ 1.0f, 0.0f, 0.0f } ) ); // 지수 16, 눈금 2^-8, 가수 256
    SW_EXPECT_EQUAL( 0xF80001FFu, PackUtil::packRGB9E5( float3{ 65408.0f, 0.0f, 0.0f } ) );
    SW_EXPECT_EQUAL( 0xF80001FFu, PackUtil::packRGB9E5( float3{ 1.0e9f, -1.0f, 0.0f } ) ); // 상한으로 자른다
    SW_EXPECT_EQUAL( 0u, PackUtil::packRGB9E5( float3{ -1.0f, 0.0f, -0.5f } ) );
    const float3 back = PackUtil::unpackRGB9E5( 0x80000100u );
    SW_EXPECT_EQUAL( 1.0f, back._x );
    SW_EXPECT_EQUAL( 0.0f, back._y );
    SW_EXPECT_EQUAL( 0.0f, back._z );
}

/**
 * @brief [PackUtilTest] 셰이더 짝 — pack.hlsli 가 C++ 와 같은 이름 · 같은 인자 순서의 함수를 모두 정의한다(한쪽만 더하거나 바꾸면 여기서 진다)
 */
SW_TEST_CASE( PackUtilTest, ShaderPairDefinesSameFunctions )
{
    const string text = Internal::readShaderPair();
    SW_REQUIRE_FALSE( text.empty() );
    const utf8* const arrSignature[] = {
        "static uint PackUtil::extractBits( uint value, uint offset, uint count )",
        "static uint PackUtil::insertBits( uint value, uint field, uint offset, uint count )",
        "static uint PackUtil::packUnorm8x4( float4 value )",
        "static float4 PackUtil::unpackUnorm8x4( uint packed )",
        "static uint PackUtil::packSnorm8x4( float4 value )",
        "static float4 PackUtil::unpackSnorm8x4( uint packed )",
        "static uint PackUtil::packUnorm16x2( float2 value )",
        "static float2 PackUtil::unpackUnorm16x2( uint packed )",
        "static uint PackUtil::packSnorm16x2( float2 value )",
        "static float2 PackUtil::unpackSnorm16x2( uint packed )",
        "static uint PackUtil::packRGB10A2( float4 value )",
        "static float4 PackUtil::unpackRGB10A2( uint packed )",
        "static uint PackUtil::packHalf( float value )",
        "static float PackUtil::unpackHalf( uint bits )",
        "static uint PackUtil::packHalf2x16( float2 value )",
        "static float2 PackUtil::unpackHalf2x16( uint packed )",
        "static float3 PackUtil::unpackRGB9E5( uint packed )",
    };
    for ( const utf8* pSignature : arrSignature )
    {
        SW_EXPECT_TRUE( text.find( pSignature ) != string::npos );
    }
    // half 경계는 C++ `half` 의 상수와 같은 비트다(셰이더가 f32tof16 이 아니라 같은 비트 연산을 쓴다).
    SW_EXPECT_TRUE( text.find( "absoluteBits >= 0x477FF000u" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "absoluteBits > 0x33000000u" ) != string::npos );
}
