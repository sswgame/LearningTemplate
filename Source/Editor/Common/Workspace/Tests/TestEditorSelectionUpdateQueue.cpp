/**
 * @file TestEditorSelectionUpdateQueue.cpp
 * @brief 선택이 되돌리기 기록 밖인지 봅니다 — 선택을 바꿔도 스택에 단계가 생기지 않고, 되돌리기 알림(`CommandStack::ObjectEditEvent`)은 선택을 바로 고치지 않고
 *        고칠 일만 남겨 UI 가 입힐 때(`EditorSelectionUpdateQueue::applyTo`) 선택이 바뀐다.
 * @details 미리보기 씬에 묶은 선택으로 본다(서비스 없음 — `TestEditorSelectionBatch` 와 같은 틀).
 */
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorSelectionUpdateQueue.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

/**
 * @brief [EditorSelectionUpdateQueueTest] 선택을 바꿔도 되돌리기 스택에는 아무것도 오르지 않는다 — Ctrl+Z 가 클릭을 되감지 않는다
 */
SW_TEST_CASE( EditorSelectionUpdateQueueTest, SelectionIsOutsideUndoHistory )
{
    sw::Scene        scene( "SelectionHistoryProbe" );
    sw::CommandStack stack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };
    sw::editor::EditorSelection selection;
    selection.bindPreviewScope( scene, stack );

    sw::GameObject* pFirst  = scene.getObjectManager()->createGameObject( sw::hashed_string( "First" ) );
    sw::GameObject* pSecond = scene.getObjectManager()->createGameObject( sw::hashed_string( "Second" ) );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );

    selection.selectObject( pFirst, sw::editor::SelectionMode::Replace );
    selection.selectObject( pSecond, sw::editor::SelectionMode::Add );
    selection.selectObject( pFirst, sw::editor::SelectionMode::Remove );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == pSecond );
    SW_EXPECT_EQUAL( size_t{ 0 }, stack.getCommandCount() );
    SW_EXPECT_FALSE( stack.canUndo() );
}

/**
 * @brief [EditorSelectionUpdateQueueTest] 되돌리기 알림은 선택을 바로 고치지 않는다 — 줄에 남은 고칠 일을 UI 가 입힐 때 지운 오브젝트가 빠지고 되살린 오브젝트만 골라진다
 * @details 결함이면 기록 · 되돌리기 실행(게임 단계)이 UI 상태인 선택을 바로 쓴다. `Modified` 알림은 선택에 할 일이 없어 줄에 들지 않는다.
 */
SW_TEST_CASE( EditorSelectionUpdateQueueTest, UndoNotificationsWaitForTheUI )
{
    sw::Scene        scene( "SelectionUpdateProbe" );
    sw::CommandStack stack{
        sw::UndoBufferLimit{ 128u, 1ull << 20 }
    };
    sw::editor::EditorSelection selection;
    selection.bindPreviewScope( scene, stack );

    sw::GameObject* pKept     = scene.getObjectManager()->createGameObject( sw::hashed_string( "Kept" ) );
    sw::GameObject* pRemoved  = scene.getObjectManager()->createGameObject( sw::hashed_string( "Removed" ) );
    sw::GameObject* pRestored = scene.getObjectManager()->createGameObject( sw::hashed_string( "Restored" ) );
    SW_REQUIRE_NOT_NULL( pKept );
    SW_REQUIRE_NOT_NULL( pRemoved );
    SW_REQUIRE_NOT_NULL( pRestored );
    selection.selectObjects( { pKept, pRemoved }, sw::editor::SelectionMode::Replace );

    sw::editor::EditorSelectionUpdateQueue updates;
    updates.push( sw::CommandStack::ObjectEditEvent{ pKept->getObjectID(), sw::ObjectEditKind::Modified } );
    SW_EXPECT_TRUE( updates.isEmpty() );
    updates.push( sw::CommandStack::ObjectEditEvent{ pRemoved->getObjectID(), sw::ObjectEditKind::Destroyed } );
    SW_EXPECT_FALSE( updates.isEmpty() );
    SW_EXPECT_TRUE( selection.hasObject( pRemoved ) ); // 아직 UI 가 입히지 않았다
    SW_EXPECT_EQUAL( size_t{ 2 }, selection.getSelectedObjectCount() );

    updates.applyTo( selection );
    SW_EXPECT_TRUE( updates.isEmpty() );
    SW_EXPECT_FALSE( selection.hasObject( pRemoved ) );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == pKept );

    updates.push( sw::CommandStack::ObjectEditEvent{ pRestored->getObjectID(), sw::ObjectEditKind::Recreated } );
    SW_EXPECT_FALSE( selection.hasObject( pRestored ) );
    updates.applyTo( selection );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == pRestored );
    SW_EXPECT_EQUAL( size_t{ 1 }, selection.getSelectedObjectCount() );
    SW_EXPECT_EQUAL( size_t{ 0 }, stack.getCommandCount() ); // 선택을 고친 것도 기록이 아니다
}
