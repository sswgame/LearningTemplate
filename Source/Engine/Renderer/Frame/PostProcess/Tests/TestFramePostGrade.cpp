#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/View/PostProcessValues.h"
#include "Engine/Renderer/Frame/PostProcess/FramePostGrade.h"

using namespace sw;

/** @brief [FramePostGradeTest] 기본 후처리 값은 그레이딩을 끈다 — 화이트 밸런스 단위 행렬, 범위 값 중립, LUT 없음 */
SW_TEST_CASE( FramePostGradeTest, NeutralValuesTurnGradingOff )
{
    const PostGradeConstants constants = PostGradeConstants::make( PostProcessValues{}, 16 );
    SW_EXPECT_NEAR( 0.0f, constants._params._z, 0.0f );
    SW_EXPECT_NEAR( 0.0f, constants._lut._x, 0.0f ); // 세기 0 이라 LUT 가 있어도 걸지 않는다
    SW_EXPECT_NEAR( 1.0f, constants._arrWhiteBalance[0]._x, 0.0f );
    SW_EXPECT_NEAR( 1.0f, constants._arrWhiteBalance[1]._y, 0.0f );
    SW_EXPECT_NEAR( 1.0f, constants._arrWhiteBalance[2]._z, 0.0f );
    for ( uint32 range = 0; range < PostGradeConstants::kRangeCount; ++range )
    {
        const float4& saturation = constants._arrRange[range * PostGradeConstants::kRangeFieldCount];
        const float4& offset     = constants._arrRange[range * PostGradeConstants::kRangeFieldCount + 4];
        SW_EXPECT_NEAR( 1.0f, saturation._y, 0.0f );
        SW_EXPECT_NEAR( 0.0f, offset._y, 0.0f );
    }
}

/** @brief [FramePostGradeTest] 화이트 밸런스는 6500 K · 틴트 0 에서 단위 행렬이고, 낮은 온도는 흰색을 푸르게(빨강 < 파랑), 높은 온도는 붉게 돌린다 */
SW_TEST_CASE( FramePostGradeTest, WhiteBalanceShiftsTowardsTheCompensatingColour )
{
    float4 arrRow[3];
    PostGradeConstants::computeWhiteBalance( 6500.0f, 0.0f, arrRow );
    SW_EXPECT_NEAR( 1.0f, arrRow[0]._x, 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, arrRow[0]._y, 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, arrRow[1]._y, 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, arrRow[2]._z, 1.0e-4f );

    // 흰색 (1, 1, 1) 의 결과는 행의 합이다.
    PostGradeConstants::computeWhiteBalance( 3000.0f, 0.0f, arrRow );
    const float32 warmRed  = arrRow[0]._x + arrRow[0]._y + arrRow[0]._z;
    const float32 warmBlue = arrRow[2]._x + arrRow[2]._y + arrRow[2]._z;
    SW_EXPECT_TRUE( warmRed < warmBlue );

    PostGradeConstants::computeWhiteBalance( 12000.0f, 0.0f, arrRow );
    const float32 coolRed  = arrRow[0]._x + arrRow[0]._y + arrRow[0]._z;
    const float32 coolBlue = arrRow[2]._x + arrRow[2]._y + arrRow[2]._z;
    SW_EXPECT_TRUE( coolRed > coolBlue );

    // 틴트 양수는 초록을 뺀다.
    PostGradeConstants::computeWhiteBalance( 6500.0f, 1.0f, arrRow );
    const float32 tintGreen = arrRow[1]._x + arrRow[1]._y + arrRow[1]._z;
    SW_EXPECT_TRUE( tintGreen < 1.0f );
}

/** @brief [FramePostGradeTest] 전역 값은 범위 값에 곱해지고(w 는 rgb 에 곱한다) 오프셋은 더해진다. 하나라도 중립이 아니면 그레이딩이 켜진다 */
SW_TEST_CASE( FramePostGradeTest, GlobalValuesCombineIntoEveryRange )
{
    PostProcessValues values;
    values._colorGain                  = float4{ 2.0f, 1.0f, 1.0f, 0.5f };
    values._highlightsGain             = float4{ 1.0f, 3.0f, 1.0f, 1.0f };
    values._colorOffset                = float4{ 0.0f, 0.0f, 0.1f, 0.05f };
    const PostGradeConstants constants = PostGradeConstants::make( values, 0 );
    SW_EXPECT_NEAR( 1.0f, constants._params._z, 0.0f );
    constexpr uint32 kGain          = 3;
    constexpr uint32 kOffset        = 4;
    const float4&    shadowsGain    = constants._arrRange[0 * PostGradeConstants::kRangeFieldCount + kGain];
    const float4&    highlightsGain = constants._arrRange[2 * PostGradeConstants::kRangeFieldCount + kGain];
    SW_EXPECT_NEAR( 1.0f, shadowsGain._x, 1.0e-6f ); // 2 × 0.5
    SW_EXPECT_NEAR( 0.5f, shadowsGain._y, 1.0e-6f );
    SW_EXPECT_NEAR( 1.5f, highlightsGain._y, 1.0e-6f ); // 1 × 3 × 0.5
    const float4& midtonesOffset = constants._arrRange[1 * PostGradeConstants::kRangeFieldCount + kOffset];
    SW_EXPECT_NEAR( 0.05f, midtonesOffset._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.15f, midtonesOffset._z, 1.0e-6f );
}

/** @brief [FramePostGradeTest] LUT 주소: 조각 안의 빨강 · 초록 끝은 텍셀 중심이고 조각은 가로 N 칸씩 옮긴다. 띠 모양만 받는다. 세기가 있어도 LUT 가 없으면 걸지 않는다 */
SW_TEST_CASE( FramePostGradeTest, LutAddressesTexelCentresOfTheBlueSlice )
{
    constexpr uint32 kSize = 16;
    const float2     black = PostGradeConstants::computeLutUv( float3{ 0.0f, 0.0f, 0.0f }, kSize, 0 );
    SW_EXPECT_NEAR( 0.5f / 256.0f, black._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.5f / 16.0f, black._y, 1.0e-6f );
    const float2 white = PostGradeConstants::computeLutUv( float3{ 1.0f, 1.0f, 1.0f }, kSize, 15 );
    SW_EXPECT_NEAR( 255.5f / 256.0f, white._x, 1.0e-6f );
    SW_EXPECT_NEAR( 15.5f / 16.0f, white._y, 1.0e-6f );
    const float2 slice3 = PostGradeConstants::computeLutUv( float3{ 0.0f, 0.5f, 0.0f }, kSize, 3 );
    SW_EXPECT_NEAR( ( 3.0f * 16.0f + 0.5f ) / 256.0f, slice3._x, 1.0e-6f );
    SW_EXPECT_NEAR( ( 7.5f + 0.5f ) / 16.0f, slice3._y, 1.0e-6f );

    SW_EXPECT_TRUE( PostGradeConstants::isLutStrip( 256, 16 ) );
    SW_EXPECT_TRUE( PostGradeConstants::isLutStrip( 1024, 32 ) );
    SW_EXPECT_FALSE( PostGradeConstants::isLutStrip( 256, 256 ) );
    SW_EXPECT_FALSE( PostGradeConstants::isLutStrip( 1, 1 ) );

    PostProcessValues values;
    values._colorGradingLutIntensity = 1.0f;
    SW_EXPECT_NEAR( 0.0f, PostGradeConstants::make( values, 0 )._lut._x, 0.0f );
    SW_EXPECT_NEAR( 16.0f, PostGradeConstants::make( values, kSize )._lut._x, 0.0f );
}
