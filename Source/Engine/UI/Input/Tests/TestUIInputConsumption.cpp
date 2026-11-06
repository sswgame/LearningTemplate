// UIInputConsumptionTest · UIInputRouterTest — UI 가 쓴 물리 입력이 뗄 때까지 게임에 보이지 않는 규칙, 마지막으로 쓴 장치로 입력 방식을 정하는 규칙(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/Devices/MouseDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/UI/Input/UIInputConsumption.h"
#include "Engine/UI/Input/UIInputRouter.h"
#include "Engine/UI/WidgetTree/UIEvents.h"

using namespace sw;

namespace
{
    /** @brief 창 없이 장치 상태만 쓰는 입력 관리자와, 소비할 액션 하나가 든 맵입니다. */
    struct UIConsumptionScene
    {
        InputManager        _input;
        InputMap            _map;
        UIInputConsumption  _consumption;
        const hashed_string _action{ "Accept" };

        UIConsumptionScene() { SW_EXPECT_TRUE( _input.initialize() ); }
        ~UIConsumptionScene() { _input.shutdown(); }

        void setKey( Key key, bool bDown ) { _input.getKeyboard()->setKeyDown( key, bDown ); }
        bool isConsumed() const { return _consumption.isActionConsumed( _map, _input, _action ); }
    };
} // namespace

/** @brief [UIInputConsumptionTest] 지금 눌린 슬롯만 소비한다. 뗀 프레임까지 소비로 남고 그 다음 update 에서 빠진다 */
SW_TEST_CASE( UIInputConsumptionTest, HeldSlotStaysConsumedUntilAfterRelease )
{
    UIConsumptionScene scene;
    scene._map.bind( scene._action, Key::Enter );
    scene._consumption.consumeAction( scene._map, scene._input, scene._action ); // 눌리지 않았으니 적을 것이 없다
    SW_EXPECT_FALSE( scene._consumption.hasConsumedInput() );
    SW_EXPECT_FALSE( scene.isConsumed() );

    scene.setKey( Key::Enter, true );
    scene._consumption.consumeAction( scene._map, scene._input, scene._action );
    SW_EXPECT_TRUE( scene.isConsumed() );
    scene._consumption.update( scene._input ); // 아직 누르고 있다
    SW_EXPECT_TRUE( scene.isConsumed() );

    scene.setKey( Key::Enter, false );
    scene._consumption.update( scene._input ); // 뗀 프레임: 뗌으로 발화하는 액션도 막는다
    SW_EXPECT_TRUE( scene.isConsumed() );
    scene._consumption.update( scene._input );
    SW_EXPECT_FALSE( scene.isConsumed() );
    SW_EXPECT_FALSE( scene._consumption.hasConsumedInput() );
}

/** @brief [UIInputConsumptionTest] 같은 액션의 소비되지 않은 다른 키를 함께 누르면 게임에 보인다 */
SW_TEST_CASE( UIInputConsumptionTest, FreeBindingKeepsTheActionVisible )
{
    UIConsumptionScene scene;
    scene._map.bind( scene._action, Key::Enter );
    scene._map.bind( scene._action, Key::Space );
    scene.setKey( Key::Enter, true );
    scene._consumption.consumeAction( scene._map, scene._input, scene._action );
    SW_EXPECT_TRUE( scene.isConsumed() );
    SW_EXPECT_TRUE( scene._consumption.isSlotConsumed( InputSlot::fromKey( Key::Enter ) ) );
    SW_EXPECT_FALSE( scene._consumption.isSlotConsumed( InputSlot::fromKey( Key::Space ) ) );

    scene.setKey( Key::Space, true );
    SW_EXPECT_FALSE( scene.isConsumed() );
}

/** @brief [UIInputConsumptionTest] 조합 키는 트리거만 소비한다(수정 키를 소비하면 달리기 Shift 가 묶인다). 맵이 달라도 슬롯으로 비교한다 */
SW_TEST_CASE( UIInputConsumptionTest, ChordConsumesOnlyTheTrigger )
{
    UIConsumptionScene scene;
    scene._map.bindChord( scene._action, Key::LeftShift, Key::Tab );
    scene.setKey( Key::LeftShift, true );
    scene.setKey( Key::Tab, true );
    scene._consumption.consumeAction( scene._map, scene._input, scene._action );
    SW_EXPECT_TRUE( scene._consumption.isSlotConsumed( InputSlot::fromKey( Key::Tab ) ) );
    SW_EXPECT_FALSE( scene._consumption.isSlotConsumed( InputSlot::fromKey( Key::LeftShift ) ) );

    InputMap            gameMap;
    const hashed_string gameAction( "GameTab" );
    const hashed_string runAction( "Run" );
    gameMap.bind( gameAction, Key::Tab );
    gameMap.bind( runAction, Key::LeftShift );
    SW_EXPECT_TRUE( scene._consumption.isActionConsumed( gameMap, scene._input, gameAction ) );
    SW_EXPECT_FALSE( scene._consumption.isActionConsumed( gameMap, scene._input, runAction ) );
}

/** @brief [UIInputConsumptionTest] 마우스 누름은 뗄 때까지 남고, 뗌은 이번 프레임만 막아 바로 다음 누름을 먹지 않는다 */
SW_TEST_CASE( UIInputConsumptionTest, MouseButtonEventLifetime )
{
    UIConsumptionScene scene;
    const InputSlot    slot = InputSlot::fromMouseButton( MouseButton::Left );
    UIPointerEvent     event;
    event._button = MouseButton::Left;
    event._kind   = UIPointerEventKind::Down;
    scene._input.getMouse()->setButtonDown( MouseButton::Left, true );
    scene._consumption.consumeMouseButton( event );
    scene._consumption.update( scene._input );
    SW_EXPECT_TRUE( scene._consumption.isSlotConsumed( slot ) );

    event._kind = UIPointerEventKind::Up;
    scene._input.getMouse()->setButtonDown( MouseButton::Left, false );
    scene._consumption.consumeMouseButton( event );
    SW_EXPECT_TRUE( scene._consumption.isSlotConsumed( slot ) ); // 뗀 프레임
    scene._consumption.update( scene._input );
    SW_EXPECT_FALSE( scene._consumption.isSlotConsumed( slot ) );
}

/** @brief [UIInputConsumptionTest] 직접 소비한 슬롯은 clear 로 모두 풀린다. 뗀 프레임에 다시 소비하면 새 누름으로 남는다 */
SW_TEST_CASE( UIInputConsumptionTest, ConsumeSlotAndClear )
{
    UIConsumptionScene scene;
    const InputSlot    slot = InputSlot::fromKey( Key::Escape );
    scene._consumption.consumeSlot( slot );
    scene._consumption.update( scene._input ); // 눌리지 않았으니 뗀 것으로 적힌다
    SW_EXPECT_TRUE( scene._consumption.isSlotConsumed( slot ) );
    scene._consumption.consumeSlot( slot ); // 다시 소비
    scene._consumption.update( scene._input );
    SW_EXPECT_TRUE( scene._consumption.isSlotConsumed( slot ) );
    scene._consumption.clear();
    SW_EXPECT_FALSE( scene._consumption.hasConsumedInput() );
}

/** @brief [UIInputConsumptionTest] 기울인 스틱은 소비되고, 놓은 것으로 볼 기울기 아래로 돌아오면 풀린다. 살짝 기울인 스틱은 소비하지 않는다 */
SW_TEST_CASE( UIInputConsumptionTest, StickReleasesBelowTheThreshold )
{
    UIConsumptionScene scene;
    GamepadDevice*     pPad = scene._input.getGamepad( 0 );
    if ( pPad == nullptr )
        return; // 이 플랫폼에는 게임패드 장치가 없다
    pPad->setVirtualSession( true );
    pPad->setVirtualConnected( true );
    scene._map.bindGamepadStick2D( scene._action, GamepadStick::Left );

    pPad->setAxis( 0, 1.0f );
    SW_REQUIRE_TRUE( pPad->getLeftStick()._x > UIInputConsumption::kStickReleaseMagnitude );
    scene._consumption.consumeAction( scene._map, scene._input, scene._action );
    SW_EXPECT_TRUE( scene.isConsumed() );
    scene._consumption.update( scene._input );
    SW_EXPECT_TRUE( scene.isConsumed() );

    pPad->setAxis( 0, 0.0f );
    scene._consumption.update( scene._input );
    SW_EXPECT_FALSE( scene.isConsumed() );
    SW_EXPECT_FALSE( scene._consumption.hasConsumedInput() );

    scene._consumption.consumeAction( scene._map, scene._input, scene._action ); // 놓은 스틱은 적지 않는다
    SW_EXPECT_FALSE( scene._consumption.hasConsumedInput() );
    pPad->setVirtualSession( false );
}

/** @brief [UIInputRouterTest] 입력 방식 판단: 키 · 패드는 탐색, 마우스는 포인터다. 작은 스틱 떨림과 뗌은 방식을 가리키지 않는다 */
SW_TEST_CASE( UIInputRouterTest, RawEventsPointAtAMode )
{
    UIInputMode mode = UIInputMode::Pointer;
    SW_REQUIRE_TRUE( UIInputRouter::tryGetInputMode( RawInputEvent::makeKeyDown( Key::A ), mode ) );
    SW_EXPECT_TRUE( mode == UIInputMode::Navigation );
    SW_REQUIRE_TRUE( UIInputRouter::tryGetInputMode( RawInputEvent::makeMouseMove( 1, 1 ), mode ) );
    SW_EXPECT_TRUE( mode == UIInputMode::Pointer );
    SW_REQUIRE_TRUE( UIInputRouter::tryGetInputMode( RawInputEvent::makeMouseButtonDown( MouseButton::Left ), mode ) );
    SW_EXPECT_TRUE( mode == UIInputMode::Pointer );
    SW_EXPECT_FALSE( UIInputRouter::tryGetInputMode( RawInputEvent::makeKeyUp( Key::A ), mode ) );

    RawInputEvent axis;
    axis._type                            = RawInputEventType::GamepadAxis;
    axis._payload._gamepadData._axisValue = 0.1f;
    SW_EXPECT_FALSE( UIInputRouter::tryGetInputMode( axis, mode ) );
    axis._payload._gamepadData._axisValue = -0.9f;
    SW_REQUIRE_TRUE( UIInputRouter::tryGetInputMode( axis, mode ) );
    SW_EXPECT_TRUE( mode == UIInputMode::Navigation );
}

/** @brief [UIInputRouterTest] 프레임 안에서 마지막 장치가 정한다. 첫 커서 이벤트와 같은 자리 이동은 패드로 고른 방식을 지우지 않는다 */
SW_TEST_CASE( UIInputRouterTest, PollFollowsTheLastDeviceAndIgnoresStillCursor )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    UIInputRouter router;
    SW_REQUIRE_TRUE( router.initialize( input, AssetID{} ) );
    SW_EXPECT_TRUE( router.getInputMode() == UIInputMode::Pointer );
    SW_EXPECT_FALSE( router.setInputMode( UIInputMode::Pointer ) ); // 같으면 바뀌지 않는다
    SW_EXPECT_TRUE( router.setInputMode( UIInputMode::Navigation ) );

    UIInputMode mode = UIInputMode::Pointer;
    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeMouseMove( 10, 10 ) ) ); // 창이 뜰 때의 첫 이동: 위치만 기억한다
    input.beginFrame();
    SW_EXPECT_FALSE( router.pollInputMode( mode ) );
    SW_EXPECT_TRUE( mode == UIInputMode::Navigation );
    input.endFrame();

    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeMouseMove( 10, 10 ) ) ); // 같은 자리
    input.beginFrame();
    SW_EXPECT_FALSE( router.pollInputMode( mode ) );
    input.endFrame();

    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeMouseMove( 20, 10 ) ) );
    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::A ) ) );
    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeKeyUp( Key::A ) ) );
    SW_REQUIRE_TRUE( input.postRawEvent( RawInputEvent::makeMouseMove( 30, 10 ) ) );
    input.beginFrame();
    SW_EXPECT_TRUE( router.pollInputMode( mode ) );
    SW_EXPECT_TRUE( mode == UIInputMode::Pointer );
    input.endFrame();

    router.shutdown();
    input.shutdown();
}
