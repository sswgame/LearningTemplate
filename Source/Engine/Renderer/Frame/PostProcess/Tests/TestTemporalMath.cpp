#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/PostProcess/TemporalMath.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"

// 시간 누적 · 업스케일 · 노출 히스토그램의 스칼라 식(temporal_math.hlsli — 셰이더와 같은 파일)과 해상도 공간 크기(배율 → 렌더 크기)를 잰다(nogpu).

using namespace sw;
using shadermath::TemporalMath;

namespace
{
    struct TemporalMathTestUtil
    {
        static float32 sumCatmullRom( float32 fraction )
        {
            return TemporalMath::computeCatmullRomWeight0( fraction ) + TemporalMath::computeCatmullRomWeight1( fraction ) + TemporalMath::computeCatmullRomWeight2( fraction ) +
                   TemporalMath::computeCatmullRomWeight3( fraction );
        }
    };
} // namespace

/** @brief [TemporalMathTest] Catmull-Rom 네 탭 가중은 어느 자리에서나 합이 1 이고, 텍셀 중심(0)에서는 그 텍셀만 읽는다 */
SW_TEST_CASE( TemporalMathTest, CatmullRomWeightsSumToOneAndInterpolateTexelCenters )
{
    for ( uint32 step = 0; step < 10; ++step )
        SW_EXPECT_NEAR( 1.0f, TemporalMathTestUtil::sumCatmullRom( static_cast<float32>( step ) * 0.1f ), 1.0e-5f );
    SW_EXPECT_NEAR( 0.0f, TemporalMath::computeCatmullRomWeight0( 0.0f ), 1.0e-7f );
    SW_EXPECT_NEAR( 1.0f, TemporalMath::computeCatmullRomWeight1( 0.0f ), 1.0e-7f );
    SW_EXPECT_NEAR( 0.0f, TemporalMath::computeCatmullRomWeight2( 0.0f ), 1.0e-7f );
    SW_EXPECT_NEAR( 0.0f, TemporalMath::computeCatmullRomWeight3( 0.0f ), 1.0e-7f );
}

/** @brief [TemporalMathTest] 두 텍셀 한가운데(0.5)는 좌우 대칭이고 바깥 탭은 음수다(날카로운 보간 — 바이리니어와 다른 점) */
SW_TEST_CASE( TemporalMathTest, CatmullRomWeightsAreSymmetricAtTheMidpoint )
{
    SW_EXPECT_NEAR( TemporalMath::computeCatmullRomWeight0( 0.5f ), TemporalMath::computeCatmullRomWeight3( 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR( TemporalMath::computeCatmullRomWeight1( 0.5f ), TemporalMath::computeCatmullRomWeight2( 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR( -0.0625f, TemporalMath::computeCatmullRomWeight0( 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.5625f, TemporalMath::computeCatmullRomWeight1( 0.5f ), 1.0e-6f );
}

/** @brief [TemporalMathTest] 기록이 이번 그림과 같고 서 있으면 상한, 휘도가 크게 다르거나 기준 속도 이상으로 움직이면 하한이다 */
SW_TEST_CASE( TemporalMathTest, HistoryWeightSpansMinToMaxByTrustAndMotion )
{
    constexpr float32 kMin    = 0.75f;
    constexpr float32 kMax    = 0.9f;
    constexpr float32 kReject = 8.0f;
    SW_EXPECT_NEAR( kMax, TemporalMath::computeHistoryWeight( 0.5f, 0.5f, 0.0f, kMin, kMax, kReject ), 1.0e-6f );
    SW_EXPECT_NEAR( kMin, TemporalMath::computeHistoryWeight( 1.0f, 0.0f, 0.0f, kMin, kMax, kReject ), 1.0e-6f );
    SW_EXPECT_NEAR( kMin, TemporalMath::computeHistoryWeight( 0.5f, 0.5f, kReject, kMin, kMax, kReject ), 1.0e-6f );
    SW_EXPECT_NEAR( kMin, TemporalMath::computeHistoryWeight( 0.5f, 0.5f, kReject * 4.0f, kMin, kMax, kReject ), 1.0e-6f ); // 속도 몫은 1 에서 멈춘다
}

/** @brief [TemporalMathTest] 기록 비율은 휘도 차 · 속도가 클수록 줄기만 한다(단조) — 고스팅이 빠른 곳에서 먼저 줄어든다 */
SW_TEST_CASE( TemporalMathTest, HistoryWeightDecreasesWithDifferenceAndMotion )
{
    float32 previous = 1.0f;
    for ( uint32 step = 0; step <= 10; ++step )
    {
        const float32 weight = TemporalMath::computeHistoryWeight( 1.0f, 1.0f - static_cast<float32>( step ) * 0.1f, 0.0f, 0.75f, 0.9f, 8.0f );
        SW_EXPECT_TRUE( weight <= previous + 1.0e-6f );
        previous = weight;
    }
    previous = 1.0f;
    for ( uint32 step = 0; step <= 10; ++step )
    {
        const float32 weight = TemporalMath::computeHistoryWeight( 1.0f, 1.0f, static_cast<float32>( step ), 0.75f, 0.9f, 8.0f );
        SW_EXPECT_TRUE( weight <= previous + 1.0e-6f );
        previous = weight;
    }
}

/** @brief [TemporalMathTest] 어두운 픽셀의 작은 휘도 차는 분모 하한 덕에 신뢰도를 0 으로 끌어내리지 않고, 하한이 상한보다 크게 적혀도 상한을 넘지 않는다 */
SW_TEST_CASE( TemporalMathTest, HistoryWeightEdgeCases )
{
    SW_EXPECT_TRUE( TemporalMath::computeHistoryWeight( 0.02f, 0.01f, 0.0f, 0.75f, 0.9f, 8.0f ) > 0.88f ); // 차 0.01 / 하한 0.2 → 신뢰도 0.95
    SW_EXPECT_NEAR( 0.8f, TemporalMath::computeHistoryWeight( 1.0f, 0.0f, 0.0f, 0.95f, 0.8f, 8.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.8f, TemporalMath::computeHistoryWeight( 1.0f, 1.0f, 0.0f, 0.95f, 0.8f, 8.0f ), 1.0e-6f );
}

/** @brief [TemporalMathTest] 업샘플 가중은 거리 0 에서 1 이고 멀수록 준다(언리얼 TAAU 의 exp(-2.29 d²) — 렌더 픽셀 하나 떨어지면 약 0.1) */
SW_TEST_CASE( TemporalMathTest, UpsampleWeightFallsOffWithDistance )
{
    SW_EXPECT_NEAR( 1.0f, TemporalMath::computeUpsampleWeight( 0.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.1015f, TemporalMath::computeUpsampleWeight( 1.0f ), 1.0e-3f );
    float32 previous = 1.0f;
    for ( uint32 step = 1; step <= 8; ++step )
    {
        const float32 weight = TemporalMath::computeUpsampleWeight( static_cast<float32>( step ) * 0.25f );
        SW_EXPECT_TRUE( weight < previous );
        previous = weight;
    }
    SW_EXPECT_NEAR( 1.0f, TemporalMath::computeUpsampleWeight( -1.0f ), 1.0e-6f ); // 음수 거리²(반올림 오차)는 0 으로 본다
}

/** @brief [TemporalMathTest] 업스케일 기록 비율 — 표본이 출력 픽셀 위에 있으면(덮임 1) 그대로, 멀수록 1 쪽(기록을 더 믿는다)이고 덮임 0 이면 1 이다 */
SW_TEST_CASE( TemporalMathTest, UpscaledHistoryWeightTrustsHistoryWhenSamplesAreFar )
{
    SW_EXPECT_NEAR( 0.9f, TemporalMath::computeUpscaledHistoryWeight( 0.9f, 1.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.95f, TemporalMath::computeUpscaledHistoryWeight( 0.9f, 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR( 1.0f, TemporalMath::computeUpscaledHistoryWeight( 0.9f, 0.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, TemporalMath::computeUpscaledHistoryWeight( 0.0f, 1.0f ), 1.0e-6f ); // 기록을 버리는 픽셀은 덮임과 상관없이 이번 그림
}

/** @brief [TemporalMathTest] 히스토그램 칸마다의 식 — 백분위 → 표본 수(위 컷은 아래 컷보다 작지 않다), 걸친 몫, 칸 가운데 */
SW_TEST_CASE( TemporalMathTest, HistogramPerBinTerms )
{
    SW_EXPECT_NEAR( 10.0f, TemporalMath::computeHistogramLowCount( 100.0f, 10.0f ), 1.0e-5f );
    SW_EXPECT_NEAR( 90.0f, TemporalMath::computeHistogramHighCount( 100.0f, 10.0f, 90.0f ), 1.0e-5f );
    SW_EXPECT_NEAR( 60.0f, TemporalMath::computeHistogramHighCount( 100.0f, 60.0f, 10.0f ), 1.0e-5f );     // 뒤집힌 컷은 점
    SW_EXPECT_NEAR( 100.0f, TemporalMath::computeHistogramHighCount( 100.0f, 0.0f, 150.0f ), 1.0e-5f );    // 100 % 를 넘지 않는다
    SW_EXPECT_NEAR( 2.0f, TemporalMath::computeHistogramKeptCount( 8.0f, 12.0f, 10.0f, 13.0f ), 1.0e-6f ); // 칸 [8, 12) 중 컷 [10, 13) 안은 [10, 12) — 걸친 2 만 센다
    SW_EXPECT_NEAR( 0.0f, TemporalMath::computeHistogramKeptCount( 0.0f, 5.0f, 10.0f, 13.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.5f, TemporalMath::computeHistogramBinCenterLog( 0.0f, 0.0f, 4.0f, 4.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 3.5f, TemporalMath::computeHistogramBinCenterLog( 3.0f, 0.0f, 4.0f, 4.0f ), 1.0e-6f );
}

/** @brief [TemporalMathTest] 렌더 공간 크기 = 출력 × 배율(반올림, 1 이상 출력 이하) — 배율 1 이상이면 그대로(두 공간이 같다), 출력 0 은 0 */
SW_TEST_CASE( TemporalMathTest, RenderExtentScalesTheOutputExtent )
{
    SW_EXPECT_EQUAL( 1920u, TransientAttachmentPool::computeRenderExtent( 1920u, 1.0f ) );
    SW_EXPECT_EQUAL( 960u, TransientAttachmentPool::computeRenderExtent( 1920u, 0.5f ) );
    SW_EXPECT_EQUAL( 810u, TransientAttachmentPool::computeRenderExtent( 1080u, 0.75f ) );
    SW_EXPECT_EQUAL( 541u, TransientAttachmentPool::computeRenderExtent( 1081u, 0.5f ) ); // 540.5 → 반올림
    SW_EXPECT_EQUAL( 1u, TransientAttachmentPool::computeRenderExtent( 1u, 0.25f ) );
    SW_EXPECT_EQUAL( 0u, TransientAttachmentPool::computeRenderExtent( 0u, 0.5f ) );
    SW_EXPECT_EQUAL( 1280u, TransientAttachmentPool::computeRenderExtent( 1280u, 2.0f ) );
}
