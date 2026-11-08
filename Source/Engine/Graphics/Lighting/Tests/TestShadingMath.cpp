// ShadingMathTest — 셰이딩 모델 스칼라 식(shading_math.hlsli, C++ 와 같은 파일)의 물리 성질과 경계 · 극한값을 잰다(nogpu).
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Lighting/ShadingMath.h"

using namespace sw;
using shadermath::ShadingMath;

namespace
{
    struct ShadingMathTestUtil
    {
        static constexpr int32 kThetaSteps = 512;

        /** @brief 반구 위의 ∫ f(cosθ) dω 를 중점 규칙으로 잽니다(f 가 방위각에 무관할 때). */
        template <typename TFunction>
        [[nodiscard]] static float64 integrateHemisphere( TFunction pfnValue )
        {
            float64 sum = 0.0;
            for ( int32 step = 0; step < kThetaSteps; ++step )
            {
                const float64 theta = ( step + 0.5 ) / kThetaSteps * MathUtil::kHalfPi;
                sum += pfnValue( static_cast<float32>( std::cos( theta ) ) ) * std::sin( theta ) * ( MathUtil::kHalfPi / kThetaSteps );
            }
            return sum * 2.0 * MathUtil::kPi;
        }

        /** @brief 구 전체의 ∫ f(cosθ) dω 입니다. */
        template <typename TFunction>
        [[nodiscard]] static float64 integrateSphere( TFunction pfnValue )
        {
            float64 sum = 0.0;
            for ( int32 step = 0; step < kThetaSteps * 2; ++step )
            {
                const float64 theta = ( step + 0.5 ) / ( kThetaSteps * 2 ) * MathUtil::kPi;
                sum += pfnValue( static_cast<float32>( std::cos( theta ) ) ) * std::sin( theta ) * ( MathUtil::kPi / ( kThetaSteps * 2 ) );
            }
            return sum * 2.0 * MathUtil::kPi;
        }

        /** @brief 한 주기(주기 1)를 고르게 나눈 표본의 평균입니다. */
        [[nodiscard]] static float64 computeAverageCoverage( float32 halfWidth, float32 footprint )
        {
            constexpr int32 kSamples = 4096;
            float64         sum      = 0.0;
            for ( int32 sample = 0; sample < kSamples; ++sample )
                sum += ShadingMath::computeLineCoverage( ( sample + 0.5f ) / kSamples, halfWidth, footprint );
            return sum / kSamples;
        }
    };
} // namespace

/** @brief [ShadingMathTest] GGX · 시엔(Charlie) 분포가 정규화되어 있다 — ∫ D cosθh dω = 1(엔진 단위 D·π 를 되돌려 잰다) */
SW_TEST_CASE( ShadingMathTest, DistributionsAreNormalized )
{
    for ( const float32 alpha : { 0.2f, 0.5f, 0.9f } )
    {
        const float64 ggx = ShadingMathTestUtil::integrateHemisphere( [alpha]( float32 cosine )
        { return ShadingMath::computeDistributionGGXTimesPi( cosine, alpha ) / MathUtil::kPi * cosine; } );
        SW_EXPECT_NEAR( 1.0, ggx, 0.02 );
    }
    for ( const float32 sheenAlpha : { 0.1f, 0.4f, 1.0f } )
    {
        const float64 charlie = ShadingMathTestUtil::integrateHemisphere( [sheenAlpha]( float32 cosine )
        { return ShadingMath::computeDistributionCharlieTimesPi( cosine, sheenAlpha ) / MathUtil::kPi * cosine; } );
        SW_EXPECT_NEAR( 1.0, charlie, 0.02 );
    }
}

/** @brief [ShadingMathTest] α 가 같은 비등방 GGX 는 등방 GGX 와 같다(비단 · 헤어라인 금속의 시작값) */
SW_TEST_CASE( ShadingMathTest, AnisotropicGGXMatchesIsotropicAtZeroAnisotropy )
{
    const float32 alpha          = 0.3f;
    const float32 alphaTangent   = ShadingMath::computeAlphaTangent( alpha, 0.0f );
    const float32 alphaBitangent = ShadingMath::computeAlphaBitangent( alpha, 0.0f );
    SW_EXPECT_NEAR( alpha, alphaTangent, 1e-6 );
    // 반각 벡터 (sin θ cos φ, sin θ sin φ, cos θ) 에서 같은 값
    const float32 normalDotHalf = 0.9f;
    const float32 sinHalf       = std::sqrt( 1.0f - normalDotHalf * normalDotHalf );
    const float32 anisotropic   = ShadingMath::computeDistributionAnisotropicGGXTimesPi( normalDotHalf, sinHalf * 0.6f, sinHalf * 0.8f, alphaTangent, alphaBitangent );
    SW_EXPECT_NEAR( ShadingMath::computeDistributionGGXTimesPi( normalDotHalf, alpha ), anisotropic, 1e-3 );
    // 비등방이 켜지면 탄젠트 쪽으로 늘어난다 — 탄젠트 방향 반각에서 값이 커진다
    const float32 stretched = ShadingMath::computeDistributionAnisotropicGGXTimesPi( normalDotHalf, sinHalf, 0.0f, ShadingMath::computeAlphaTangent( alpha, 0.8f ),
                                                                                     ShadingMath::computeAlphaBitangent( alpha, 0.8f ) );
    SW_EXPECT_TRUE( stretched > anisotropic );
}

/** @brief [ShadingMathTest] 감싼 확산은 빛을 늘리거나 줄이지 않는다 — 구 전체 적분이 램버트(π)와 같다 */
SW_TEST_CASE( ShadingMathTest, WrappedDiffuseConservesEnergy )
{
    for ( const float32 wrap : { 0.0f, 0.3f, 0.8f } )
    {
        const float64 integral = ShadingMathTestUtil::integrateSphere( [wrap]( float32 cosine )
        { return ShadingMath::computeWrappedDiffuse( cosine, wrap ); } );
        SW_EXPECT_NEAR( MathUtil::kPi, integral, 0.01 );
    }
    SW_EXPECT_NEAR( 0.5, ShadingMath::computeWrappedDiffuse( 0.5f, 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeWrappedDiffuse( -0.2f, 0.0f ), 1e-6 );
    SW_EXPECT_TRUE( ShadingMath::computeWrappedDiffuse( -0.2f, 0.5f ) > 0.0f ); // 감쌈: 그늘 쪽으로 빛이 번진다
    // 투과는 뒤에서 오는 빛만 받는다
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeDiffuseTransmission( 1.0f, 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeDiffuseTransmission( -1.0f, 0.0f ), 1e-6 );
}

/** @brief [ShadingMathTest] 슐릭 프레넬과 굴절률 F0 — 수직은 F0, 스치면 1, 유리(1.5)는 0.04 */
SW_TEST_CASE( ShadingMathTest, FresnelEndpoints )
{
    SW_EXPECT_NEAR( 0.04, ShadingMath::computeDielectricF0( 1.5f ), 1e-4 );
    SW_EXPECT_NEAR( 0.04, ShadingMath::computeFresnelSchlick( 0.04f, 1.0f ), 1e-6 );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeFresnelSchlick( 0.04f, 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeDielectricF0( 1.0f ), 1e-6 );
}

/** @brief [ShadingMathTest] 박막 두께 0 은 막 없는 바탕의 프레넬이고, 반사율은 0..1 이며 두께에 따라 색이 바뀐다 */
SW_TEST_CASE( ShadingMathTest, ThinFilmReducesToBaseFresnelAndIridesces )
{
    const float32 baseIor = 1.68f; // 진주층(아라고나이트)
    SW_EXPECT_NEAR( ShadingMath::computeDielectricF0( baseIor ), ShadingMath::computeThinFilmReflectance( 1.0f, 0.0f, 1.33f, baseIor, 550.0f ), 1e-4 );
    for ( float32 thickness = 0.0f; thickness <= 1000.0f; thickness += 50.0f )
    {
        for ( const float32 cosine : { 1.0f, 0.5f, 0.1f } )
        {
            const float32 reflectance = ShadingMath::computeThinFilmReflectance( cosine, thickness, 1.33f, baseIor, 532.0f );
            SW_EXPECT_TRUE( reflectance >= 0.0f && reflectance <= 1.0f );
        }
    }
    const float32 red  = ShadingMath::computeThinFilmReflectance( 1.0f, 250.0f, 1.33f, baseIor, 650.0f );
    const float32 blue = ShadingMath::computeThinFilmReflectance( 1.0f, 250.0f, 1.33f, baseIor, 450.0f );
    SW_EXPECT_TRUE( std::abs( red - blue ) > 0.005f );
}

/** @brief [ShadingMathTest] 상자로 거른 선 덮임 — 발자국이 칸 이상이면 덮임 비율 2w, 작으면 계단, 그 사이는 이어진다 */
SW_TEST_CASE( ShadingMathTest, LineCoverageIsStableAcrossFootprints )
{
    const float32 halfWidth = 0.05f;
    SW_EXPECT_NEAR( 2.0f * halfWidth, ShadingMath::computeLineCoverage( 0.37f, halfWidth, 1.0f ), 1e-4 );
    SW_EXPECT_NEAR( 2.0f * halfWidth, ShadingMath::computeLineCoverage( 0.81f, halfWidth, 3.0f ), 1e-4 );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeLineCoverage( 0.0f, halfWidth, 0.001f ), 1e-4 );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeLineCoverage( 0.5f, halfWidth, 0.001f ), 1e-4 );
    for ( const float32 footprint : { 0.002f, 0.03f, 0.2f, 0.7f } )
        SW_EXPECT_NEAR( 2.0f * halfWidth, ShadingMathTestUtil::computeAverageCoverage( halfWidth, footprint ), 2e-3 );
}

/** @brief [ShadingMathTest] 시엔 가시성은 양수이고 유한하다(털의 머리결 엽은 Marschner — TestHairShading 이 본다) */
SW_TEST_CASE( ShadingMathTest, SheenVisibilityIsPositive )
{
    SW_EXPECT_TRUE( ShadingMath::computeVisibilityNeubelt( 0.0f, 0.0f ) > 0.0f );
}

/** @brief [ShadingMathTest] 홍채 시차 — 각막 굴절은 기울기를 줄이고 굴절률 1 이면 그대로다 */
SW_TEST_CASE( ShadingMathTest, IrisParallaxFollowsSnell )
{
    const float32 sinIncident = 0.5f;
    const float32 tanIncident = sinIncident / std::sqrt( 1.0f - sinIncident * sinIncident );
    SW_EXPECT_NEAR( tanIncident, ShadingMath::computeRefractedTangent( sinIncident, 1.0f ), 1e-4 );
    SW_EXPECT_TRUE( ShadingMath::computeRefractedTangent( sinIncident, 1.376f ) < tanIncident );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeRefractedTangent( 0.0f, 1.376f ), 1e-6 );
}

/** @brief [ShadingMathTest] 동공 — 확장은 반지름을 정하고, 밝을수록 좁으며, 모양마다 가운데 · 가장자리가 안 · 밖이다 */
SW_TEST_CASE( ShadingMathTest, PupilShapesAndDilation )
{
    SW_EXPECT_NEAR( 0.2, ShadingMath::computePupilRadius( 0.2f, 0.6f, 0.0f ), 1e-6 );
    SW_EXPECT_NEAR( 0.6, ShadingMath::computePupilRadius( 0.2f, 0.6f, 1.0f ), 1e-6 );
    SW_EXPECT_NEAR( 1.0, ShadingMath::computeDilationFromLuminance( 0.001f, 0.01f, 1000.0f ), 1e-6 );
    SW_EXPECT_NEAR( 0.0, ShadingMath::computeDilationFromLuminance( 5000.0f, 0.01f, 1000.0f ), 1e-6 );
    SW_EXPECT_TRUE( ShadingMath::computeDilationFromLuminance( 1.0f, 0.01f, 1000.0f ) > ShadingMath::computeDilationFromLuminance( 100.0f, 0.01f, 1000.0f ) );

    for ( float32 shape = 0.0f; shape < shadermath::kPupilShapeCount; shape += 1.0f )
    {
        // 반지름 0.95 의 열린 동공: 홍채 가장자리 근처는 밖이다
        SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shape, 0.95f, 0.0f, 0.2f, 0.6f, 0.5f ) > 0.0f );
        SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shape, 0.0f, 0.95f, 0.2f, 0.6f, 0.5f ) > 0.0f );
    }
    // 둥근 · 틈 · 막대는 가운데가 안이고, W 는 가운데가 홍채(혀) 쪽이라 가운데 위가 안이다.
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeRound, 0.0f, 0.0f, 0.2f, 0.6f, 0.5f ) < 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeVerticalSlit, 0.0f, 0.0f, 0.2f, 0.6f, 0.0f ) < 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeHorizontalBar, 0.0f, 0.0f, 0.2f, 0.6f, 0.0f ) < 0.0f );
    // 세로 틈: 좁으면 옆은 밖, 위아래는 안 — 넓게 열리면 옆도 안
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeVerticalSlit, 0.2f, 0.0f, 0.2f, 0.6f, 0.0f ) > 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeVerticalSlit, 0.0f, 0.5f, 0.2f, 0.6f, 0.0f ) < 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeVerticalSlit, 0.2f, 0.0f, 0.2f, 0.6f, 1.0f ) < 0.0f );
    // 가로 막대: 좁으면 위는 밖, 옆은 안
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeHorizontalBar, 0.0f, 0.2f, 0.2f, 0.6f, 0.0f ) > 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeHorizontalBar, 0.5f, 0.0f, 0.2f, 0.6f, 0.0f ) < 0.0f );
    // W 자: 골짜기(x = ±0.3, 곡선 아래)는 안이고 위쪽 끝은 밖
    const float32 valleyY = shadermath::kPupilShapeCephalopodCurve * ( 0.0f - 0.125f * 0.36f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeCephalopod, 0.3f, valleyY, 0.2f, 0.6f, 0.5f ) < 0.0f );
    SW_EXPECT_TRUE( ShadingMath::computePupilDistance( shadermath::kPupilShapeCephalopod, 0.3f, 0.6f, 0.2f, 0.6f, 0.5f ) > 0.0f );
}
