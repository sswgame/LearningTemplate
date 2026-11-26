#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/PackedValueTraits.h"
#include "Core/Math/Radians.h"
#include "Core/TestFramework/TestFramework.h"

namespace sw
{
    // 도 값과 라디안 값은 이름 있는 팩토리로만 들어온다 — float32 에서 암시로 만들지도, float32 로 암시로 풀리지도 않는다.
    static_assert( std::is_constructible_v<Radians, float32> == false, "Radians 는 float32 에서 암시로 만들지 않는다" );
    static_assert( std::is_convertible_v<Radians, float32> == false, "Radians 는 float32 로 암시로 풀리지 않는다" );
    static_assert( kIsPackedValue<Radians> && PackedValueTraits<Radians>::kComponentCount == 1, "Radians 의 바깥 표현은 성분 하나(도)다" );
    static_assert( std::is_constructible_v<EulerAngles, float3> == false && std::is_convertible_v<EulerAngles, float3> == false, "EulerAngles 는 float3 에서 암시로 만들지도 풀리지도 않는다" );
    static_assert( kIsPackedValue<EulerAngles> && PackedValueTraits<EulerAngles>::kComponentCount == 3, "EulerAngles 의 바깥 표현은 도 셋이다" );
    static_assert( Radians::fromDegrees( 180.0f ).radians() == static_cast<float32>( MathUtil::kPi64 ), "180 도는 float32 π 다" );
} // namespace sw

/**
 * @brief [RadiansTest] 도 · 라디안 팩토리와 산술 · 비교가 라디안 값 하나로 계산된다
 */
SW_TEST_CASE( RadiansTest, FactoriesAndArithmeticComputeOnRadians )
{
    const sw::Radians right = sw::Radians::fromDegrees( 90.0f );
    SW_EXPECT_NEAR( sw::MathUtil::kHalfPi, right.radians(), 1e-6f );
    SW_EXPECT_NEAR( 90.0f, right.degrees(), 1e-4f );
    SW_EXPECT_EQUAL( 0.0f, sw::Radians{}.radians() );

    const sw::Radians half = sw::Radians::fromRadians( 0.5f );
    SW_EXPECT_NEAR( 1.0f, ( half + half ).radians(), 1e-6f );
    SW_EXPECT_NEAR( 0.0f, ( half - half ).radians(), 1e-6f );
    SW_EXPECT_NEAR( -0.5f, ( -half ).radians(), 1e-6f );
    SW_EXPECT_NEAR( 1.5f, ( half * 3.0f ).radians(), 1e-6f );
    SW_EXPECT_NEAR( 1.5f, ( 3.0f * half ).radians(), 1e-6f );
    SW_EXPECT_NEAR( 0.25f, ( half / 2.0f ).radians(), 1e-6f );
    SW_EXPECT_TRUE( half < right && right > half && half <= half && half >= half && half != right );
    SW_EXPECT_TRUE( sw::MathUtil::max( half, right ) == right );

    sw::Radians accumulated{};
    accumulated += right;
    accumulated -= half;
    SW_EXPECT_NEAR( sw::MathUtil::kHalfPi - 0.5f, accumulated.radians(), 1e-6f );
}

/**
 * @brief [RadiansTest] 바깥 표현(성분)은 도이고, 손으로 쓴 짧은 도 값은 라디안으로 담았다 풀어도 같은 글이다
 * @details float32 로만 자르면 `2.5` 도가 `2.4999998` 로 다시 저장됐다 — 같은 라디안으로 돌아오는 가장 짧은 십진수를 고른다.
 */
SW_TEST_CASE( RadiansTest, ComponentIsDegreesAndKeepsShortAuthoredValues )
{
    using Traits = sw::PackedValueTraits<sw::Radians>;
    for ( const float32 authored : { 0.0f, 0.5f, 0.6f, 2.5f, 6.0f, 25.0f, 28.64789f, 40.0f, 45.0f, 60.0f, 90.0f, 180.0f, -135.0f, 359.5f } )
    {
        sw::PackedFloats floats{};
        floats._arrValue[0]       = authored;
        const sw::Radians angle   = Traits::fromFloats( floats );
        const float32     written = Traits::toFloats( angle )._arrValue[0];
        SW_EXPECT_EQUAL( authored, written );
        SW_EXPECT_TRUE( sw::Radians::fromDegrees( written ) == angle );
    }
}

/**
 * @brief [RadiansTest] 아무 라디안 값이든 도 성분으로 풀었다 담으면 라디안 float32 한 칸 안이다(도 float32 가 라디안 float32 보다 거칠 수 있다)
 */
SW_TEST_CASE( RadiansTest, ComponentRoundTripStaysWithinOneStep )
{
    using Traits = sw::PackedValueTraits<sw::Radians>;
    for ( const float32 radians : { 3.14159f, 1.5707963f, 1.0f, 0.4363323f, -2.0f, 6.2831853f, 1e-6f } )
    {
        const sw::Radians angle    = sw::Radians::fromRadians( radians );
        const sw::Radians restored = Traits::fromFloats( Traits::toFloats( angle ) );
        const float32     spacing  = std::nextafter( sw::MathUtil::abs( radians ), sw::MathUtil::kMaxFloat ) - sw::MathUtil::abs( radians );
        SW_EXPECT_TRUE( sw::MathUtil::abs( restored.radians() - radians ) <= spacing );
    }
}

/** @brief [RadiansTest] 오일러 회전의 성분은 도 셋(피치, 요, 롤)이고 손으로 쓴 도가 그대로 돌아오며, 코드로 꺼낼 때만 단위를 고른다 */
SW_TEST_CASE( RadiansTest, EulerAnglesAreThreeDegreeComponents )
{
    using Traits = sw::PackedValueTraits<sw::EulerAngles>;
    sw::PackedFloats floats{};
    floats._arrValue[0]          = 0.0f;
    floats._arrValue[1]          = -25.0f;
    floats._arrValue[2]          = 2.5f;
    const sw::EulerAngles angles = Traits::fromFloats( floats );
    SW_EXPECT_NEAR( -25.0f * sw::MathUtil::kDegreeToRadian, angles.radians()._y, 1e-6f );
    SW_EXPECT_NEAR( 2.5f, angles.degrees()._z, 1e-4f );
    SW_EXPECT_EQUAL( -25.0f, Traits::toFloats( angles )._arrValue[1] );
    SW_EXPECT_EQUAL( 2.5f, Traits::toFloats( angles )._arrValue[2] );

    sw::EulerAngles sum = sw::EulerAngles::fromRadians( sw::float3{ 0.5f, 0.0f, 0.0f } );
    sum += sw::EulerAngles::fromRadians( sw::float3{ 1.0f, 0.25f, 0.0f } ) * 0.5f;
    SW_EXPECT_NEAR( 1.0f, sum.radians()._x, 1e-6f );
    SW_EXPECT_NEAR( 0.125f, sum.radians()._y, 1e-6f );
    SW_EXPECT_TRUE( sum != angles && angles == Traits::fromFloats( Traits::toFloats( angles ) ) );
}
