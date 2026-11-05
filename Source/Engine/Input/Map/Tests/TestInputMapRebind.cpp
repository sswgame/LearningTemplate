#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/RawInputEvent.h"

using namespace sw;

namespace
{
    struct InputMapRebindTestUtil
    {
        static bool hasChord( const InputMap& map, const utf8* pAction, uint32 bindIndex, Key key, uint8 modifierMask )
        {
            const ActionBinding* pBinding = map.getBinding( hashed_string( pAction ), bindIndex );
            return pBinding != nullptr && pBinding->_arrSlot[0] == InputSlot::fromKey( key ) && pBinding->_modifierMask == modifierMask;
        }
    };
    using Util = InputMapRebindTestUtil;
} // namespace

// 결함: 겹침은 상대의 모든 바인딩에서 찾는데 맞바꾸기는 첫 키보드 바인딩만 고쳐, 바꾼 뒤에도 겹쳐 한 번 눌러 둘 다 발화했다.
SW_TEST_CASE( InputMapRebindTest, SwapChangesTheBindingThatConflicts )
{
    InputMap    map;
    const uint8 ctrl = static_cast<uint8>( ModifierKey::Ctrl );
    map.bindShortcut( hashed_string( "A" ), Key::A, ctrl );
    map.bindShortcut( hashed_string( "B" ), Key::B, ctrl );
    map.bindShortcut( hashed_string( "B" ), Key::C, ctrl );

    SW_REQUIRE_TRUE( map.swapKeyShortcut( hashed_string( "A" ), 0, hashed_string( "B" ), Key::C, ctrl ) );

    SW_EXPECT_TRUE( Util::hasChord( map, "A", 0, Key::C, ctrl ) );
    SW_EXPECT_TRUE( Util::hasChord( map, "B", 0, Key::B, ctrl ) ); // 겹치지 않던 바인딩은 그대로다
    SW_EXPECT_TRUE( Util::hasChord( map, "B", 1, Key::A, ctrl ) );
    hashed_string conflict;
    SW_EXPECT_FALSE( map.findSameLayerInputConflict( hashed_string( "A" ), 0, conflict ) );
}

// 상대에게 그 입력이 없으면 맞바꿀 것이 없다. 아무것도 바뀌지 않는다.
SW_TEST_CASE( InputMapRebindTest, SwapRefusesWhenTheOtherDoesNotHoldTheInput )
{
    InputMap    map;
    const uint8 ctrl = static_cast<uint8>( ModifierKey::Ctrl );
    map.bindShortcut( hashed_string( "A" ), Key::A, ctrl );
    map.bindShortcut( hashed_string( "B" ), Key::B, ctrl );

    SW_EXPECT_FALSE( map.swapKeyShortcut( hashed_string( "A" ), 0, hashed_string( "B" ), Key::C, ctrl ) );
    SW_EXPECT_FALSE( map.swapKeyShortcut( hashed_string( "A" ), 0, hashed_string( "Nobody" ), Key::B, ctrl ) );
    SW_EXPECT_FALSE( map.swapKeyShortcut( hashed_string( "Nobody" ), 0, hashed_string( "B" ), Key::B, ctrl ) );
    SW_EXPECT_TRUE( Util::hasChord( map, "A", 0, Key::A, ctrl ) );
    SW_EXPECT_TRUE( Util::hasChord( map, "B", 0, Key::B, ctrl ) );
}

// 결함: 상대를 먼저 고치고 자기 쪽이 실패하면 절반만 바뀌었다 - 어느 쪽이 실패해도 그대로다.
SW_TEST_CASE( InputMapRebindTest, SwapChangesNothingWhenOneSideCannotChange )
{
    SW_EXPECT_LOG( "InputMap.RebindBindingNotKeyboard" );
    InputMap    map;
    const uint8 ctrl = static_cast<uint8>( ModifierKey::Ctrl );
    map.bind( hashed_string( "Pad" ), InputSlot::fromGamepadButton( GamepadButton::RightShoulder, 0 ), ActionTrigger::Pressed );
    map.bindShortcut( hashed_string( "B" ), Key::B, ctrl );

    SW_EXPECT_FALSE( map.swapKeyShortcut( hashed_string( "Pad" ), 0, hashed_string( "B" ), Key::B, ctrl ) );
    SW_EXPECT_TRUE( Util::hasChord( map, "B", 0, Key::B, ctrl ) );
    const ActionBinding* pPad = map.getBinding( hashed_string( "Pad" ), 0 );
    SW_REQUIRE_NOT_NULL( pPad );
    SW_EXPECT_TRUE( pPad->_arrSlot[0]._deviceKind == InputDeviceKind::Gamepad );
}
