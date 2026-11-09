// InputMapChordTest — 같은 키의 맨 바인딩과 조합 바인딩이 함께 있을 때 지금 눌린 수정 키에 꼭 맞는 쪽만 발화한다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Devices/KeyboardDevice.h"
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
            _input.beginFrame( kFrameSeconds );
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

/** @brief [InputMapChordTest] 같은 키의 조합이 없으면 Shift 는 맨 키를 막지 않는다(Shift 달리기 + WASD) */
SW_TEST_CASE( InputMapChordTest, ShiftAloneDoesNotBlockOtherKeys )
{
    ChordScene          scene;
    const hashed_string move{ "MoveForward" };
    scene._map.bind( move, Key::W );
    scene.press( Key::W, true );
    SW_EXPECT_TRUE( scene._map.wasActionPressed( move ) );
}
