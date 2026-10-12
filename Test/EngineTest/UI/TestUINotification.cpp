#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Screen/UINotificationService.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "EngineTest/UI/UIScopedVariable.h"

#include "TestFramework/TestFramework.h"

// UINotificationTest — 알림 · 토스트(8-3): 셋까지 쌓고 대기열(우선순위), 같은 글 합치기("x2"), 시간이 다 되면 내리고 다음을 올림, 입력 · 포커스를 받지 않음.
// 엔진 문서 engine/ui/notifications.ui.xml · parts/notification.ui.xml 을 쓴다. 디바이스 없음(nogpu).

namespace
{
    struct UINotificationTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        static sw::UINotificationDesc make( const utf8* pText, float32 durationSeconds = 4.0f, int32 priority = 0 )
        {
            sw::UINotificationDesc desc{};
            desc._text            = pText;
            desc._durationSeconds = durationSeconds;
            desc._priority        = priority;
            return desc;
        }
    };

    struct UINotificationFixture
    {
        sw::InputManager _input;
        sw::UISystem     _ui;
        sw::UIViewport   _viewport;

        UINotificationFixture()
            : _input{}
            , _ui{}
            , _viewport{}
        {
            _viewport._size         = sw::float2{ 1920.0f, 1080.0f };
            _viewport._physicalSize = _viewport._size;
            SW_EXPECT_TRUE( _input.initialize() );
            SW_EXPECT_TRUE( _ui.initialize( _input, nullptr, "engine/input/ui.input.xml" ) );
        }

        ~UINotificationFixture()
        {
            _ui.shutdown();
            _input.shutdown();
        }

        UINotificationFixture( const UINotificationFixture& )            = delete;
        UINotificationFixture& operator=( const UINotificationFixture& ) = delete;

        void runFrame( float32 deltaSeconds = UINotificationTestUtil::kFrameSeconds )
        {
            _input.beginFrame( deltaSeconds );
            _ui.processInput( deltaSeconds );
            _ui.update( deltaSeconds, _viewport );
            _input.endFrame();
        }

        sw::UINotificationService& notes() { return _ui.getNotifications(); }
    };
} // namespace

/** @brief [UINotificationTest] 넷째부터는 기다린다 — 우선순위가 큰 것이 먼저 나오고, 보이는 것이 내려가면 다음이 올라온다 */
SW_TEST_CASE( UINotificationTest, QueuesBeyondThree )
{
    using Util = UINotificationTestUtil;
    UINotificationFixture fixture;
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
    const sw::UIScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::PanelWidget* pStack = pScreen->getTree().findWidget<sw::PanelWidget>( "Stack" );
    SW_ASSERT_NOT_NULL( pStack );
    SW_EXPECT_EQUAL( 4u, pStack->getChildCount() ); // 내린 항목의 위젯은 페이드가 끝날 때까지 남는다
    SW_EXPECT_EQUAL( 1u, fixture.notes().getFadingCount() );
    fixture.runFrame( 0.25f );
    SW_EXPECT_EQUAL( 3u, pStack->getChildCount() ); // 페이드가 끝나 지웠다
    SW_EXPECT_EQUAL( 0u, fixture.notes().getFadingCount() );
}

/** @brief [UINotificationTest] 같은 글을 다시 올리면 새로 쌓지 않고 센다("x2") · 시간을 다시 측정한다 */
SW_TEST_CASE( UINotificationTest, SameTextMergesWithCount )
{
    using Util = UINotificationTestUtil;
    UINotificationFixture fixture;
    fixture.notes().post( Util::make( "Game saved", 2.0f ) );
    fixture.runFrame( 1.5f );
    fixture.notes().post( Util::make( "Game saved", 2.0f ) );
    fixture.runFrame( 1.0f ); // 다시 측정한다 — 처음부터 2.5 초지만 남아 있다
    SW_ASSERT_EQUAL( 1u, fixture.notes().getVisibleCount() );
    SW_EXPECT_EQUAL( 2u, fixture.notes().getVisibleCountOf( 0 ) );
    const sw::UIScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::TextWidget* pCount = pScreen->getTree().findWidget<sw::TextWidget>( "0.Count" ); // 항목마다 이름이 다르다(번호.Count)
    SW_ASSERT_NOT_NULL( pCount );
    SW_EXPECT_STREQ( "x2", pCount->getText().c_str() );
    SW_EXPECT_TRUE( pCount->isVisible() );
}

/** @brief [UINotificationTest] 시간이 다 되면 내리고, 보일 것이 없으면 알림 화면을 닫는다 */
SW_TEST_CASE( UINotificationTest, ExpiresAfterDuration )
{
    using Util = UINotificationTestUtil;
    UINotificationFixture fixture;
    fixture.notes().post( Util::make( "short", 0.5f ) );
    fixture.runFrame();
    SW_EXPECT_EQUAL( 1u, fixture._ui.getScreenCount() );
    fixture.runFrame( 0.3f );
    SW_EXPECT_EQUAL( 1u, fixture.notes().getVisibleCount() );
    fixture.runFrame( 0.3f );
    SW_EXPECT_EQUAL( 0u, fixture.notes().getVisibleCount() );
    SW_EXPECT_EQUAL( 1u, fixture.notes().getFadingCount() );
    SW_EXPECT_EQUAL( 1u, fixture._ui.getScreenCount() ); // 사라지는 항목이 있는 동안은 화면을 닫지 않는다
    fixture.runFrame( 0.25f );
    SW_EXPECT_EQUAL( 0u, fixture.notes().getFadingCount() );
    fixture.runFrame();
    SW_EXPECT_EQUAL( 0u, fixture._ui.getScreenCount() );
    SW_EXPECT_TRUE( fixture.notes().getScreen() == sw::kInvalidUIScreenHandle );
}

/** @brief [UINotificationTest] 알림은 오버레이 층 — 메뉴가 떠 있어도 활성 화면 · 포커스 · 게임 입력 막기가 그대로다 */
SW_TEST_CASE( UINotificationTest, NeverTakesFocusOrInput )
{
    using Util = UINotificationTestUtil;
    UINotificationFixture fixture;
    fixture.runFrame();
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == nullptr );
    fixture.notes().post( Util::make( "hello" ) );
    fixture.runFrame();
    const sw::UIScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    SW_EXPECT_TRUE( pScreen->getDesc()._layer == sw::UILayer::Overlay );
    SW_EXPECT_FALSE( pScreen->takesFocus() );
    SW_EXPECT_FALSE( pScreen->receivesPointer() );
    SW_EXPECT_TRUE( fixture._ui.getActiveScreen() == nullptr );
    SW_EXPECT_FALSE( fixture._ui.isGameInputBlocked() );
    SW_EXPECT_FALSE( fixture._ui.wantsCursor() );
    SW_EXPECT_FALSE( fixture._ui.getUIInputMap()->isLayerEnabled( "UI" ) ); // UI 행동을 켜지 않는다
}

/**
 * @brief [UINotificationTest] 항목은 불투명도 0 에서 1 로 들어오고(페이드 인), 시간이 다 되면 자리는 바로 비고 위젯은 1 에서 0 으로 사라진 뒤 지워진다
 * @details 변이: `showEntry` 의 시작 불투명도 0 을 빼면 첫 프레임에 이미 1 이라 진다. `startFadeOut` 이 바로 지우면 페이드 도중 위젯이 없어 진다.
 */
SW_TEST_CASE( UINotificationTest, EntryFadesInAndOut )
{
    using Util = UINotificationTestUtil;
    UINotificationFixture fixture;
    fixture.notes().post( Util::make( "fade", 1.0f ) );
    fixture.runFrame( 0.0f );
    const sw::UIScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::Widget* pEntry = pScreen->getTree().findWidgetByName( "0" );
    SW_ASSERT_NOT_NULL( pEntry );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pEntry->getOpacity(), 1e-4f ); // 막 붙었다
    fixture.runFrame( sw::UINotificationService::kFadeSeconds * 0.5f );
    SW_EXPECT_TRUE( pEntry->getOpacity() > 0.0f && pEntry->getOpacity() < 1.0f );
    fixture.runFrame( sw::UINotificationService::kFadeSeconds );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pEntry->getOpacity(), 1e-4f );

    fixture.runFrame( 1.0f ); // 시간이 다 됐다
    SW_EXPECT_EQUAL( 0u, fixture.notes().getVisibleCount() );
    const sw::Widget* pFading = pScreen->getTree().findWidgetByName( "0" );
    SW_ASSERT_NOT_NULL( pFading ); // 위젯은 페이드 동안 남는다
    fixture.runFrame( sw::UINotificationService::kFadeSeconds * 0.5f );
    SW_EXPECT_TRUE( pFading->getOpacity() < 1.0f );
    fixture.runFrame( sw::UINotificationService::kFadeSeconds );
    // 보이는 항목도 사라지는 항목도 없으면 서비스가 화면을 닫는다 — `pScreen` 은 이미 없으므로 읽지 않고 화면이 닫혔는지 본다.
    SW_EXPECT_EQUAL( 0u, fixture.notes().getFadingCount() );
    SW_EXPECT_TRUE( fixture._ui.findScreen( fixture.notes().getScreen() ) == nullptr );
}

/**
 * @brief [UINotificationTest] 움직임 줄이기이면 페이드 없이 바로 보이고 바로 지워진다
 * @details 변이: `startFadeOut` 이 `gv_uiReduceMotion` 을 보지 않으면 사라지는 목록에 한 항목이 남아 진다.
 */
SW_TEST_CASE( UINotificationTest, ReduceMotionSkipsFades )
{
    using Util = UINotificationTestUtil;
    const ScopedBoolVariable reduceMotion( "gv_uiReduceMotion", true );
    UINotificationFixture    fixture;
    fixture.notes().post( Util::make( "instant", 0.5f ) );
    fixture.runFrame( 0.0f );
    const sw::UIScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    const sw::Widget* pEntry = pScreen->getTree().findWidgetByName( "0" );
    SW_ASSERT_NOT_NULL( pEntry );
    fixture.runFrame( 0.0f ); // 트윈 단계가 끝 값을 쓴다
    SW_EXPECT_NEAR_EQUAL( 1.0f, pEntry->getOpacity(), 1e-4f );
    fixture.runFrame( 0.6f );
    SW_EXPECT_EQUAL( 0u, fixture.notes().getFadingCount() );
    SW_EXPECT_TRUE( pScreen->getTree().findWidgetByName( "0" ) == nullptr );
}

/**
 * @brief [UINotificationTest] 항목마다 위젯 이름이 겹치지 않는다 — `<번호>` · `<번호>.Message` · `<번호>.Count` 가 한 트리에 한 번씩이다
 * @details 변이: `showEntry` 의 `setName` 을 빼면 둘째 항목부터 이름이 겹쳐 "1.Message" 를 찾지 못한다.
 */
SW_TEST_CASE( UINotificationTest, EntryWidgetNamesAreUnique )
{
    using Util = UINotificationTestUtil;
    UINotificationFixture fixture;
    fixture.notes().post( Util::make( "first" ) );
    fixture.notes().post( Util::make( "second" ) );
    fixture.notes().post( Util::make( "third" ) );
    fixture.runFrame();
    const sw::UIScreen* pScreen = fixture._ui.findScreen( fixture.notes().getScreen() );
    SW_ASSERT_NOT_NULL( pScreen );
    const utf8* arrExpected[] = { "first", "second", "third" };
    for ( uint32 index = 0; index < 3; ++index )
    {
        const sw::string      messageName = sw::to_string( index ) + ".Message";
        const sw::TextWidget* pMessage    = pScreen->getTree().findWidget<sw::TextWidget>( sw::hashed_string( messageName ) );
        SW_ASSERT_NOT_NULL( pMessage );
        SW_EXPECT_STREQ( arrExpected[index], pMessage->getText().c_str() );
        SW_EXPECT_NOT_NULL( pScreen->getTree().findWidget<sw::TextWidget>( sw::hashed_string( sw::to_string( index ) + ".Count" ) ) );
    }
    SW_EXPECT_TRUE( pScreen->getTree().findWidgetByName( "Message" ) == nullptr ); // 조각의 옛 이름은 남지 않는다
}
