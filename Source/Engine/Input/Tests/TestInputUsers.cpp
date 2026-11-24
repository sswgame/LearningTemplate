// InputUsersTest — 로컬 멀티의 입력 사용자: 패드 짝짓기 규칙(사용자 1 부터, 남으면 사용자 0, 끊겨도 남는 짝)과 사용자마다의 맵이 제 장치만 읽는 것(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputUserDevices.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Virtual/VirtualInputScript.h"

using namespace sw;

/** @brief [InputUsersTest] 패드는 사용자 1 부터 받고 남으면 사용자 0 이 받는다. 지은 짝은 패드가 끊기거나 다른 패드가 먼저 나타나도 바뀌지 않는다 */
SW_TEST_CASE( InputUsersTest, PadsPairWithUsersOnce )
{
    vector<InputUserDevices> listUser( 2 );
    listUser[0]._bKeyboardMouse = SW_TRUE;
    InputUserDevices::pairDevices( listUser, {} );
    SW_EXPECT_FALSE( listUser[0]._padIndex.has_value() || listUser[1]._padIndex.has_value() );

    InputUserDevices::pairDevices( listUser, { 1u } );
    SW_EXPECT_TRUE( listUser[1]._padIndex.has_value() && *listUser[1]._padIndex == 1u );
    SW_EXPECT_FALSE( listUser[0]._padIndex.has_value() );

    InputUserDevices::pairDevices( listUser, { 0u } ); // 1 번이 끊기고 0 번이 나타났다 — 사용자 1 의 짝은 남고 사용자 0 이 받는다
    SW_EXPECT_EQUAL( 1u, *listUser[1]._padIndex );
    SW_EXPECT_TRUE( listUser[0]._padIndex.has_value() && *listUser[0]._padIndex == 0u );

    InputUserDevices::pairDevices( listUser, { 0u, 1u, 2u } ); // 모두 짝이 있다 — 남는 패드는 아무도 쓰지 않는다
    SW_EXPECT_EQUAL( 0u, *listUser[0]._padIndex );
    SW_EXPECT_EQUAL( 1u, *listUser[1]._padIndex );
}

/** @brief [InputUsersTest] 로컬 멀티를 열면 키보드는 사용자 0 의 맵만, 패드는 짝지은 사용자의 맵만 읽는다. 닫으면 통합 맵이 다시 모두 읽고, 열린 채로 · 범위 밖은 거절한다 */
SW_TEST_CASE( InputUsersTest, EachUserReadsOnlyTheirDevices )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "users.input.xml" ),
                                              "<InputMap defaultLayer=\"Game\"><layers><layer name=\"Game\"/></layers><action name=\"Jump\" layer=\"Game\"><bind source=\"Key\" code=\"Space\"/><bind source=\"Gamepad\" code=\"A\"/></action></InputMap>" ) );
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    SW_REQUIRE_TRUE( input.getInputMap().loadBundle( vector<AssetID>{ test::ScopedScratchDomain::makeID( "users.input.xml" ) } ) );
    SW_EXPECT_NULL( input.findUserMap( 1 ) );
    SW_EXPECT_LOG( "InputManager.LocalUsersRefused" );
    SW_EXPECT_FALSE( input.openLocalUsers( 1 ) );
    SW_REQUIRE_TRUE( input.openLocalUsers( 2 ) );
    SW_EXPECT_FALSE( input.openLocalUsers( 2 ) );
    SW_EXPECT_EQUAL( 2u, input.getLocalUserCount() );
    InputMap* const pFirst  = input.findUserMap( 0 );
    InputMap* const pSecond = input.findUserMap( 1 );
    SW_REQUIRE_TRUE( pFirst == &input.getInputMap() && pSecond != nullptr );
    const hashed_string jump( "Jump" );

    // 배타 가상 입력이라 이 컴퓨터에 꽂힌 패드가 끼지 않는다.
    VirtualInputScript script;
    script.addEvent( 0, RawInputEvent::makeKeyDown( Key::Space ) );
    script.addEvent( 1, RawInputEvent::makeKeyUp( Key::Space ) );
    script.addGamepadConnection( 1, 1 );
    script.addEvent( 2, RawInputEvent::makeGamepadButtonDown( GamepadButton::A, 1 ) );
    Subscription attachment = input.attachVirtualInput( script, VirtualInputMode::Exclusive );

    input.beginFrame( 0.016f );
    SW_EXPECT_TRUE( pFirst->isActionDown( jump ) );
    SW_EXPECT_FALSE( pSecond->isActionDown( jump ) );
    input.endFrame();

    input.beginFrame( 0.016f );
    input.endFrame();
    input.beginFrame( 0.016f );
    SW_EXPECT_FALSE( pFirst->isActionDown( jump ) );
    SW_EXPECT_TRUE( pSecond->isActionDown( jump ) ); // 바인딩의 패드 0 은 "내 패드" — 짝지은 1 번이다
    input.endFrame();

    attachment.reset();
    input.closeLocalUsers();
    SW_EXPECT_EQUAL( 1u, input.getLocalUserCount() );
    SW_EXPECT_NULL( input.findUserMap( 1 ) );
    input.shutdown();
}
