#include "pch.h"

#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/GridPanel.h"
#include "Engine/UI/Layout/OverlayPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/Layout/WrapPanel.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"

#include "EngineTest/LocalizationTestUtil.h"
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

    struct UiLayoutGridTestUtil
    {
        static sw::UiGridTrack makeTrack( sw::UiGridTrackKind kind, float32 value )
        {
            sw::UiGridTrack track{};
            track._kind  = kind;
            track._value = value;
            return track;
        }

        static void setCell( sw::Widget& widget, uint16 column, uint16 row, uint16 columnSpan, uint16 rowSpan )
        {
            sw::WidgetLayoutSlot slot = widget.getLayoutSlot();
            slot._column              = column;
            slot._row                 = row;
            slot._columnSpan          = columnSpan;
            slot._rowSpan             = rowSpan;
            widget.setLayoutSlot( slot );
        }

        /**
         * @brief 캔버스 루트를 채우는 세로 스크롤 패널(200×100) 안에 세로 상자 하나 · 높이 40 줄 다섯(내용 높이 200)을 짓습니다.
         * @details 스크롤 패널을 루트가 아닌 자리에 둔다 — 루트면 뷰포트 걷기가 늘 다시 놓아 kArrange 뿌리 경로를 시험하지 못한다.
         */
        static sw::ScrollPanel* makeScrollList( sw::test::UiLayoutFixture& fixture, sw::vector<sw::test::TestFixedWidget*>& outListItem )
        {
            sw::CanvasPanel*     pCanvas = fixture.setRoot<sw::CanvasPanel>( "canvas" );
            sw::ScrollPanel*     pScroll = fixture.addPanel<sw::ScrollPanel>( pCanvas, "scroll" );
            sw::WidgetLayoutSlot slot    = pScroll->getLayoutSlot();
            slot._anchorMax              = sw::float2{ 1.0f, 1.0f };
            slot._offsetMax              = sw::float2{};
            pScroll->setLayoutSlot( slot );
            sw::BoxPanel* pContent = fixture.addPanel<sw::BoxPanel>( pScroll, "content" );
            pContent->setOrientation( sw::UiOrientation::Vertical );
            const utf8* const arrName[] = { "item0", "item1", "item2", "item3", "item4" };
            for ( uint32 index = 0; index < 5; ++index )
            {
                outListItem.push_back( fixture.addFixed( pContent, sw::hashed_string( arrName[index] ), 50.0f, 40.0f ) );
            }
            return pScroll;
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

/** @brief [UiLayoutTest] 격자 열 트랙 Auto · Fixed · Fill — Auto 는 내용, Fixed 는 값, Fill 은 남은 것(간격 10) */
SW_TEST_CASE( UiLayoutTest, GridAutoFixedFillTracks )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    sw::GridPanel*            pGrid = fixture.setRoot<sw::GridPanel>( "grid" );
    pGrid->setColumns( { UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Auto, 0.0f ), UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Fixed, 100.0f ),
                         UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Fill, 1.0f ) } );
    pGrid->setCellSpacing( sw::float2{ 10.0f, 0.0f } );
    UiLayoutGridTestUtil::setCell( *fixture.addFixed( pGrid, "a", 50.0f, 20.0f ), 0, 0, 1, 1 );
    UiLayoutGridTestUtil::setCell( *fixture.addFixed( pGrid, "b", 30.0f, 30.0f ), 1, 0, 1, 1 );
    UiLayoutGridTestUtil::setCell( *fixture.addFixed( pGrid, "c", 10.0f, 10.0f ), 2, 0, 1, 1 );
    fixture.update();
    SW_EXPECT_STREQ( "grid 0.00 0.00 400.00 100.00\n"
                     "  a 0.00 0.00 50.00 100.00\n"
                     "  b 60.00 0.00 100.00 100.00\n"
                     "  c 170.00 0.00 230.00 100.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 넓이 2 자식이 덮은 Auto 트랙 합보다 크면 모자란 만큼을 그 Auto 트랙들에 고르게 더한다 */
SW_TEST_CASE( UiLayoutTest, GridSpanGrowsAutoTracks )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    sw::GridPanel*            pGrid = fixture.setRoot<sw::GridPanel>( "grid" );
    pGrid->setColumns( { UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Auto, 0.0f ), UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Auto, 0.0f ),
                         UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Fill, 1.0f ) } );
    pGrid->setRows( { UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Auto, 0.0f ), UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Auto, 0.0f ) } );
    UiLayoutGridTestUtil::setCell( *fixture.addFixed( pGrid, "a", 30.0f, 20.0f ), 0, 0, 1, 1 );
    UiLayoutGridTestUtil::setCell( *fixture.addFixed( pGrid, "b", 100.0f, 20.0f ), 0, 1, 2, 1 );
    fixture.update();
    SW_EXPECT_STREQ( "grid 0.00 0.00 400.00 100.00\n"
                     "  a 0.00 0.00 65.00 20.00\n"
                     "  b 0.00 20.00 100.00 20.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 트랙 밖 열 번호는 경고 한 번 + 마지막 트랙으로 */
SW_TEST_CASE( UiLayoutTest, GridOutOfRangeCellWarns )
{
    SW_TEST_DEFENSIVE_SCOPE( "a grid child names a column past the last track" );
    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    sw::GridPanel*            pGrid = fixture.setRoot<sw::GridPanel>( "grid" );
    pGrid->setColumns( { UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Fixed, 50.0f ), UiLayoutGridTestUtil::makeTrack( sw::UiGridTrackKind::Fixed, 50.0f ) } );
    UiLayoutGridTestUtil::setCell( *fixture.addFixed( pGrid, "a", 10.0f, 10.0f ), 5, 0, 1, 1 );
    fixture.update();
    SW_EXPECT_EQUAL( 1u, pGrid->getOutOfRangeCellCount() );
    SW_EXPECT_STREQ( "grid 0.00 0.00 400.00 100.00\n"
                     "  a 50.00 0.00 50.00 100.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 흐름 패널: 줄이 차면 다음 줄(칸 간격 10 · 줄 간격 5), 줄 높이는 그 줄의 최대 */
SW_TEST_CASE( UiLayoutTest, WrapPanelBreaksRows )
{
    sw::test::UiLayoutFixture fixture( 250.0f, 200.0f );
    sw::WrapPanel*            pWrap = fixture.setRoot<sw::WrapPanel>( "wrap" );
    pWrap->setItemSpacing( 10.0f );
    pWrap->setLineSpacing( 5.0f );
    fixture.addFixed( pWrap, "a", 100.0f, 20.0f );
    fixture.addFixed( pWrap, "b", 100.0f, 30.0f );
    fixture.addFixed( pWrap, "c", 100.0f, 20.0f );
    fixture.update();
    SW_EXPECT_STREQ( "wrap 0.00 0.00 250.00 200.00\n"
                     "  a 0.00 0.00 100.00 30.00\n"
                     "  b 110.00 0.00 100.00 30.00\n"
                     "  c 0.00 35.00 100.00 20.00\n",
                     fixture.dump().c_str() );
}

/** @brief [UiLayoutTest] 스크롤 오프셋은 [0, 내용 − 보이는 크기] 로 묶이고, 내용은 -오프셋에 놓이며 패널은 자식을 자른다 */
SW_TEST_CASE( UiLayoutTest, ScrollClampsOffsetAndClips )
{
    sw::test::UiLayoutFixture              fixture( 200.0f, 100.0f );
    sw::vector<sw::test::TestFixedWidget*> listItem;
    sw::ScrollPanel*                       pScroll = UiLayoutGridTestUtil::makeScrollList( fixture, listItem );
    fixture.update();
    SW_EXPECT_TRUE( pScroll->clipsChildren() );
    SW_EXPECT_NEAR_EQUAL( 200.0f, pScroll->getContentSize()._y, 0.001f );
    pScroll->setScrollOffset( sw::float2{ 30.0f, 500.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pScroll->getScrollOffset()._x, 0.001f ); // 가로는 스크롤 축이 아니다
    SW_EXPECT_NEAR_EQUAL( 100.0f, pScroll->getScrollOffset()._y, 0.001f );
    fixture.update();
    SW_EXPECT_NEAR_EQUAL( -100.0f, fixture.getTree().findWidgetByName( "content" )->getGeometry()._position._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 60.0f, listItem[4]->getGeometry()._position._y, 0.001f );
    pScroll->setScrollOffset( sw::float2{ 0.0f, -20.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pScroll->getScrollOffset()._y, 0.001f );
}

/** @brief [UiLayoutTest] 보이게 하기는 최소한만 옮긴다 — 아래로 넘친 줄은 아래 변에, 위로 넘친 줄은 위 변에, 보이는 줄은 그대로 */
SW_TEST_CASE( UiLayoutTest, ScrollIntoViewMovesMinimally )
{
    sw::test::UiLayoutFixture              fixture( 200.0f, 100.0f );
    sw::vector<sw::test::TestFixedWidget*> listItem;
    sw::ScrollPanel*                       pScroll = UiLayoutGridTestUtil::makeScrollList( fixture, listItem );
    pScroll->setNavigationMargin( 0.0f );
    fixture.update();
    SW_EXPECT_TRUE( pScroll->scrollIntoView( *listItem[3] ) ); // 120..160 → 아래 변 100 에 맞춘다
    SW_EXPECT_NEAR_EQUAL( 60.0f, pScroll->getScrollOffset()._y, 0.001f );
    fixture.update();
    SW_EXPECT_FALSE( pScroll->scrollIntoView( *listItem[2] ) ); // 20..60 — 이미 보인다
    SW_EXPECT_TRUE( pScroll->scrollIntoView( *listItem[0] ) );  // -60..-20 → 위 변 0 에
    SW_EXPECT_NEAR_EQUAL( 0.0f, pScroll->getScrollOffset()._y, 0.001f );
}

/** @brief [UiLayoutTest] 스크롤 오프셋 변화는 배치만 다시 한다 — 다시 잰 위젯 0 */
SW_TEST_CASE( UiLayoutTest, ScrollOffsetChangeIsArrangeOnly )
{
    sw::test::UiLayoutFixture              fixture( 200.0f, 100.0f );
    sw::vector<sw::test::TestFixedWidget*> listItem;
    sw::ScrollPanel*                       pScroll = UiLayoutGridTestUtil::makeScrollList( fixture, listItem );
    fixture.update();
    pScroll->setScrollOffset( sw::float2{ 0.0f, 30.0f } );
    SW_EXPECT_EQUAL( 0u, fixture.update() );
    SW_EXPECT_EQUAL( 1u, listItem[0]->getMeasureCount() );
    SW_EXPECT_NEAR_EQUAL( -30.0f, listItem[0]->getGeometry()._position._y, 0.001f );
}

/**
 * @brief [UiLayoutTest] `UiSystem::update` 는 화면 트리마다 레이아웃을 돌린다 — 루트는 뷰포트(UI 단위) 전체, 자식은 자기 슬롯대로 놓인다
 * @details 변이: `UiSystem::update` 의 `UiLayoutPass::update` 줄을 빼면 라벨이 놓이지 않아 진다.
 */
SW_TEST_CASE( UiLayoutTest, UiSystemLaysOutScreenTrees )
{
    sw::UiSystem                     ui;
    sw::unique_ptr<sw::OverlayPanel> root   = sw::make_unique<sw::OverlayPanel>();
    sw::Widget*                      pLabel = root->addChild( sw::make_unique<sw::test::TestFixedWidget>( "label", sw::float2{ 100.0f, 40.0f } ) );
    UiLayoutTestUtil::setAlignment( *pLabel, sw::UiAlignment::Center, sw::UiAlignment::Center, sw::float4{} );
    sw::UiScreenDesc desc{};
    desc._layer = sw::UiLayer::Hud;
    (void)ui.pushScreen( sw::make_unique<sw::UiScreen>( desc, std::move( root ) ) );

    sw::UiViewport viewport{};
    viewport._size         = sw::float2{ 800.0f, 600.0f };
    viewport._physicalSize = sw::float2{ 1600.0f, 1200.0f };
    viewport._uiScale      = 2.0f;
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_NEAR_EQUAL( 350.0f, pLabel->getGeometry()._position._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 280.0f, pLabel->getGeometry()._position._y, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, pLabel->getGeometry()._size._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 40.0f, pLabel->getGeometry()._size._y, 0.001f );
}

/**
 * @brief [UiLayoutTest] 오른쪽에서 왼쪽 문화권: 가로 상자 순서가 거꾸로, 캔버스 앵커 · 오프셋 x 가 거울(오른쪽 아래 모서리 → 왼쪽 아래), 슬롯 여백 왼 ↔ 오 ·
 *        정렬 Start ↔ End
 */
SW_TEST_CASE( UiLayoutTest, RtlMirrorsBoxCanvasAndAlignment )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 300.0f );
    fixture.getContext()._bRightToLeft = true;
    sw::CanvasPanel* pCanvas           = fixture.setRoot<sw::CanvasPanel>( "canvas" );
    sw::BoxPanel*    pRow              = fixture.addPanel<sw::BoxPanel>( pCanvas, "row" );
    pRow->setSpacing( 10.0f );
    UiLayoutTestUtil::setAnchors( *pRow, sw::float2{ 0.0f, 0.0f }, sw::float2{ 1.0f, 0.0f }, sw::float2{ 20.0f, 10.0f }, sw::float2{ -10.0f, 40.0f } );
    fixture.addFixed( pRow, "a", 50.0f, 20.0f );
    fixture.addFixed( pRow, "b", 70.0f, 20.0f );
    sw::BoxPanel* pColumn = fixture.addPanel<sw::BoxPanel>( pCanvas, "column" );
    pColumn->setOrientation( sw::UiOrientation::Vertical );
    UiLayoutTestUtil::setAnchors( *pColumn, sw::float2{ 0.0f, 0.0f }, sw::float2{ 1.0f, 0.0f }, sw::float2{ 0.0f, 50.0f }, sw::float2{ 0.0f, 150.0f } );
    sw::Widget* pC = fixture.addFixed( pColumn, "c", 50.0f, 20.0f );
    sw::Widget* pD = fixture.addFixed( pColumn, "d", 40.0f, 30.0f );
    UiLayoutTestUtil::setAlignment( *pC, sw::UiAlignment::Start, sw::UiAlignment::Fill, sw::float4{ 10.0f, 0.0f, 0.0f, 0.0f } );
    UiLayoutTestUtil::setAlignment( *pD, sw::UiAlignment::End, sw::UiAlignment::Fill, sw::float4{} );
    sw::Widget* pCorner = fixture.addFixed( pCanvas, "corner", 60.0f, 20.0f );
    UiLayoutTestUtil::setAnchors( *pCorner, sw::float2{ 1.0f, 1.0f }, sw::float2{ 1.0f, 1.0f }, sw::float2{}, sw::float2{} );
    sw::WidgetLayoutSlot slot = pCorner->getLayoutSlot();
    slot._bAutoSize           = true;
    slot._growHorizontal      = sw::UiGrowDirection::Begin;
    slot._growVertical        = sw::UiGrowDirection::Begin;
    pCorner->setLayoutSlot( slot );
    fixture.update();
    SW_EXPECT_STREQ( "canvas 0.00 0.00 400.00 300.00\n"
                     "  row 10.00 10.00 370.00 30.00\n"
                     "    a 330.00 10.00 50.00 30.00\n"
                     "    b 250.00 10.00 70.00 30.00\n"
                     "  column 0.00 50.00 400.00 100.00\n"
                     "    c 340.00 50.00 50.00 20.00\n"
                     "    d 0.00 70.00 40.00 30.00\n"
                     "  corner 0.00 280.00 60.00 20.00\n",
                     fixture.dump().c_str() );
    SW_EXPECT_TRUE( pRow->isRightToLeft() );
}

/** @brief [UiLayoutTest] 흐름 방향을 LeftToRight 로 고정한 패널(숫자 칸 · 시계)은 RTL 문화권에서도 자식을 왼쪽부터 놓고, Inherit 으로 되돌리면 배치만 다시 한다 */
SW_TEST_CASE( UiLayoutTest, FlowDirectionOverrideStaysLtr )
{
    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    fixture.getContext()._bRightToLeft = true;
    sw::BoxPanel*              pRoot   = fixture.setRoot<sw::BoxPanel>( "box" );
    sw::BoxPanel*              pInner  = fixture.addPanel<sw::BoxPanel>( pRoot, "inner" );
    sw::test::TestFixedWidget* pX      = fixture.addFixed( pInner, "x", 50.0f, 20.0f );
    fixture.addFixed( pInner, "y", 60.0f, 20.0f );
    fixture.addFixed( pRoot, "z", 30.0f, 20.0f );
    pInner->setFlowDirection( sw::UiFlowDirection::LeftToRight );
    fixture.update();
    SW_EXPECT_STREQ( "box 0.00 0.00 400.00 100.00\n"
                     "  inner 290.00 0.00 110.00 100.00\n"
                     "    x 290.00 0.00 50.00 100.00\n"
                     "    y 340.00 0.00 60.00 100.00\n"
                     "  z 260.00 0.00 30.00 100.00\n",
                     fixture.dump().c_str() );
    SW_EXPECT_TRUE( pRoot->isRightToLeft() );
    SW_EXPECT_FALSE( pInner->isRightToLeft() );
    SW_EXPECT_FALSE( pX->isRightToLeft() );

    pInner->setFlowDirection( sw::UiFlowDirection::Inherit );
    SW_EXPECT_EQUAL( 0u, fixture.update() ); // 크기는 그대로 — 배치만
    SW_EXPECT_STREQ( "box 0.00 0.00 400.00 100.00\n"
                     "  inner 290.00 0.00 110.00 100.00\n"
                     "    x 350.00 0.00 50.00 100.00\n"
                     "    y 290.00 0.00 60.00 100.00\n"
                     "  z 260.00 0.00 30.00 100.00\n",
                     fixture.dump().c_str() );
    SW_EXPECT_TRUE( pX->isRightToLeft() );
}

/** @brief [UiLayoutTest] 의사 문화권 qps-plocm(RLO 거울)은 오른쪽에서 왼쪽 문화권이라 배치를 거울로 켜고, en 으로 돌아오면 더러움 없이도 다시 놓는다 */
SW_TEST_CASE( UiLayoutTest, PseudoMirroredLocaleFlipsLayout )
{
    sw::LocalizationManager loc;
    SW_ASSERT_TRUE( sw::test::LocalizationTestUtil::loadEngineCultures( loc ) );
    SW_ASSERT_TRUE( loc.setCurrentLanguage( "qps-plocm" ) );
    SW_ASSERT_TRUE( sw::UiLayoutPass::isCultureRightToLeft( &loc ) );

    sw::test::UiLayoutFixture fixture( 400.0f, 100.0f );
    fixture.getContext()._bRightToLeft = sw::UiLayoutPass::isCultureRightToLeft( &loc );
    sw::BoxPanel* pBox                 = fixture.setRoot<sw::BoxPanel>( "box" );
    fixture.addFixed( pBox, "a", 50.0f, 20.0f );
    fixture.addFixed( pBox, "b", 70.0f, 20.0f );
    fixture.update();
    SW_EXPECT_STREQ( "box 0.00 0.00 400.00 100.00\n"
                     "  a 350.00 0.00 50.00 100.00\n"
                     "  b 280.00 0.00 70.00 100.00\n",
                     fixture.dump().c_str() );

    SW_ASSERT_TRUE( loc.setCurrentLanguage( "en" ) );
    fixture.getContext()._bRightToLeft = sw::UiLayoutPass::isCultureRightToLeft( &loc );
    SW_EXPECT_EQUAL( 0u, fixture.update() );
    SW_EXPECT_STREQ( "box 0.00 0.00 400.00 100.00\n"
                     "  a 0.00 0.00 50.00 100.00\n"
                     "  b 50.00 0.00 70.00 100.00\n",
                     fixture.dump().c_str() );
}

/**
 * @brief [UiLayoutTest] `UiSystem` 의 레이아웃 문맥은 문화권 방향을 읽는다 — 거울 의사 문화권(qps-plocm)이면 화면 루트의 가로 상자가 오른쪽부터 놓인다
 * @details 변이: `UiSystem::makeLayoutContext` 의 `_bRightToLeft` 줄을 빼면 a 가 왼쪽에 남아 진다.
 */
SW_TEST_CASE( UiLayoutTest, UiSystemFollowsCultureDirection )
{
    sw::LocalizationManager loc;
    SW_ASSERT_TRUE( sw::test::LocalizationTestUtil::loadEngineCultures( loc ) );
    SW_ASSERT_TRUE( loc.setCurrentLanguage( "qps-plocm" ) );

    sw::UiSystem ui;
    ui.setLocalization( &loc );
    sw::unique_ptr<sw::BoxPanel> root = sw::make_unique<sw::BoxPanel>();
    sw::Widget*                  pA   = root->addChild( sw::make_unique<sw::test::TestFixedWidget>( "a", sw::float2{ 50.0f, 20.0f } ) );
    sw::UiScreenDesc             desc{};
    desc._layer = sw::UiLayer::Hud;
    (void)ui.pushScreen( sw::make_unique<sw::UiScreen>( desc, std::move( root ) ) );
    sw::UiViewport viewport{};
    viewport._size         = sw::float2{ 400.0f, 100.0f };
    viewport._physicalSize = viewport._size;
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_TRUE( ui.makeLayoutContext()._bRightToLeft );
    SW_EXPECT_NEAR_EQUAL( 350.0f, pA->getGeometry()._position._x, 0.001f );

    SW_ASSERT_TRUE( loc.setCurrentLanguage( "en" ) );
    ui.update( 1.0f / 60.0f, viewport );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pA->getGeometry()._position._x, 0.001f );
}
