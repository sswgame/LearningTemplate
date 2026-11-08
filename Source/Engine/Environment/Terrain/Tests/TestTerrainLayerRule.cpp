// 지형 레이어 규칙(경사 · 고도 띠)과 높이 섞기 — CPU 식. 셰이더(terrain.hlsl)는 여기서 돌지 않으므로 같은 식의 닫힌 값을 박는다. 디바이스 없음(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Placement/SurfaceLayerWeights.h"
#include "Engine/Environment/Terrain/HeightfieldAsset.h"
#include "Engine/Environment/Terrain/TerrainHeightfield.h"
#include "Engine/Environment/Terrain/TerrainLayerRule.h"

using namespace sw;

/**
 * @brief [TerrainLayerRuleTest] 띠는 끝값까지 1, 바깥으로 페이드 폭만큼 0 까지 내려가는 사다리꼴이다 · 경사는 노멀 y 의 역코사인(도)이다
 * @details 변이: 페이드를 끝값 가운데에 두면(+1 대신 +0.5) 끝값이 0.5 라 평지(경사 0)가 [0, 40] 띠에 반만 덮인다. 페이드 0 을 그대로 나누면 무한대가 나온다.
 */
namespace
{
    constexpr float32 kDeg = sw::MathUtil::kDegreeToRadian; // 경사는 라디안으로 쥔다 — 테스트 값은 읽기 쉽게 도로 적고 옮긴다
} // namespace

SW_TEST_CASE( TerrainLayerRuleTest, BandIsTrapezoidAroundEdges )
{
    SW_EXPECT_NEAR( 1.0f, TerrainLayerRule::computeBand( 45.0f, 30.0f, 90.0f, 10.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, TerrainLayerRule::computeBand( 30.0f, 30.0f, 90.0f, 10.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.5f, TerrainLayerRule::computeBand( 25.0f, 30.0f, 90.0f, 10.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.75f, TerrainLayerRule::computeBand( 27.5f, 30.0f, 90.0f, 10.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, TerrainLayerRule::computeBand( 20.0f, 30.0f, 90.0f, 10.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, TerrainLayerRule::computeBand( 90.0f, 30.0f, 90.0f, 10.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, TerrainLayerRule::computeBand( 0.0f, 0.0f, 40.0f, 6.0f ), 1.0e-6f ); // 평지는 [0, 40] 띠에 가득이다
    // 페이드 0 은 계단이다(나누지 않는다).
    SW_EXPECT_NEAR( 1.0f, TerrainLayerRule::computeBand( 30.1f, 30.0f, 90.0f, 0.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, TerrainLayerRule::computeBand( 29.9f, 30.0f, 90.0f, 0.0f ), 1.0e-6f );

    SW_EXPECT_NEAR( 0.0f, TerrainLayerRule::computeSlopeRadians( 1.0f ), 1.0e-4f );
    SW_EXPECT_NEAR( MathUtil::kHalfPi, TerrainLayerRule::computeSlopeRadians( 0.0f ), 1.0e-4f );
    SW_EXPECT_NEAR( 60.0f * MathUtil::kDegreeToRadian, TerrainLayerRule::computeSlopeRadians( 0.5f ), 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, TerrainLayerRule::computeSlopeRadians( 1.5f ), 1.0e-4f ); // 단위를 넘은 값은 자른다(NaN 이 아니다)
}

/**
 * @brief [TerrainLayerRuleTest] 덮는 정도 = 세기 × 경사 띠 × 고도 띠 · 기본 규칙은 꺼져 있다(세기 0)
 */
SW_TEST_CASE( TerrainLayerRuleTest, CoverageMultipliesStrengthAndBands )
{
    const TerrainLayerRule off{};
    SW_EXPECT_FALSE( off.isActive() );
    SW_EXPECT_NEAR( 0.0f, off.computeCoverage( 45.0f * kDeg, 10.0f ), 0.0f );

    TerrainLayerRule snow{};
    snow._strength      = 0.5f;
    snow._slopeRange    = float2{ 0.0f * kDeg, 40.0f * kDeg };
    snow._slopeFade     = 10.0f * kDeg;
    snow._altitudeRange = float2{ 20.0f, 100000.0f };
    snow._altitudeFade  = 4.0f;
    SW_EXPECT_TRUE( snow.isActive() );
    SW_EXPECT_NEAR( 0.5f, snow.computeCoverage( 10.0f * kDeg, 30.0f ), 1.0e-6f );   // 완만하고 높다
    SW_EXPECT_NEAR( 0.5f, snow.computeCoverage( 10.0f * kDeg, 20.0f ), 1.0e-6f );   // 설선 끝값은 띠 안이다
    SW_EXPECT_NEAR( 0.25f, snow.computeCoverage( 10.0f * kDeg, 18.0f ), 1.0e-6f );  // 설선 아래 페이드 가운데
    SW_EXPECT_NEAR( 0.0f, snow.computeCoverage( 10.0f * kDeg, 10.0f ), 1.0e-6f );   // 설선 아래
    SW_EXPECT_NEAR( 0.0f, snow.computeCoverage( 60.0f * kDeg, 30.0f ), 1.0e-6f );   // 가파르면 쌓이지 않는다
    SW_EXPECT_NEAR( 0.125f, snow.computeCoverage( 45.0f * kDeg, 18.0f ), 1.0e-6f ); // 두 페이드의 가운데가 곱해진다
}

/**
 * @brief [TerrainLayerRuleTest] 덮기는 다른 레이어를 (1 − c) 배로 줄이고 그 레이어에 c 를 더한다 - 합 1 이 남는다
 */
SW_TEST_CASE( TerrainLayerRuleTest, CoverageKeepsSumOne )
{
    SurfaceLayerWeights weights{
        float4{0.5f, 0.5f, 0.0f, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 0.0f}
    };
    const SurfaceLayerWeights covered = weights.computeCovered( 6, 0.25f );
    SW_EXPECT_NEAR( 0.375f, covered.get( 0 ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.375f, covered.get( 1 ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.25f, covered.get( 6 ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, covered.getSum(), 1.0e-6f );

    const SurfaceLayerWeights full = weights.computeCovered( 1, 1.0f );
    SW_EXPECT_NEAR( 1.0f, full.get( 1 ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, full.get( 0 ), 1.0e-6f );
}

/**
 * @brief [TerrainLayerRuleTest] 높이 섞기 - 높은 레이어가 경계에서 이기고, 깊이 0 은 섞지 않으며, 칠하지 않은 레이어는 높아도 나오지 않는다
 * @details 변이: 키를 덧셈(가중치 + 높이)으로 바꾸면 마지막 검사(가중치 0 인 높은 레이어)가 진다. 문턱을 깊이만큼 내리지 않으면 반반 경계가 한쪽만 남지 않는다.
 */
SW_TEST_CASE( TerrainLayerRuleTest, HeightBlendFavoursHigherLayer )
{
    float32 arrHeight[SurfaceLayerWeights::kLayerCount]{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    const SurfaceLayerWeights half{
        float4{0.5f, 0.5f, 0.0f, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 0.0f}
    };
    const SurfaceLayerWeights sharp = half.computeHeightBlended( arrHeight, 0.2f );
    SW_EXPECT_NEAR( 1.0f, sharp.get( 0 ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, sharp.get( 1 ), 1.0e-6f );

    const SurfaceLayerWeights linear = half.computeHeightBlended( arrHeight, 0.0f );
    SW_EXPECT_NEAR( 0.5f, linear.get( 0 ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.5f, linear.get( 1 ), 1.0e-6f );

    const SurfaceLayerWeights lean{
        float4{0.3f, 0.7f, 0.0f, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 0.0f}
    };
    const SurfaceLayerWeights mixed = lean.computeHeightBlended( arrHeight, 0.2f );
    SW_EXPECT_NEAR( 1.0f / 3.0f, mixed.get( 0 ), 1.0e-5f );
    SW_EXPECT_NEAR( 2.0f / 3.0f, mixed.get( 1 ), 1.0e-5f );

    const SurfaceLayerWeights unpainted{
        float4{0.0f, 1.0f, 0.0f, 0.0f},
        float4{0.0f, 0.0f, 0.0f, 0.0f}
    };
    const SurfaceLayerWeights kept = unpainted.computeHeightBlended( arrHeight, 0.2f );
    SW_EXPECT_NEAR( 1.0f, kept.get( 1 ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, kept.get( 3 ), 1.0e-6f );
}

/**
 * @brief [TerrainLayerRuleTest] 높이장의 월드 질의는 칠한 가중치에 규칙을 덮고, 정규 좌표 질의(칠한 값 — 브러시)는 그대로다 · 규칙을 비우면 칠한 값으로 돌아간다
 * @details 변이: 월드 질의가 규칙을 건너뛰면 설선 위 평지의 눈 레이어 1 이 진다. 경사 규칙이 평지를 덮으면 바위 레이어가 0 이 아니다.
 */
SW_TEST_CASE( TerrainLayerRuleTest, HeightfieldWorldQueryAppliesRules )
{
    HeightfieldAsset data;
    data._resolution = 3;
    data._listHeight.assign( 9, 65535u ); // 평지, 높이 30 m(범위 0..30)
    TerrainHeightfield field;
    SW_REQUIRE_TRUE( field.initialize( data, float3{ 0.0f, 0.0f, 0.0f }, float2{ 2.0f, 2.0f }, 0.0f, 30.0f ) );

    TerrainLayerRule rock{};
    rock._strength   = 1.0f;
    rock._slopeRange = float2{ 40.0f * kDeg, 90.0f * kDeg };
    TerrainLayerRule snow{};
    snow._strength      = 0.5f;
    snow._altitudeRange = float2{ 20.0f, 100000.0f };
    vector<TerrainLayerRule> listRule( 7 );
    listRule[2] = rock;
    listRule[6] = snow;
    field.setLayerRules( listRule );

    const SurfaceLayerWeights covered = field.computeLayerWeightsAt( 1.0f, 1.0f );
    SW_EXPECT_NEAR( 0.5f, covered.get( 6 ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.5f, covered.get( 0 ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, covered.get( 2 ), 1.0e-5f ); // 평지라 경사 규칙은 덮지 않는다
    SW_EXPECT_NEAR( 1.0f, covered.getSum(), 1.0e-5f );

    const SurfaceLayerWeights painted = field.computeLayerWeightsAtUV( 0.5f, 0.5f );
    SW_EXPECT_NEAR( 1.0f, painted.get( 0 ), 1.0e-5f );

    field.setLayerRules( vector<TerrainLayerRule>{} );
    SW_EXPECT_NEAR( 1.0f, field.computeLayerWeightsAt( 1.0f, 1.0f ).get( 0 ), 1.0e-5f );
}
