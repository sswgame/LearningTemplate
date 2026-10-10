#include "pch.h"

#include "Editor/Panels/MaterialPreviewShading.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [MaterialPreviewShadingTest] 값 글은 빈 칸 · 쉼표가 섞여도 읽고, 모자란 칸은 0 이다
 * @details `MaterialProperty` 는 Engine 이 내보내지 않는 타입이라 `readInputs` 는 시나리오 `editor/materialpreview` 가 본다.
 */
SW_TEST_CASE( MaterialPreviewShadingTest, ParsesMixedSeparators )
{
    float32      arrValue[4]{};
    const uint32 filled = MaterialPreviewShading::parseFloats( "1, 0.5  0", arrValue, 4 );
    SW_EXPECT_EQUAL( 3u, filled );
    SW_EXPECT_NEAR_EQUAL( 1.0f, arrValue[0], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, arrValue[1], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, arrValue[2], 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, arrValue[3], 1e-6f );
    SW_EXPECT_EQUAL( 0u, MaterialPreviewShading::parseFloats( "", arrValue, 4 ) );
}

/**
 * @brief [MaterialPreviewShadingTest] 빨간 머티리얼은 평균 (R - B) 가 양수, 파란 것은 음수, 회색은 0 근처다 — 원 밖은 검정이다
 */
SW_TEST_CASE( MaterialPreviewShadingTest, MeanRedMinusBlueFollowsTheBaseColor )
{
    MaterialPreviewInputs red{};
    red._baseColor = float3{ 1.0f, 0.0f, 0.0f };
    MaterialPreviewInputs blue{};
    blue._baseColor = float3{ 0.0f, 0.0f, 1.0f };
    MaterialPreviewInputs grey{};
    grey._baseColor = float3{ 0.5f, 0.5f, 0.5f };

    SW_EXPECT_TRUE( MaterialPreviewShading::computeMeanRedMinusBlue( red ) > 40.0f );
    SW_EXPECT_TRUE( MaterialPreviewShading::computeMeanRedMinusBlue( blue ) < -40.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, MaterialPreviewShading::computeMeanRedMinusBlue( grey ), 1e-3f );

    const float3 outside = MaterialPreviewShading::shadePoint( red, 0.9f, 0.9f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, outside._x, 1e-6f );
    // 빛은 왼쪽 위에서 온다 — 왼쪽 위가 오른쪽 아래보다 밝다.
    SW_EXPECT_TRUE( MaterialPreviewShading::shadePoint( grey, -0.4f, -0.4f )._x > MaterialPreviewShading::shadePoint( grey, 0.4f, 0.4f )._x );
}
