#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Layout/UILayoutPass.h"
#include "Engine/UI/WidgetTree/Tests/UITestWidgets.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/ListViewWidget.h"

// ListViewWidgetTest — 가상 목록: 보이는 줄 + 1 만 만들고, 한 줄 스크롤은 한 위젯만 다시 잇고, 항목이 적으면 남는 줄을 접는다. 위젯 트리만 돈다(nogpu).

using namespace sw;

namespace
{
    struct ListViewTestUtil
    {
        static constexpr float2  kViewport{ 100.0f, 100.0f };
        static constexpr float32 kRowHeight = 20.0f; ///< 보이는 줄 5 + 1
        /** @brief 바인더가 불린 횟수입니다 — 위젯이 아니라 테스트가 센다(바인딩은 이 콜백을 부르는 일이다). `makeList` 가 0 으로 되돌린다. */
        static inline uint32 s_bindCount = 0;

        /** @brief 잎 위젯 줄을 만들고 항목 번호를 위젯 이름(바인더가 부른 횟수와 함께)으로 남기는 목록을 루트에 놓습니다. */
        static ListViewWidget& makeList( WidgetTree& tree, uint32 itemCount )
        {
            ListViewWidget& list = UITestUtil::setRoot<ListViewWidget>( tree );
            list.setRowHeight( kRowHeight );
            list.setRowFactory( ListViewWidget::RowFactoryDelegate::create( []() -> unique_ptr<Widget>
            { return make_unique<UITestLeaf>( float2{ 10.0f, 10.0f } ); } ) );
            s_bindCount = 0;
            list.setRowBinder( ListViewWidget::RowBinderDelegate::create( []( Widget& row, uint32 itemIndex )
            {
                ++s_bindCount;
                row.setName( hashed_string( to_string( static_cast<uint64>( itemIndex ) ) ) );
            } ) );
            list.setItemCount( itemCount );
            return list;
        }
    };
    using Util = ListViewTestUtil;
} // namespace

/** @brief [ListViewWidgetTest] 항목이 많아도 줄 위젯은 보이는 줄 + 1 뿐이고, 줄은 항목 번호로 찾는다 */
SW_TEST_CASE( ListViewWidgetTest, CreatesOnlyTheVisibleRows )
{
    WidgetTree      tree;
    ListViewWidget& list = Util::makeList( tree, 10000 );
    (void)UITestUtil::layout( tree, Util::kViewport );
    SW_EXPECT_EQUAL( 6u, list.getCreatedRowCount() );
    SW_EXPECT_EQUAL( 6u, Util::s_bindCount );
    SW_EXPECT_TRUE( list.findItemIndex( *list.getChild( 0 ) ) == 0u );
    SW_EXPECT_TRUE( list.findItemIndex( *list.getChild( 5 ) ) == 5u );
}

/** @brief [ListViewWidgetTest] 한 줄 스크롤은 위로 나간 위젯 하나만 새 항목에 다시 잇는다 */
SW_TEST_CASE( ListViewWidgetTest, ScrollingOneRowRebindsOneWidget )
{
    WidgetTree      tree;
    ListViewWidget& list = Util::makeList( tree, 100 );
    (void)UITestUtil::layout( tree, Util::kViewport );
    const uint32 bindsBefore = Util::s_bindCount;
    list.setScrollOffset( Util::kRowHeight );
    (void)UITestUtil::layout( tree, Util::kViewport );
    SW_EXPECT_EQUAL( bindsBefore + 1u, Util::s_bindCount );
    SW_EXPECT_TRUE( list.findItemIndex( *list.getChild( 0 ) ) == 6u ); // 항목 k 는 줄 k % 6 이 맡는다
    SW_EXPECT_TRUE( list.findItemIndex( *list.getChild( 1 ) ) == 1u );
    SW_EXPECT_EQUAL( 6u, list.getCreatedRowCount() );
}

/** @brief [ListViewWidgetTest] 스크롤은 끝에서 멈추고, 항목으로 옮기기는 보이게 하는 최소만 움직인다 */
SW_TEST_CASE( ListViewWidgetTest, ScrollClampsAndScrollToItemMovesMinimally )
{
    WidgetTree      tree;
    ListViewWidget& list = Util::makeList( tree, 100 );
    (void)UITestUtil::layout( tree, Util::kViewport );
    list.setScrollOffset( 1.0e6f );
    SW_EXPECT_EQUAL( 1900.0f, list.getScrollOffset() ); // 100 × 20 − 100
    list.setScrollOffset( 0.0f );
    list.scrollToItem( 2 ); // 이미 보인다
    SW_EXPECT_EQUAL( 0.0f, list.getScrollOffset() );
    list.scrollToItem( 10 ); // 아래 끝에 걸린다: 11 × 20 − 100
    SW_EXPECT_EQUAL( 120.0f, list.getScrollOffset() );
    list.scrollToItem( 1 ); // 위 끝에 걸린다
    SW_EXPECT_EQUAL( 20.0f, list.getScrollOffset() );
}

/** @brief [ListViewWidgetTest] 항목이 줄보다 적으면 남는 줄을 접고, 항목을 없애면 어떤 줄도 항목을 맡지 않는다 */
SW_TEST_CASE( ListViewWidgetTest, ExtraRowsCollapseWhenItemsShrink )
{
    WidgetTree      tree;
    ListViewWidget& list = Util::makeList( tree, 100 );
    (void)UITestUtil::layout( tree, Util::kViewport );
    list.setItemCount( 2 );
    (void)UITestUtil::layout( tree, Util::kViewport );
    SW_EXPECT_EQUAL( 6u, list.getCreatedRowCount() );
    SW_EXPECT_TRUE( list.getChild( 1 )->getVisibility() != WidgetVisibility::Collapsed );
    SW_EXPECT_TRUE( list.getChild( 2 )->getVisibility() == WidgetVisibility::Collapsed );
    SW_EXPECT_FALSE( list.findItemIndex( *list.getChild( 2 ) ).has_value() );
    list.setItemCount( 0 );
    (void)UITestUtil::layout( tree, Util::kViewport );
    SW_EXPECT_FALSE( list.findItemIndex( *list.getChild( 0 ) ).has_value() );
}

/** @brief [ListViewWidgetTest] 휠은 내려가고, 끝에서는 처리하지 않아 부모가 받는다 */
SW_TEST_CASE( ListViewWidgetTest, WheelScrollsUntilTheEnd )
{
    WidgetTree      tree;
    ListViewWidget& list = Util::makeList( tree, 6 ); // 넘치는 양은 20
    (void)UITestUtil::layout( tree, Util::kViewport );
    UIPointerEvent wheel;
    wheel._kind  = UIPointerEventKind::Wheel;
    wheel._wheel = -1.0f;
    SW_EXPECT_TRUE( list.onPointerEvent( wheel, UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 20.0f, list.getScrollOffset() );
    SW_EXPECT_FALSE( list.onPointerEvent( wheel, UIRoutePhase::Bubble ).isHandled() );
}
