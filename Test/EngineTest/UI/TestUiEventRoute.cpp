#include "pch.h"

#include "Engine/UI/Core/PanelWidget.h"
#include "Engine/UI/Core/UiEventRouter.h"
#include "Engine/UI/Core/UiPointerState.h"
#include "Engine/UI/Core/WidgetTree.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"

#include "EngineTest/UI/UiLayoutTestUtil.h"
#include "EngineTest/UI/UiTestWidgets.h"

#include "TestFramework/TestFramework.h"

// UiEventRouteTest — 히트 테스트(보임 · 렌더 변환 · 자르기) · 사건 경로(터널링 → 버블링 · 처리에서 멈춤) · 포인터 잡기 · 호버 알림. 디바이스 없음(nogpu).

namespace
{
    /** @brief 루트 패널(0, 0, 400 × 300)이 든 트리입니다. 위젯은 붙이고 `place` 로 화면 사각형에 놓습니다. */
    struct UiRouteFixture
    {
        sw::WidgetTree               _tree;
        sw::uitest::UiEventRecord    _record;
        sw::uitest::TestPanelWidget* _pRoot;

        UiRouteFixture()
            : _tree{}
            , _record{}
            , _pRoot{ nullptr }
        {
            auto root        = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
            _pRoot           = root.get();
            _pRoot->_pRecord = &_record;
            _tree.setRoot( std::move( root ) );
            sw::uitest::UiTestUtil::placeWidget( *_pRoot, 0.0f, 0.0f, 400.0f, 300.0f );
        }

        sw::uitest::TestPanelWidget* addPanel( sw::PanelWidget& parent, const sw::hashed_string& name, float32 x, float32 y, float32 width, float32 height )
        {
            auto* pPanel     = static_cast<sw::uitest::TestPanelWidget*>( parent.addChild( sw::make_unique<sw::uitest::TestPanelWidget>( name ) ) );
            pPanel->_pRecord = &_record;
            sw::uitest::UiTestUtil::placeWidget( *pPanel, x, y, width, height );
            return pPanel;
        }

        sw::uitest::TestBoxWidget* addBox( sw::PanelWidget& parent, const sw::hashed_string& name, float32 x, float32 y, float32 width, float32 height )
        {
            auto* pBox     = static_cast<sw::uitest::TestBoxWidget*>( parent.addChild( sw::make_unique<sw::uitest::TestBoxWidget>( name ) ) );
            pBox->_pRecord = &_record;
            sw::uitest::UiTestUtil::placeWidget( *pBox, x, y, width, height );
            return pBox;
        }

        /** @brief 점 아래 경로의 잎 이름입니다(없으면 빈 글). */
        sw::string hitLeafName( float32 x, float32 y ) const
        {
            sw::UiWidgetPath path{};
            if ( sw::UiEventRouter::hitTest( _tree, sw::float2{ x, y }, path ) == false )
                return sw::string{};
            const sw::Widget* pLeaf = _tree.findWidgetById( path.getLeaf() );
            return pLeaf != nullptr ? sw::string{ pLeaf->getName().c_str() } : sw::string{};
        }

        static sw::UiPointerEvent makePointer( sw::UiPointerEventKind kind, float32 x, float32 y )
        {
            sw::UiPointerEvent event{};
            event._kind     = kind;
            event._position = sw::float2{ x, y };
            return event;
        }
    };

    struct UiScrollRouteUtil
    {
        /** @brief 200 × 100 스크롤 패널(루트) 안에 높이 40 항목 다섯(내용 200 — 최대 오프셋 100)을 짓고 놓습니다. */
        static sw::ScrollPanel* makeScrollList( sw::test::UiLayoutFixture& fixture )
        {
            sw::ScrollPanel* pScroll  = fixture.setRoot<sw::ScrollPanel>( "scroll" );
            sw::BoxPanel*    pContent = fixture.addPanel<sw::BoxPanel>( pScroll, "content" );
            pContent->setOrientation( sw::UiOrientation::Vertical );
            for ( uint32 index = 0; index < 5; ++index )
            {
                fixture.addFixed( pContent, sw::hashed_string( "item" + sw::to_string( index ) ), 50.0f, 40.0f );
            }
            fixture.update();
            return pScroll;
        }

        static sw::UiPointerEvent makeEvent( sw::UiPointerEventKind kind, float32 x, float32 y )
        {
            sw::UiPointerEvent event{};
            event._kind     = kind;
            event._position = sw::float2{ x, y };
            return event;
        }
    };
} // namespace

/** @brief [UiEventRouteTest] 겹친 두 자식 — 뒤에 붙인 것이 위라 겹친 곳은 위 자식, 경로는 뿌리 → 잎 */
SW_TEST_CASE( UiEventRouteTest, HitTestPicksTopmostDeepest )
{
    UiRouteFixture               fixture;
    sw::uitest::TestPanelWidget* pBack = fixture.addPanel( *fixture._pRoot, "back", 0.0f, 0.0f, 200.0f, 200.0f );
    fixture.addBox( *pBack, "backBox", 0.0f, 0.0f, 50.0f, 50.0f );
    fixture.addBox( *fixture._pRoot, "front", 100.0f, 100.0f, 200.0f, 150.0f );

    SW_EXPECT_STREQ( "front", fixture.hitLeafName( 150.0f, 150.0f ).c_str() );
    SW_EXPECT_STREQ( "back", fixture.hitLeafName( 60.0f, 60.0f ).c_str() );
    SW_EXPECT_STREQ( "backBox", fixture.hitLeafName( 10.0f, 10.0f ).c_str() );
    SW_EXPECT_STREQ( "root", fixture.hitLeafName( 350.0f, 50.0f ).c_str() );
    SW_EXPECT_STREQ( "", fixture.hitLeafName( 450.0f, 50.0f ).c_str() );

    sw::UiWidgetPath path{};
    SW_ASSERT_TRUE( sw::UiEventRouter::hitTest( fixture._tree, sw::float2{ 10.0f, 10.0f }, path ) );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( path._listWidget.size() ) );
    SW_EXPECT_EQUAL( fixture._pRoot->getId(), path._listWidget[0] );
    SW_EXPECT_EQUAL( pBack->getId(), path._listWidget[1] );
}

/** @brief [UiEventRouteTest] HitTestInvisible 패널 아래 자식은 못 받고 SelfHitTestInvisible 패널 아래 자식은 받는다 — 패널 자신은 둘 다 못 받는다. Hidden 은 못 받는다 */
SW_TEST_CASE( UiEventRouteTest, HitTestRespectsVisibility )
{
    UiRouteFixture               fixture;
    sw::uitest::TestPanelWidget* pBlocked = fixture.addPanel( *fixture._pRoot, "blocked", 0.0f, 0.0f, 100.0f, 100.0f );
    fixture.addBox( *pBlocked, "blockedChild", 0.0f, 0.0f, 50.0f, 50.0f );
    pBlocked->setVisibility( sw::WidgetVisibility::HitTestInvisible );
    sw::uitest::TestPanelWidget* pPassing = fixture.addPanel( *fixture._pRoot, "passing", 200.0f, 0.0f, 100.0f, 100.0f );
    fixture.addBox( *pPassing, "passingChild", 200.0f, 0.0f, 50.0f, 50.0f );
    pPassing->setVisibility( sw::WidgetVisibility::SelfHitTestInvisible );
    sw::uitest::TestBoxWidget* pHidden = fixture.addBox( *fixture._pRoot, "hidden", 0.0f, 200.0f, 50.0f, 50.0f );
    pHidden->setVisibility( sw::WidgetVisibility::Hidden );

    SW_EXPECT_STREQ( "root", fixture.hitLeafName( 10.0f, 10.0f ).c_str() );
    SW_EXPECT_STREQ( "root", fixture.hitLeafName( 80.0f, 80.0f ).c_str() );
    SW_EXPECT_STREQ( "passingChild", fixture.hitLeafName( 210.0f, 10.0f ).c_str() );
    SW_EXPECT_STREQ( "root", fixture.hitLeafName( 280.0f, 80.0f ).c_str() );
    SW_EXPECT_STREQ( "root", fixture.hitLeafName( 10.0f, 210.0f ).c_str() );
}

/** @brief [UiEventRouteTest] 45° 돌린 정사각형 — 축 상자 모서리(돌린 사각형 밖)는 못 맞히고, 원래 사각형 밖이라도 돌린 사각형 안이면 맞힌다 */
SW_TEST_CASE( UiEventRouteTest, HitTestRespectsRenderTransform )
{
    UiRouteFixture fixture;
    fixture._pRoot->setVisibility( sw::WidgetVisibility::SelfHitTestInvisible );
    sw::uitest::TestBoxWidget* pBox = fixture.addBox( *fixture._pRoot, "diamond", 100.0f, 100.0f, 100.0f, 100.0f );
    sw::WidgetRenderTransform  transform{};
    transform._angle = sw::MathUtil::toRadian( 45.0f );
    pBox->setRenderTransform( transform );
    sw::uitest::UiTestUtil::placeWidget( *pBox, 100.0f, 100.0f, 100.0f, 100.0f );

    SW_EXPECT_STREQ( "diamond", fixture.hitLeafName( 150.0f, 150.0f ).c_str() ); // 가운데(피벗)
    SW_EXPECT_STREQ( "", fixture.hitLeafName( 105.0f, 105.0f ).c_str() );        // 축 상자의 모서리 — |dx| + |dy| = 90 > 70.7
    SW_EXPECT_STREQ( "diamond", fixture.hitLeafName( 150.0f, 85.0f ).c_str() );  // 원래 사각형 위쪽 밖이지만 돌린 꼭짓점 안
}

/** @brief [UiEventRouteTest] 자르는 패널 밖으로 삐져나온 자식 영역의 점은 아무것도 맞히지 않는다 — 자르지 않으면 자식이 맞는다 */
SW_TEST_CASE( UiEventRouteTest, ClippingPanelBlocksOutsidePoints )
{
    UiRouteFixture fixture;
    fixture._pRoot->setVisibility( sw::WidgetVisibility::SelfHitTestInvisible );
    sw::uitest::TestPanelWidget* pClip = fixture.addPanel( *fixture._pRoot, "clip", 0.0f, 0.0f, 100.0f, 100.0f );
    fixture.addBox( *pClip, "overflow", 50.0f, 50.0f, 100.0f, 100.0f );
    pClip->setClipChildren( true );

    SW_EXPECT_STREQ( "overflow", fixture.hitLeafName( 60.0f, 60.0f ).c_str() );
    SW_EXPECT_STREQ( "", fixture.hitLeafName( 120.0f, 120.0f ).c_str() );

    pClip->setClipChildren( false );
    SW_EXPECT_STREQ( "overflow", fixture.hitLeafName( 120.0f, 120.0f ).c_str() );
}

/** @brief [UiEventRouteTest] 루트 > 패널 > 버튼: 터널링(뿌리부터) 다음 버블링(잎부터) */
SW_TEST_CASE( UiEventRouteTest, TunnelThenBubbleOrder )
{
    UiRouteFixture               fixture;
    sw::uitest::TestPanelWidget* pPanel = fixture.addPanel( *fixture._pRoot, "panel", 0.0f, 0.0f, 200.0f, 200.0f );
    fixture.addBox( *pPanel, "button", 10.0f, 10.0f, 50.0f, 50.0f );

    sw::UiWidgetPath path{};
    SW_ASSERT_TRUE( sw::UiEventRouter::hitTest( fixture._tree, sw::float2{ 20.0f, 20.0f }, path ) );
    sw::WidgetId      handler = sw::kInvalidWidgetId;
    const sw::UiReply reply =
        sw::UiEventRouter::routePointerEvent( fixture._tree, path, UiRouteFixture::makePointer( sw::UiPointerEventKind::Down, 20.0f, 20.0f ), handler );
    SW_EXPECT_FALSE( reply.isHandled() );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetId, handler );
    SW_EXPECT_STREQ( "root T, panel T, button T, button B, panel B, root B", fixture._record.joined().c_str() );
}

/** @brief [UiEventRouteTest] 패널이 터널링에서 처리하면 경로가 멈춘다 — 버튼은 아무것도 받지 않는다 */
SW_TEST_CASE( UiEventRouteTest, HandledStopsRoute )
{
    UiRouteFixture               fixture;
    sw::uitest::TestPanelWidget* pPanel = fixture.addPanel( *fixture._pRoot, "panel", 0.0f, 0.0f, 200.0f, 200.0f );
    fixture.addBox( *pPanel, "button", 10.0f, 10.0f, 50.0f, 50.0f );
    pPanel->_bHandleTunnel = true;

    sw::UiWidgetPath path{};
    SW_ASSERT_TRUE( sw::UiEventRouter::hitTest( fixture._tree, sw::float2{ 20.0f, 20.0f }, path ) );
    sw::WidgetId      handler = sw::kInvalidWidgetId;
    const sw::UiReply reply =
        sw::UiEventRouter::routePointerEvent( fixture._tree, path, UiRouteFixture::makePointer( sw::UiPointerEventKind::Down, 20.0f, 20.0f ), handler );
    SW_EXPECT_TRUE( reply.isHandled() );
    SW_EXPECT_EQUAL( pPanel->getId(), handler );
    SW_EXPECT_STREQ( "root T, panel T", fixture._record.joined().c_str() );
}

/** @brief [UiEventRouteTest] Down 에서 포인터를 잡은 슬라이더는 포인터가 밖으로 나가도 Move · Up 을 받고, Up 에서 놓는다 */
SW_TEST_CASE( UiEventRouteTest, CaptureRoutesToCapturerOutsideItsRect )
{
    UiRouteFixture             fixture;
    sw::uitest::TestBoxWidget* pSlider = fixture.addBox( *fixture._pRoot, "slider", 10.0f, 10.0f, 100.0f, 20.0f );
    sw::uitest::TestBoxWidget* pOther  = fixture.addBox( *fixture._pRoot, "other", 200.0f, 200.0f, 100.0f, 50.0f );
    pSlider->_bHandleBubble            = true;
    pSlider->_bCaptureOnDown           = true;
    pOther->_bHandleBubble             = true;

    sw::UiPointerState        pointer;
    const sw::UiPointerResult down = pointer.process( fixture._tree, UiRouteFixture::makePointer( sw::UiPointerEventKind::Down, 20.0f, 20.0f ) );
    SW_EXPECT_TRUE( down._bHandled == SW_TRUE );
    SW_EXPECT_EQUAL( pSlider->getId(), pointer.getCapturedWidget() );

    fixture._record._listLine.clear();
    const sw::UiPointerResult move = pointer.process( fixture._tree, UiRouteFixture::makePointer( sw::UiPointerEventKind::Move, 250.0f, 220.0f ) );
    SW_EXPECT_EQUAL( pSlider->getId(), move._handler ); // "other" 위지만 잡은 슬라이더가 받는다
    SW_EXPECT_STREQ( "root T, slider T, slider B", fixture._record.joined().c_str() );

    const sw::UiPointerResult up = pointer.process( fixture._tree, UiRouteFixture::makePointer( sw::UiPointerEventKind::Up, 250.0f, 220.0f ) );
    SW_EXPECT_EQUAL( pSlider->getId(), up._handler );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetId, pointer.getCapturedWidget() );

    const sw::UiPointerResult after = pointer.process( fixture._tree, UiRouteFixture::makePointer( sw::UiPointerEventKind::Move, 250.0f, 220.0f ) );
    SW_EXPECT_EQUAL( pOther->getId(), after._handler ); // 놓은 뒤에는 다시 히트 테스트
}

/** @brief [UiEventRouteTest] A 에서 B 로 옮기면 A 쪽은 잎부터 Leave, B 쪽은 뿌리부터 Enter — 공통 조상(루트)은 아무것도 받지 않는다 */
SW_TEST_CASE( UiEventRouteTest, HoverEnterLeaveDiff )
{
    UiRouteFixture               fixture;
    sw::uitest::TestPanelWidget* pPanelA = fixture.addPanel( *fixture._pRoot, "panelA", 0.0f, 0.0f, 100.0f, 100.0f );
    fixture.addBox( *pPanelA, "a", 10.0f, 10.0f, 50.0f, 50.0f );
    sw::uitest::TestPanelWidget* pPanelB = fixture.addPanel( *fixture._pRoot, "panelB", 200.0f, 0.0f, 100.0f, 100.0f );
    fixture.addBox( *pPanelB, "b", 210.0f, 10.0f, 50.0f, 50.0f );

    sw::UiPointerState pointer;
    (void)pointer.process( fixture._tree, UiRouteFixture::makePointer( sw::UiPointerEventKind::Move, 20.0f, 20.0f ) );
    SW_EXPECT_STREQ( "root Enter, panelA Enter, a Enter, root T, panelA T, a T, a B, panelA B, root B", fixture._record.joined().c_str() );

    fixture._record._listLine.clear();
    (void)pointer.process( fixture._tree, UiRouteFixture::makePointer( sw::UiPointerEventKind::Move, 220.0f, 20.0f ) );
    SW_EXPECT_STREQ( "a Leave, panelA Leave, panelB Enter, b Enter, root T, panelB T, b T, b B, panelB B, root B", fixture._record.joined().c_str() );

    fixture._record._listLine.clear();
    pointer.clearHover();
    SW_EXPECT_STREQ( "b Leave, panelB Leave, root Leave", fixture._record.joined().c_str() );
}

/**
 * @brief [UiEventRouteTest] 캔버스 패널의 겹친 자식은 z 순서가 큰 쪽이 위라 그쪽이 맞는다 — 자식 순서로는 뒤가 위지만 앞 자식의 z 가 크다
 * @details 변이: `UiEventRouter::hitTest` 가 `collectPaintOrder` 를 무시하고 자식 역순으로 돌면 "back" 이 맞아 진다.
 */
SW_TEST_CASE( UiEventRouteTest, CanvasZOrderDecidesHitTarget )
{
    UiRouteFixture   fixture;
    sw::CanvasPanel* pCanvas = static_cast<sw::CanvasPanel*>( fixture._pRoot->addChild( sw::make_unique<sw::CanvasPanel>() ) );
    sw::uitest::UiTestUtil::placeWidget( *pCanvas, 0.0f, 0.0f, 400.0f, 300.0f );
    sw::uitest::TestBoxWidget* pFront = fixture.addBox( *pCanvas, "front", 50.0f, 50.0f, 100.0f, 100.0f );
    fixture.addBox( *pCanvas, "back", 100.0f, 100.0f, 100.0f, 100.0f );
    sw::WidgetLayoutSlot slot = pFront->getLayoutSlot();
    slot._zOrder              = 1;
    pFront->setLayoutSlot( slot );

    SW_EXPECT_STREQ( "front", fixture.hitLeafName( 120.0f, 120.0f ).c_str() ); // 겹친 곳 — z 1 이 위
    SW_EXPECT_STREQ( "back", fixture.hitLeafName( 180.0f, 180.0f ).c_str() );  // back 만 있는 곳
}

/**
 * @brief [UiEventRouteTest] 휠은 포인터 아래 스크롤 패널을 한 칸(48)씩 옮기고, 끝에 닿아 못 옮기면 처리하지 않는다(바깥 스크롤로 간다)
 * @details 변이: `ScrollPanel::onPointerEvent` 의 휠 처리를 빼면 오프셋이 0 으로 남는다.
 */
SW_TEST_CASE( UiEventRouteTest, WheelScrollsPanelUnderPointer )
{
    sw::test::UiLayoutFixture fixture( 200.0f, 100.0f );
    sw::ScrollPanel*          pScroll = UiScrollRouteUtil::makeScrollList( fixture );
    sw::UiPointerState        pointer;
    sw::UiPointerEvent        wheel = UiScrollRouteUtil::makeEvent( sw::UiPointerEventKind::Wheel, 50.0f, 50.0f );
    wheel._wheel                    = -1.0f; // 아래로 한 칸
    SW_EXPECT_TRUE( pointer.process( fixture.getTree(), wheel )._bHandled == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 48.0f, pScroll->getScrollOffset()._y, 0.001f );
    wheel._wheel = 1.0f;
    SW_EXPECT_TRUE( pointer.process( fixture.getTree(), wheel )._bHandled == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pScroll->getScrollOffset()._y, 0.001f );
    SW_EXPECT_TRUE( pointer.process( fixture.getTree(), wheel )._bHandled == SW_FALSE ); // 이미 위 끝
}

/**
 * @brief [UiEventRouteTest] 스크롤 막대 엄지를 누르면 포인터를 잡고, 끈 거리 × (최대 오프셋 / 엄지가 움직일 트랙)만큼 옮기며, 떼면 놓는다
 * @details 트랙 100 · 엄지 50 · 최대 오프셋 100 — 25 끌면 50. 변이: 막대 누름 처리(`beginScrollBarDrag`)를 빼면 잡지 않아 오프셋이 그대로다.
 */
SW_TEST_CASE( UiEventRouteTest, ScrollBarDragMovesOffset )
{
    sw::test::UiLayoutFixture        fixture( 200.0f, 100.0f );
    sw::ScrollPanel*                 pScroll = UiScrollRouteUtil::makeScrollList( fixture );
    sw::ScrollPanel::ScrollBarLayout bar{};
    SW_ASSERT_TRUE( pScroll->computeScrollBar( sw::UiOrientation::Vertical, bar ) );
    SW_EXPECT_NEAR_EQUAL( 194.0f, bar._thumbPosition._x, 0.001f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, bar._thumbSize._y, 0.001f );
    SW_EXPECT_FALSE( pScroll->computeScrollBar( sw::UiOrientation::Horizontal, bar ) );

    sw::UiPointerState pointer;
    SW_EXPECT_TRUE( pointer.process( fixture.getTree(), UiScrollRouteUtil::makeEvent( sw::UiPointerEventKind::Down, 197.0f, 10.0f ) )._bHandled == SW_TRUE );
    SW_EXPECT_EQUAL( pScroll->getId(), pointer.getCapturedWidget() );
    (void)pointer.process( fixture.getTree(), UiScrollRouteUtil::makeEvent( sw::UiPointerEventKind::Move, 150.0f, 35.0f ) ); // 막대 밖으로 나가도 잡고 있다
    SW_EXPECT_NEAR_EQUAL( 50.0f, pScroll->getScrollOffset()._y, 0.001f );
    (void)pointer.process( fixture.getTree(), UiScrollRouteUtil::makeEvent( sw::UiPointerEventKind::Up, 150.0f, 35.0f ) );
    SW_EXPECT_EQUAL( sw::kInvalidWidgetId, pointer.getCapturedWidget() );
    SW_EXPECT_FALSE( pScroll->isDraggingScrollBar() );
}
