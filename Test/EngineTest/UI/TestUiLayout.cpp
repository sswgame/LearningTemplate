#include "pch.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/OverlayPanel.h"

#include "EngineTest/UI/UiLayoutTestUtil.h"

#include "TestFramework/TestFramework.h"

// UiLayoutTest — 레이아웃 두 걷기(measure · arrange)와 패널. 결과는 사각형 덤프 글(UiLayoutDump)로 견준다. 디바이스 없음(nogpu).

namespace
{
    struct UiLayoutTestUtil
    {
        static void setAlignment( sw::Widget& widget, sw::UiAlignment horizontal, sw::UiAlignment vertical, const sw::float4& padding )
        {
            sw::WidgetLayoutSlot slot = widget.getLayoutSlot();
            slot._horizontalAlignment = horizontal;
            slot._verticalAlignment   = vertical;
            slot._padding             = padding;
            widget.setLayoutSlot( slot );
        }

        static void setAnchors( sw::Widget& widget, const sw::float2& anchorMin, const sw::float2& anchorMax, const sw::float2& offsetMin,
                                const sw::float2& offsetMax )
        {
            sw::WidgetLayoutSlot slot = widget.getLayoutSlot();
            slot._anchorMin           = anchorMin;
            slot._anchorMax           = anchorMax;
            slot._offsetMin           = offsetMin;
            slot._offsetMax           = offsetMax;
            widget.setLayoutSlot( slot );
        }
    };
} // namespace

/** @brief [UiLayoutTest] 가로 상자: Auto 둘은 원하는 크기, Fill 하나가 남은 너비, 간격 10 */
SW_TEST_CASE( UiLayoutTest, HorizontalBoxAutoAndFill )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    sw::BoxPanel*             pBox = fixture.setRoot<sw::BoxPanel>( "box" );
    pBox->setSpacing( 10.0f );
    fixture.addFixed( pBox, "a", 50.0f, 20.0f );
    fixture.addFixed( pBox, "b", 70.0f, 40.0f );
    fixture.addFixed( pBox, "c", 10.0f, 10.0f, sw::UiSizeRule::Fill );
    fixture.update();
    SW_EXPECT_STREQ( "box 0.00 0.00 400.00 100.00\n"
                     "  a 0.00 0.00 50.00 100.00\n"
                     "  b 60.00 0.00 70.00 100.00\n"
                     "  c 140.00 0.00 260.00 100.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 슬롯 안 정렬(가운데 · 끝)과 여백 — 세로 상자의 교차축 */
SW_TEST_CASE( UiLayoutTest, AlignmentAndPaddingInSlot )
{
    sw::test::UiLayoutFixture fixture( 200.0f, 200.0f );
    sw::BoxPanel*             pBox = fixture.setRoot<sw::BoxPanel>( "box" );
    pBox->setOrientation( sw::UiOrientation::Vertical );
    sw::Widget* pA = fixture.addFixed( pBox, "a", 50.0f, 20.0f );
    sw::Widget* pB = fixture.addFixed( pBox, "b", 40.0f, 30.0f );
    UiLayoutTestUtil::setAlignment( *pA, sw::UiAlignment::Center, sw::UiAlignment::Fill, sw::float4{ 10.0f, 5.0f, 10.0f, 5.0f } );
    UiLayoutTestUtil::setAlignment( *pB, sw::UiAlignment::End, sw::UiAlignment::Fill, sw::float4{} );
    fixture.update();
    SW_EXPECT_STREQ( "box 0.00 0.00 200.00 200.00\n"
                     "  a 75.00 5.00 50.00 20.00\n"
                     "  b 160.00 30.00 40.00 30.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 채우기 비 1 : 3 → 남은 너비를 1/4 · 3/4 */
SW_TEST_CASE( UiLayoutTest, FillWeightsSplitRemaining )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    sw::BoxPanel*             pBox = fixture.setRoot<sw::BoxPanel>( "box" );
    fixture.addFixed( pBox, "a", 0.0f, 0.0f, sw::UiSizeRule::Fill );
    sw::Widget*          pB   = fixture.addFixed( pBox, "b", 0.0f, 0.0f, sw::UiSizeRule::Fill );
    sw::WidgetLayoutSlot slot = pB->getLayoutSlot();
    slot._fillWeight          = 3.0f;
    pB->setLayoutSlot( slot );
    fixture.update();
    SW_EXPECT_STREQ( "box 0.00 0.00 400.00 100.00\n"
                     "  a 0.00 0.00 100.00 100.00\n"
                     "  b 100.00 0.00 300.00 100.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] Collapsed 는 자리 0 · 간격도 없음, Hidden 은 자리를 지킨다 */
SW_TEST_CASE( UiLayoutTest, CollapsedTakesNoSpaceHiddenDoes )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    sw::BoxPanel*             pBox = fixture.setRoot<sw::BoxPanel>( "box" );
    pBox->setSpacing( 10.0f );
    fixture.addFixed( pBox, "a", 50.0f, 20.0f );
    fixture.addFixed( pBox, "b", 60.0f, 20.0f )->setVisibility( sw::WidgetVisibility::Collapsed );
    fixture.addFixed( pBox, "c", 70.0f, 20.0f )->setVisibility( sw::WidgetVisibility::Hidden );
    fixture.addFixed( pBox, "d", 80.0f, 20.0f );
    fixture.update();
    SW_EXPECT_STREQ( "box 0.00 0.00 400.00 100.00\n"
                     "  a 0.00 0.00 50.00 100.00\n"
                     "  b collapsed\n"
                     "  c 60.00 0.00 70.00 100.00\n"
                     "  d 140.00 0.00 80.00 100.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 접힌 패널 아래에서 바뀐 크기는 다시 보일 때 반영된다(접힌 동안 더러움을 지우지 않는다) */
SW_TEST_CASE( UiLayoutTest, CollapsedSubtreeChangeAppliesWhenShown )
{
    sw::test::UiLayoutFixture  fixture( 400.0f, 100.0f );
    sw::BoxPanel*              pRow   = fixture.setRoot<sw::BoxPanel>( "row" );
    sw::BoxPanel*              pInner = fixture.addPanel<sw::BoxPanel>( pRow, "inner" );
    sw::test::TestFixedWidget* pA     = fixture.addFixed( pInner, "a", 50.0f, 20.0f );
    fixture.addFixed( pRow, "b", 30.0f, 10.0f );
    pInner->setVisibility( sw::WidgetVisibility::Collapsed );
    fixture.update();
    pA->setSize( sw::float2{ 80.0f, 20.0f } );
    fixture.update();
    pInner->setVisibility( sw::WidgetVisibility::Visible );
    fixture.update();
    SW_EXPECT_STREQ( "row 0.00 0.00 400.00 100.00\n"
                     "  inner 0.00 0.00 80.00 100.00\n"
                     "    a 0.00 0.00 80.00 100.00\n"
                     "  b 80.00 0.00 30.00 100.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 캔버스: 위 변에 늘어난 띠 · 가운데 점 · 오른쪽 아래 모서리에 붙는 자동 크기(Begin) */
SW_TEST_CASE( UiLayoutTest, CanvasAnchorsStretchAndPoint )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 300.0f );
    sw::CanvasPanel*          pCanvas = fixture.setRoot<sw::CanvasPanel>( "canvas" );
    sw::Widget*               pBand   = fixture.addFixed( pCanvas, "band", 5.0f, 5.0f );
    sw::Widget*               pPoint  = fixture.addFixed( pCanvas, "point", 5.0f, 5.0f );
    sw::Widget*               pCorner = fixture.addFixed( pCanvas, "corner", 60.0f, 20.0f );
    UiLayoutTestUtil::setAnchors( *pBand, sw::float2{ 0.0f, 0.0f }, sw::float2{ 1.0f, 0.0f }, sw::float2{ 10.0f, 10.0f }, sw::float2{ -10.0f, 40.0f } );
    UiLayoutTestUtil::setAnchors( *pPoint, sw::float2{ 0.5f, 0.5f }, sw::float2{ 0.5f, 0.5f }, sw::float2{ -25.0f, -25.0f }, sw::float2{ 25.0f, 25.0f } );
    UiLayoutTestUtil::setAnchors( *pCorner, sw::float2{ 1.0f, 1.0f }, sw::float2{ 1.0f, 1.0f }, sw::float2{}, sw::float2{} );
    sw::WidgetLayoutSlot slot = pCorner->getLayoutSlot();
    slot._bAutoSize           = true;
    slot._growHorizontal      = sw::UiGrowDirection::Begin;
    slot._growVertical        = sw::UiGrowDirection::Begin;
    pCorner->setLayoutSlot( slot );
    fixture.update();
    SW_EXPECT_STREQ( "canvas 0.00 0.00 400.00 300.00\n"
                     "  band 10.00 10.00 380.00 30.00\n"
                     "  point 175.00 125.00 50.00 50.00\n"
                     "  corner 340.00 280.00 60.00 20.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 겹침: 채우는 바탕 · 가운데 글 · 오른쪽 위 배지(여백) */
SW_TEST_CASE( UiLayoutTest, OverlayStacksChildrenWithAlignment )
{
    sw::test::UiLayoutFixture fixture( 300.0f, 200.0f );
    sw::OverlayPanel*         pOverlay = fixture.setRoot<sw::OverlayPanel>( "overlay" );
    fixture.addFixed( pOverlay, "background", 10.0f, 10.0f );
    sw::Widget* pLabel = fixture.addFixed( pOverlay, "label", 100.0f, 40.0f );
    sw::Widget* pBadge = fixture.addFixed( pOverlay, "badge", 20.0f, 20.0f );
    UiLayoutTestUtil::setAlignment( *pLabel, sw::UiAlignment::Center, sw::UiAlignment::Center, sw::float4{} );
    UiLayoutTestUtil::setAlignment( *pBadge, sw::UiAlignment::End, sw::UiAlignment::Start, sw::float4{ 0.0f, 5.0f, 5.0f, 0.0f } );
    fixture.update();
    SW_EXPECT_STREQ( "overlay 0.00 0.00 300.00 200.00\n"
                     "  background 0.00 0.00 300.00 200.00\n"
                     "  label 100.00 80.00 100.00 40.00\n"
                     "  badge 275.00 5.00 20.00 20.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] Fill 칸의 줄 바꿈 글은 그 칸 너비로 높이를 정하고, 뷰포트 너비가 바뀌면 같은 걷기에 높이가 바뀐다 */
SW_TEST_CASE( UiLayoutTest, WrappedTextHeightFollowsFillWidth )
{
    sw::test::UiLayoutFixture fixture( 300.0f, 400.0f );
    sw::BoxPanel*             pColumn = fixture.setRoot<sw::BoxPanel>( "column" );
    pColumn->setOrientation( sw::UiOrientation::Vertical );
    sw::BoxPanel* pRow = fixture.addPanel<sw::BoxPanel>( pColumn, "row" );
    fixture.addFixed( pRow, "icon", 100.0f, 20.0f );
    sw::Widget*          pText = pRow->addChild( sw::make_unique<sw::test::TestWrapWidget>( "text", 50 ) );
    sw::WidgetLayoutSlot slot  = pText->getLayoutSlot();
    slot._sizeRule             = sw::UiSizeRule::Fill;
    pText->setLayoutSlot( slot );
    fixture.update();
    SW_EXPECT_STREQ( "column 0.00 0.00 300.00 400.00\n"
                     "  row 0.00 0.00 300.00 60.00\n"
                     "    icon 0.00 0.00 100.00 60.00\n"
                     "    text 100.00 0.00 200.00 60.00\n",
                     fixture.dump().c_str() );

    fixture.getContext()._viewportSize = sw::float2{ 600.0f, 400.0f };
    fixture.update();
    SW_EXPECT_STREQ( "column 0.00 0.00 600.00 400.00\n"
                     "  row 0.00 0.00 600.00 20.00\n"
                     "    icon 0.00 0.00 100.00 20.00\n"
                     "    text 100.00 0.00 500.00 20.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 고정 크기 패널(레이아웃 경계) 아래 위젯 하나가 바뀌면 그 패널 아래만 다시 잰다 — 루트 · 형제는 0 */
SW_TEST_CASE( UiLayoutTest, OnlyDirtySubtreeIsMeasured )
{
    sw::test::UiLayoutFixture  fixture( 400.0f, 300.0f );
    sw::CanvasPanel*           pCanvas = fixture.setRoot<sw::CanvasPanel>( "canvas" );
    sw::BoxPanel*              pFixed  = fixture.addPanel<sw::BoxPanel>( pCanvas, "fixed" );
    sw::test::TestFixedWidget* pA      = fixture.addFixed( pFixed, "a", 50.0f, 20.0f );
    sw::test::TestFixedWidget* pB      = fixture.addFixed( pFixed, "b", 30.0f, 20.0f );
    sw::test::TestFixedWidget* pOther  = fixture.addFixed( pCanvas, "other", 30.0f, 20.0f );
    sw::WidgetLayoutSlot       slot    = pFixed->getLayoutSlot();
    slot._widthOverride                = 200.0f;
    slot._heightOverride               = 40.0f;
    slot._offsetMax                    = sw::float2{ 200.0f, 40.0f };
    pFixed->setLayoutSlot( slot );
    SW_EXPECT_TRUE( pFixed->isLayoutBoundary() );
    fixture.update();
    SW_EXPECT_EQUAL( 1u, pA->getMeasureCount() );
    SW_EXPECT_EQUAL( 1u, pB->getMeasureCount() );
    SW_EXPECT_EQUAL( 1u, pOther->getMeasureCount() );

    pA->setSize( sw::float2{ 70.0f, 20.0f } );
    SW_EXPECT_EQUAL( 2u, fixture.update() ); // 경계 패널 자신 + a
    SW_EXPECT_EQUAL( 2u, pA->getMeasureCount() );
    SW_EXPECT_EQUAL( 1u, pB->getMeasureCount() );
    SW_EXPECT_EQUAL( 1u, pOther->getMeasureCount() );
    SW_EXPECT_STREQ( "canvas 0.00 0.00 400.00 300.00\n"
                     "  fixed 0.00 0.00 200.00 40.00\n"
                     "    a 0.00 0.00 70.00 40.00\n"
                     "    b 70.00 0.00 30.00 40.00\n"
                     "  other 0.00 0.00 100.00 30.00\n",
                     fixture.dump().c_str() );
    SW_EXPECT_EQUAL( 0u, fixture.update() ); // 더러운 것이 없으면 아무것도 재지 않는다
}

/** @brief [UiLayoutTest] 배율 1.5 에서 회전 없는 축은 물리 픽셀에 맞춘다: x 10.3(15.45 px) → 10.0(15 px) */
SW_TEST_CASE( UiLayoutTest, PixelSnapAtFractionalScale )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 300.0f );
    fixture.getContext()._uiScale = 1.5f;
    sw::CanvasPanel* pCanvas      = fixture.setRoot<sw::CanvasPanel>( "canvas" );
    sw::Widget*      pChild       = fixture.addFixed( pCanvas, "c", 5.0f, 5.0f );
    UiLayoutTestUtil::setAnchors( *pChild, sw::float2{}, sw::float2{}, sw::float2{ 10.3f, 0.0f }, sw::float2{ 60.3f, 20.0f } );
    fixture.update();
    SW_EXPECT_STREQ( "canvas 0.00 0.00 400.00 300.00\n"
                     "  c 10.00 0.00 50.00 20.00\n",
                     fixture.dump().c_str() );
    SW_EXPECT_STREQ( "canvas 0.00 0.00 600.00 450.00\n"
                     "  c 15.00 0.00 75.00 30.00\n",
                     fixture.dump( 1.5f ).c_str() );
}
