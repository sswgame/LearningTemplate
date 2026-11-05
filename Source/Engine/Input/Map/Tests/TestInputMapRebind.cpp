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

// 지우기는 세 목록(현재 · 기본 · 상태)에서 함께 지운다. 기본 목록이 어긋나면 되돌리기가 다른 바인딩을 살린다.
SW_TEST_CASE( InputMapRebindTest, RemoveBindingKeepsTheListsAligned )
{
    InputMap            map;
    const hashed_string action( "Jump" );
    map.bind( action, Key::A );
    map.bind( action, Key::B );
    map.bind( action, Key::C );

    SW_EXPECT_FALSE( map.removeBinding( action, 3 ) );
    SW_EXPECT_FALSE( map.removeBinding( hashed_string( "Nobody" ), 0 ) );
    SW_REQUIRE_TRUE( map.removeBinding( action, 1 ) );

    SW_REQUIRE_EQUAL( uint32{ 2 }, map.getBindingCount( action ) );
    SW_EXPECT_TRUE( Util::hasChord( map, "Jump", 1, Key::C, 0 ) );
    const ActionBinding* pDefault = map.getDefaultBinding( action, 1 );
    SW_REQUIRE_NOT_NULL( pDefault );
    SW_EXPECT_TRUE( pDefault->_arrSlot[0] == InputSlot::fromKey( Key::C ) );
}

// 지운 액션의 핸들은 무효이고, 같은 이름을 다시 만들면 비어 있는 새 액션이다. 남은 액션의 순서(저장 순서)는 그대로다.
SW_TEST_CASE( InputMapRebindTest, RemoveActionInvalidatesHandlesAndKeepsOrder )
{
    InputMap map;
    map.bind( hashed_string( "A" ), Key::A );
    map.bind( hashed_string( "B" ), Key::B );
    map.bind( hashed_string( "C" ), Key::C );
    const ActionHandle handle = map.getActionHandle( hashed_string( "B" ) );

    SW_EXPECT_FALSE( map.removeAction( hashed_string( "Nobody" ) ) );
    SW_REQUIRE_TRUE( map.removeAction( hashed_string( "B" ) ) );

    SW_EXPECT_FALSE( map.hasAction( hashed_string( "B" ) ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, map.getActionNames().size() );
    SW_EXPECT_TRUE( map.getActionNames()[0] == hashed_string( "A" ) );
    SW_EXPECT_TRUE( map.getActionNames()[1] == hashed_string( "C" ) );
    SW_EXPECT_FALSE( map.isActionDown( handle ) );

    map.createAction( hashed_string( "B" ) );
    SW_EXPECT_EQUAL( uint32{ 0 }, map.getBindingCount( hashed_string( "B" ) ) );
    SW_EXPECT_TRUE( map.getActionHandle( hashed_string( "B" ) )._generation != handle._generation );
}

// 레이어 옮기기는 이미 있는 레이어로만 간다. 기본 바인딩도 함께 옮겨 되돌려도 다른 레이어로 돌아가지 않는다.
SW_TEST_CASE( InputMapRebindTest, SetBindingLayerNeedsAnExistingLayer )
{
    InputMap            map;
    const hashed_string action( "Jump" );
    map.bind( action, Key::A );
    map.registerLayer( hashed_string( "Menu" ) );

    SW_EXPECT_FALSE( map.setBindingLayer( action, 0, hashed_string( "Nowhere" ) ) );
    SW_EXPECT_FALSE( map.setBindingLayer( action, 1, hashed_string( "Menu" ) ) );
    SW_REQUIRE_TRUE( map.setBindingLayer( action, 0, hashed_string( "Menu" ) ) );
    SW_EXPECT_TRUE( map.findBindingLayer( action, 0 ) == hashed_string( "Menu" ) );
    SW_EXPECT_TRUE( map.resetActionToDefault( action ) );
    SW_EXPECT_TRUE( map.findBindingLayer( action, 0 ) == hashed_string( "Menu" ) );
}
