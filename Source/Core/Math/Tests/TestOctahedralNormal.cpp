#include "pch.h"

#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/OctahedralNormal.h"
#include "Core/Math/VectorMath.h"
#include "Core/Math/XorShift32.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief 옥타헤드럴 법선 테스트의 도우미입니다. */
        struct OctahedralNormalTestInternal
        {
            static constexpr uint32 kSampleCount = 20000;
            static constexpr uint32 kSeed        = 0x7F4A7C15u;
            /** @brief 방향 오차 상한(라디안 ≈ 단위 벡터 사이의 거리)입니다. 좌표 한 칸이 2/65535 라 실제 최대는 이보다 한참 작습니다. */
            static constexpr float32 kMaxAngleError = 1.0e-4f;

            /** @brief 인코딩했다 푼 방향이 원래 방향에서 벗어난 정도(두 단위 벡터 사이의 거리 ≈ 작은 각도)입니다. */
            static float32 measureError( const float3& direction )
            {
                const float3 unit    = direction.normalize();
                const float3 decoded = OctahedralNormal::fromNormal( unit ).toNormal();
                return ( decoded - unit ).getLength();
            }

            /**
             * @brief `binding.hlsli` 의 `VertexAnimation::unpackNormal` 본문을 한 줄씩 옮긴 기준 식입니다. 아래 HLSL 과 어긋나면 이 함수를 같이 고칩니다.
             * @code
             *     float3       normal = float3( octant, 1.0f - abs( octant.x ) - abs( octant.y ) );
             *     const float2 signs  = float2( normal.x >= 0.0f ? 1.0f : -1.0f, normal.y >= 0.0f ? 1.0f : -1.0f );
             *     const float2 folded = ( 1.0f - abs( normal.yx ) ) * signs;
             *     normal.xy           = ( normal.z < 0.0f ) ? folded : normal.xy;
             *     return normalize( normal );
             * @endcode
             */
            static float3 decodeLikeShader( const float32 octantX, const float32 octantY )
            {
                float3        normal{ octantX, octantY, 1.0f - MathUtil::abs( octantX ) - MathUtil::abs( octantY ) };
                const float32 signX   = normal._x >= 0.0f ? 1.0f : -1.0f;
                const float32 signY   = normal._y >= 0.0f ? 1.0f : -1.0f;
                const float32 foldedX = ( 1.0f - MathUtil::abs( normal._y ) ) * signX;
                const float32 foldedY = ( 1.0f - MathUtil::abs( normal._x ) ) * signY;
                if ( normal._z < 0.0f )
                {
                    normal._x = foldedX;
                    normal._y = foldedY;
                }
                return normal.normalize();
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [OctahedralNormalTest] 축 방향 여섯 개 — 극점 · 축은 오차 없이 풀리고 좌표가 정해진 정수다(+z = (0,0), -z = (최대,최대))
 */
SW_TEST_CASE( OctahedralNormalTest, AxisDirectionsAreExact )
{
    using sw::OctahedralNormal;
    using sw::OctahedralNormalTestInternal;
    const sw::float3 arrAxis[6]{
        sw::float3{ 1.0f,  0.0f,  0.0f},
        sw::float3{-1.0f,  0.0f,  0.0f},
        sw::float3{ 0.0f,  1.0f,  0.0f},
        sw::float3{ 0.0f, -1.0f,  0.0f},
        sw::float3{ 0.0f,  0.0f,  1.0f},
        sw::float3{ 0.0f,  0.0f, -1.0f}
    };
    for ( const sw::float3& axis : arrAxis )
    {
        SW_EXPECT_TRUE( OctahedralNormalTestInternal::measureError( axis ) <= 1.0e-6f );
    }

    SW_EXPECT_EQUAL( 0, OctahedralNormal::fromNormal( arrAxis[4] )._u.getRaw() );
    SW_EXPECT_EQUAL( 0, OctahedralNormal::fromNormal( arrAxis[4] )._v.getRaw() );
    SW_EXPECT_EQUAL( 32767, OctahedralNormal::fromNormal( arrAxis[5] )._u.getRaw() );
    SW_EXPECT_EQUAL( 32767, OctahedralNormal::fromNormal( arrAxis[5] )._v.getRaw() );
    SW_EXPECT_EQUAL( 32767, OctahedralNormal::fromNormal( arrAxis[0] )._u.getRaw() );
    SW_EXPECT_EQUAL( 0, OctahedralNormal::fromNormal( arrAxis[0] )._v.getRaw() );
    SW_EXPECT_EQUAL( -32767, OctahedralNormal::fromNormal( arrAxis[1] )._u.getRaw() );
    SW_EXPECT_EQUAL( 32767, OctahedralNormal::fromNormal( arrAxis[2] )._v.getRaw() );
    SW_EXPECT_EQUAL( -32767, OctahedralNormal::fromNormal( arrAxis[3] )._v.getRaw() );
}

/**
 * @brief [OctahedralNormalTest] 여덟 옥탄트 모두에서, 축 근처 · 적도 근처 · 대각선 방향까지 왕복 방향 오차가 상한 이하다
 */
SW_TEST_CASE( OctahedralNormalTest, EveryOctantRoundTripsWithinTheErrorLimit )
{
    using sw::OctahedralNormalTestInternal;
    const float32 arrMagnitude[]{ 0.0f, 0.001f, 0.02f, 0.3f, 0.6f, 1.0f };
    float32       worstError     = 0.0f;
    uint32        directionCount = 0;
    for ( const float32 signX : { 1.0f, -1.0f } )
    {
        for ( const float32 signY : { 1.0f, -1.0f } )
        {
            for ( const float32 signZ : { 1.0f, -1.0f } )
            {
                for ( const float32 magnitudeX : arrMagnitude )
                {
                    for ( const float32 magnitudeY : arrMagnitude )
                    {
                        for ( const float32 magnitudeZ : arrMagnitude )
                        {
                            const sw::float3 direction{ signX * magnitudeX, signY * magnitudeY, signZ * magnitudeZ };
                            if ( direction.getLengthSquared() <= 0.0f )
                                continue;
                            worstError = sw::MathUtil::max( worstError, OctahedralNormalTestInternal::measureError( direction ) );
                            ++directionCount;
                        }
                    }
                }
            }
        }
    }
    SW_EXPECT_TRUE( 0u < directionCount );
    SW_EXPECT_TRUE( worstError <= OctahedralNormalTestInternal::kMaxAngleError );
}

/**
 * @brief [OctahedralNormalTest] 구 위에 고르게 뿌린 결정적 난수 방향의 왕복 오차가 상한 이하이고, 푼 값은 항상 단위 길이다
 */
SW_TEST_CASE( OctahedralNormalTest, RandomDirectionsStayWithinTheErrorLimit )
{
    using sw::OctahedralNormalTestInternal;
    uint32  state       = OctahedralNormalTestInternal::kSeed;
    float32 worstError  = 0.0f;
    float32 worstLength = 0.0f;
    for ( uint32 sampleIndex = 0; sampleIndex < OctahedralNormalTestInternal::kSampleCount; ++sampleIndex )
    {
        state                    = sw::XorShift32::next( state );
        const float32 height     = sw::XorShift32::toUnit24( state ) * 2.0f - 1.0f;
        state                    = sw::XorShift32::next( state );
        const float32    azimuth = sw::XorShift32::toUnit24( state ) * sw::MathUtil::kTwoPi;
        const float32    radius  = std::sqrt( 1.0f - height * height );
        const sw::float3 direction{ radius * std::cos( azimuth ), radius * std::sin( azimuth ), height };
        worstError  = sw::MathUtil::max( worstError, OctahedralNormalTestInternal::measureError( direction ) );
        worstLength = sw::MathUtil::max( worstLength, sw::MathUtil::abs( sw::OctahedralNormal::fromNormal( direction ).toNormal().getLength() - 1.0f ) );
    }
    SW_EXPECT_TRUE( worstError <= OctahedralNormalTestInternal::kMaxAngleError );
    SW_EXPECT_TRUE( worstLength <= 1.0e-6f );
}

/**
 * @brief [OctahedralNormalTest] 풀이는 셰이더 `binding.hlsli` 의 `VertexAnimation::unpackNormal` 과 같은 식이다 — 모든 좌표 영역(접힘 · 부호 경계 포함)에서 일치
 * @details 두 쪽이 어긋나면 CPU 에서 만든 법선과 GPU 가 푼 법선이 달라진다. 셰이더 식을 바꾸면 `decodeLikeShader` 와 `OctahedralNormal::decodeOctant` 를 함께 바꾼다.
 */
SW_TEST_CASE( OctahedralNormalTest, DecodeMatchesShaderFormula )
{
    using sw::OctahedralNormalTestInternal;
    float32 worstDifference = 0.0f;
    for ( int32 rawU = -32767; rawU <= 32767; rawU += 509 )
    {
        for ( int32 rawV = -32767; rawV <= 32767; rawV += 509 )
        {
            const sw::OctahedralNormal encoded{ sw::snorm16::fromRaw( static_cast<int16>( rawU ) ), sw::snorm16::fromRaw( static_cast<int16>( rawV ) ) };
            const sw::float3           expected = OctahedralNormalTestInternal::decodeLikeShader( encoded._u.toFloat(), encoded._v.toFloat() );
            worstDifference                     = sw::MathUtil::max( worstDifference, ( encoded.toNormal() - expected ).getLength() );
        }
    }
    // 부호 경계(0)와 접힘 경계(|u| + |v| = 1)를 정확히 지난다.
    for ( const int16 raw : { static_cast<int16>( 0 ), static_cast<int16>( 32767 ), static_cast<int16>( -32767 ), static_cast<int16>( 16384 ), static_cast<int16>( -16383 ) } )
    {
        const int16                edgeRaw = static_cast<int16>( raw >= 0 ? 32767 - raw : -32767 - raw ); // |u| + |v| = 1
        const sw::OctahedralNormal encoded{ sw::snorm16::fromRaw( raw ), sw::snorm16::fromRaw( edgeRaw ) };
        const sw::float3           expected = OctahedralNormalTestInternal::decodeLikeShader( encoded._u.toFloat(), encoded._v.toFloat() );
        worstDifference                     = sw::MathUtil::max( worstDifference, ( encoded.toNormal() - expected ).getLength() );
    }
    SW_EXPECT_TRUE( worstDifference <= 1.0e-6f );
}

/**
 * @brief [OctahedralNormalTest] 결정성 — 같은 입력은 같은 비트이고, 길이가 달라도 방향이 같으면(2 의 거듭제곱 배) 같은 비트이고, 영벡터 · NaN 은 +z 다
 */
SW_TEST_CASE( OctahedralNormalTest, EncodingIsDeterministic )
{
    uint32 state    = 0x13579BDFu;
    uint32 mismatch = 0;
    for ( uint32 sampleIndex = 0; sampleIndex < 4096; ++sampleIndex )
    {
        state                             = sw::XorShift32::next( state );
        const float32 x                   = sw::XorShift32::toUnit24( state ) * 2.0f - 1.0f;
        state                             = sw::XorShift32::next( state );
        const float32 y                   = sw::XorShift32::toUnit24( state ) * 2.0f - 1.0f;
        state                             = sw::XorShift32::next( state );
        const float32              z      = sw::XorShift32::toUnit24( state ) * 2.0f - 1.0f;
        const sw::OctahedralNormal first  = sw::OctahedralNormal::fromNormal( sw::float3{ x, y, z } );
        const sw::OctahedralNormal second = sw::OctahedralNormal::fromNormal( sw::float3{ x, y, z } );
        const sw::OctahedralNormal scaled = sw::OctahedralNormal::fromNormal( sw::float3{ x * 8.0f, y * 8.0f, z * 8.0f } );
        if ( first._u.getRaw() != second._u.getRaw() || first._v.getRaw() != second._v.getRaw() )
            ++mismatch;
        if ( first._u.getRaw() != scaled._u.getRaw() || first._v.getRaw() != scaled._v.getRaw() )
            ++mismatch;
    }
    SW_EXPECT_EQUAL( 0u, mismatch );

    const sw::OctahedralNormal zero = sw::OctahedralNormal::fromNormal( sw::float3{ 0.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( zero._u.getRaw() == 0 && zero._v.getRaw() == 0 );
    const float32              notANumber = sw::BitUtil::bitsToFloat( 0x7FC00000u );
    const sw::OctahedralNormal broken     = sw::OctahedralNormal::fromNormal( sw::float3{ notANumber, 1.0f, notANumber } );
    SW_EXPECT_TRUE( broken._u.getRaw() == 0 && broken._v.getRaw() == 0 );
}

/**
 * @brief [OctahedralNormalTest] 메모리 배치는 _u, _v 순서의 snorm16 두 개(R16G16_SNORM)다
 */
SW_TEST_CASE( OctahedralNormalTest, MemoryLayoutIsTwoSnorm16 )
{
    const sw::OctahedralNormal encoded{ sw::snorm16::fromRaw( 1234 ), sw::snorm16::fromRaw( -4321 ) };
    int16                      arrWord[2]{};
    sw::Memory::copy( arrWord, &encoded, sizeof( encoded ) );
    SW_EXPECT_EQUAL( 1234, arrWord[0] );
    SW_EXPECT_EQUAL( -4321, arrWord[1] );
}
