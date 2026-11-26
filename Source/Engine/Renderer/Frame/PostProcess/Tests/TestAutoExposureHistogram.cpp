#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"

// 자동 노출 히스토그램(auto_exposure.hlsl 과 같은 식) — log2 휘도 칸 고르기와 백분위 컷 평균.

using namespace sw;

/** @brief [AutoExposureHistogramTest] 칸은 log2 휘도 범위를 고르게 나누고, 범위 밖 · 검은 텍셀은 끝 칸에 든다 */
SW_TEST_CASE( AutoExposureHistogramTest, BinCoversTheLogRangeAndClampsOutside )
{
    constexpr uint32 kBinCount = 16;
    SW_EXPECT_EQUAL( 0u, FrameAutoExposure::computeHistogramBin( 0.0f, -8.0f, 8.0f, kBinCount ) );               // 검은 텍셀
    SW_EXPECT_EQUAL( 0u, FrameAutoExposure::computeHistogramBin( 1.0e-4f, -8.0f, 8.0f, kBinCount ) );            // 아래 끝 밖(log2 ≈ -13.3)
    SW_EXPECT_EQUAL( 8u, FrameAutoExposure::computeHistogramBin( 1.0f, -8.0f, 8.0f, kBinCount ) );               // log2 0 → 가운데 칸의 시작
    SW_EXPECT_EQUAL( 9u, FrameAutoExposure::computeHistogramBin( 2.5f, -8.0f, 8.0f, kBinCount ) );               // log2 ≈ 1.32
    SW_EXPECT_EQUAL( kBinCount - 1u, FrameAutoExposure::computeHistogramBin( 1.0e6f, -8.0f, 8.0f, kBinCount ) ); // 위 끝 밖
    SW_EXPECT_EQUAL( 0u, FrameAutoExposure::computeHistogramBin( 1.0f, -8.0f, 8.0f, 0 ) );                       // 칸 없음
}

/** @brief [AutoExposureHistogramTest] 컷이 없으면(0 · 100 %) 칸 가운데 값의 표본 가중 평균이다 */
SW_TEST_CASE( AutoExposureHistogramTest, NoCutIsTheWeightedMeanOfBinCenters )
{
    // 범위 [0, 4) 를 네 칸 — 가운데는 0.5, 1.5, 2.5, 3.5.
    const vector<uint32> listBin{ 1, 0, 0, 3 };
    SW_EXPECT_NEAR( ( 0.5f + 3.0f * 3.5f ) / 4.0f, FrameAutoExposure::computeHistogramPercentileMeanLog( listBin, 0.0f, 4.0f, 0.0f, 100.0f ), 1.0e-5f );
}

/** @brief [AutoExposureHistogramTest] 아래 · 위 몫을 버리면 끝의 소수(하늘 · 검은 띠)가 평균을 끌지 못한다 — 경계에 걸친 칸은 걸친 몫만 센다 */
SW_TEST_CASE( AutoExposureHistogramTest, PercentileCutDropsTheTails )
{
    // 10 표본: 어두운 끝 1, 가운데 8, 밝은 끝 1. 10 % · 90 % 컷이면 가운데 칸만 남는다.
    const vector<uint32> listBin{ 1, 8, 0, 1 };
    SW_EXPECT_NEAR( 1.5f, FrameAutoExposure::computeHistogramPercentileMeanLog( listBin, 0.0f, 4.0f, 10.0f, 90.0f ), 1.0e-5f );
    // 25 % · 100 % — 아래 2.5 표본을 버린다: 첫 칸 1 전부, 둘째 칸 1.5. 남는 것은 둘째 칸 6.5 · 넷째 칸 1.
    const float32 expected = ( 6.5f * 1.5f + 1.0f * 3.5f ) / 7.5f;
    SW_EXPECT_NEAR( expected, FrameAutoExposure::computeHistogramPercentileMeanLog( listBin, 0.0f, 4.0f, 25.0f, 100.0f ), 1.0e-5f );
}

/** @brief [AutoExposureHistogramTest] 두 백분위가 같거나 뒤집히면 아래 경계가 든 칸의 가운데, 표본이 없으면 범위 가운데다 */
SW_TEST_CASE( AutoExposureHistogramTest, DegenerateInputsHaveDefinedResults )
{
    const vector<uint32> listBin{ 2, 0, 2, 0 };
    SW_EXPECT_NEAR( 2.5f, FrameAutoExposure::computeHistogramPercentileMeanLog( listBin, 0.0f, 4.0f, 60.0f, 60.0f ), 1.0e-5f );
    SW_EXPECT_NEAR( 2.5f, FrameAutoExposure::computeHistogramPercentileMeanLog( listBin, 0.0f, 4.0f, 60.0f, 10.0f ), 1.0e-5f );
    const vector<uint32> listEmpty( 4, 0u );
    SW_EXPECT_NEAR( 2.0f, FrameAutoExposure::computeHistogramPercentileMeanLog( listEmpty, 0.0f, 4.0f, 10.0f, 90.0f ), 1.0e-5f );
}

/** @brief [AutoExposureHistogramTest] 칸 고르기와 평균이 맞물린다 — 한 휘도만 있는 화면의 평균은 그 휘도가 든 칸의 가운데다 */
SW_TEST_CASE( AutoExposureHistogramTest, SingleLuminanceRoundTripsToItsBinCenter )
{
    constexpr uint32 kBinCount = FrameAutoExposure::kHistogramBinCount;
    vector<uint32>   listBin( kBinCount, 0u );
    const uint32     bin   = FrameAutoExposure::computeHistogramBin( 0.18f, -10.0f, 6.0f, kBinCount );
    listBin[bin]           = 100;
    const float32 binWidth = 16.0f / static_cast<float32>( kBinCount );
    SW_EXPECT_NEAR( -10.0f + ( static_cast<float32>( bin ) + 0.5f ) * binWidth, FrameAutoExposure::computeHistogramPercentileMeanLog( listBin, -10.0f, 6.0f, 10.0f, 90.0f ), 1.0e-5f );
    SW_EXPECT_NEAR( MathUtil::log2( 0.18f ), FrameAutoExposure::computeHistogramPercentileMeanLog( listBin, -10.0f, 6.0f, 10.0f, 90.0f ), binWidth * 0.5f );
}
