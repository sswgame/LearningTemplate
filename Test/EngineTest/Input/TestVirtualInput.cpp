#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/RawInputEvent.h"
#include "Engine/Input/VirtualInputScript.h"

#include "TestFramework/TestFramework.h"

// 가상 입력 — OS 사건과 같은 재생 자리, 프레임 번호, 배타 모드(OS 사건 · 포커스 잃음 무시).
namespace
{
    struct VirtualInputTestInternal
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        static void runFrame( sw::InputManager& input )
        {
            input.beginFrame( kFrameSeconds );
            input.endFrame();
        }
    };
} // namespace

/**
 * @brief [VirtualInputTest] 적어 둔 프레임에 정확히 들어오고, Pressed trigger 는 누른 프레임 한 번만 참이다(Shooter3D Q/E 와 같은 바인딩)
 */
SW_TEST_CASE( VirtualInputTest, ScriptedTapTriggersOnItsFrameOnly )
{
    using Internal = VirtualInputTestInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bindAxis1DComposite( "SwitchWeapon", sw::Key::Q, sw::Key::E, {}, sw::ActionTrigger::Pressed );

    sw::VirtualInputScript script;
    SW_ASSERT_TRUE( script.addTap( 3, sw::InputSlot::fromKey( sw::Key::E ), 4 ) ); // 3 에 누르고 7 에 뗀다
    input.attachVirtualInput( &script );

    uint32 triggeredCount = 0;
    for ( uint32 frame = 0; frame < 10; ++frame )
    {
        input.beginFrame( Internal::kFrameSeconds );
        if ( inputMap.wasActionTriggered( "SwitchWeapon" ) )
        {
            SW_EXPECT_EQUAL( 3u, frame );
            SW_EXPECT_TRUE( inputMap.getAxis1D( "SwitchWeapon" ) > 0.0f );
            ++triggeredCount;
        }
        SW_EXPECT_EQUAL( frame >= 3 && frame < 7, inputMap.isActionDown( "SwitchWeapon" ) );
        input.endFrame();
    }
    SW_EXPECT_EQUAL( 1u, triggeredCount );
    SW_EXPECT_TRUE( script.isFinished( input.getVirtualFrameIndex() ) );
    input.detachVirtualInput();
    input.shutdown();
}

/**
 * @brief [VirtualInputTest] 배타 모드는 OS 키 사건과 창 포커스 잃음을 무시한다 — 창이 포커스를 못 받아도 가상 키가 눌린 채 남는다
 */
SW_TEST_CASE( VirtualInputTest, ExclusiveModeIgnoresOsEventsAndFocusLoss )
{
    using Internal = VirtualInputTestInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bind( "Fire", sw::Key::J, sw::ActionTrigger::Down );
    inputMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Down );

    sw::VirtualInputScript script;
    SW_ASSERT_TRUE( script.addSlot( 1, sw::InputSlot::fromKey( sw::Key::J ), true ) );
    input.attachVirtualInput( &script, sw::VirtualInputMode::Exclusive );
    SW_EXPECT_TRUE( input.isOsInputSuppressed() );

    Internal::runFrame( input ); // 0
    // OS 가 낸 것처럼: 사람이 Space 를 누른다(플랫폼 처리기가 넣는 그 큐).
    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) ) );
    input.beginFrame( Internal::kFrameSeconds ); // 1 — 가상 J 누름
    SW_EXPECT_TRUE( inputMap.isActionDown( "Fire" ) );
    SW_EXPECT_FALSE( inputMap.isActionDown( "Jump" ) );
    input.endFrame();

    // 창이 포커스를 잃는다 — 플랫폼 처리기의 포커스 사건과 같은 큐 · 같은 사건.
    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeFocusChange( false ) ) );
    input.beginFrame( Internal::kFrameSeconds ); // 2 — 포커스 잃음은 무시, J 는 아직 눌림
    SW_EXPECT_TRUE( inputMap.isActionDown( "Fire" ) );
    input.endFrame();
    input.detachVirtualInput();
    SW_EXPECT_FALSE( input.isOsInputSuppressed() );

    // 떼면 OS 입력이 다시 들어온다.
    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) ) );
    input.beginFrame( Internal::kFrameSeconds );
    SW_EXPECT_TRUE( inputMap.isActionDown( "Jump" ) );
    SW_EXPECT_FALSE( inputMap.isActionDown( "Fire" ) ); // 뗄 때 장치 상태를 지운다
    input.endFrame();
    input.shutdown();
}

/**
 * @brief [VirtualInputTest] 혼합 모드는 OS 사건도 받는다 — 가상 키와 사람 키가 함께 든다
 */
SW_TEST_CASE( VirtualInputTest, MixedModeKeepsOsEvents )
{
    using Internal = VirtualInputTestInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bind( "Fire", sw::Key::J, sw::ActionTrigger::Down );
    inputMap.bind( "Jump", sw::Key::Space, sw::ActionTrigger::Down );

    sw::VirtualInputScript script;
    SW_ASSERT_TRUE( script.addSlot( 0, sw::InputSlot::fromKey( sw::Key::J ), true ) );
    input.attachVirtualInput( &script, sw::VirtualInputMode::Mixed );
    SW_EXPECT_FALSE( input.isOsInputSuppressed() );
    SW_EXPECT_TRUE( input.postRawEvent( sw::RawInputEvent::makeKeyDown( sw::Key::Space ) ) );
    input.beginFrame( Internal::kFrameSeconds );
    SW_EXPECT_TRUE( inputMap.isActionDown( "Fire" ) );
    SW_EXPECT_TRUE( inputMap.isActionDown( "Jump" ) );
    input.endFrame();
    input.detachVirtualInput();
    input.shutdown();
}

/**
 * @brief [VirtualInputTest] 가상 패드 — 연결 사건 전에는 끊김, 연결 뒤 버튼이 바인딩에 든다(실제 패드 폴링이 덮지 않는다)
 */
SW_TEST_CASE( VirtualInputTest, VirtualGamepadConnectsAndDrivesBindings )
{
    using Internal = VirtualInputTestInternal;
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::GamepadDevice* pPad = input.getGamepad( 0 );
    if ( pPad == nullptr )
        SW_TEST_SKIP( "this platform registers no gamepad slot" );
    sw::InputMap& inputMap = input.getInputMap();
    inputMap.bind( "Jump", sw::GamepadButton::A, sw::ActionTrigger::Down );
    sw::VirtualInputScript script;
    script.addEvent( 1, sw::RawInputEvent::makeGamepadConnection( 0, true ) );
    SW_ASSERT_TRUE( script.addSlot( 2, sw::InputSlot::fromGamepadButton( sw::GamepadButton::A ), true ) );
    input.attachVirtualInput( &script );

    input.beginFrame( Internal::kFrameSeconds ); // 0 — 가상 세션은 끊김으로 시작한다
    SW_EXPECT_FALSE( pPad->isConnected() );
    input.endFrame();
    input.beginFrame( Internal::kFrameSeconds ); // 1 — 연결 사건
    SW_EXPECT_TRUE( pPad->isConnected() );
    input.endFrame();
    input.beginFrame( Internal::kFrameSeconds ); // 2 — A 누름
    SW_EXPECT_TRUE( inputMap.isActionDown( "Jump" ) );
    input.endFrame();
    input.beginFrame( Internal::kFrameSeconds ); // 3 — 누른 채(폴링이 덮지 않는다)
    SW_EXPECT_TRUE( inputMap.isActionDown( "Jump" ) );
    input.endFrame();
    input.detachVirtualInput();
    SW_EXPECT_FALSE( pPad->isInVirtualSession() );
    input.shutdown();
}

/**
 * @brief [VirtualInputTest] 같은 프레임의 사건은 더한 순서를 지키고, 다시 붙이면 프레임 0 부터 다시 낸다
 */
SW_TEST_CASE( VirtualInputTest, ScriptKeepsOrderAndRewinds )
{
    sw::VirtualInputScript script;
    SW_ASSERT_TRUE( script.addSlot( 2, sw::InputSlot::fromKey( sw::Key::A ), true ) );
    SW_ASSERT_TRUE( script.addSlot( 1, sw::InputSlot::fromKey( sw::Key::B ), true ) );
    SW_ASSERT_TRUE( script.addSlot( 2, sw::InputSlot::fromKey( sw::Key::A ), false ) );
    SW_EXPECT_FALSE( script.addSlot( 0, sw::InputSlot::fromCustom( sw::InputDeviceKind::Gamepad, 100 ), true ) ); // 트리거는 축이다

    sw::vector<sw::RawInputEvent> listEvent;
    script.emitFrame( 2, listEvent );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listEvent.size() ) );
    SW_EXPECT_TRUE( listEvent[0]._type == sw::RawInputEventType::KeyDown );
    SW_EXPECT_TRUE( listEvent[1]._type == sw::RawInputEventType::KeyUp );

    listEvent.clear();
    script.emitFrame( 1, listEvent ); // 되돌아간 프레임
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listEvent.size() ) );
    SW_EXPECT_TRUE( listEvent[0]._payload._keyData._key == sw::Key::B );
    SW_EXPECT_FALSE( script.isFinished( 2 ) );
    SW_EXPECT_TRUE( script.isFinished( 3 ) );
}

/**
 * @brief [VirtualInputTest] 마우스 버튼은 그 프레임까지 옮긴 커서 자리에서 눌린다 — (0, 0) 으로 커서를 끌고 가지 않는다
 * @details 버튼 사건이 커서 자리를 싣는데(OS 와 같다) 스크립트가 늘 (0, 0) 을 실으면 누르는 순간 커서가 모서리로 가, 커서 아래를 고르는
 *          조작(RTS 선택 · 경영 게임 배치)을 시나리오로 낼 수 없다.
 */
SW_TEST_CASE( VirtualInputTest, MouseButtonPressesAtTheScriptedPointer )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::VirtualInputScript script;
    script.addMousePosition( 1, 120, 80 );
    SW_ASSERT_TRUE( script.addTap( 2, sw::InputSlot::fromMouseButton( sw::MouseButton::Left ), 1 ) );
    script.addMousePosition( 4, 300, 200 );
    SW_ASSERT_TRUE( script.addSlot( 4, sw::InputSlot::fromMouseButton( sw::MouseButton::Right ), true ) );
    input.attachVirtualInput( &script );

    for ( uint32 frame = 0; frame < 5; ++frame )
    {
        input.beginFrame( 0.016f );
        if ( frame == 2 )
        {
            SW_EXPECT_TRUE( input.wasMouseButtonPressed( sw::MouseButton::Left ) );
            SW_EXPECT_TRUE( input.getMousePosition() == sw::int2( 120, 80 ) );
        }
        if ( frame == 3 )
            SW_EXPECT_TRUE( input.getMousePosition() == sw::int2( 120, 80 ) ); // 뗄 때도 그 자리
        if ( frame == 4 )
        {
            SW_EXPECT_TRUE( input.isMouseButtonDown( sw::MouseButton::Right ) );
            SW_EXPECT_TRUE( input.getMousePosition() == sw::int2( 300, 200 ) );
        }
        input.endFrame();
    }
    input.detachVirtualInput();
    input.shutdown();
}
