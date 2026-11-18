/**
 * @file TestEditorAutosaveRestoreRequest.cpp
 * @brief 복원 요청(`EditorAutosaveCommands::requestRestore`)이 붙잡는 사본 id 로 게임 단계가 후보를 다시 찾는 규칙(`findRecoverableIndex`)을 봅니다.
 * @details 요청과 실행 사이에 다른 줄을 지우면 번호가 당겨진다 — id 로 찾아야 요청한 줄을 연다. 에디터 상태 없이 후보 목록만 쓴다.
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Scene/EditorAutosaveCommands.h"

/**
 * @brief [EditorAutosaveRestoreRequestTest] 앞 줄을 지워 번호가 당겨져도 사본 id 로 같은 후보를 찾고, 지운 후보는 찾지 않는다
 */
SW_TEST_CASE( EditorAutosaveRestoreRequestTest, FindsTheRequestedCopyAfterRowsShift )
{
    using sw::editor::EditorAutosaveCommands;
    using sw::editor::EditorAutosaveEntry;

    sw::vector<EditorAutosaveEntry> listEntry( 3 );
    listEntry[0]._snapshotID = "autosave/maps_a.scene.xml/1.scene.xml";
    listEntry[1]._snapshotID = "autosave/maps_b.scene.xml/2.scene.xml";
    listEntry[2]._snapshotID = "./autosave/maps_c.scene.xml/1.scene.xml"; // 견줄 때 정규화한다

    sw::uint32 index = 0;
    SW_REQUIRE_TRUE( EditorAutosaveCommands::findRecoverableIndex( listEntry, "autosave/maps_c.scene.xml/1.scene.xml", index ) );
    SW_EXPECT_EQUAL( sw::uint32{ 2 }, index );

    listEntry.erase( listEntry.begin() ); // 요청 뒤 실행 전에 앞 줄을 지웠다
    SW_REQUIRE_TRUE( EditorAutosaveCommands::findRecoverableIndex( listEntry, "autosave/maps_c.scene.xml/1.scene.xml", index ) );
    SW_EXPECT_EQUAL( sw::uint32{ 1 }, index );

    SW_EXPECT_FALSE( EditorAutosaveCommands::findRecoverableIndex( listEntry, "autosave/maps_a.scene.xml/1.scene.xml", index ) );
}
