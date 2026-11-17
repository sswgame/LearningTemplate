#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"

// 자동 렌더 배율의 단계(RS-4) — 풀은 그대로 두고 그리는 사각형만 줄인다(할당 0), 지난 사각형 배율이 남아 히스토리를 지난 배율로 읽는다. 디바이스 없이 돈다.

using namespace sw;

namespace
{
    bool isNear( float32 a, float32 b )
    {
        return MathUtil::abs( a - b ) < 1.0e-5f;
    }
} // namespace

/** @brief [FrameViewResourcesTest] 배율 단계는 풀 크기를 바꾸지 않고(다시 만들지 않는다) 사각형만 줄인다. 반해상도 첨부는 비율로, 크기를 따로 준 첨부(그림자 맵)는 그대로다. */
SW_TEST_CASE( FrameViewResourcesTest, AutoScaleStepAllocatesNothing )
{
    FrameViewTarget view;
    view._transientPool.setSize( 1280, 720 );
    view.updateViewRect( 1.0f );
    SW_EXPECT_EQUAL( view._rect._width, 1280u );
    SW_EXPECT_EQUAL( view._rect._height, 720u );
    SW_EXPECT_EQUAL( view.scaleToRect( 1280, true, true ), 1280u ); // 사각형 = 풀이면 뷰포트를 바꾸지 않는다

    view.updateViewRect( 0.7f );
    SW_EXPECT_EQUAL( view._transientPool.getWidth(), 1280u );
    SW_EXPECT_EQUAL( view._transientPool.getHeight(), 720u );
    SW_EXPECT_TRUE( view._transientPool.isEmpty() ); // 단계가 첨부를 만들지 않았다
    SW_EXPECT_EQUAL( view._rect._width, 896u );
    SW_EXPECT_EQUAL( view._rect._height, 504u );
    SW_EXPECT_EQUAL( view.scaleToRect( 1280, true, true ), 896u );
    SW_EXPECT_EQUAL( view.scaleToRect( 640, true, true ), 448u );    // 반해상도
    SW_EXPECT_EQUAL( view.scaleToRect( 2048, false, true ), 2048u ); // 그림자 맵
    view.updateViewRect( 0.0f );
    SW_EXPECT_EQUAL( view._rect._width, static_cast<uint32>( 1280.0f * FrameViewTarget::kMinRectScale + 0.5f ) ); // 하한
}

/** @brief [FrameViewResourcesTest] 단계가 바뀐 프레임에도 히스토리를 버리지 않는다 — 지난 사각형 배율이 셰이더 상수의 zw 로 간다(TAA 는 지난 배율로 읽는다). */
SW_TEST_CASE( FrameViewResourcesTest, HistorySurvivesAutoScaleStep )
{
    FrameViewTarget view;
    view._transientPool.setSize( 1000, 500 );
    view.updateViewRect( 1.0f );
    view.beginViewHistory();
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_FALSE );

    view.updateViewRect( 0.8f ); // 단계 — 풀도 기록 플래그도 그대로다
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_FALSE );
    SW_EXPECT_TRUE( isNear( view._previousRectScale._x, 1.0f ) && isNear( view._previousRectScale._y, 1.0f ) );
    const FrameRendererUtil::ViewRectConstants constants =
        FrameRendererUtil::makeViewRectConstants( view._rect._width, view._rect._height, 1000, 500, view._previousRectScale );
    SW_EXPECT_TRUE( isNear( constants._scale._x, 0.8f ) && isNear( constants._scale._z, 1.0f ) );

    view.updateViewRect( 0.8f ); // 다음 프레임 — 지난 배율도 0.8
    SW_EXPECT_TRUE( isNear( view._previousRectScale._x, 0.8f ) );
}

/** @brief [FrameViewResourcesTest] 깊이 히스토리를 읽는 쪽(가림 타일 · 컬링 가림 판정 · GPU 입자 깊이 충돌)이 받는 크기는 지난 프레임 사각형이다 — 단계 프레임에도 가림 · 충돌을 끄지 않는다. */
SW_TEST_CASE( FrameViewResourcesTest, DepthHistoryReadsPreviousRect )
{
    FrameViewTarget view;
    view._transientPool.setSize( 1280, 720 );
    view.updateViewRect( 1.0f );
    view.updateViewRect( 0.5f ); // 단계 프레임 — 히스토리는 배율 1 로 그린 지난 프레임이다
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 1280, true ), 1280u );
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 720, false ), 720u );
    view.updateViewRect( 0.5f ); // 다음 프레임 — 히스토리는 반 크기 사각형에 그렸다
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 1280, true ), 640u );
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 720, false ), 360u );
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 1, true ), 1u ); // 0 이 되지 않는다
}
