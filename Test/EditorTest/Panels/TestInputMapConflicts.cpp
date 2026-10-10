#include "pch.h"

#include "Editor/Panels/InputMapConflicts.h"

#include "Engine/Input/Map/InputMap.h"

#include "TestFramework/TestFramework.h"

// InputMapConflicts — Input Map Editor 의 바인딩 충돌 찾기(ImGui 없음).

/**
 * @brief [InputMapConflictsTest] 같은 레이어의 두 액션이 같은 키를 쓰면 충돌이고, 다른 레이어 · 같은 액션 안의 겹침은 충돌이 아니다
 */
SW_TEST_CASE( InputMapConflictsTest, SameKeySameLayerIsAConflict )
{
    sw::InputMap inputMap;
    inputMap.bind( sw::hashed_string( "Jump" ), sw::Key::Space, sw::ActionTrigger::Pressed, sw::hashed_string( "Gameplay" ) );
    inputMap.bind( sw::hashed_string( "Jump" ), sw::Key::W, sw::ActionTrigger::Pressed, sw::hashed_string( "Gameplay" ) );
    inputMap.bind( sw::hashed_string( "Dash" ), sw::Key::Space, sw::ActionTrigger::Pressed, sw::hashed_string( "Gameplay" ) );
    inputMap.bind( sw::hashed_string( "Confirm" ), sw::Key::Space, sw::ActionTrigger::Pressed, sw::hashed_string( "Title" ) );

    sw::vector<sw::editor::InputMapConflict> listConflict;
    sw::editor::InputMapConflicts::collect( inputMap, listConflict );
    SW_ASSERT_EQUAL( size_t( 1 ), listConflict.size() );
    SW_EXPECT_TRUE( listConflict[0]._actionA == sw::hashed_string( "Jump" ) );
    SW_EXPECT_TRUE( listConflict[0]._actionB == sw::hashed_string( "Dash" ) );
    SW_EXPECT_EQUAL( 0u, listConflict[0]._bindIndexA );
    SW_EXPECT_TRUE( sw::editor::InputMapConflicts::involves( listConflict, sw::hashed_string( "Dash" ) ) );
    SW_EXPECT_FALSE( sw::editor::InputMapConflicts::involves( listConflict, sw::hashed_string( "Confirm" ) ) );

    // 발화 규칙을 바꿔도 충돌은 그대로다 — 키가 같다.
    SW_EXPECT_TRUE( inputMap.setBindingTrigger( sw::hashed_string( "Dash" ), 0, sw::ActionTrigger::DoubleTap ) );
    SW_EXPECT_TRUE( inputMap.getBindingTrigger( sw::hashed_string( "Dash" ), 0 ) == sw::ActionTrigger::DoubleTap );
    SW_EXPECT_FALSE( inputMap.setBindingTrigger( sw::hashed_string( "Dash" ), 5, sw::ActionTrigger::Tap ) );
    SW_ASSERT_TRUE( inputMap.rebindKey( sw::hashed_string( "Dash" ), sw::Key::LeftShift, 0 ) );
    sw::editor::InputMapConflicts::collect( inputMap, listConflict );
    SW_EXPECT_TRUE( listConflict.empty() );
}

/**
 * @brief [InputMapConflictsTest] 조합 키는 조합 전체가 같아야 겹친다 — Ctrl+F7 과 Ctrl+F8 은 수식 키 슬롯이 같아도 충돌이 아니다
 */
SW_TEST_CASE( InputMapConflictsTest, ChordsConflictOnlyWhenTheWholeChordMatches )
{
    sw::InputMap inputMap;
    inputMap.bindChord( sw::hashed_string( "ReloadEditor" ), sw::Key::LeftControl, sw::Key::F7, sw::ActionTrigger::Pressed, sw::hashed_string( "Debug" ) );
    inputMap.bindChord( sw::hashed_string( "ReloadShaders" ), sw::Key::LeftControl, sw::Key::F8, sw::ActionTrigger::Pressed, sw::hashed_string( "Debug" ) );
    inputMap.bind( sw::hashed_string( "Screenshot" ), sw::Key::F8, sw::ActionTrigger::Pressed, sw::hashed_string( "Debug" ) );
    sw::vector<sw::editor::InputMapConflict> listConflict;
    sw::editor::InputMapConflicts::collect( inputMap, listConflict );
    SW_EXPECT_TRUE( listConflict.empty() );

    inputMap.bindChord( sw::hashed_string( "ReloadGame" ), sw::Key::LeftControl, sw::Key::F7, sw::ActionTrigger::Pressed, sw::hashed_string( "Debug" ) );
    sw::editor::InputMapConflicts::collect( inputMap, listConflict );
    SW_ASSERT_EQUAL( size_t( 1 ), listConflict.size() );
    SW_EXPECT_TRUE( listConflict[0]._slot == sw::InputSlot::fromKey( sw::Key::F7 ) );
}
