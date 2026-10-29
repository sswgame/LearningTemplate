#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/Half.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"
#include "Core/Testing/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief half 테스트의 도우미입니다. */
        struct HalfTestInternal
        {
            static constexpr uint32 kSampleCount = 50000;
            static constexpr uint32 kSeed        = 0x2545F491u;

            static uint32 bitsOf( const float32 value ) { return half::fromFloat( value ).getBits(); }

            /** @brief half 비트 패턴이 뜻하는 값을 `ldexp` 로 따로 계산합니다(유한한 값만) — 변환 코드와 같은 식을 쓰지 않는 기준입니다. */
            static float32 referenceValue( const uint32 bits )
            {
                const int32   exponent = static_cast<int32>( ( bits >> 10 ) & 0x1Fu );
                const int32   mantissa = static_cast<int32>( bits & 0x3FFu );
                const float32 magnitude =
                    exponent == 0 ? std::ldexp( static_cast<float32>( mantissa ), -24 ) : std::ldexp( static_cast<float32>( 1024 + mantissa ), exponent - 25 );
                return ( bits & 0x8000u ) != 0u ? -magnitude : magnitude;
            }

            /** @brief float32 비트 패턴(NaN · 무한대 포함)을 결정적 난수로 만들어 half 로 바꾼 결과를 한 값으로 접습니다. */
            static uint32 hashRandomBitPatterns()
            {
                uint32 state = 0x9E3779B9u;
                uint32 hash  = 0u;
                for ( uint32 sampleIndex = 0; sampleIndex < 4096; ++sampleIndex )
                {
                    state = XorShift32::next( state );
                    hash  = hash * 31u + half::fromFloat( BitUtil::bitsToFloat( state ) ).getBits();
                }
                return hash;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [HalfTest] 알려진 값의 비트 패턴 — 0 · -0 · 1 · -2 · 0.5 · 최대 · 최소 정규 · 최소 비정규 · 0.1 · 3.140625
 */
SW_TEST_CASE( HalfTest, KnownBitPatterns )
{
    using sw::HalfTestInternal;
    SW_EXPECT_EQUAL( 0x0000u, HalfTestInternal::bitsOf( 0.0f ) );
    SW_EXPECT_EQUAL( 0x8000u, HalfTestInternal::bitsOf( -0.0f ) );
    SW_EXPECT_EQUAL( 0x3C00u, HalfTestInternal::bitsOf( 1.0f ) );
    SW_EXPECT_EQUAL( 0xC000u, HalfTestInternal::bitsOf( -2.0f ) );
    SW_EXPECT_EQUAL( 0x3800u, HalfTestInternal::bitsOf( 0.5f ) );
    SW_EXPECT_EQUAL( 0x7BFFu, HalfTestInternal::bitsOf( sw::half::kMaxValue ) );
    SW_EXPECT_EQUAL( 0x0400u, HalfTestInternal::bitsOf( sw::half::kMinNormalValue ) );
    SW_EXPECT_EQUAL( 0x0001u, HalfTestInternal::bitsOf( sw::half::kMinSubnormalValue ) );
    SW_EXPECT_EQUAL( 0x2E66u, HalfTestInternal::bitsOf( 0.1f ) );
    SW_EXPECT_EQUAL( 0x4248u, HalfTestInternal::bitsOf( 3.140625f ) );
    SW_EXPECT_EQUAL( 0x3C01u, HalfTestInternal::bitsOf( 1.0009765625f ) ); // 1 + 2^-10: 1 보다 큰 첫 half
}

/**
 * @brief [HalfTest] 가운데 값은 짝수 쪽으로, 가운데를 조금이라도 넘으면 위로 반올림한다(정규수 · 비정규수 · 최대값 넘침)
 */
SW_TEST_CASE( HalfTest, RoundsToNearestEven )
{
    using sw::HalfTestInternal;
    const float32 tieBelowEven = 1.00048828125f; // 1 + 2^-11: 0x3C00 과 0x3C01 의 한가운데 → 짝수 0x3C00
    SW_EXPECT_EQUAL( 0x3C00u, HalfTestInternal::bitsOf( tieBelowEven ) );
    SW_EXPECT_EQUAL( 0x3C01u, HalfTestInternal::bitsOf( sw::BitUtil::bitsToFloat( sw::BitUtil::floatToBits( tieBelowEven ) + 1u ) ) );
    SW_EXPECT_EQUAL( 0x3C00u, HalfTestInternal::bitsOf( sw::BitUtil::bitsToFloat( sw::BitUtil::floatToBits( tieBelowEven ) - 1u ) ) );
    SW_EXPECT_EQUAL( 0x3C02u, HalfTestInternal::bitsOf( 1.00146484375f ) ); // 1 + 3 * 2^-11: 0x3C01 과 0x3C02 의 한가운데 → 짝수 0x3C02

    SW_EXPECT_EQUAL( 0x0000u, HalfTestInternal::bitsOf( std::ldexp( 1.0f, -25 ) ) ); // 최소 비정규수의 절반 → 짝수 0
    SW_EXPECT_EQUAL( 0x0001u, HalfTestInternal::bitsOf( sw::BitUtil::bitsToFloat( sw::BitUtil::floatToBits( std::ldexp( 1.0f, -25 ) ) + 1u ) ) );
    SW_EXPECT_EQUAL( 0x0002u, HalfTestInternal::bitsOf( std::ldexp( 3.0f, -25 ) ) );    // 1.5 단계 → 짝수 2
    SW_EXPECT_EQUAL( 0x0400u, HalfTestInternal::bitsOf( std::ldexp( 1023.5f, -24 ) ) ); // 가장 큰 비정규수와 최소 정규수의 한가운데 → 짝수인 최소 정규수

    SW_EXPECT_EQUAL( 0x7BFFu, HalfTestInternal::bitsOf( 65519.0f ) );
    SW_EXPECT_EQUAL( 0x7C00u, HalfTestInternal::bitsOf( 65520.0f ) ); // 65504 와 65536 의 한가운데 → 짝수인 무한대
}

/**
 * @brief [HalfTest] 무한대 · 범위 넘침 · 너무 작은 값 · NaN — 부호를 지키고 NaN 은 NaN 으로 남는다
 */
SW_TEST_CASE( HalfTest, InfinityOverflowUnderflowAndNaN )
{
    using sw::HalfTestInternal;
    const float32 infinity = sw::BitUtil::bitsToFloat( 0x7F800000u );
    SW_EXPECT_EQUAL( 0x7C00u, HalfTestInternal::bitsOf( infinity ) );
    SW_EXPECT_EQUAL( 0xFC00u, HalfTestInternal::bitsOf( -infinity ) );
    SW_EXPECT_EQUAL( 0x7C00u, HalfTestInternal::bitsOf( 1.0e10f ) );
    SW_EXPECT_EQUAL( 0xFC00u, HalfTestInternal::bitsOf( -1.0e10f ) );
    SW_EXPECT_EQUAL( 0x0000u, HalfTestInternal::bitsOf( 1.0e-10f ) );
    SW_EXPECT_EQUAL( 0x8000u, HalfTestInternal::bitsOf( -1.0e-10f ) );
    SW_EXPECT_EQUAL( 0x0000u, HalfTestInternal::bitsOf( sw::BitUtil::bitsToFloat( 0x00000001u ) ) ); // float32 비정규수

    SW_EXPECT_EQUAL( 0x7E00u, HalfTestInternal::bitsOf( sw::BitUtil::bitsToFloat( 0x7FC00000u ) ) );
    SW_EXPECT_EQUAL( 0xFE00u, HalfTestInternal::bitsOf( sw::BitUtil::bitsToFloat( 0xFFC00000u ) ) );
    SW_EXPECT_TRUE( sw::half::fromFloat( sw::BitUtil::bitsToFloat( 0x7F800001u ) ).isNaN() ); // 페이로드가 사라지는 신호 NaN 도 무한대가 되지 않는다
    SW_EXPECT_TRUE( sw::half::fromFloat( sw::BitUtil::bitsToFloat( 0x7FFFFFFFu ) ).isNaN() );

    SW_EXPECT_TRUE( sw::half::fromBits( 0x7C00u ).isInfinity() && sw::half::fromBits( 0xFC00u ).isInfinity() );
    SW_EXPECT_FALSE( sw::half::fromBits( 0x7C00u ).isNaN() );
    SW_EXPECT_FALSE( sw::half::fromBits( 0x7C00u ).isFinite() );
    SW_EXPECT_TRUE( sw::half::fromBits( 0x7BFFu ).isFinite() && sw::half::fromBits( 0x0001u ).isFinite() );
    SW_EXPECT_EQUAL( sw::BitUtil::floatToBits( infinity ), sw::BitUtil::floatToBits( sw::half::fromBits( 0x7C00u ).toFloat() ) );
    SW_EXPECT_TRUE( sw::BitUtil::floatToBits( sw::half::fromBits( 0xFC00u ).toFloat() ) == 0xFF800000u );
}

/**
 * @brief [HalfTest] 65536 개 비트 패턴을 모두 float32 로 풀고 다시 담으면 같은 비트가 된다(NaN 은 NaN 으로), 푼 값은 `ldexp` 기준값과 정확히 같다
 */
SW_TEST_CASE( HalfTest, EveryBitPatternRoundTrips )
{
    using sw::HalfTestInternal;
    uint32 mismatchCount = 0;
    uint32 valueMismatch = 0;
    for ( uint32 bits = 0; bits <= 0xFFFFu; ++bits )
    {
        const sw::half source = sw::half::fromBits( static_cast<uint16>( bits ) );
        const float32  value  = source.toFloat();
        const sw::half back   = sw::half::fromFloat( value );
        if ( source.isNaN() )
        {
            const bool bSameSign = ( back.getBits() & 0x8000u ) == ( bits & 0x8000u );
            if ( back.isNaN() == false || bSameSign == false )
                ++mismatchCount;
            continue;
        }
        if ( back.getBits() != bits )
            ++mismatchCount;
        if ( source.isInfinity() == false && sw::BitUtil::floatToBits( value ) != sw::BitUtil::floatToBits( HalfTestInternal::referenceValue( bits ) ) )
            ++valueMismatch;
    }
    SW_EXPECT_EQUAL( 0u, mismatchCount );
    SW_EXPECT_EQUAL( 0u, valueMismatch );
}

/**
 * @brief [HalfTest] `toFiniteFloat`(반사 프로브 읽기의 `decodeHalf`) — 65536 개 비트 패턴 모두, 유한값(서브노멀 포함)은 `toFloat` 와 같고 무한대 · NaN 은 부호 있는 float32 최대값이다
 */
SW_TEST_CASE( HalfTest, ToFiniteFloatClampsOnlyNonFinite )
{
    uint32 finiteMismatch    = 0;
    uint32 nonFiniteMismatch = 0;
    uint32 nonFiniteCount    = 0;
    for ( uint32 bits = 0; bits <= 0xFFFFu; ++bits )
    {
        const sw::half source = sw::half::fromBits( static_cast<uint16>( bits ) );
        const float32  value  = source.toFiniteFloat();
        if ( source.isFinite() )
        {
            if ( sw::BitUtil::floatToBits( value ) != sw::BitUtil::floatToBits( source.toFloat() ) )
                ++finiteMismatch;
            continue;
        }
        ++nonFiniteCount;
        const float32 expected = ( bits & 0x8000u ) != 0u ? -sw::MathUtil::kMaxFloat : sw::MathUtil::kMaxFloat;
        if ( value != expected )
            ++nonFiniteMismatch;
    }
    SW_EXPECT_EQUAL( 0u, finiteMismatch );
    SW_EXPECT_EQUAL( 0u, nonFiniteMismatch );
    SW_EXPECT_EQUAL( 2u * 1024u, nonFiniteCount );                                                  // 지수가 모두 1 인 패턴: 부호 2 × 가수 1024 (무한대 2 + NaN 2046)
    SW_EXPECT_EQUAL( sw::half::kMinSubnormalValue, sw::half::fromBits( 0x0001u ).toFiniteFloat() ); // 가장 작은 서브노멀
    SW_EXPECT_EQUAL( -sw::half::kMinSubnormalValue, sw::half::fromBits( 0x8001u ).toFiniteFloat() );
}

/**
 * @brief [HalfTest] 정규수의 상대 오차는 2^-11 이하, 비정규수의 절대 오차는 2^-25 이하다(결정적 난수 표본)
 */
SW_TEST_CASE( HalfTest, RoundTripErrorIsWithinHalfStep )
{
    using sw::HalfTestInternal;
    uint32  state             = HalfTestInternal::kSeed;
    float32 worstRelative     = 0.0f;
    float32 worstSubnormalAbs = 0.0f;
    for ( uint32 sampleIndex = 0; sampleIndex < HalfTestInternal::kSampleCount; ++sampleIndex )
    {
        state                   = sw::XorShift32::next( state );
        const float32 mantissa  = 1.0f + sw::XorShift32::toUnit24( state );
        state                   = sw::XorShift32::next( state );
        const int32   exponent  = static_cast<int32>( state % 29u ) - 14; // 2^-14 .. 2^14 (최대 half 아래)
        const float32 value     = std::ldexp( mantissa, exponent );
        const float32 roundTrip = sw::half::fromFloat( value ).toFloat();
        worstRelative           = sw::MathUtil::max( worstRelative, sw::MathUtil::abs( roundTrip - value ) / value );

        state                     = sw::XorShift32::next( state );
        const float32 subnormal   = sw::XorShift32::toUnit24( state ) * sw::half::kMinNormalValue;
        const float32 subnormalRt = sw::half::fromFloat( subnormal ).toFloat();
        worstSubnormalAbs         = sw::MathUtil::max( worstSubnormalAbs, sw::MathUtil::abs( subnormalRt - subnormal ) );
    }
    SW_EXPECT_TRUE( worstRelative <= std::ldexp( 1.0f, -11 ) );
    SW_EXPECT_TRUE( worstSubnormalAbs <= std::ldexp( 1.0f, -25 ) );
}

/**
 * @brief [HalfTest] 결정성 — 같은 입력은 같은 비트이고, 컴파일 타임(constexpr)과 런타임 결과가 같다
 */
SW_TEST_CASE( HalfTest, ConversionIsDeterministicAndConstexpr )
{
    constexpr sw::half kCompileTime = sw::half::fromFloat( 0.1f );
    static_assert( kCompileTime.getBits() == 0x2E66u, "constexpr 변환이 런타임 변환과 같은 비트여야 한다" );
    static_assert( sw::half::fromBits( 0x2E66u ).toFloat() == 0.0999755859375f, "constexpr 풀이가 정확해야 한다" );

    volatile float32 runtimeValue = 0.1f;
    SW_EXPECT_EQUAL( static_cast<uint32>( kCompileTime.getBits() ), static_cast<uint32>( sw::half::fromFloat( runtimeValue ).getBits() ) );

    SW_EXPECT_EQUAL( sw::HalfTestInternal::hashRandomBitPatterns(), sw::HalfTestInternal::hashRandomBitPatterns() );
}

/**
 * @brief [HalfTest] half2 · half3 · half4 는 성분마다 담았다 푼다 — 크기는 4 · 6 · 8 바이트
 */
SW_TEST_CASE( HalfTest, VectorsStoreComponentwise )
{
    static_assert( sizeof( sw::half2 ) == 4 && sizeof( sw::half3 ) == 6 && sizeof( sw::half4 ) == 8, "묶음 크기" );

    const sw::half2  packed2 = sw::half2::fromFloat2( sw::float2{ 1.5f, -0.25f } );
    const sw::float2 back2   = packed2.toFloat2();
    SW_EXPECT_EQUAL( 1.5f, back2._x );
    SW_EXPECT_EQUAL( -0.25f, back2._y );

    const sw::half3  packed3 = sw::half3::fromFloat3( sw::float3{ 2.0f, 0.0f, -65504.0f } );
    const sw::float3 back3   = packed3.toFloat3();
    SW_EXPECT_EQUAL( 2.0f, back3._x );
    SW_EXPECT_EQUAL( 0.0f, back3._y );
    SW_EXPECT_EQUAL( -65504.0f, back3._z );

    const sw::half4  packed4 = sw::half4::fromFloat4( sw::float4{ 0.1f, 0.2f, 0.3f, 1.0f } );
    const sw::float4 back4   = packed4.toFloat4();
    SW_EXPECT_NEAR( 0.1f, back4._x, 0.1f * 0.0005f );
    SW_EXPECT_NEAR( 0.2f, back4._y, 0.2f * 0.0005f );
    SW_EXPECT_NEAR( 0.3f, back4._z, 0.3f * 0.0005f );
    SW_EXPECT_EQUAL( 1.0f, back4._w );
    SW_EXPECT_EQUAL( 0x3C00u, static_cast<uint32>( packed4._w.getBits() ) );
}

/**
 * @brief [HalfTest] 월드 좌표에 쓰면 안 되는 이유 — 1024 이상에서 단위 간격이 1 이 되어 0.5 m 이동이 사라진다(규칙의 근거를 코드로 남김)
 */
SW_TEST_CASE( HalfTest, LargeValuesLoseSubUnitPrecision )
{
    SW_EXPECT_EQUAL( 1500.0f, sw::half::fromFloat( 1500.0f ).toFloat() );
    SW_EXPECT_EQUAL( 1500.0f, sw::half::fromFloat( 1500.4f ).toFloat() ); // 0.4 m 이동이 사라진다
    SW_EXPECT_EQUAL( 1501.0f, sw::half::fromFloat( 1500.6f ).toFloat() ); // 다음 값은 1 m 뒤다
}
