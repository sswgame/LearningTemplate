#include "pch.h"

#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/WidgetTree.h"

#include "EngineTest/UI/UITestWidgets.h"

#include "TestFramework/TestFramework.h"

// WidgetTreeTest — 위젯 트리 뼈대: 번호표 · 이름 · 무효화 이유와 레이아웃 경계. 디바이스 없음(nogpu).

namespace
{
    struct WidgetTreeTestUtil
    {
        static bool containsID( const sw::vector<sw::WidgetID>& listID, sw::WidgetID id )
        {
            for ( const sw::WidgetID value : listID )
            {
                if ( value == id )
                    return true;
            }
            return false;
        }
    };
} // namespace

/** @brief [WidgetTreeTest] 붙이면 번호 · 이름으로 찾고, 떼면 못 찾는다. 같은 이름 둘이면 경고 한 번 · 먼저 붙은 것 */
SW_TEST_CASE( WidgetTreeTest, AddRemoveRegistersIDsAndNames )
{
    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );

    sw::Widget* const pA = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "a" ) );
    SW_EXPECT_EQUAL( 2u, tree.getWidgetCount() );
    SW_EXPECT_TRUE( tree.findWidgetByID( pA->getID() ) == pA );
    SW_EXPECT_TRUE( tree.findWidget<sw::uitest::TestBoxWidget>( "a" ) == pA );
    SW_EXPECT_TRUE( pA->getTree() == &tree );
    SW_EXPECT_TRUE( pA->getParent() == pRoot );

    {
        SW_TEST_DEFENSIVE_SCOPE( "a name used twice is reported" ); // 거절 · 실패 로그가 이 구간이 보려는 동작이다
        test::ScopedLogCollector logs;
        sw::Widget* const        pSecond = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "a" ) );
        SW_EXPECT_EQUAL( 1u, logs.countWithID( "UI.WidgetNameDuplicate" ) );
        SW_EXPECT_TRUE( tree.findWidgetByName( "a" ) == pA );
        SW_EXPECT_TRUE( tree.findWidgetByID( pSecond->getID() ) == pSecond );
    }

    const sw::WidgetID         removedID = pA->getID();
    sw::unique_ptr<sw::Widget> removed   = pRoot->removeChild( pA );
    SW_ASSERT_TRUE( removed != nullptr );
    SW_EXPECT_TRUE( removed->getTree() == nullptr );
    SW_EXPECT_TRUE( removed->getParent() == nullptr );
    SW_EXPECT_TRUE( tree.findWidgetByID( removedID ) == nullptr );
    SW_EXPECT_TRUE( tree.findWidgetByName( "a" ) == nullptr ); // 이름의 주인이 떨어졌다 — 둘째 "a" 는 이름에 오르지 않았다
    SW_EXPECT_EQUAL( 2u, tree.getWidgetCount() );

    // 번호는 다시 쓰지 않는다 — 떼었다 다시 붙여도 같은 번호, 새 위젯은 새 번호.
    sw::Widget* const pReattached = pRoot->addChild( std::move( removed ) );
    SW_EXPECT_EQUAL( removedID, pReattached->getID() );
    SW_EXPECT_TRUE( tree.findWidget<sw::uitest::TestBoxWidget>( "a" ) == pReattached );
}

/** @brief [WidgetTreeTest] 루트 > 고정 크기 패널 > 박스: 박스의 kLayout 은 고정 패널에서 멈춘다 — 더러운 루트 = 그 패널, 루트에는 kChildLayout 이 없다 */
SW_TEST_CASE( WidgetTreeTest, LayoutDirtyStopsAtLayoutBoundary )
{
    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );
    sw::PanelWidget* pFixed = static_cast<sw::PanelWidget*>( pRoot->addChild( sw::make_unique<sw::uitest::TestPanelWidget>( "fixed", true ) ) );
    sw::Widget*      pBox   = pFixed->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "box" ) );
    tree.clearAllDirty();
    SW_EXPECT_FALSE( tree.hasPendingWork() );

    pBox->invalidate( sw::WidgetDirty::kLayout );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) );
    SW_EXPECT_EQUAL( pFixed->getID(), tree.getLayoutDirtyRoots()[0] );
    SW_EXPECT_TRUE( ( pFixed->getDirtyFlags() & sw::WidgetDirty::kChildLayout ) != 0 );
    SW_EXPECT_EQUAL( 0u, pRoot->getDirtyFlags() & sw::WidgetDirty::kChildLayout );

    // 같은 분기가 다시 더러워져도 루트는 하나다.
    pBox->invalidate( sw::WidgetDirty::kLayout );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) );

    // 경계가 없으면 루트까지 올라간다.
    tree.clearAllDirty();
    static_cast<sw::uitest::TestPanelWidget*>( pFixed )->_bLayoutBoundary = false;
    pBox->invalidate( sw::WidgetDirty::kLayout );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) );
    SW_EXPECT_EQUAL( pRoot->getID(), tree.getLayoutDirtyRoots()[0] );
}

/** @brief [WidgetTreeTest] 그리기만 바뀌면 레이아웃 루트는 없고 그리기 목록에 하나 — 불투명도도 그리기 쪽이고, 렌더 변환은 그 위젯만 다시 놓는다(재기 없음) */
SW_TEST_CASE( WidgetTreeTest, PaintDirtyDoesNotTouchLayout )
{
    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );
    sw::Widget* pBox = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "box" ) );
    tree.clearAllDirty();

    pBox->invalidate( sw::WidgetDirty::kPaint );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( tree.getPaintDirtyWidgets().size() ) );
    SW_EXPECT_EQUAL( pBox->getID(), tree.getPaintDirtyWidgets()[0] );

    pBox->setOpacity( 0.5f ); // 이미 그리기 목록에 있다 — 두 번 적지 않는다
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( tree.getPaintDirtyWidgets().size() ) );

    tree.clearAllDirty();
    sw::WidgetRenderTransform transform{};
    transform._angle = sw::MathUtil::toRadian( 30.0f );
    pBox->setOpacity( 0.25f );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) ); // 불투명도는 그림만
    pBox->setRenderTransform( transform );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) ); // 렌더 변환은 기하에 얹힌다 — 그 위젯만 배치(재기 없음)
    SW_EXPECT_EQUAL( pBox->getID(), tree.getLayoutDirtyRoots()[0] );
    SW_EXPECT_TRUE( ( pBox->getDirtyFlags() & sw::WidgetDirty::kLayout ) == 0 );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( tree.getPaintDirtyWidgets().size() ) );
}

/** @brief [WidgetTreeTest] 보임이 바뀌면 보수적으로 레이아웃 — Collapsed 로도, Hidden → Visible 로도 레이아웃 루트가 생긴다 */
SW_TEST_CASE( WidgetTreeTest, CollapsedToggleIsLayoutDirty )
{
    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );
    sw::Widget* pBox = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "box" ) );
    tree.clearAllDirty();

    pBox->setVisibility( sw::WidgetVisibility::Collapsed );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) );
    SW_EXPECT_TRUE( WidgetTreeTestUtil::containsID( tree.getPaintDirtyWidgets(), pBox->getID() ) );

    pBox->setVisibility( sw::WidgetVisibility::Hidden );
    tree.clearAllDirty();
    pBox->setVisibility( sw::WidgetVisibility::Visible );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) );
    SW_EXPECT_TRUE( ( pBox->getDirtyFlags() & sw::WidgetDirty::kLayout ) != 0 );
}

/** @brief [WidgetTreeTest] 같은 값을 다시 넣는 세터는 아무것도 무효화하지 않는다 */
SW_TEST_CASE( WidgetTreeTest, SettersWithSameValueDoNotInvalidate )
{
    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );
    sw::Widget* pBox = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "box" ) );
    pBox->setOpacity( 0.5f );
    tree.clearAllDirty();

    pBox->setOpacity( 0.5f );
    pBox->setVisibility( sw::WidgetVisibility::Visible );
    pBox->setEnabled( true );
    pBox->setName( "box" );
    pBox->setRenderTransform( sw::WidgetRenderTransform{} );
    pBox->setArrangedGeometry( pBox->getGeometry() );
    SW_EXPECT_FALSE( tree.hasPendingWork() );
    SW_EXPECT_EQUAL( 0u, pBox->getDirtyFlags() );
}

/** @brief [WidgetTreeTest] 떨어진 동안 쌓인 무효화는 붙을 때 트리로 온다 — 새 위젯은 레이아웃 · 그리기 · 스타일 모두 더럽다 */
SW_TEST_CASE( WidgetTreeTest, DetachedInvalidationArrivesOnAttach )
{
    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );
    tree.clearAllDirty();

    auto        box  = sw::make_unique<sw::uitest::TestBoxWidget>( "box" );
    sw::Widget* pBox = pRoot->addChild( std::move( box ) );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( tree.getLayoutDirtyRoots().size() ) );
    SW_EXPECT_TRUE( WidgetTreeTestUtil::containsID( tree.getPaintDirtyWidgets(), pBox->getID() ) );
    SW_EXPECT_TRUE( WidgetTreeTestUtil::containsID( tree.getStyleDirtyWidgets(), pBox->getID() ) );
}

/** @brief [WidgetTreeTest] 구조 세대는 붙기 · 떨어지기 · 이름 바꾸기에서만 오르고 무효화로는 그대로다 — 위젯 목록을 캐시하는 UI 미리보기가 이것만 본다 */
SW_TEST_CASE( WidgetTreeTest, StructureGenerationTracksAttachDetachAndRename )
{
    sw::WidgetTree   tree;
    auto             root  = sw::make_unique<sw::uitest::TestPanelWidget>( "root" );
    sw::PanelWidget* pRoot = root.get();
    tree.setRoot( std::move( root ) );

    const uint64      afterRoot = tree.getStructureGeneration();
    sw::Widget* const pA        = pRoot->addChild( sw::make_unique<sw::uitest::TestBoxWidget>( "a" ) );
    const uint64      afterAdd  = tree.getStructureGeneration();
    SW_EXPECT_TRUE( afterAdd > afterRoot );

    pA->invalidate( sw::WidgetDirty::kPaint | sw::WidgetDirty::kLayout );
    tree.clearAllDirty();
    SW_EXPECT_EQUAL( afterAdd, tree.getStructureGeneration() );

    pA->setName( "renamed" );
    const uint64 afterRename = tree.getStructureGeneration();
    SW_EXPECT_TRUE( afterRename > afterAdd );

    sw::unique_ptr<sw::Widget> removed = pRoot->removeChild( pA );
    SW_ASSERT_TRUE( removed != nullptr );
    SW_EXPECT_TRUE( tree.getStructureGeneration() > afterRename );
}
