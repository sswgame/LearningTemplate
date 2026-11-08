#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Lighting/ShadowFilter.h"

// PCSS 커널 크기 식(셰이더 lighting.hlsli 의 Shadow::sampleCascade · LightData::sampleLocalShadow 와 같은 식)을 GPU 없이 본다. 픽셀은 시나리오 game.empty.shadow_pcss 가 본다.

/**
 * @brief [ShadowFilterTest] 방향광 반그림자 배율은 깊이 구간 × 2 tan(시직경 / 2) ÷ 텍셀 길이다 — 원천이 없으면 0 이다
 */
SW_TEST_CASE( ShadowFilterTest, DirectionalPenumbraScaleFollowsSourceAngle )
{
    const float32 angle = sw::MathUtil::toRadian( 0.5357f );
    const float32 scale = sw::ShadowFilter::computeDirectionalPenumbraScale( 40.0f, angle, 0.02f );
    SW_EXPECT_NEAR( scale, 40.0f * 2.0f * sw::MathUtil::tan( angle * 0.5f ) / 0.02f, 1e-3f );
    SW_EXPECT_NEAR( sw::ShadowFilter::computeDirectionalPenumbraScale( 40.0f, 0.0f, 0.02f ), 0.0f, 1e-9f );
    // 텍셀 길이 0(볼륨이 무너진 투영)은 0 으로 나누지 않는다.
    SW_EXPECT_NEAR( sw::ShadowFilter::computeDirectionalPenumbraScale( 40.0f, angle, 0.0f ), 0.0f, 1e-9f );
}

/**
 * @brief [ShadowFilterTest] 방향광 커널은 가리개에서 멀수록 넓고, 캐스케이드 배율만큼 넓어지며, 하한 · 상한 텍셀 안이다
 */
SW_TEST_CASE( ShadowFilterTest, DirectionalKernelGrowsWithBlockerDistanceAndClamps )
{
    using Filter           = sw::ShadowFilter;
    const float32 touching = Filter::computeDirectionalKernelRadius( 0.50f, 0.49f, 200.0f, 1.0f );
    const float32 distant  = Filter::computeDirectionalKernelRadius( 0.50f, 0.45f, 200.0f, 1.0f );
    const float32 zoomed   = Filter::computeDirectionalKernelRadius( 0.50f, 0.49f, 200.0f, 2.0f );
    SW_EXPECT_NEAR( touching, 0.5f * 0.01f * 200.0f, 1e-4f );
    SW_EXPECT_TRUE( distant > touching );
    SW_EXPECT_NEAR( zoomed, touching * 2.0f, 1e-4f );
    // 접한 가리개(차 0)는 하한, 아주 먼 가리개는 상한이다. 받는 점 뒤의 깊이(음수 차)도 하한이다.
    SW_EXPECT_NEAR( Filter::computeDirectionalKernelRadius( 0.5f, 0.5f, 200.0f, 1.0f ), Filter::kMinKernelRadiusTexels, 1e-6f );
    SW_EXPECT_NEAR( Filter::computeDirectionalKernelRadius( 0.9f, 0.0f, 200.0f, 4.0f ), Filter::kMaxKernelRadiusTexels, 1e-6f );
    SW_EXPECT_NEAR( Filter::computeDirectionalKernelRadius( 0.4f, 0.5f, 200.0f, 1.0f ), Filter::kMinKernelRadiusTexels, 1e-6f );
}

/**
 * @brief [ShadowFilterTest] 점광 · 스폿 커널은 닮은 삼각형이다 — 가리개가 받는 면의 절반 거리면 반그림자 반폭이 원천 반지름과 같다
 */
SW_TEST_CASE( ShadowFilterTest, LocalKernelFollowsSimilarTriangles )
{
    using Filter               = sw::ShadowFilter;
    const float32 texelAngle   = 0.002f; // 거리 1 m 에서 텍셀 2 mm
    const float32 sourceRadius = 0.01f;
    // 받는 면 4 m, 가리개 2 m: 반폭 = 0.01 · (4 − 2) / 2 = 0.01 m, 받는 면 텍셀 = 0.008 m → 1.25 텍셀.
    SW_EXPECT_NEAR( Filter::computeLocalKernelRadius( 4.0f, 2.0f, sourceRadius, texelAngle ), 1.25f, 1e-4f );
    // 원천이 클수록 넓고 상한에서 멈춘다.
    SW_EXPECT_NEAR( Filter::computeLocalKernelRadius( 4.0f, 2.0f, 1.0f, texelAngle ), Filter::kMaxKernelRadiusTexels, 1e-6f );
    // 원천 0 은 하한(딱딱한 그림자도 텍셀 계단은 거른다).
    SW_EXPECT_NEAR( Filter::computeLocalKernelRadius( 4.0f, 2.0f, 0.0f, texelAngle ), Filter::kMinKernelRadiusTexels, 1e-6f );
}

/**
 * @brief [ShadowFilterTest] 원근 NDC 깊이를 되돌리면 시선 깊이다 — 가까운 면은 0, 먼 면은 1 이다
 */
SW_TEST_CASE( ShadowFilterTest, LinearizePerspectiveDepthInvertsProjection )
{
    const float32 nearPlane = 0.05f;
    const float32 farPlane  = 6.0f;
    SW_EXPECT_NEAR( sw::ShadowFilter::linearizePerspectiveDepth( 0.0f, nearPlane, farPlane ), nearPlane, 1e-5f );
    SW_EXPECT_NEAR( sw::ShadowFilter::linearizePerspectiveDepth( 1.0f, nearPlane, farPlane ), farPlane, 1e-3f );
    const float32 viewDepth = 2.5f;
    const float32 ndc       = farPlane / ( farPlane - nearPlane ) * ( 1.0f - nearPlane / viewDepth );
    SW_EXPECT_NEAR( sw::ShadowFilter::linearizePerspectiveDepth( ndc, nearPlane, farPlane ), viewDepth, 1e-3f );
}

/**
 * @brief [ShadowFilterTest] 셰이더 상수는 (품질, 방향광 배율, 상한, 하한) 순서다 — PCF 는 0 이라 셰이더가 3x3 PCF 를 고른다
 */
SW_TEST_CASE( ShadowFilterTest, ShaderParamsCarryModeAndKernelBounds )
{
    const sw::float4 pcf  = sw::ShadowFilter::makeShaderParams( sw::ShadowFilterMode::Pcf3x3, 12.0f );
    const sw::float4 pcss = sw::ShadowFilter::makeShaderParams( sw::ShadowFilterMode::Pcss, 12.0f );
    SW_EXPECT_NEAR( pcf._x, 0.0f, 1e-9f );
    SW_EXPECT_NEAR( pcss._x, 1.0f, 1e-9f );
    SW_EXPECT_NEAR( pcss._y, 12.0f, 1e-9f );
    SW_EXPECT_NEAR( pcss._z, sw::ShadowFilter::kMaxKernelRadiusTexels, 1e-9f );
    SW_EXPECT_NEAR( pcss._w, sw::ShadowFilter::kMinKernelRadiusTexels, 1e-9f );
}
