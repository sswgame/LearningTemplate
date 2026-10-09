#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Viewport/EditorGridUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorGridUtilTest] 굵은 선은 월드 인덱스가 5 의 배수인 선이다 — 음수 쪽도 같고, 카메라가 어디 있든 같은 월드 선이다
 */
SW_TEST_CASE( EditorGridUtilTest, MajorLinesAreWorldMultiplesOfFive )
{
    SW_EXPECT_TRUE( EditorGridUtil::isMajorLine( 0 ) );
    SW_EXPECT_TRUE( EditorGridUtil::isMajorLine( 5 ) );
    SW_EXPECT_TRUE( EditorGridUtil::isMajorLine( -10 ) );
    SW_EXPECT_FALSE( EditorGridUtil::isMajorLine( 1 ) );
    SW_EXPECT_FALSE( EditorGridUtil::isMajorLine( -3 ) );
    SW_EXPECT_FALSE( EditorGridUtil::isMajorLine( 6 ) );

    // 단계 안(섞기 전)에서 선 모양은 월드 인덱스로만 정해진다.
    const EditorGridLevel level = EditorGridUtil::selectLevel( 5.0f );
    for ( int64 worldIndex = -12; worldIndex <= 12; ++worldIndex )
    {
        const EditorGridLineStyle style = EditorGridUtil::evaluateLine( worldIndex, level );
        SW_EXPECT_NEAR_EQUAL( EditorGridUtil::isMajorLine( worldIndex ) ? 1.0f : 0.0f, style._majorWeight, 1e-6f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, style._visibility, 1e-6f );
    }
}

/**
 * @brief [EditorGridUtilTest] 간격은 높이에 따라 1 · 10 · 100 m 이고, 100 m 위로는 더 커지지 않는다
 */
SW_TEST_CASE( EditorGridUtilTest, SpacingFollowsViewHeight )
{
    SW_EXPECT_NEAR_EQUAL( 1.0f, EditorGridUtil::selectLevel( 0.0f )._step, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, EditorGridUtil::selectLevel( 8.0f )._step, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, EditorGridUtil::selectLevel( -8.0f )._step, 1e-6f ); // 바닥 아래에서 올려다봐도 같다
    SW_EXPECT_NEAR_EQUAL( 10.0f, EditorGridUtil::selectLevel( 150.0f )._step, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, EditorGridUtil::selectLevel( 1500.0f )._step, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, EditorGridUtil::selectLevel( 1.0e6f )._step, 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, EditorGridUtil::selectLevel( 1.0e6f )._coarseBlend, 1e-6f );

    // 단계 앞 절반은 섞지 않고, 뒤 절반에서 0 → 1 로 간다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, EditorGridUtil::selectLevel( 20.0f )._coarseBlend, 1e-6f );
    SW_EXPECT_TRUE( EditorGridUtil::selectLevel( 60.0f )._coarseBlend > 0.0f );
    SW_EXPECT_TRUE( EditorGridUtil::selectLevel( 60.0f )._coarseBlend < 1.0f );
}

/**
 * @brief [EditorGridUtilTest] 단계 경계(높이 100 m · 1000 m) 양쪽에서 같은 월드 좌표 선이 같은 모양 · 같은 반지름이다 — 간격이 바뀌어도 튀지 않는다
 */
SW_TEST_CASE( EditorGridUtilTest, LevelBoundaryIsContinuous )
{
    for ( const float32 boundary : { 100.0f, 1000.0f } )
    {
        const EditorGridLevel below = EditorGridUtil::selectLevel( boundary * 0.9999f );
        const EditorGridLevel above = EditorGridUtil::selectLevel( boundary * 1.0001f );
        SW_EXPECT_NEAR_EQUAL( below._step * 10.0f, above._step, 1e-2f );
        SW_EXPECT_NEAR_EQUAL( below._radius, above._radius, below._radius * 1e-3f );

        // 위 단계의 선(좌표 = 위 간격의 배수)은 아래 단계에서 인덱스가 10 배다. 위 단계에 없는 아래 선은 거의 사라져 있어야 한다.
        for ( int64 aboveIndex = -12; aboveIndex <= 12; ++aboveIndex )
        {
            const EditorGridLineStyle styleBelow = EditorGridUtil::evaluateLine( aboveIndex * 10, below );
            const EditorGridLineStyle styleAbove = EditorGridUtil::evaluateLine( aboveIndex, above );
            SW_EXPECT_NEAR_EQUAL( styleAbove._visibility, styleBelow._visibility, 1e-2f );
            SW_EXPECT_NEAR_EQUAL( styleAbove._majorWeight, styleBelow._majorWeight, 1e-2f );
        }
        SW_EXPECT_TRUE( EditorGridUtil::evaluateLine( 3, below )._visibility < 0.01f );
    }
}

/**
 * @brief [EditorGridUtilTest] 가장자리 흐림은 안쪽에서 1, 반지름에서 0 이고 바깥으로 갈수록 줄기만 한다
 */
SW_TEST_CASE( EditorGridUtilTest, EdgeFadeFallsToZeroAtTheRadius )
{
    constexpr float32 kRadius = 20.0f;
    SW_EXPECT_NEAR_EQUAL( 1.0f, EditorGridUtil::computeEdgeFade( 0.0f, kRadius ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, EditorGridUtil::computeEdgeFade( 10.0f, kRadius ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, EditorGridUtil::computeEdgeFade( kRadius, kRadius ), 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, EditorGridUtil::computeEdgeFade( 30.0f, kRadius ), 1e-6f );

    float32 previous = 1.0f;
    for ( float32 distance = 0.0f; distance <= kRadius; distance += 0.5f )
    {
        const float32 fade = EditorGridUtil::computeEdgeFade( distance, kRadius );
        SW_EXPECT_TRUE( fade <= previous + 1e-6f );
        previous = fade;
    }
}
