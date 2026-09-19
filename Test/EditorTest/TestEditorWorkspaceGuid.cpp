#include "pch.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "TestFramework/TestFramework.h"

// EditorWorkspaceGuid — 오브젝트 ID ↔ GUID 두 표가 **서로의 역**으로 남는지.
//
// GUID 는 되돌리기가 오브젝트를 다시 찾는 열쇠다(`EditorTransaction::findTargetGameObject`).
// 두 표가 어긋나면 되돌리기가 엉뚱한 오브젝트에 적용되는데, 그 자리에서 터지지 않는다.

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorWorkspaceGuidTest] 같은 GUID 를 다른 오브젝트에 다시 붙이면 옛 주인이 놓아 준다
 * @details `setGuid` 는 "이 오브젝트가 들고 있던 옛 GUID" 만 끊고 "이 GUID 를 들고 있던 옛
 *          오브젝트" 는 그대로 뒀다. 그러면 옛 오브젝트가 계속 그 GUID 를 가졌다고 답하는데
 *          (`getGuid`) 정작 GUID 로 찾으면 새 오브젝트가 나온다. 되돌리기로 오브젝트를 되살릴 때
 *          (`EditorTransaction` 의 recreate) 같은 GUID 를 새 오브젝트에 다시 붙이므로 실제로 지나간다.
 */
SW_TEST_CASE( EditorWorkspaceGuidTest, ReassigningAGuidReleasesItsPreviousOwner )
{
    SelectionManager selectionManager;
    EditorWorkspace  workspace{ &selectionManager };

    constexpr uint64 kFirstObjectId  = 11;
    constexpr uint64 kSecondObjectId = 22;

    const Uuid guid = workspace.getOrAssignGuid( kFirstObjectId );
    SW_ASSERT_TRUE( guid.isNull() == false );
    SW_ASSERT_EQUAL( kFirstObjectId, workspace.findObjectIdByGuid( guid ) );

    // 같은 GUID 를 다른 오브젝트에 붙인다 — 되돌리기가 오브젝트를 되살릴 때의 모양이다.
    workspace.setGuid( kSecondObjectId, guid );

    SW_EXPECT_EQUAL( kSecondObjectId, workspace.findObjectIdByGuid( guid ) );
    SW_EXPECT_TRUE_MSG( workspace.getGuid( kFirstObjectId ).isNull(),
                        "옛 주인이 아직 그 GUID 를 가졌다고 답합니다 — 두 표가 어긋났습니다" );
    SW_EXPECT_TRUE( workspace.getGuid( kSecondObjectId ) == guid );
}

/**
 * @brief [EditorWorkspaceGuidTest] 한 오브젝트에 새 GUID 를 주면 옛 GUID 는 아무도 안 가진다
 */
SW_TEST_CASE( EditorWorkspaceGuidTest, ReassigningAnObjectReleasesItsPreviousGuid )
{
    SelectionManager selectionManager;
    EditorWorkspace  workspace{ &selectionManager };

    constexpr uint64 kObjectId = 7;

    const Uuid oldGuid = workspace.getOrAssignGuid( kObjectId );
    SW_ASSERT_TRUE( oldGuid.isNull() == false );

    const Uuid newGuid = Uuid::generate();
    SW_ASSERT_TRUE( newGuid != oldGuid );
    workspace.setGuid( kObjectId, newGuid );

    SW_EXPECT_EQUAL( kObjectId, workspace.findObjectIdByGuid( newGuid ) );
    SW_EXPECT_EQUAL( uint64( 0 ), workspace.findObjectIdByGuid( oldGuid ) );
    SW_EXPECT_TRUE( workspace.getGuid( kObjectId ) == newGuid );
}
