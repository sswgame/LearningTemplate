#include "pch.h"

#include "Core/Math/PackedTypes.h"
#include "Core/Math/VectorMath.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    // 성분 수와 판별은 컴파일 때 정해진다.
    static_assert( kIsPackedValue<half> && kIsPackedValue<half4> && kIsPackedValue<unorm8x4> && kIsPackedValue<OctahedralNormal> && kIsPackedValue<RGB10A2>,
                   "압축 타입은 특성이 있다" );
    static_assert( kIsPackedValue<float3> == false && kIsPackedValue<int32> == false && kIsPackedValue<float32> == false, "압축 타입이 아니면 특성이 비어 있다" );
    static_assert( PackedValueTraits<half>::kComponentCount == 1 && PackedValueTraits<half3>::kComponentCount == 3 && PackedValueTraits<snorm16x2>::kComponentCount == 2 &&
                       PackedValueTraits<unorm8x4>::kComponentCount == 4 && PackedValueTraits<OctahedralNormal>::kComponentCount == 2 &&
                       PackedValueTraits<RGB10A2>::kComponentCount == 4,
                   "성분 수는 메모리 성분 수와 같다" );

    namespace
    {
        /** @brief 압축 타입 특성 테스트의 도우미입니다. */
        struct PackedValueTraitsTestInternal
        {
            /** @brief `fromFloats( toFloats( value ) )` 가 같은 바이트인지입니다. */
            template <typename T>
            static bool isRestoredByFloats( const T& value )
            {
                const T restored = PackedValueTraits<T>::fromFloats( PackedValueTraits<T>::toFloats( value ) );
                return std::memcmp( &restored, &value, sizeof( T ) ) == 0;
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [PackedValueTraitsTest] 성분 실수로 풀었다 다시 담으면 같은 비트다 — half 는 NaN 을 뺀 모든 비트 패턴, 정규화 정수는 정수 최솟값을 뺀 모든 값
 * @details 예외 둘은 헤더에 적혀 있다. snorm 의 정수 최솟값은 풀면 -1 이라 -최대 로 돌아오고, half NaN 은 신호 NaN 이 조용한 NaN 이 된다.
 */
SW_TEST_CASE( PackedValueTraitsTest, FloatsRestoreEveryStoredValue )
{
    using Test = sw::PackedValueTraitsTestInternal;

    uint32 mismatchCount{ 0 };
    for ( uint32 bits = 0; bits <= 0xFFFFu; ++bits )
    {
        const sw::half value = sw::half::fromBits( static_cast<uint16>( bits ) );
        if ( value.isNaN() )
            continue;
        if ( Test::isRestoredByFloats( value ) == false )
            ++mismatchCount;
    }
    SW_EXPECT_EQUAL( uint32{ 0 }, mismatchCount );

    for ( uint32 raw = 0; raw <= 0xFFFFu; ++raw )
    {
        if ( Test::isRestoredByFloats( sw::unorm16::fromRaw( static_cast<uint16>( raw ) ) ) == false )
            ++mismatchCount;
        const int32 signedRaw = static_cast<int32>( raw ) - 32768;
        if ( signedRaw != -32768 && Test::isRestoredByFloats( sw::snorm16::fromRaw( static_cast<int16>( signedRaw ) ) ) == false )
            ++mismatchCount;
    }
    for ( uint32 raw = 0; raw <= 0xFFu; ++raw )
    {
        if ( Test::isRestoredByFloats( sw::unorm8::fromRaw( static_cast<uint8>( raw ) ) ) == false )
            ++mismatchCount;
        const int32 signedRaw = static_cast<int32>( raw ) - 128;
        if ( signedRaw != -128 && Test::isRestoredByFloats( sw::snorm8::fromRaw( static_cast<int8>( signedRaw ) ) ) == false )
            ++mismatchCount;
    }
    SW_EXPECT_EQUAL( uint32{ 0 }, mismatchCount );

    // 정수 최솟값은 풀면 -1 이고 다시 담으면 -최대 다. 알려진 예외라 값을 못 박아 둔다.
    const sw::snorm16 lowest = sw::snorm16::fromRaw( -32768 );
    SW_EXPECT_EQUAL( int16{ -32767 }, sw::PackedValueTraits<sw::snorm16>::fromFloats( sw::PackedValueTraits<sw::snorm16>::toFloats( lowest ) ).getRaw() );
}

/**
 * @brief [PackedValueTraitsTest] 묶음 · 옥타헤드럴 · RGB10A2 도 성분 순서대로 풀고 같은 비트로 되돌린다
 */
SW_TEST_CASE( PackedValueTraitsTest, ComponentsFollowMemoryOrderAndRestore )
{
    using Test = sw::PackedValueTraitsTestInternal;

    const sw::half3        halves     = sw::half3::fromFloat3( sw::float3{ 0.5f, -2.0f, 8.0f } );
    const sw::PackedFloats halfFloats = sw::PackedValueTraits<sw::half3>::toFloats( halves );
    SW_EXPECT_EQUAL( 0.5f, halfFloats._arrValue[0] );
    SW_EXPECT_EQUAL( -2.0f, halfFloats._arrValue[1] );
    SW_EXPECT_EQUAL( 8.0f, halfFloats._arrValue[2] );
    SW_EXPECT_EQUAL( 0.0f, halfFloats._arrValue[3] );
    SW_EXPECT_TRUE( Test::isRestoredByFloats( halves ) );

    sw::unorm8x4 color{};
    color._arrValue[0]                 = sw::unorm8::fromRaw( 255 );
    color._arrValue[1]                 = sw::unorm8::fromRaw( 51 );
    color._arrValue[3]                 = sw::unorm8::fromRaw( 128 );
    const sw::PackedFloats colorFloats = sw::PackedValueTraits<sw::unorm8x4>::toFloats( color );
    SW_EXPECT_EQUAL( 1.0f, colorFloats._arrValue[0] );
    SW_EXPECT_EQUAL( 0.2f, colorFloats._arrValue[1] );
    SW_EXPECT_EQUAL( 0.0f, colorFloats._arrValue[2] );
    SW_EXPECT_TRUE( Test::isRestoredByFloats( color ) );

    // 옥타헤드럴의 성분은 방향이 아니라 저장된 좌표 (u, v) 다.
    const sw::OctahedralNormal normal{ sw::snorm16::fromRaw( 16384 ), sw::snorm16::fromRaw( -32767 ) };
    const sw::PackedFloats     normalFloats = sw::PackedValueTraits<sw::OctahedralNormal>::toFloats( normal );
    SW_EXPECT_EQUAL( normal._u.toFloat(), normalFloats._arrValue[0] );
    SW_EXPECT_EQUAL( -1.0f, normalFloats._arrValue[1] );
    SW_EXPECT_TRUE( Test::isRestoredByFloats( normal ) );

    SW_EXPECT_TRUE( Test::isRestoredByFloats( sw::RGB10A2::fromRaw( 1023u, 512u, 3u, 2u ) ) );
    SW_EXPECT_TRUE( Test::isRestoredByFloats( sw::RGB10A2::fromPacked( 0xDEADBEEFu ) ) );
    SW_EXPECT_TRUE( Test::isRestoredByFloats( sw::half4::fromFloat4( sw::float4{ 1.0f, -0.0f, 65504.0f, 6.1e-5f } ) ) );
    SW_EXPECT_TRUE( Test::isRestoredByFloats( sw::snorm16x4::fromFloat4( sw::float4{ 1.0f, -1.0f, 0.3f, -0.7f } ) ) );
}

/**
 * @brief [PackedValueTraitsTest] 범위 확인은 경계를 받고 범위 밖 · NaN · 무한대를 거절한다
 * @details 읽는 쪽(글 · 인자 변환)이 `fromFloats` 의 자르기 · 무한대로 가기 전에 부르는 확인이다.
 */
SW_TEST_CASE( PackedValueTraitsTest, RangeCheckAcceptsBoundsAndRejectsEverythingElse )
{
    constexpr float32 kInfinity = std::numeric_limits<float32>::infinity();
    constexpr float32 kNaN      = std::numeric_limits<float32>::quiet_NaN();

    sw::PackedFloats floats{};
    SW_EXPECT_TRUE( sw::isPackedFloatsInRange<sw::unorm8>( floats ) );
    floats._arrValue[0] = 1.0f;
    SW_EXPECT_TRUE( sw::isPackedFloatsInRange<sw::unorm8>( floats ) );
    floats._arrValue[0] = 1.0001f;
    SW_EXPECT_FALSE( sw::isPackedFloatsInRange<sw::unorm8>( floats ) );
    floats._arrValue[0] = -0.0001f;
    SW_EXPECT_FALSE( sw::isPackedFloatsInRange<sw::unorm8>( floats ) );
    floats._arrValue[0] = -1.0f;
    SW_EXPECT_TRUE( sw::isPackedFloatsInRange<sw::snorm16>( floats ) );
    floats._arrValue[0] = -65504.0f;
    SW_EXPECT_TRUE( sw::isPackedFloatsInRange<sw::half>( floats ) );
    floats._arrValue[0] = 65505.0f;
    SW_EXPECT_FALSE( sw::isPackedFloatsInRange<sw::half>( floats ) );
    floats._arrValue[0] = kInfinity;
    SW_EXPECT_FALSE( sw::isPackedFloatsInRange<sw::half>( floats ) );
    floats._arrValue[0] = kNaN;
    SW_EXPECT_FALSE( sw::isPackedFloatsInRange<sw::half>( floats ) );

    // 앞쪽 `kComponentCount` 개만 본다 - 쓰지 않는 칸의 값은 상관없다.
    floats              = sw::PackedFloats{};
    floats._arrValue[0] = 0.5f;
    floats._arrValue[3] = 9.0f;
    SW_EXPECT_TRUE( sw::isPackedFloatsInRange<sw::unorm16x2>( floats ) );
    SW_EXPECT_FALSE( sw::isPackedFloatsInRange<sw::unorm8x4>( floats ) );
}
