#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/SphericalHarmonics.h"
#include "Core/Math/VectorMath.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    namespace
    {
        /** @brief 구면 조화 함수 테스트의 도우미입니다. */
        struct SphericalHarmonicsTestInternal
        {
            static constexpr uint32 kSampleCount = 20000;
            static constexpr uint32 kSeed        = 0x9E3779B9u;

            /** @brief 균일 구 샘플로 "방향 @p toLight 쪽 반구만 복사휘도 @p radiance" 인 하늘을 투영합니다. */
            static SH9Color projectHemisphere( const float3& toLight, const float3& radiance )
            {
                SH9Color      result{};
                uint32        state  = kSeed;
                const float32 weight = 4.0f * MathUtil::kPi / static_cast<float32>( kSampleCount );
                for ( uint32 sampleIndex = 0; sampleIndex < kSampleCount; ++sampleIndex )
                {
                    const float3 direction = SH9Color::makeUniformSphereDirection( state );
                    if ( direction.dot( toLight ) > 0.0f )
                        result.addSample( direction, radiance, weight );
                }
                return result;
            }

            /** @brief 셰이더 `swEvaluateSphericalHarmonics` 와 같은 식으로 GPU 원소를 평가합니다(채널 하나). */
            static float32 evaluateGPUChannel( const float4 ( &arrElement )[SH9Color::kGPUFloat4Count], uint32 channel, const float3& normal )
            {
                const float4& linear    = arrElement[channel];
                const float4& quadratic = arrElement[3 + channel];
                const float32 arrSquare[3]{ arrElement[6]._x, arrElement[6]._y, arrElement[6]._z };
                const float32 linearTerm    = linear._x * normal._x + linear._y * normal._y + linear._z * normal._z + linear._w;
                const float32 quadraticTerm = quadratic._x * normal._x * normal._y + quadratic._y * normal._y * normal._z + quadratic._z * normal._z * normal._z +
                                              quadratic._w * normal._x * normal._z;
                return linearTerm + quadraticTerm + arrSquare[channel] * ( normal._x * normal._x - normal._y * normal._y );
            }
        };
    } // namespace
} // namespace sw

/**
 * @brief [SphericalHarmonicsTest] 한 색 하늘은 어느 방향에서나 같은 복사휘도와 조도(π × 복사휘도)를 준다
 */
SW_TEST_CASE( SphericalHarmonicsTest, ConstantSkyIsTheSameInEveryDirection )
{
    const sw::float3   sky{ 0.2f, 0.4f, 0.8f };
    const sw::SH9Color color       = sw::SH9Color::makeConstant( sky );
    const sw::float3   arrNormal[] = {
        {1.0f, 0.0f,  0.0f},
        {0.0f, 1.0f,  0.0f},
        {0.0f, 0.0f, -1.0f},
        sw::float3{1.0f, 1.0f,  1.0f}
            .normalize()
    };
    for ( const sw::float3& normal : arrNormal )
    {
        const sw::float3 radiance   = color.evaluateRadiance( normal );
        const sw::float3 irradiance = color.evaluateIrradiance( normal );
        SW_EXPECT_NEAR_EQUAL( sky._x, radiance._x, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( sky._z, radiance._z, 1e-4f );
        SW_EXPECT_NEAR_EQUAL( sky._y * sw::MathUtil::kPi, irradiance._y, 1e-3f );
    }
}

/**
 * @brief [SphericalHarmonicsTest] 한 방향에서 오는 빛은 그 방향을 향한 면에서 조도가 가장 크고, 반대쪽은 거의 0 이다
 */
SW_TEST_CASE( SphericalHarmonicsTest, DirectionalLightPeaksTowardTheLight )
{
    const sw::float3   toLight = sw::float3{ 0.3f, 0.8f, -0.5f }.normalize();
    const sw::SH9Color color   = sw::SH9Color::makeDirectional( toLight, sw::float3{ 1.0f, 1.0f, 1.0f } );
    const float32      facing  = color.evaluateIrradiance( toLight )._x;
    const float32      away    = color.evaluateIrradiance( toLight * -1.0f )._x;
    const float32      side    = color.evaluateIrradiance( toLight.cross( sw::float3{ 0.0f, 0.0f, 1.0f } ).normalize() )._x;
    SW_EXPECT_TRUE( facing > side );
    SW_EXPECT_TRUE( side > away );
    // 2차 SH 의 클램프 코사인 근사는 정점에서 해석해(1)의 몇 퍼센트 안이고 반대쪽은 거의 0 이다(Ramamoorthi-Hanrahan 2001).
    SW_EXPECT_NEAR_EQUAL( 1.0f, facing, 0.1f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, away, 0.1f );
}

/**
 * @brief [SphericalHarmonicsTest] 반구 하늘을 균일 샘플로 투영해 평가한 조도가 해석해와 허용 오차 안이다
 * @details 위 반구만 복사휘도 1 인 하늘에서 노멀이 위면 조도는 π, 옆이면 π/2, 아래면 0 이다. 2차 SH 는 반구의 계단을 매끄럽게 펴므로 오차를 넉넉히 둔다.
 */
SW_TEST_CASE( SphericalHarmonicsTest, ProjectedHemisphereMatchesTheAnalyticIrradiance )
{
    using Internal = sw::SphericalHarmonicsTestInternal;
    const sw::float3   up{ 0.0f, 1.0f, 0.0f };
    const sw::SH9Color color = Internal::projectHemisphere( up, sw::float3{ 1.0f, 1.0f, 1.0f } );
    const float32      pi    = sw::MathUtil::kPi;
    SW_EXPECT_NEAR_EQUAL( pi, color.evaluateIrradiance( up )._x, pi * 0.08f );
    SW_EXPECT_NEAR_EQUAL( pi * 0.5f, color.evaluateIrradiance( sw::float3{ 1.0f, 0.0f, 0.0f } )._x, pi * 0.08f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, color.evaluateIrradiance( sw::float3{ 0.0f, -1.0f, 0.0f } )._x, pi * 0.08f );
}

/**
 * @brief [SphericalHarmonicsTest] 같은 씨앗의 투영은 늘 같은 계수이고, 둘을 더하면 평가도 더해진다
 */
SW_TEST_CASE( SphericalHarmonicsTest, ProjectionIsDeterministicAndAdditive )
{
    using Internal = sw::SphericalHarmonicsTestInternal;
    const sw::float3   up{ 0.0f, 1.0f, 0.0f };
    const sw::SH9Color first  = Internal::projectHemisphere( up, sw::float3{ 1.0f, 0.5f, 0.25f } );
    const sw::SH9Color second = Internal::projectHemisphere( up, sw::float3{ 1.0f, 0.5f, 0.25f } );
    for ( uint32 index = 0; index < sw::SH9Color::kCoefficientCount; ++index )
    {
        SW_EXPECT_EQUAL( first._arrCoefficient[index], second._arrCoefficient[index] );
    }
    sw::SH9Color       sum = first;
    const sw::SH9Color sky = sw::SH9Color::makeConstant( sw::float3{ 0.1f, 0.1f, 0.1f } );
    sum.add( sky );
    const sw::float3 normal = sw::float3{ 0.2f, 0.9f, 0.1f }.normalize();
    SW_EXPECT_NEAR_EQUAL( first.evaluateIrradiance( normal )._y + sky.evaluateIrradiance( normal )._y, sum.evaluateIrradiance( normal )._y, 1e-4f );
}

/**
 * @brief [SphericalHarmonicsTest] GPU 원소를 셰이더 식으로 평가하면 조도 / π 와 같다
 * @details 셰이더 `swEvaluateSphericalHarmonics`(lighting.hlsli)와 C++ `makeGPUElement` 의 상수 접기가 어긋나면 프로브가 엉뚱한 밝기로 비춘다.
 */
SW_TEST_CASE( SphericalHarmonicsTest, GPUElementEvaluatesToIrradianceOverPi )
{
    using Internal     = sw::SphericalHarmonicsTestInternal;
    sw::SH9Color color = Internal::projectHemisphere( sw::float3{ 0.3f, 0.9f, 0.2f }.normalize(), sw::float3{ 0.9f, 0.6f, 0.3f } );
    color.add( sw::SH9Color::makeDirectional( sw::float3{ -0.5f, 0.2f, 0.8f }, sw::float3{ 0.4f, 0.8f, 1.2f } ) );
    sw::float4 arrElement[sw::SH9Color::kGPUFloat4Count];
    color.makeGPUElement( arrElement );
    SW_EXPECT_NEAR_EQUAL( 0.0f, arrElement[6]._w, 0.0f );
    const sw::float3 arrNormal[] = {
        { 1.0f,  0.0f,  0.0f},
        { 0.0f, -1.0f,  0.0f},
        sw::float3{ 0.3f,  0.5f, -0.8f}
            .normalize(),
        sw::float3{-0.7f,  0.1f,  0.7f}
            .normalize()
    };
    for ( const sw::float3& normal : arrNormal )
    {
        const sw::float3 expected = color.evaluateIrradiance( normal ) * ( 1.0f / sw::MathUtil::kPi );
        SW_EXPECT_NEAR_EQUAL( expected._x, Internal::evaluateGPUChannel( arrElement, 0, normal ), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( expected._y, Internal::evaluateGPUChannel( arrElement, 1, normal ), 1e-4f );
        SW_EXPECT_NEAR_EQUAL( expected._z, Internal::evaluateGPUChannel( arrElement, 2, normal ), 1e-4f );
    }
}
