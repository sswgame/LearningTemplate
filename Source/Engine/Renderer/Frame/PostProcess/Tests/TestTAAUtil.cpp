#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/PostProcess/TAAUtil.h"

// TAA 수식(taa.hlsl 과 같은 식) — 히스토리 Catmull-Rom 가중과 적응 블렌드 계수.

using namespace sw;

/** @brief [TAAUtilTest] Catmull-Rom 네 탭 가중은 어느 자리에서나 합이 1 이고, 텍셀 중심(0)에서는 그 텍셀만 읽는다 */
SW_TEST_CASE( TAAUtilTest, CatmullRomWeightsSumToOneAndInterpolateTexelCenters )
{
    for ( uint32 step = 0; step < 10; ++step )
    {
        const float4 weights = TAAUtil::computeCatmullRomWeights( static_cast<float32>( step ) * 0.1f );
        SW_EXPECT_NEAR( 1.0f, weights._x + weights._y + weights._z + weights._w, 1.0e-5f );
    }
    const float4 atCenter = TAAUtil::computeCatmullRomWeights( 0.0f );
    SW_EXPECT_NEAR( 0.0f, atCenter._x, 1.0e-7f );
    SW_EXPECT_NEAR( 1.0f, atCenter._y, 1.0e-7f );
    SW_EXPECT_NEAR( 0.0f, atCenter._z, 1.0e-7f );
    SW_EXPECT_NEAR( 0.0f, atCenter._w, 1.0e-7f );
}

/** @brief [TAAUtilTest] 두 텍셀 한가운데(0.5)는 좌우 대칭이고 바깥 탭은 음수다(날카로운 보간 — 바이리니어와 다른 점) */
SW_TEST_CASE( TAAUtilTest, CatmullRomWeightsAreSymmetricAtTheMidpoint )
{
    const float4 weights = TAAUtil::computeCatmullRomWeights( 0.5f );
    SW_EXPECT_NEAR( weights._x, weights._w, 1.0e-6f );
    SW_EXPECT_NEAR( weights._y, weights._z, 1.0e-6f );
    SW_EXPECT_NEAR( -0.0625f, weights._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.5625f, weights._y, 1.0e-6f );
}

/** @brief [TAAUtilTest] 기록이 이번 그림과 같고 서 있으면 상한, 휘도가 크게 다르거나 기준 속도 이상으로 움직이면 하한이다 */
SW_TEST_CASE( TAAUtilTest, HistoryWeightSpansMinToMaxByConfidenceAndMotion )
{
    constexpr float32 kMin    = 0.75f;
    constexpr float32 kMax    = 0.9f;
    constexpr float32 kReject = 8.0f;
    SW_EXPECT_NEAR( kMax, TAAUtil::computeHistoryWeight( 0.5f, 0.5f, 0.0f, kMin, kMax, kReject ), 1.0e-6f );
    SW_EXPECT_NEAR( kMin, TAAUtil::computeHistoryWeight( 1.0f, 0.0f, 0.0f, kMin, kMax, kReject ), 1.0e-6f );
    SW_EXPECT_NEAR( kMin, TAAUtil::computeHistoryWeight( 0.5f, 0.5f, kReject, kMin, kMax, kReject ), 1.0e-6f );
    SW_EXPECT_NEAR( kMin, TAAUtil::computeHistoryWeight( 0.5f, 0.5f, kReject * 4.0f, kMin, kMax, kReject ), 1.0e-6f ); // 속도 몫은 1 에서 멈춘다
}

/** @brief [TAAUtilTest] 기록 비율은 휘도 차 · 속도가 클수록 줄기만 한다(단조) — 고스팅이 빠른 곳에서 먼저 줄어든다 */
SW_TEST_CASE( TAAUtilTest, HistoryWeightDecreasesWithDifferenceAndMotion )
{
    float32 previous = 1.0f;
    for ( uint32 step = 0; step <= 10; ++step )
    {
        const float32 weight = TAAUtil::computeHistoryWeight( 1.0f, 1.0f - static_cast<float32>( step ) * 0.1f, 0.0f, 0.75f, 0.9f, 8.0f );
        SW_EXPECT_TRUE( weight <= previous + 1.0e-6f );
        previous = weight;
    }
    previous = 1.0f;
    for ( uint32 step = 0; step <= 10; ++step )
    {
        const float32 weight = TAAUtil::computeHistoryWeight( 1.0f, 1.0f, static_cast<float32>( step ), 0.75f, 0.9f, 8.0f );
        SW_EXPECT_TRUE( weight <= previous + 1.0e-6f );
        previous = weight;
    }
}

/** @brief [TAAUtilTest] 어두운 픽셀의 작은 휘도 차는 분모 하한 덕에 신뢰도를 0 으로 끌어내리지 않는다 */
SW_TEST_CASE( TAAUtilTest, DarkPixelsKeepConfidenceThroughTheLuminanceFloor )
{
    const float32 weight = TAAUtil::computeHistoryWeight( 0.02f, 0.01f, 0.0f, 0.75f, 0.9f, 8.0f );
    SW_EXPECT_TRUE( weight > 0.88f ); // 차 0.01 / 하한 0.2 → 신뢰도 0.95
}

/** @brief [TAAUtilTest] 하한이 상한보다 크게 적혀도 상한을 넘지 않는다(범위가 점) */
SW_TEST_CASE( TAAUtilTest, InvertedRangeDoesNotExceedTheMaximum )
{
    SW_EXPECT_NEAR( 0.8f, TAAUtil::computeHistoryWeight( 1.0f, 0.0f, 0.0f, 0.95f, 0.8f, 8.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.8f, TAAUtil::computeHistoryWeight( 1.0f, 1.0f, 0.0f, 0.95f, 0.8f, 8.0f ), 1.0e-6f );
}
