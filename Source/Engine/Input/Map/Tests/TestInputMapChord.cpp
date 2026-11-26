// InputMapChordTest — 같은 키의 맨 바인딩과 조합 바인딩이 함께 있을 때 지금 눌린 수정 키에 꼭 맞는 쪽만 발화한다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Input/Devices/KeyboardDevice.h"
#include "Engine/Input/InputKeyboardFocus.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"

using namespace sw;

namespace
{
    /** @brief 창 없이 장치 상태만 쓰는 입력 관리자와 Z · Shift+Z 두 액션이 든 맵입니다(에디터의 배치 회전 · 되돌리기와 같은 모양). */
    struct ChordScene
    {
        InputManager             _input;
        InputMap                 _map;
        const hashed_string      _forward{ "RotateForward" };
        const hashed_string      _back{ "RotateBack" };
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        ChordScene()
        {
            SW_EXPECT_TRUE( _input.initialize() );
            _map.setInputManager( &_input );
            _map.bind( _forward, Key::Z );
            _map.bindShortcut( _back, Key::Z, ModifierKey::Shift );
        }
        ~ChordScene() { _input.shutdown(); }

        /** @brief 한 프레임을 돈다 — 장치의 지난 프레임 상태를 넘기고(눌림 가장자리) 맵을 갱신한다. */
        void runFrame()
        {
            _input.beginFrame( RealSeconds{ kFrameSeconds } );
            _map.update( kFrameSeconds );
            _input.endFrame();
        }
        /** @brief 한 프레임 안에 키를 눌렀다 뗀다(긴 프레임의 짧은 누름). 수정 키는 @p bShift 대로 누른 채다. */
        void tapWithinFrame( Key key, bool bShift )
        {
            _input.beginFrame( RealSeconds{ kFrameSeconds } );
            _input.getKeyboard()->setKeyDown( Key::LeftShift, bShift );
            _input.getKeyboard()->setKeyDown( key, true );
            _input.getKeyboard()->setKeyDown( key, false );
            _map.update( kFrameSeconds );
            _input.endFrame();
        }
        void press( Key key, bool bShift )
        {
            _input.getKeyboard()->setKeyDown( Key::LeftShift, bShift );
            _input.getKeyboard()->setKeyDown( key, true );
            runFrame();
        }
        void releaseAll()
        {
            _input.getKeyboard()->setKeyDown( Key::LeftShift, false );
            _input.getKeyboard()->setKeyDown( Key::Z, false );
            _input.getKeyboard()->setKeyDown( Key::W, false );
            runFrame();
        }
    };
} // namespace

/**
 * @brief [InputMapChordTest] Shift+Z 는 조합 바인딩만, 맨 Z 는 맨 바인딩만 발화한다.
 * @details 결함: 맨 Z 가 Shift 를 막지 않아 Shift+Z 에 두 액션이 함께 발화했고, 배치 회전은 +90 · −90 이 서로 지워 돌지 않았다(editor.placement 시나리오).
 */
SW_TEST_CASE( InputMapChordTest, MatchingChordBlocksTheBareKey )
{
    ChordScene scene;
    scene.press( Key::Z, true );
    SW_EXPECT_TRUE( scene._map.wasActionPressed( scene._back ) );
    SW_EXPECT_FALSE( scene._map.wasActionPressed( scene._forward ) );
    scene.releaseAll();

    scene.press( Key::Z, false );
    SW_EXPECT_TRUE( scene._map.wasActionPressed( scene._forward ) );
    SW_EXPECT_FALSE( scene._map.wasActionPressed( scene._back ) );
}

/** @brief [InputMapChordTest] 한 프레임 안에 눌렀다 뗀 단축키 · 조합 키도 한 번 눌린 것이다(결함: 끝 상태만 봐서 긴 프레임의 짧은 누름이 사라졌다 — 단일 키는 받았다) */
SW_TEST_CASE( InputMapChordTest, TapWithinOneFrameTriggersChords )
{
    ChordScene          scene;
    const hashed_string chord{ "ChordAction" };
    scene._map.bindChord( chord, Key::LeftShift, Key::W );

    scene.tapWithinFrame( Key::Z, true );
    SW_EXPECT_TRUE( scene._map.wasActionPressed( scene._back ) );
    SW_EXPECT_FALSE( scene._map.wasActionPressed( scene._forward ) );
    scene.runFrame();
    SW_EXPECT_TRUE( scene._map.wasActionReleased( scene._back ) ); // 다음 프레임에 떼인다
    SW_EXPECT_FALSE( scene._map.isActionDown( scene._back ) );

    scene.tapWithinFrame( Key::W, true );
    SW_EXPECT_TRUE( scene._map.wasActionPressed( chord ) );
}

/** @brief [InputMapChordTest] 같은 키의 조합이 없으면 Shift 는 맨 키를 막지 않는다(Shift 달리기 + WASD) */
SW_TEST_CASE( InputMapChordTest, ShiftAloneDoesNotBlockOtherKeys )
{
    ChordScene          scene;
    const hashed_string move{ "MoveForward" };
    scene._map.bind( move, Key::W );
    scene.press( Key::W, true );
    SW_EXPECT_TRUE( scene._map.wasActionPressed( move ) );
}

/**
 * @brief [InputMapChordTest] 액션 상태 덤프는 맵의 액션을 모두 내고, 칸마다 값은 그 액션을 따로 물은 답(`isActionDown` · `getActionPhase`)과 같다
 * @details 디버그 HUD · 에디터의 입력 뷰어가 프레임마다 한 번에 읽는 입구다. 레이어를 적지 않은 바인딩은 기본 레이어로 나온다.
 */
SW_TEST_CASE( InputMapChordTest, DebugActionStatesMatchThePerActionQueries )
{
    ChordScene               scene;
    vector<DebugActionState> listState;
    scene.press( Key::Z, false );
    SW_REQUIRE_TRUE( scene._map.isActionDown( scene._forward ) );

    scene._map.getDebugActionStates( listState );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listState.size() );
    uint32 downCount = 0;
    for ( const DebugActionState& state : listState )
    {
        SW_EXPECT_TRUE( state._action == scene._forward || state._action == scene._back );
        SW_EXPECT_TRUE( state._layer == scene._map.getDefaultLayerName() );
        SW_EXPECT_TRUE( ( state._bDown != 0 ) == scene._map.isActionDown( state._action ) );
        SW_EXPECT_TRUE( state._phase == scene._map.getActionPhase( state._action ) );
        downCount += state._bDown;
    }
    SW_EXPECT_EQUAL( 1u, downCount ); // 맨 Z 만 눌렸다

    scene.releaseAll();
    scene._map.getDebugActionStates( listState ); // 다시 부르면 앞 내용을 비우고 채운다
    SW_REQUIRE_EQUAL( size_t{ 2 }, listState.size() );
    for ( const DebugActionState& state : listState )
    {
        SW_EXPECT_TRUE( state._bDown == 0 );
    }
}

/**
 * @brief [InputMapChordTest] 셸 디버그 키 맵(포커스 무시 + `setToolShortcutReader`)은 에디터 패널이 키보드를 쥔 동안에도 Ctrl 단축키를 받고, 맨 키와 Shift 조합은 가린다
 * @details 결함: 편집 모드에서 에디터가 도구 포커스를 쥐면 셸 맵이 키보드를 통째로 가려 Ctrl+F8(셰이더 재컴파일)이 발동하지 않았다(editor.shaderreload 시나리오).
 */
SW_TEST_CASE( InputMapChordTest, ShellShortcutReadsThroughToolFocus )
{
    InputManager input;
    SW_REQUIRE_TRUE( input.initialize() );
    InputMap            shell;
    InputMap            plain; // 깃발이 없는 포커스 무시 맵은 그대로 가린다
    const hashed_string reload{ "ReloadShaders" };
    const hashed_string console{ "DevConsoleToggle" };
    const hashed_string shiftChord{ "ShiftChord" };
    for ( InputMap* pMap : { &shell, &plain } )
    {
        pMap->setInputManager( &input );
        pMap->setKeyboardFocusIgnored( true );
        pMap->bindShortcut( reload, Key::F8, ModifierKey::Ctrl );
        pMap->bind( console, Key::Grave );
        pMap->bindShortcut( shiftChord, Key::F9, ModifierKey::Shift );
    }
    shell.setToolShortcutReader( true );
    Subscription toolFocus = input.claimKeyboardFocus( InputKeyboardFocus::Tool );

    // 수정 키는 누른 채로 맵을 갱신하고 그 뒤에 뗀다(단축키는 수정 키의 지금 상태를 본다). 방아쇠 키는 한 프레임 안에 눌렀다 뗀다.
    const auto tap = [&]( Key modifier, Key key )
    {
        input.beginFrame( RealSeconds{ ChordScene::kFrameSeconds } );
        if ( modifier != Key::Unknown )
            input.getKeyboard()->setKeyDown( modifier, true );
        input.getKeyboard()->setKeyDown( key, true );
        input.getKeyboard()->setKeyDown( key, false );
        shell.update( ChordScene::kFrameSeconds );
        plain.update( ChordScene::kFrameSeconds );
        input.endFrame();
        if ( modifier != Key::Unknown )
            input.getKeyboard()->setKeyDown( modifier, false );
    };

    tap( Key::LeftControl, Key::F8 );
    SW_EXPECT_TRUE( shell.wasActionPressed( reload ) );
    SW_EXPECT_FALSE( plain.wasActionPressed( reload ) );

    tap( Key::LeftShift, Key::F9 );
    SW_EXPECT_FALSE( shell.wasActionPressed( shiftChord ) );

    tap( Key::Unknown, Key::Grave );
    SW_EXPECT_FALSE( shell.wasActionPressed( console ) );

    toolFocus.reset();
    input.shutdown();
}
