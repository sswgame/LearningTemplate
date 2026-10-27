#include "pch.h"

#include "Editor/Common/Commands/EditorCommandTarget.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorCommandTargetTest] 맥락 메뉴가 열리는 순간의 대상 — 선택 밖 항목은 그것만, 선택 안 항목은 선택 전체, 빈 곳은 선택 그대로(EditorUX 10-3 R2 · R3)
 * @details 패널마다 달랐던 규칙(Hierarchy 는 누른 한 행에만, Console 만 선택을 바꿈)을 한 함수로 둔다. 어댑터(`EditorInputGUI::drawContextMenuFor`)가 이것만 부른다.
 */
SW_TEST_CASE( EditorCommandTargetTest, MenuTargetFollowsSelectionRules )
{
    const vector<uint64> listSelected{ 3, 5 };
    bool                 bSelectOnly = false;

    const EditorCommandTarget outside = EditorContextMenuRules::makeMenuTarget( EditorCommandTargetKind::GameObject, 9, listSelected, bSelectOnly );
    SW_EXPECT_TRUE( bSelectOnly );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( outside._listSelectedID.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 9 ), outside._listSelectedID[0] );
    SW_EXPECT_TRUE( outside._kind == EditorCommandTargetKind::GameObject );

    const EditorCommandTarget inside = EditorContextMenuRules::makeMenuTarget( EditorCommandTargetKind::GameObject, 5, listSelected, bSelectOnly );
    SW_EXPECT_FALSE( bSelectOnly );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( inside._listSelectedID.size() ) );
    SW_EXPECT_EQUAL( static_cast<uint64>( 5 ), inside._hoveredID );
    SW_EXPECT_FALSE( inside.hasSingleSelection() );

    const EditorCommandTarget emptySpace = EditorContextMenuRules::makeMenuTarget( EditorCommandTargetKind::GameObject, 0, listSelected, bSelectOnly );
    SW_EXPECT_FALSE( bSelectOnly );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( emptySpace._listSelectedID.size() ) );

    const EditorCommandTarget emptyNoSelection = EditorContextMenuRules::makeMenuTarget( EditorCommandTargetKind::Asset, 0, {}, bSelectOnly );
    SW_EXPECT_FALSE( bSelectOnly );
    SW_EXPECT_TRUE( emptyNoSelection._listSelectedID.empty() );
}
