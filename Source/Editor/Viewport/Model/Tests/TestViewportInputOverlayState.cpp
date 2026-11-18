// 입력 오버레이 값 모으기(`ViewportInputOverlayState::gather`) — 패드 버튼이 오버레이 버튼 비트로, 패드가 없으면 가운데 · 0 · 뗌, 액션 목록은 요청할 때만.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Viewport/ViewportInputOverlay.h"

#include "Engine/Input/Devices/GamepadDevice.h"
#include "Engine/Input/InputManager.h"

SW_TEST_CASE( ViewportInputOverlayStateTest, PadButtonsBecomeOverlayButtonBits )
{
    sw::InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    sw::GamepadDevice* pPad = input.getGamepad( 0 );
    if ( pPad == nullptr )
    {
        input.shutdown();
        SW_TEST_SKIP( "no gamepad device on this platform" );
    }
    pPad->setVirtualSession( true );
    pPad->setVirtualConnected( true );
    pPad->setButtonDown( sw::GamepadButton::B, true );
    pPad->setButtonDown( sw::GamepadButton::Y, true );

    sw::editor::ViewportInputOverlayState state;
    state._listActiveAction.push_back( "stale" );
    sw::editor::ViewportInputOverlayState::gather( input, false, state );

    SW_EXPECT_FALSE( state.isFaceButtonDown( sw::editor::ViewportOverlayFaceButton::A ) );
    SW_EXPECT_TRUE( state.isFaceButtonDown( sw::editor::ViewportOverlayFaceButton::B ) );
    SW_EXPECT_FALSE( state.isFaceButtonDown( sw::editor::ViewportOverlayFaceButton::X ) );
    SW_EXPECT_TRUE( state.isFaceButtonDown( sw::editor::ViewportOverlayFaceButton::Y ) );
    SW_EXPECT_TRUE( state._listActiveAction.empty() ); // 액션을 묻지 않으면 지난 목록도 비운다

    pPad->setButtonDown( sw::GamepadButton::B, false );
    pPad->setButtonDown( sw::GamepadButton::Y, false );
    pPad->setVirtualConnected( false );
    sw::editor::ViewportInputOverlayState::gather( input, true, state );
    SW_EXPECT_EQUAL( 0u, static_cast<sw::uint32>( state._faceButtonDownMask ) );
    SW_EXPECT_EQUAL( 0.0f, state._leftTrigger );
    SW_EXPECT_EQUAL( 0.0f, state._leftStick._x );
    input.shutdown();
}
