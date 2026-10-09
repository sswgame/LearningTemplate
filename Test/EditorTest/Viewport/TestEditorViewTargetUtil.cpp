#include "pch.h"

#include "Editor/Viewport/EditorViewTargetUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorViewTargetUtilTest] 보이는 패널의 뷰만 호스트에 알린다 — 둘 다 안 보이면 씬 뷰 RT 를 계속 알린다(백버퍼로 떨어지지 않게)
 */
SW_TEST_CASE( EditorViewTargetUtilTest, OnlyDrawnViewsAreRequested )
{
    // 씬 뷰만 보인다(기본 탭) — 게임 뷰 RT 요청은 0 이다.
    SW_EXPECT_TRUE( EditorViewTargetUtil::shouldRequestSceneView( true, false ) );
    SW_EXPECT_FALSE( EditorViewTargetUtil::shouldRequestGameView( false ) );
    // 게임 뷰만 보인다 — 씬 뷰는 그리지 않는다.
    SW_EXPECT_FALSE( EditorViewTargetUtil::shouldRequestSceneView( false, true ) );
    SW_EXPECT_TRUE( EditorViewTargetUtil::shouldRequestGameView( true ) );
    // 둘 다 보인다(옆으로 나란히 도킹).
    SW_EXPECT_TRUE( EditorViewTargetUtil::shouldRequestSceneView( true, true ) );
    SW_EXPECT_TRUE( EditorViewTargetUtil::shouldRequestGameView( true ) );
    // 둘 다 안 보인다 — 씬 뷰 RT 를 알린다.
    SW_EXPECT_TRUE( EditorViewTargetUtil::shouldRequestSceneView( false, false ) );
}

/**
 * @brief [EditorViewTargetUtilTest] 한 픽셀 차이로는 RT 를 다시 만들지 않고, 처음이거나 두 픽셀 넘게 바뀌면 다시 만든다
 */
SW_TEST_CASE( EditorViewTargetUtilTest, ResizeIgnoresOnePixelJitter )
{
    SW_EXPECT_TRUE( EditorViewTargetUtil::needsResize( 0, 0, 640, 360 ) );
    SW_EXPECT_FALSE( EditorViewTargetUtil::needsResize( 640, 360, 641, 359 ) );
    SW_EXPECT_TRUE( EditorViewTargetUtil::needsResize( 640, 360, 642, 360 ) );
    SW_EXPECT_TRUE( EditorViewTargetUtil::needsResize( 640, 360, 640, 300 ) );
    SW_EXPECT_FALSE( EditorViewTargetUtil::needsResize( 640, 360, 0, 360 ) );
}

/**
 * @brief [EditorViewTargetUtilTest] 16:9 는 영역 안에 가장 크게 가운데로 들고, 자유 비율은 영역 전체다(정수 픽셀)
 */
SW_TEST_CASE( EditorViewTargetUtilTest, GameViewImageFitsTheAspect )
{
    const EditorViewRect freeRect = EditorViewTargetUtil::fitGameViewImage( float2{ 801.6f, 400.2f }, EditorGameViewAspect::Free );
    SW_EXPECT_NEAR_EQUAL( 801.0f, freeRect._size._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 400.0f, freeRect._size._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, freeRect._offset._x, 1e-4f );

    // 넓은 영역 — 높이에 맞추고 좌우가 남는다.
    const EditorViewRect wide = EditorViewTargetUtil::fitGameViewImage( float2{ 1000.0f, 360.0f }, EditorGameViewAspect::Ratio16x9 );
    SW_EXPECT_NEAR_EQUAL( 640.0f, wide._size._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 360.0f, wide._size._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 180.0f, wide._offset._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, wide._offset._y, 1e-4f );

    // 좁은 영역 — 폭에 맞추고 위아래가 남는다.
    const EditorViewRect tall = EditorViewTargetUtil::fitGameViewImage( float2{ 320.0f, 600.0f }, EditorGameViewAspect::Ratio16x9 );
    SW_EXPECT_NEAR_EQUAL( 320.0f, tall._size._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 180.0f, tall._size._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 210.0f, tall._offset._y, 1e-4f );

    // 빈 영역은 빈 사각형이다.
    const EditorViewRect empty = EditorViewTargetUtil::fitGameViewImage( float2{ 0.0f, 0.0f }, EditorGameViewAspect::Ratio16x9 );
    SW_EXPECT_NEAR_EQUAL( 0.0f, empty._size._x, 1e-4f );
}
