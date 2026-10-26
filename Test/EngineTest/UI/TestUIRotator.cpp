#include "pch.h"

#include "Core/Container/vector.h"

#include "Engine/UI/Base/UIEvents.h"
#include "Engine/UI/Layout/BoxPanel.h"
#include "Engine/UI/Widgets/RotatorWidget.h"

#include "EngineTest/UI/UILayoutTestUtil.h"

#include "TestFramework/TestFramework.h"

// UIRotatorTest — 좌우로 고르는 선택 상자(RotatorWidget): 패드 좌우, 끝에서 돌기, 알림, 항목 하나일 때 처리하지 않음, 오른쪽에서 왼쪽 방향, 포인터(왼쪽 화살표 쪽은 이전). 디바이스 없음(nogpu).

namespace
{
    struct UIRotatorTestUtil
    {
        static sw::vector<sw::string> makeOptions()
        {
            return sw::vector<sw::string>{ "windowed", "borderless", "fullscreen" };
        }

        static sw::UIActionEvent makeAction( const utf8* pAction )
        {
            sw::UIActionEvent event{};
            event._action = sw::hashed_string( pAction );
            return event;
        }

        static sw::UIPointerEvent makePointer( sw::UIPointerEventKind kind, float32 x, float32 y )
        {
            sw::UIPointerEvent event{};
            event._kind     = kind;
            event._button   = sw::MouseButton::Left;
            event._position = sw::float2{ x, y };
            return event;
        }
    };

    /** @brief 가로 상자 안의 로테이터 하나(항목 셋, 첫 항목 고름)를 레이아웃까지 돌린 장치입니다. */
    struct UIRotatorFixture
    {
        sw::test::UILayoutFixture _layout;
        sw::RotatorWidget*        _pRotator;

        explicit UIRotatorFixture( bool bRightToLeft )
            : _layout{ 400.0f, 100.0f }
            , _pRotator{ nullptr }
        {
            _layout.getContext()._bRightToLeft = bRightToLeft;
            sw::BoxPanel* pRoot                = _layout.setRoot<sw::BoxPanel>( "root" );
            _pRotator                          = static_cast<sw::RotatorWidget*>( pRoot->addChild( sw::make_unique<sw::RotatorWidget>() ) );
            sw::WidgetLayoutSlot slot          = _pRotator->getLayoutSlot(); // 글꼴 없이 돌리므로 크기를 정해 준다
            slot._widthOverride                = 200.0f;
            slot._heightOverride               = 40.0f;
            _pRotator->setLayoutSlot( slot );
            _pRotator->setOptions( UIRotatorTestUtil::makeOptions() );
            _pRotator->setSelectedIndex( 0 );
            (void)_layout.update();
        }
    };
} // namespace

/**
 * @brief [UIRotatorTest] 좌우 행동이 항목을 하나씩 넘기고 끝에서 반대쪽 끝으로 돈다 — 알림은 바뀐 인덱스로 한 번씩, 처음에서 왼쪽은 마지막
 * @details 변이: `stepSelection` 의 나머지 계산(`( current + step ) % count + count`)을 빼면 처음에서 왼쪽이 음수 인덱스가 되어 진다.
 */
SW_TEST_CASE( UIRotatorTest, StepsWrapAndNotify )
{
    using Util = UIRotatorTestUtil;
    UIRotatorFixture       fixture( false );
    sw::RotatorWidget&     rotator = *fixture._pRotator;
    sw::vector<uint32>     listNotified;
    const sw::Subscription selectionSubscription = rotator.getOnSelectionChanged().add( [&listNotified]( uint32 index )
    { listNotified.push_back( index ); } );

    const sw::UIActionEvent right = Util::makeAction( sw::UIActionName::kNavigateRight );
    const sw::UIActionEvent left  = Util::makeAction( sw::UIActionName::kNavigateLeft );
    SW_EXPECT_TRUE( rotator.onActionEvent( right, sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 1u, rotator.getSelectedIndex() );
    SW_EXPECT_TRUE( rotator.onActionEvent( right, sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_TRUE( rotator.onActionEvent( right, sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 0u, rotator.getSelectedIndex() ); // 끝에서 처음으로
    SW_EXPECT_TRUE( rotator.onActionEvent( left, sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 2u, rotator.getSelectedIndex() ); // 처음에서 왼쪽은 마지막
    SW_ASSERT_EQUAL( size_t{ 4 }, listNotified.size() );
    SW_EXPECT_EQUAL( 1u, listNotified[0] );
    SW_EXPECT_EQUAL( 2u, listNotified[1] );
    SW_EXPECT_EQUAL( 0u, listNotified[2] );
    SW_EXPECT_EQUAL( 2u, listNotified[3] );

    SW_EXPECT_TRUE( rotator.onActionEvent( Util::makeAction( sw::UIActionName::kAccept ), sw::UIRoutePhase::Bubble ).isHandled() ); // 확인은 다음
    SW_EXPECT_EQUAL( 0u, rotator.getSelectedIndex() );
}

/**
 * @brief [UIRotatorTest] 항목이 하나뿐이면 좌우 행동을 처리하지 않아 포커스가 옆으로 떠난다 — 선택도 그대로
 * @details 변이: 항목 수 검사(`size() >= 2`)를 빼면 처리 완료가 되어 포커스 탐색이 막혀 진다.
 */
SW_TEST_CASE( UIRotatorTest, SingleOptionLeavesArrowsToNavigation )
{
    using Util = UIRotatorTestUtil;
    UIRotatorFixture   fixture( false );
    sw::RotatorWidget& rotator = *fixture._pRotator;
    rotator.setOptions( sw::vector<sw::string>{ "only" } );
    rotator.setSelectedIndex( 0 );
    SW_EXPECT_FALSE( rotator.onActionEvent( Util::makeAction( sw::UIActionName::kNavigateRight ), sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_FALSE( rotator.onActionEvent( Util::makeAction( sw::UIActionName::kNavigateLeft ), sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 0u, rotator.getSelectedIndex() );
    SW_EXPECT_FALSE( rotator.stepSelection( 1 ) );
}

/**
 * @brief [UIRotatorTest] 오른쪽에서 왼쪽 위젯에서는 화면 기준 오른쪽 키가 이전 항목이다(화살표가 뒤집혀 보이므로)
 * @details 변이: `onActionEvent` 의 `bRight != isRightToLeft()` 를 `bRight` 로 바꾸면 RTL 에서도 다음으로 가 진다.
 */
SW_TEST_CASE( UIRotatorTest, ArrowKeysMirrorInRightToLeft )
{
    using Util = UIRotatorTestUtil;
    UIRotatorFixture   fixture( true );
    sw::RotatorWidget& rotator = *fixture._pRotator;
    SW_ASSERT_TRUE( rotator.isRightToLeft() );
    SW_EXPECT_TRUE( rotator.onActionEvent( Util::makeAction( sw::UIActionName::kNavigateRight ), sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 2u, rotator.getSelectedIndex() ); // 0 에서 이전 = 마지막
    SW_EXPECT_TRUE( rotator.onActionEvent( Util::makeAction( sw::UIActionName::kNavigateLeft ), sw::UIRoutePhase::Bubble ).isHandled() );
    SW_EXPECT_EQUAL( 0u, rotator.getSelectedIndex() );
}

/**
 * @brief [UIRotatorTest] 포인터: 왼쪽 화살표 쪽을 누르고 떼면 이전, 가운데나 오른쪽이면 다음. 오른쪽에서 왼쪽에서는 화살표 위치가 뒤집힌다
 * @details 변이: `handleClick` 이 눌린 위치를 보지 않으면 왼쪽 화살표를 눌러도 다음으로 가 진다.
 */
SW_TEST_CASE( UIRotatorTest, PointerLeftArrowStepsBack )
{
    using Util = UIRotatorTestUtil;
    for ( const bool bRightToLeft : { false, true } )
    {
        UIRotatorFixture   fixture( bRightToLeft );
        sw::RotatorWidget& rotator = *fixture._pRotator;
        const sw::UIRect   bounds  = rotator.getGeometry().computeScreenBounds();
        const float32      midY    = ( bounds.getTop() + bounds.getBottom() ) * 0.5f;
        const float32      leftX   = bounds.getLeft() + 8.0f;
        const float32      rightX  = bounds.getRight() - 8.0f;
        const float32      middleX = ( bounds.getLeft() + bounds.getRight() ) * 0.5f;
        SW_ASSERT_TRUE( bounds.getRight() - bounds.getLeft() > 80.0f );

        const auto click = [&]( float32 x )
        {
            (void)rotator.onPointerEvent( Util::makePointer( sw::UIPointerEventKind::Down, x, midY ), sw::UIRoutePhase::Bubble );
            (void)rotator.onPointerEvent( Util::makePointer( sw::UIPointerEventKind::Up, x, midY ), sw::UIRoutePhase::Bubble );
        };
        // 왼쪽에서 오른쪽: 왼쪽 화살표 = 이전. 오른쪽에서 왼쪽: 오른쪽 화살표 = 이전.
        const float32 previousX = bRightToLeft ? rightX : leftX;
        const float32 nextX     = bRightToLeft ? leftX : rightX;
        click( nextX );
        SW_EXPECT_EQUAL( 1u, rotator.getSelectedIndex() );
        click( middleX );
        SW_EXPECT_EQUAL( 2u, rotator.getSelectedIndex() );
        click( previousX );
        SW_EXPECT_EQUAL( 1u, rotator.getSelectedIndex() );
    }
}
