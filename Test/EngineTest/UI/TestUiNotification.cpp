#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Screen/UiNotificationService.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "TestFramework/TestFramework.h"

// UiNotificationTest — 알림 · 토스트(8-3): 셋까지 쌓고 대기열(우선순위), 같은 글 합치기("x2"), 시간이 다 되면 내리고 다음을 올림, 입력 · 포커스를 받지 않음.
// 엔진 문서 engine/ui/notifications.ui.xml · parts/notification.ui.xml 을 쓴다. 디바이스 없음(nogpu).

namespace
{
    struct UiNotificationTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        static sw::UiNotificationDesc make( const utf8* pText, float32 durationSeconds = 4.0f, int32 priority = 0 )
        {
            sw::UiNotificationDesc desc{};
            desc._text            = pText;
            desc._durationSeconds = durationSeconds;
            desc._priority        = priority;
            return desc;
        }
    };

    struct UiNotificationFixture
    {
        sw::InputManager _input;
        sw::UiSystem     _ui;
        sw::UiViewport   _viewport;

        UiNotificationFixture()
            : _input{}
            , _ui{}
            , _viewport{}
        {
            _viewport._size         = sw::float2{ 1920.0f, 1080.0f };
            _viewport._physicalSize = _viewport._size;
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr, "engine/input/ui.input.xml" ) );
        }

        ~UiNotificationFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UiNotificationFixture( const UiNotificationFixture& )            = delete;
        UiNotificationFixture& operator=( const UiNotificationFixture& ) = delete;

        void runFrame( float32 deltaSeconds = UiNotificationTestUtil::kFrameSeconds )
        {
            _input.beginFrame( deltaSeconds );
            _ui.processInput( deltaSeconds );
            _ui.update( deltaSeconds, _viewport );
            _input.endFrame();
        }

        sw::UiNotificationService& notes() { return _ui.getNotifications(); }
    };
} // namespace

/** @brief [UiNotificationTest] 넷째부터는 기다린다 — 우선순위가 큰 것이 먼저 나오고, 보이는 것이 내려가면 다음이 올라온다 */
SW_TEST_CASE( UiNotificationTest, QueuesBeyondThree )
{
    using Util = UiNotificationTestUtil;
    UiNotificationFixture fixture;
    fixture.notes().post( Util::make( "one", 1.0f ) );
    fixture.notes().post( Util::make( "two", 5.0f ) );
    fixture.notes().post( Util::make( "three", 5.0f ) );
    fixture.notes().post( Util::make( "four", 5.0f ) );
    fixture.notes().post( Util::make( "urgent", 5.0f, 10 ) );
    fixture.runFrame();
    SW_ASSERT_EQUAL( 3u, fixture.notes().getVisibleCount() );
    SW_EXPECT_EQUAL( 2u, fixture.notes().getQueuedCount() );
    SW_EXPECT_STREQ( "urgent", fixture.notes().getVisibleText( 0 ).c_str() ); // 우선순위가 큰 것이 먼저
    SW_EXPECT_STREQ( "one", fixture.notes().getVisibleText( 1 ).c_str() );
    SW_EXPECT_STREQ( "two", fixture.notes().getVisibleText( 2 ).c_str() );

    fixture.runFrame( 1.1f ); // "one" 이 끝났다 — 기다리던 "three"
    SW_ASSERT_EQUAL( 3u, fixture.notes().getVisibleCount() );
    SW_EXPECT_STREQ( "three", fixture.notes().getVisibleText( 2 ).c_str() );
    SW_EXPECT_EQUAL( 1u, fixture.notes().getQueuedCount() );
    const sw::UiScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::PanelWidget* pStack = pScreen->getTree().findWidget<sw::PanelWidget>( "Stack" );
    SW_ASSERT_NOT_NULL( pStack );
    SW_EXPECT_EQUAL( 3u, pStack->getChildCount() ); // 내린 항목의 위젯은 지웠다
}

/** @brief [UiNotificationTest] 같은 글을 다시 올리면 새로 쌓지 않고 센다("x2") · 시간을 다시 잰다 */
SW_TEST_CASE( UiNotificationTest, SameTextMergesWithCount )
{
    using Util = UiNotificationTestUtil;
    UiNotificationFixture fixture;
    fixture.notes().post( Util::make( "Game saved", 2.0f ) );
    fixture.runFrame( 1.5f );
    fixture.notes().post( Util::make( "Game saved", 2.0f ) );
    fixture.runFrame( 1.0f ); // 다시 잰다 — 처음부터 2.5 초지만 남아 있다
    SW_ASSERT_EQUAL( 1u, fixture.notes().getVisibleCount() );
    SW_EXPECT_EQUAL( 2u, fixture.notes().getVisibleCountOf( 0 ) );
    const sw::UiScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::TextWidget* pCount = pScreen->getTree().findWidget<sw::TextWidget>( "Count" );
    SW_ASSERT_NOT_NULL( pCount );
    SW_EXPECT_STREQ( "x2", pCount->getText().c_str() );
    SW_EXPECT_TRUE( pCount->isVisible() );
}

/** @brief [UiNotificationTest] 시간이 다 되면 내리고, 보일 것이 없으면 알림 화면을 닫는다 */
SW_TEST_CASE( UiNotificationTest, ExpiresAfterDuration )
{
    using Util = UiNotificationTestUtil;
    UiNotificationFixture fixture;
    fixture.notes().post( Util::make( "short", 0.5f ) );
    fixture.runFrame();
    SW_EXPECT_EQUAL( 1u, fixture._ui.getScreenCount() );
    fixture.runFrame( 0.3f );
    SW_EXPECT_EQUAL( 1u, fixture.notes().getVisibleCount() );
    fixture.runFrame( 0.3f );
    SW_EXPECT_EQUAL( 0u, fixture.notes().getVisibleCount() );
    fixture.runFrame();
    SW_EXPECT_EQUAL( 0u, fixture._ui.getScreenCount() );
    SW_EXPECT_TRUE( fixture.notes().getScreen() == sw::kInvalidUiScreenHandle );
}

/** @brief [UiNotificationTest] 알림은 오버레이 층 — 메뉴가 떠 있어도 활성 화면 · 포커스 · 게임 입력 막기가 그대로다 */
SW_TEST_CASE( UiNotificationTest, NeverTakesFocusOrInput )
{
    using Util = UiNotificationTestUtil;
    UiNotificationFixture fixture;
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == nullptr );
    fixture.notes().post( Util::make( "hello" ) );
    fixture.runFrame();
    const sw::UiScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_TRUE( pScreen->getDesc()._layer == sw::UiLayer::Overlay );
    SW_EXPECT_FALSE( pScreen->takesFocus() );
    SW_EXPECT_FALSE( pScreen->receivesPointer() );
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == nullptr );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );
    SW_EXPECT_FALSE( fixture._ui.wantsCursor() );
    SW_EXPECT_FALSE( fixture._ui.getUiInputMap()->isLayerEnabled( "UI" ) ); // UI 행동을 켜지 않는다
}
