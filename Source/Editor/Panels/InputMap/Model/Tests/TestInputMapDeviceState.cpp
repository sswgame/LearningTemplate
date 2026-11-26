/**
 * @file TestInputMapDeviceState.cpp
 * @brief Input Map Editor 의 장치 모니터 값 묶음(`InputMapViewModel::gatherDeviceState`)이 입력 매니저의 상태를 그대로 옮기는지 봅니다 — 패널(View)은 이 값만 읽는다.
 * @details 지역 입력 매니저를 쓴다(에디터 · 창 없음). 패드 연결은 기계마다 달라 보지 않는다.
 */
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Panels/InputMap/Model/InputMapViewModel.h"

#include "Engine/Input/InputManager.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [InputMapDeviceStateTest] 눌린 키 · 마우스 위치 · 단추가 값 묶음에 담기고, 다시 모으면 앞 프레임 값이 남지 않는다
 */
SW_TEST_CASE( InputMapDeviceStateTest, GathersHeldKeysAndMouse )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );

    SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeKeyDown( Key::A ) ) );
    SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeMouseMove( 10, 20 ) ) );
    input.beginFrame( RealSeconds{ 0.016f } );

    InputMapDeviceState state;
    InputMapViewModel::gatherDeviceState( input, state );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listHeldKey.size() );
    SW_EXPECT_TRUE( state._listHeldKey[0] == Key::A );
    SW_EXPECT_EQUAL( 10, state._mousePosition._x );
    SW_EXPECT_EQUAL( 20, state._mousePosition._y );
    SW_EXPECT_FALSE( state._bMouseLeftDown );
    SW_EXPECT_TRUE( state._pActiveDeviceName != nullptr && state._pActiveDeviceName[0] != '\0' );
    input.endFrame();

    SW_EXPECT_TRUE( input.postRawEvent( RawInputEvent::makeKeyUp( Key::A ) ) );
    input.beginFrame( RealSeconds{ 0.016f } );
    InputMapViewModel::gatherDeviceState( input, state );
    SW_EXPECT_TRUE( state._listHeldKey.empty() ); // 재사용 버퍼 — 앞 프레임의 키가 남지 않는다
    input.endFrame();
    input.shutdown();
}
