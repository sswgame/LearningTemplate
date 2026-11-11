#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Layout/OverlayPanel.h"
#include "Engine/UI/Layout/ScrollPanel.h"
#include "Engine/UI/WidgetTree/Tests/UITestWidgets.h"
#include "Engine/UI/WidgetTree/UIFocusManager.h"
#include "Engine/UI/WidgetTree/UINavigationSolver.h"
#include "Engine/UI/WidgetTree/UIPointerState.h"
#include "Engine/UI/WidgetTree/UIWidgetPath.h"
#include "Engine/UI/Widgets/ButtonWidget.h"
#include "Engine/UI/Widgets/SliderWidget.h"

using namespace sw;

namespace
{
    /** @brief 2 × 2 로 놓은 포커스 가능 위젯입니다(왼쪽 위 a, 오른쪽 위 b, 왼쪽 아래 c, 오른쪽 아래 d). */
    struct UIFocusGrid
    {
        WidgetTree   _tree;
        CanvasPanel* _pCanvas{ nullptr };
        UITestLeaf*  _pA{ nullptr };
        UITestLeaf*  _pB{ nullptr };
        UITestLeaf*  _pC{ nullptr };
        UITestLeaf*  _pD{ nullptr };

        UIFocusGrid()
        {
            _pCanvas = &UITestUtil::setRoot<CanvasPanel>( _tree );
            _pA      = &UITestUtil::addFocusable( *_pCanvas, "a", 0.0f, 0.0f );
            _pB      = &UITestUtil::addFocusable( *_pCanvas, "b", 100.0f, 0.0f );
            _pC      = &UITestUtil::addFocusable( *_pCanvas, "c", 0.0f, 100.0f );
            _pD      = &UITestUtil::addFocusable( *_pCanvas, "d", 100.0f, 100.0f );
            layout();
        }

        void layout() { (void)UITestUtil::layout( _tree, float2{ 200.0f, 200.0f } ); }

        WidgetID findNext( const Widget& from, UINavigationDirection direction ) const { return UINavigationSolver::findNextWidget( _tree, from.getID(), direction ); }
    };

    /** @brief 받은 이벤트를 차례로 적는 기록입니다. */
    struct UIEventLog
    {
        vector<string> _listEntry;

        void add( const utf8* pName, const utf8* pWhat ) { _listEntry.push_back( string( pName ) + ":" + pWhat ); }

        /** @brief 지금까지의 기록을 쉼표로 이어 돌려주고 비웁니다. */
        string take()
        {
            string joined;
            for ( const string& entry : _listEntry )
            {
                joined += joined.empty() ? string{} : string( "," );
                joined += entry;
            }
            _listEntry.clear();
            return joined;
        }

        static const utf8* getPhaseName( UIRoutePhase phase ) { return phase == UIRoutePhase::Tunnel ? "T" : "B"; }
    };

    /** @brief 이벤트를 기록하는 겹침 패널입니다. */
    class UIRecordingPanel final : public OverlayPanel
    {
    public:
        explicit UIRecordingPanel( UIEventLog& log, const utf8* pName )
            : OverlayPanel{}
            , _pLog{ &log }
            , _pLabel{ pName }
        {
        }

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override
        {
            (void)event;
            _pLog->add( _pLabel, UIEventLog::getPhaseName( phase ) );
            return UIReply::makeUnhandled();
        }
        void onHoverChanged( bool bHovered ) override { _pLog->add( _pLabel, bHovered ? "+" : "-" ); }

    private:
        UIEventLog* _pLog;
        const utf8* _pLabel;
    };

    /** @brief 이벤트를 기록하고, 정한 대로 처리 · 잡기 · 놓기를 하는 잎입니다. */
    class UIRecordingLeaf final : public Widget
    {
    public:
        explicit UIRecordingLeaf( UIEventLog& log, const utf8* pName )
            : Widget{}
            , _pLog{ &log }
            , _pLabel{ pName }
            , _bHandleTunnel{ SW_FALSE }
            , _bHandleBubble{ SW_FALSE }
            , _bCaptureOnDown{ SW_FALSE }
        {
        }

        void setHandleTunnel() { _bHandleTunnel = SW_TRUE; }
        void setHandleBubble() { _bHandleBubble = SW_TRUE; }
        void setCaptureOnDown()
        {
            _bHandleBubble  = SW_TRUE;
            _bCaptureOnDown = SW_TRUE;
        }

        UIReply onPointerEvent( const UIPointerEvent& event, UIRoutePhase phase ) override
        {
            _pLog->add( _pLabel, UIEventLog::getPhaseName( phase ) );
            const bool bHandled = phase == UIRoutePhase::Tunnel ? _bHandleTunnel == SW_TRUE : _bHandleBubble == SW_TRUE;
            if ( bHandled == false )
                return UIReply::makeUnhandled();
            UIReply reply = UIReply::makeHandled();
            if ( _bCaptureOnDown == SW_TRUE && event._kind == UIPointerEventKind::Down )
                reply.capturePointer().requestFocus( getID() );
            if ( _bCaptureOnDown == SW_TRUE && event._kind == UIPointerEventKind::Up )
                reply.releasePointer();
            return reply;
        }
        void onHoverChanged( bool bHovered ) override { _pLog->add( _pLabel, bHovered ? "+" : "-" ); }
        void onPointerCaptureLost() override { _pLog->add( _pLabel, "lost" ); }

    private:
        UIEventLog* _pLog;
        const utf8* _pLabel;
        uint8       _bHandleTunnel  : 1;
        uint8       _bHandleBubble  : 1;
        uint8       _bCaptureOnDown : 1;
    };

    /** @brief 기록 패널을 루트로, 그 안에 왼쪽 반 잎 하나를 둔 트리입니다. */
    struct UIEventScene
    {
        WidgetTree        _tree;
        UIEventLog        _log;
        UIRecordingPanel* _pRoot{ nullptr };
        UIRecordingLeaf*  _pLeaf{ nullptr };

        UIEventScene()
        {
            unique_ptr<UIRecordingPanel> root = make_unique<UIRecordingPanel>( _log, "root" );
            _pRoot                            = root.get();
            _tree.setRoot( std::move( root ) );
            unique_ptr<UIRecordingLeaf> leaf = make_unique<UIRecordingLeaf>( _log, "leaf" );
            _pLeaf                           = leaf.get();
            WidgetLayoutSlot slot;
            slot._horizontalAlignment = UIAlignment::Start;
            slot._widthOverride       = 50.0f;
            leaf->setLayoutSlot( slot );
            _pRoot->addChild( std::move( leaf ) );
            (void)UITestUtil::layout( _tree, float2{ 100.0f, 100.0f } );
        }

        static UIPointerEvent makeEvent( UIPointerEventKind kind, float32 x, float32 y )
        {
            UIPointerEvent event;
            event._kind     = kind;
            event._position = float2{ x, y };
            return event;
        }
    };
} // namespace

// ---------------------------------------------------------------------- 탐색

SW_TEST_CASE( UINavigationTest, SpatialSearchFindsTheNearestInThatDirection )
{
    const UIFocusGrid grid;
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Right ) == grid._pB->getID() );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Down ) == grid._pC->getID() );
    SW_EXPECT_TRUE( grid.findNext( *grid._pD, UINavigationDirection::Left ) == grid._pC->getID() );
    SW_EXPECT_TRUE( grid.findNext( *grid._pD, UINavigationDirection::Up ) == grid._pB->getID() );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Left ) == kInvalidWidgetID );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Up ) == kInvalidWidgetID );
}

SW_TEST_CASE( UINavigationTest, TabOrderFollowsTheDocumentAndWraps )
{
    const UIFocusGrid grid;
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Next ) == grid._pB->getID() );
    SW_EXPECT_TRUE( grid.findNext( *grid._pD, UINavigationDirection::Next ) == grid._pA->getID() );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Previous ) == grid._pD->getID() );
}

// 명시 이웃이 받을 수 있으면 간다. 받을 수 없으면 다음 규칙으로 넘어간다.
SW_TEST_CASE( UINavigationTest, ExplicitTargetWinsOnlyWhenFocusable )
{
    UIFocusGrid      grid;
    WidgetNavigation navigation;
    navigation._right._rule   = UINavigationRule::Explicit;
    navigation._right._target = hashed_string( "d" );
    grid._pA->setNavigation( navigation );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Right ) == grid._pD->getID() );

    grid._pD->setEnabled( false );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Right ) == grid._pB->getID() );
}

SW_TEST_CASE( UINavigationTest, WrapGoesToTheFarEndStopStays )
{
    UIFocusGrid      grid;
    WidgetNavigation wrap;
    wrap._right._rule = UINavigationRule::Wrap;
    grid._pCanvas->setNavigation( wrap );
    SW_EXPECT_TRUE( grid.findNext( *grid._pB, UINavigationDirection::Right ) == grid._pA->getID() ); // 같은 줄을 먼저 고른다
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Right ) == grid._pB->getID() );

    // 잎의 Stop 은 조상의 Wrap 보다 먼저 보이고, 자기 안에 후보가 없어 멈춘다.
    WidgetNavigation stop;
    stop._right._rule = UINavigationRule::Stop;
    grid._pA->setNavigation( stop );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Right ) == kInvalidWidgetID );
}

SW_TEST_CASE( UINavigationTest, SkipsWhatCannotTakeFocus )
{
    UIFocusGrid grid;
    grid._pB->setEnabled( false );
    SW_EXPECT_FALSE( UINavigationSolver::canReceiveFocus( *grid._pB ) );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Right ) == grid._pD->getID() );
    grid._pD->setVisibility( WidgetVisibility::Hidden );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Right ) == kInvalidWidgetID );
    SW_EXPECT_TRUE( grid.findNext( *grid._pA, UINavigationDirection::Next ) == grid._pC->getID() );

    // 꺼진 조상 아래는 모두 받지 못한다.
    grid._pCanvas->setEnabled( false );
    SW_EXPECT_FALSE( UINavigationSolver::canReceiveFocus( *grid._pA ) );
    grid._pCanvas->setEnabled( true );
    SW_EXPECT_TRUE( UINavigationSolver::findFirstFocusable( *grid._pCanvas ) == grid._pA->getID() );
}

// 자르는 패널 밖으로 나간 위젯은 후보가 아니다. 스크롤 패널이 자른 것은 옮겨 보이게 하므로 후보다.
SW_TEST_CASE( UINavigationTest, ClippedOutWidgetsAreNotCandidates )
{
    WidgetTree   tree;
    CanvasPanel& canvas  = UITestUtil::setRoot<CanvasPanel>( tree );
    UITestLeaf&  from    = UITestUtil::addFocusable( canvas, "from", 0.0f, 0.0f );
    CanvasPanel& clipped = UITestUtil::addPanel<CanvasPanel>( canvas, UITestUtil::makeCanvasSlot( 100.0f, 0.0f, 50.0f, 50.0f ) );
    clipped.setClipChildren( true );
    UITestLeaf& hidden = UITestUtil::addFocusable( clipped, "hidden", 200.0f, 0.0f );
    (void)UITestUtil::layout( tree, float2{ 500.0f, 200.0f } );
    SW_EXPECT_TRUE( UINavigationSolver::findNextWidget( tree, from.getID(), UINavigationDirection::Right ) == kInvalidWidgetID );

    hidden.setLayoutSlot( UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 50.0f, 50.0f ) );
    (void)UITestUtil::layout( tree, float2{ 500.0f, 200.0f } );
    SW_EXPECT_TRUE( UINavigationSolver::findNextWidget( tree, from.getID(), UINavigationDirection::Right ) == hidden.getID() );
}

// ---------------------------------------------------------------------- 포커스

SW_TEST_CASE( UIFocusManagerTest, SetFocusNotifiesBothSides )
{
    UIFocusGrid    grid;
    UIFocusManager manager;
    SW_EXPECT_TRUE( manager.setFocus( grid._tree, grid._pA->getID() ) );
    SW_EXPECT_TRUE( grid._pA->hasFocus() );
    SW_EXPECT_EQUAL( 1u, grid._pA->getFocusGainedCount() );

    SW_EXPECT_TRUE( manager.setFocus( grid._tree, grid._pA->getID() ) ); // 같으면 알리지 않는다
    SW_EXPECT_EQUAL( 1u, grid._pA->getFocusGainedCount() );

    SW_EXPECT_TRUE( manager.setFocus( grid._tree, grid._pB->getID() ) );
    SW_EXPECT_EQUAL( 1u, grid._pA->getFocusLostCount() );
    SW_EXPECT_TRUE( manager.getFocusedWidget() == grid._pB->getID() );

    manager.clearFocus();
    SW_EXPECT_TRUE( manager.getFocusedWidget() == kInvalidWidgetID );
    SW_EXPECT_EQUAL( 1u, grid._pB->getFocusLostCount() );
}

SW_TEST_CASE( UIFocusManagerTest, RefusesWhatCannotTakeFocus )
{
    UIFocusGrid    grid;
    UIFocusManager manager;
    SW_EXPECT_FALSE( manager.setFocus( grid._tree, grid._pCanvas->getID() ) ); // 패널은 받지 않는다
    grid._pA->setEnabled( false );
    SW_EXPECT_FALSE( manager.setFocus( grid._tree, grid._pA->getID() ) );
    SW_EXPECT_FALSE( manager.setFocus( grid._tree, kInvalidWidgetID ) );
    SW_EXPECT_TRUE( manager.getFocusedWidget() == kInvalidWidgetID );
}

SW_TEST_CASE( UIFocusManagerTest, NavigateMovesAndFailsAtTheEdge )
{
    UIFocusGrid    grid;
    UIFocusManager manager;
    SW_EXPECT_FALSE( manager.navigate( grid._tree, UINavigationDirection::Right ) ); // 포커스가 없다
    SW_REQUIRE_TRUE( manager.setFocus( grid._tree, grid._pA->getID() ) );
    SW_EXPECT_TRUE( manager.navigate( grid._tree, UINavigationDirection::Right ) );
    SW_EXPECT_TRUE( manager.getFocusedWidget() == grid._pB->getID() );
    SW_EXPECT_FALSE( manager.navigate( grid._tree, UINavigationDirection::Right ) );
    SW_EXPECT_TRUE( manager.getFocusedWidget() == grid._pB->getID() );
}

SW_TEST_CASE( UIFocusManagerTest, NavigateScrollsTheNewFocusIntoView )
{
    WidgetTree       tree;
    ScrollPanel&     scroll = UITestUtil::setRoot<ScrollPanel>( tree );
    WidgetLayoutSlot contentSlot;
    contentSlot._heightOverride = 500.0f;
    CanvasPanel& content        = UITestUtil::addPanel<CanvasPanel>( scroll, contentSlot );
    UITestLeaf&  top            = UITestUtil::addFocusable( content, "top", 0.0f, 0.0f );
    UITestLeaf&  bottom         = UITestUtil::addFocusable( content, "bottom", 0.0f, 400.0f );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UIFocusManager manager;
    SW_REQUIRE_TRUE( manager.setFocus( tree, top.getID() ) );
    SW_EXPECT_TRUE( manager.navigate( tree, UINavigationDirection::Down ) );
    SW_EXPECT_TRUE( manager.getFocusedWidget() == bottom.getID() );
    SW_EXPECT_TRUE( scroll.getScrollOffset()._y > 0.0f );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    const float32 bottomEdge = bottom.getGeometry()._position._y + bottom.getGeometry()._size._y;
    SW_EXPECT_TRUE( bottomEdge <= 100.0f );
}

// 포커스를 가진 위젯이 떨어지면 트리가 포커스를 풀고, 다음 포커스가 정상으로 잡힌다.
SW_TEST_CASE( UIFocusManagerTest, RemovingTheFocusedWidgetReleasesFocus )
{
    UIFocusGrid    grid;
    UIFocusManager manager;
    SW_REQUIRE_TRUE( manager.setFocus( grid._tree, grid._pA->getID() ) );
    unique_ptr<Widget> removed = grid._pCanvas->removeChild( grid._pA );
    SW_EXPECT_TRUE( grid._tree.getFocusedWidget() == kInvalidWidgetID );
    SW_EXPECT_TRUE( manager.getFocusedWidget() == kInvalidWidgetID );
    SW_EXPECT_TRUE( manager.setFocus( grid._tree, grid._pB->getID() ) );
    SW_EXPECT_TRUE( grid._pB->hasFocus() );
}

SW_TEST_CASE( UIFocusManagerTest, MovingBetweenTreesLeavesTheOldOne )
{
    UIFocusGrid    first;
    UIFocusGrid    second;
    UIFocusManager manager;
    SW_REQUIRE_TRUE( manager.setFocus( first._tree, first._pA->getID() ) );
    SW_REQUIRE_TRUE( manager.setFocus( second._tree, second._pB->getID() ) );
    SW_EXPECT_TRUE( first._tree.getFocusedWidget() == kInvalidWidgetID );
    SW_EXPECT_EQUAL( 1u, first._pA->getFocusLostCount() );
    SW_EXPECT_TRUE( second._tree.getFocusedWidget() == second._pB->getID() );
    SW_EXPECT_FALSE( manager.navigate( first._tree, UINavigationDirection::Right ) ); // 포커스가 없는 트리다
}

// 트리와 관리자 어느 쪽이 먼저 사라져도 다른 쪽이 끊긴 포인터를 쓰지 않는다.
SW_TEST_CASE( UIFocusManagerTest, EitherSideMayDieFirst )
{
    UIFocusManager manager;
    {
        UIFocusGrid grid;
        SW_REQUIRE_TRUE( manager.setFocus( grid._tree, grid._pA->getID() ) );
    }
    SW_EXPECT_TRUE( manager.getFocusedWidget() == kInvalidWidgetID );
    manager.clearFocus();

    UIFocusGrid grid;
    {
        UIFocusManager shortLived;
        SW_REQUIRE_TRUE( shortLived.setFocus( grid._tree, grid._pA->getID() ) );
    }
    SW_EXPECT_TRUE( manager.setFocus( grid._tree, grid._pB->getID() ) );
}

SW_TEST_CASE( UIFocusManagerTest, FocusPathRunsFromTheRoot )
{
    UIFocusGrid    grid;
    UIFocusManager manager;
    UIWidgetPath   path;
    manager.makeFocusPath( grid._tree, path );
    SW_EXPECT_TRUE( path.isEmpty() );
    SW_REQUIRE_TRUE( manager.setFocus( grid._tree, grid._pC->getID() ) );
    manager.makeFocusPath( grid._tree, path );
    SW_REQUIRE_EQUAL( size_t{ 2 }, path._listWidget.size() );
    SW_EXPECT_TRUE( path._listWidget[0] == grid._pCanvas->getID() );
    SW_EXPECT_TRUE( path.getLeaf() == grid._pC->getID() );
}

// ---------------------------------------------------------------------- 히트 테스트

SW_TEST_CASE( UIEventRouterTest, HitTestFindsTheDeepestUnderThePoint )
{
    UIEventScene scene;
    UIWidgetPath path;
    SW_EXPECT_TRUE( UIWidgetPath::hitTest( scene._tree, float2{ 10.0f, 10.0f }, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == scene._pLeaf->getID() );
    SW_EXPECT_TRUE( path.contains( scene._pRoot->getID() ) );

    SW_EXPECT_TRUE( UIWidgetPath::hitTest( scene._tree, float2{ 90.0f, 10.0f }, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == scene._pRoot->getID() );

    SW_EXPECT_FALSE( UIWidgetPath::hitTest( scene._tree, float2{ 500.0f, 10.0f }, path ) );
    SW_EXPECT_TRUE( path.isEmpty() );
}

SW_TEST_CASE( UIEventRouterTest, VisibilityDecidesWhatIsHit )
{
    UIEventScene scene;
    UIWidgetPath path;
    scene._pLeaf->setVisibility( WidgetVisibility::Hidden );
    SW_EXPECT_TRUE( UIWidgetPath::hitTest( scene._tree, float2{ 10.0f, 10.0f }, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == scene._pRoot->getID() );

    scene._pLeaf->setVisibility( WidgetVisibility::HitTestInvisible );
    SW_EXPECT_TRUE( UIWidgetPath::hitTest( scene._tree, float2{ 10.0f, 10.0f }, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == scene._pRoot->getID() );

    scene._pLeaf->setVisibility( WidgetVisibility::Visible );
    scene._pRoot->setVisibility( WidgetVisibility::SelfHitTestInvisible );
    SW_EXPECT_TRUE( UIWidgetPath::hitTest( scene._tree, float2{ 10.0f, 10.0f }, path ) );  // 자식은 받는다
    SW_EXPECT_FALSE( UIWidgetPath::hitTest( scene._tree, float2{ 90.0f, 10.0f }, path ) ); // 자기는 받지 않는다

    scene._pRoot->setVisibility( WidgetVisibility::HitTestInvisible );
    SW_EXPECT_FALSE( UIWidgetPath::hitTest( scene._tree, float2{ 10.0f, 10.0f }, path ) );
}

SW_TEST_CASE( UIEventRouterTest, TopmostChildIsHitFirstFollowingZOrder )
{
    WidgetTree       tree;
    CanvasPanel&     canvas = UITestUtil::setRoot<CanvasPanel>( tree );
    WidgetLayoutSlot slot   = UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 50.0f, 50.0f );
    slot._zOrder            = 1;
    UITestLeaf& raised      = UITestUtil::addLeaf( canvas, float2{}, slot );
    slot._zOrder            = 0;
    UITestLeaf& lowered     = UITestUtil::addLeaf( canvas, float2{}, slot );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 100.0f } );
    UIWidgetPath path;
    SW_REQUIRE_TRUE( UIWidgetPath::hitTest( tree, float2{ 10.0f, 10.0f }, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == raised.getID() ); // 뒤에 붙은 쪽이 아니라 z 가 높은 쪽이다
    SW_EXPECT_TRUE( path.contains( lowered.getID() ) == false );
}

// 자르는 패널 밖으로 삐져나온 자식은 그 자리에서 눌리지 않는다.
SW_TEST_CASE( UIEventRouterTest, ClippedChildIsNotHitOutsideItsPanel )
{
    WidgetTree   tree;
    CanvasPanel& root    = UITestUtil::setRoot<CanvasPanel>( tree );
    CanvasPanel& clipped = UITestUtil::addPanel<CanvasPanel>( root, UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 50.0f, 50.0f ) );
    UITestLeaf&  leaf    = UITestUtil::addLeaf( clipped, float2{}, UITestUtil::makeCanvasSlot( 0.0f, 0.0f, 100.0f, 100.0f ) );
    (void)UITestUtil::layout( tree, float2{ 200.0f, 200.0f } );
    UIWidgetPath path;
    SW_REQUIRE_TRUE( UIWidgetPath::hitTest( tree, float2{ 80.0f, 80.0f }, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == leaf.getID() ); // 자르지 않으면 받는다
    clipped.setClipChildren( true );
    SW_REQUIRE_TRUE( UIWidgetPath::hitTest( tree, float2{ 80.0f, 80.0f }, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == root.getID() );
}

SW_TEST_CASE( UIEventRouterTest, RouteTunnelsThenBubblesAndStopsWhenHandled )
{
    UIEventScene scene;
    UIWidgetPath path;
    SW_REQUIRE_TRUE( UIWidgetPath::hitTest( scene._tree, float2{ 10.0f, 10.0f }, path ) );
    const UIPointerEvent event   = UIEventScene::makeEvent( UIPointerEventKind::Down, 10.0f, 10.0f );
    WidgetID             handler = kInvalidWidgetID;
    SW_EXPECT_FALSE( UIWidgetPath::routePointerEvent( scene._tree, path, event, handler ).isHandled() );
    SW_EXPECT_TRUE( scene._log.take() == "root:T,leaf:T,leaf:B,root:B" );
    SW_EXPECT_TRUE( handler == kInvalidWidgetID );

    scene._pLeaf->setHandleBubble();
    SW_EXPECT_TRUE( UIWidgetPath::routePointerEvent( scene._tree, path, event, handler ).isHandled() );
    SW_EXPECT_TRUE( scene._log.take() == "root:T,leaf:T,leaf:B" );
    SW_EXPECT_TRUE( handler == scene._pLeaf->getID() );

    scene._pLeaf->setHandleTunnel();
    SW_EXPECT_TRUE( UIWidgetPath::routePointerEvent( scene._tree, path, event, handler ).isHandled() );
    SW_EXPECT_TRUE( scene._log.take() == "root:T,leaf:T" );
}

SW_TEST_CASE( UIEventRouterTest, DisabledWidgetsBlockButDoNotReceive )
{
    UIEventScene scene;
    scene._pLeaf->setEnabled( false );
    UIWidgetPath path;
    SW_REQUIRE_TRUE( UIWidgetPath::hitTest( scene._tree, float2{ 10.0f, 10.0f }, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == scene._pLeaf->getID() ); // 아래로 새지 않는다
    WidgetID handler = kInvalidWidgetID;
    (void)UIWidgetPath::routePointerEvent( scene._tree, path, UIEventScene::makeEvent( UIPointerEventKind::Down, 10.0f, 10.0f ), handler );
    SW_EXPECT_TRUE( scene._log.take() == "root:T,root:B" );
}

SW_TEST_CASE( UIEventRouterTest, PathToAWidgetGoesRootFirst )
{
    UIEventScene scene;
    UIWidgetPath path;
    SW_REQUIRE_TRUE( UIWidgetPath::makePathTo( scene._tree, scene._pLeaf->getID(), path ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, path._listWidget.size() );
    SW_EXPECT_TRUE( path._listWidget[0] == scene._pRoot->getID() );
    SW_EXPECT_TRUE( path._listWidget[1] == scene._pLeaf->getID() );
    SW_EXPECT_FALSE( UIWidgetPath::makePathTo( scene._tree, kInvalidWidgetID, path ) );
}

// ---------------------------------------------------------------------- 포인터 상태

SW_TEST_CASE( UIPointerStateTest, HoverEntersFromTheRootLeavesFromTheLeaf )
{
    UIEventScene   scene;
    UIPointerState pointer;
    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 10.0f, 10.0f ) );
    SW_EXPECT_TRUE( pointer.getHoveredWidget() == scene._pLeaf->getID() );
    SW_EXPECT_TRUE( scene._pLeaf->isHovered() );
    SW_EXPECT_TRUE( scene._log.take() == "root:+,leaf:+,root:T,leaf:T,leaf:B,root:B" );

    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 90.0f, 10.0f ) );
    SW_EXPECT_FALSE( scene._pLeaf->isHovered() );
    SW_EXPECT_TRUE( scene._pRoot->isHovered() );
    SW_EXPECT_TRUE( scene._log.take() == "leaf:-,root:T,root:B" );

    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 500.0f, 10.0f ) );
    SW_EXPECT_TRUE( pointer.getHoverTree() == nullptr );
    SW_EXPECT_TRUE( scene._log.take() == "root:-" );
}

SW_TEST_CASE( UIPointerStateTest, CaptureKeepsEventsOnTheWidgetUntilReleased )
{
    UIEventScene scene;
    scene._pLeaf->setCaptureOnDown();
    UIPointerState        pointer;
    const UIPointerResult down = pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Down, 10.0f, 10.0f ) );
    SW_EXPECT_TRUE( down._bHandled == SW_TRUE );
    SW_EXPECT_TRUE( down._handler == scene._pLeaf->getID() );
    SW_EXPECT_TRUE( down._focusRequest == scene._pLeaf->getID() );
    SW_EXPECT_TRUE( pointer.getCapturedWidget() == scene._pLeaf->getID() );

    // 위젯 밖이어도 잡은 위젯으로 간다.
    const UIPointerResult move = pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 500.0f, 500.0f ) );
    SW_EXPECT_TRUE( move._handler == scene._pLeaf->getID() );
    SW_EXPECT_TRUE( pointer.getHoveredWidget() == scene._pLeaf->getID() );

    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Up, 500.0f, 500.0f ) );
    SW_EXPECT_TRUE( pointer.getCapturedWidget() == kInvalidWidgetID );
    const UIPointerResult after = pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 500.0f, 500.0f ) );
    SW_EXPECT_TRUE( after._bHitWidget == SW_FALSE );
}

SW_TEST_CASE( UIPointerStateTest, CaptureEndsWhenTheWidgetIsRemoved )
{
    UIEventScene scene;
    scene._pLeaf->setCaptureOnDown();
    UIPointerState pointer;
    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Down, 10.0f, 10.0f ) );
    unique_ptr<Widget>    removed = scene._pRoot->removeChild( scene._pLeaf );
    const UIPointerResult result  = pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 90.0f, 10.0f ) );
    SW_EXPECT_TRUE( pointer.getCapturedWidget() == kInvalidWidgetID );
    SW_EXPECT_TRUE( result._bHitWidget == SW_TRUE );
    SW_EXPECT_TRUE( pointer.getHoveredWidget() == scene._pRoot->getID() );
}

/** @brief [UIPointerStateTest] 잡기 취소: 잡은 위젯에 한 번 알리고 놓는다. 그 뒤 이벤트는 점 아래로 간다. 잡은 것이 없거나 위젯이 사라졌으면 아무 일도 없다 */
SW_TEST_CASE( UIPointerStateTest, CancelCaptureTellsTheWidgetOnce )
{
    UIEventScene scene;
    scene._pLeaf->setCaptureOnDown();
    UIPointerState pointer;
    pointer.cancelCapture(); // 잡은 것이 없다
    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Down, 10.0f, 10.0f ) );
    (void)scene._log.take();

    pointer.cancelCapture();
    SW_EXPECT_TRUE( scene._log.take() == "leaf:lost" );
    SW_EXPECT_TRUE( pointer.getCapturedWidget() == kInvalidWidgetID );
    SW_EXPECT_TRUE( pointer.getCaptureTree() == nullptr );
    pointer.cancelCapture();
    SW_EXPECT_TRUE( scene._log.take().empty() );

    const UIPointerResult after = pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 500.0f, 500.0f ) );
    SW_EXPECT_TRUE( after._bHitWidget == SW_FALSE ); // 밖의 점은 더는 위젯으로 가지 않는다

    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Down, 10.0f, 10.0f ) );
    (void)scene._log.take();
    unique_ptr<Widget> removed = scene._pRoot->removeChild( scene._pLeaf );
    pointer.cancelCapture(); // 이미 떨어진 위젯에는 알리지 않는다
    SW_EXPECT_TRUE( scene._log.take().empty() );
    SW_EXPECT_TRUE( pointer.getCapturedWidget() == kInvalidWidgetID );
}

/** @brief [UIPointerStateTest] 잡기 취소: 누른 버튼은 눌림이 풀리고 뒤늦은 뗌에도 클릭이 없다 */
SW_TEST_CASE( UIPointerStateTest, CancelCaptureEndsButtonPress )
{
    WidgetTree               tree;
    unique_ptr<ButtonWidget> button  = make_unique<ButtonWidget>();
    ButtonWidget*            pButton = button.get();
    tree.setRoot( std::move( button ) );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 20.0f } );
    uint32             clickCount{ 0 };
    const Subscription clicked = pButton->getOnClicked().add( [&clickCount]( WidgetID )
    { ++clickCount; } );

    UIPointerState pointer;
    (void)pointer.process( tree, UIEventScene::makeEvent( UIPointerEventKind::Down, 10.0f, 10.0f ) );
    SW_REQUIRE_TRUE( pButton->isPressed() );
    pointer.cancelCapture();
    SW_EXPECT_FALSE( pButton->isPressed() );
    (void)pointer.process( tree, UIEventScene::makeEvent( UIPointerEventKind::Up, 10.0f, 10.0f ) );
    SW_EXPECT_EQUAL( 0u, clickCount );
}

/** @brief [UIPointerStateTest] 잡기 취소: 끌던 슬라이더는 그 뒤의 이동으로 값이 바뀌지 않는다 */
SW_TEST_CASE( UIPointerStateTest, CancelCaptureEndsSliderDrag )
{
    WidgetTree               tree;
    unique_ptr<SliderWidget> slider  = make_unique<SliderWidget>();
    SliderWidget*            pSlider = slider.get();
    tree.setRoot( std::move( slider ) );
    (void)UITestUtil::layout( tree, float2{ 100.0f, 20.0f } );

    UIPointerState pointer;
    (void)pointer.process( tree, UIEventScene::makeEvent( UIPointerEventKind::Down, 20.0f, 10.0f ) );
    SW_REQUIRE_TRUE( pointer.getCapturedWidget() == pSlider->getID() );
    const float32 valueAtCancel = pSlider->getValue();
    pointer.cancelCapture();
    (void)pointer.process( tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 90.0f, 10.0f ) );
    SW_EXPECT_NEAR( valueAtCancel, pSlider->getValue(), 1.0e-6f );
}

SW_TEST_CASE( UIPointerStateTest, ClearHoverAndForgetTree )
{
    UIEventScene   scene;
    UIPointerState pointer;
    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Move, 10.0f, 10.0f ) );
    (void)scene._log.take();
    pointer.clearHover();
    SW_EXPECT_TRUE( pointer.getHoveredWidget() == kInvalidWidgetID );
    SW_EXPECT_TRUE( scene._log.take() == "leaf:-,root:-" );

    scene._pLeaf->setCaptureOnDown();
    (void)pointer.process( scene._tree, UIEventScene::makeEvent( UIPointerEventKind::Down, 10.0f, 10.0f ) );
    (void)scene._log.take();
    pointer.removeTree( scene._tree );
    SW_EXPECT_TRUE( pointer.getCaptureTree() == nullptr );
    SW_EXPECT_TRUE( pointer.getHoverTree() == nullptr );
    SW_EXPECT_TRUE( scene._log.take().empty() ); // 알리지 않고 버린다
}

// ---------------------------------------------------------------------- 트리

SW_TEST_CASE( WidgetTreeTest, NamesAndIdsFollowTheWidgets )
{
    UIFocusGrid grid;
    SW_EXPECT_TRUE( grid._tree.findWidgetByName( hashed_string( "a" ) ) == grid._pA );
    SW_EXPECT_TRUE( grid._tree.findWidgetByID( grid._pA->getID() ) == grid._pA );
    SW_EXPECT_EQUAL( 5u, grid._tree.getWidgetCount() );

    grid._pA->setName( hashed_string( "renamed" ) );
    SW_EXPECT_TRUE( grid._tree.findWidgetByName( hashed_string( "a" ) ) == nullptr );
    SW_EXPECT_TRUE( grid._tree.findWidgetByName( hashed_string( "renamed" ) ) == grid._pA );

    const WidgetID     id      = grid._pB->getID();
    unique_ptr<Widget> removed = grid._pCanvas->removeChild( grid._pB );
    SW_EXPECT_TRUE( grid._tree.findWidgetByID( id ) == nullptr );
    SW_EXPECT_TRUE( grid._tree.findWidgetByName( hashed_string( "b" ) ) == nullptr );
    SW_EXPECT_EQUAL( 4u, grid._tree.getWidgetCount() );
    SW_EXPECT_TRUE( removed->getTree() == nullptr );
}

SW_TEST_CASE( WidgetTreeTest, StructureGenerationOnlyMovesWithStructure )
{
    UIFocusGrid  grid;
    const uint64 before = grid._tree.getStructureGeneration();
    grid._pA->setOpacity( 0.5f );
    grid._pA->setLayoutSlot( UITestUtil::makeCanvasSlot( 1.0f, 2.0f, 3.0f, 4.0f ) );
    SW_EXPECT_TRUE( grid._tree.getStructureGeneration() == before );
    unique_ptr<Widget> removed = grid._pCanvas->removeChild( grid._pB );
    SW_EXPECT_TRUE( grid._tree.getStructureGeneration() > before );
}

SW_TEST_CASE( WidgetTreeTest, InvalidationCollectsJobsUntilCleared )
{
    UIFocusGrid grid;
    grid._tree.clearAllDirty();
    SW_EXPECT_FALSE( grid._tree.hasPendingJobs() );

    grid._pA->invalidate( WidgetDirty::kPaint );
    SW_EXPECT_TRUE( grid._tree.hasPendingJobs() );
    SW_EXPECT_EQUAL( size_t{ 1 }, grid._tree.getPaintDirtyWidgets().size() );
    grid._pA->invalidate( WidgetDirty::kPaint );
    SW_EXPECT_EQUAL( size_t{ 1 }, grid._tree.getPaintDirtyWidgets().size() ); // 같은 위젯은 한 번이다

    grid._tree.clearAllDirty();
    SW_EXPECT_FALSE( grid._tree.hasPendingJobs() );
    SW_EXPECT_EQUAL( 0u, grid._pA->getDirtyFlags() );
}

SW_TEST_CASE( WidgetTreeTest, HierarchyEnabledLooksAtAncestors )
{
    UIFocusGrid grid;
    SW_EXPECT_TRUE( grid._pA->isEnabledInHierarchy() );
    grid._pCanvas->setEnabled( false );
    SW_EXPECT_TRUE( grid._pA->isEnabled() );
    SW_EXPECT_FALSE( grid._pA->isEnabledInHierarchy() );
}

/**
 * @brief [UIEventRouterTest] 손가락이 조작 사이 빈 곳을 짚으면 넓힌 상자에 드는 가장 가까운 조작이 받고, 넓히지 않으면(0) 마우스와 같다
 */
SW_TEST_CASE( UIEventRouterTest, TouchHitTestGrowsSmallTargets )
{
    UIFocusGrid  grid;
    UIWidgetPath path;
    SW_REQUIRE_TRUE( UIWidgetPath::hitTestTouch( grid._tree, float2{ 60.0f, 20.0f }, 80.0f, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == grid._pA->getID() );
    SW_EXPECT_TRUE( path.contains( grid._pCanvas->getID() ) );

    (void)UIWidgetPath::hitTestTouch( grid._tree, float2{ 60.0f, 20.0f }, 0.0f, path ); // 캔버스가 받거나 아무도 받지 않는다 — 조작은 아니다
    SW_EXPECT_TRUE( path.getLeaf() != grid._pA->getID() );

    // 두 조작 사이 한가운데보다 B 쪽이면 B 가 받는다(중심 거리)
    SW_REQUIRE_TRUE( UIWidgetPath::hitTestTouch( grid._tree, float2{ 90.0f, 20.0f }, 80.0f, path ) );
    SW_EXPECT_TRUE( path.getLeaf() == grid._pB->getID() );
}
