// PrefabEditorViewModelTest — 프리팹 창의 상태: 문서 루트를 다시 스폰해야 하는지, 중첩 요청의 거절과 실패, 씬이 없을 때의 명령(스폰 · 저장은 시나리오 previewreopen 이 본다 — nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Scene/EditorPrefabIsolation.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Panels/Prefab/Model/PrefabEditorViewModel.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;
using namespace sw::editor;

/** @brief [PrefabEditorViewModelTest] 스폰한 적 없는 문서는 다시 스폰해야 하고, 씬이 없으면 스폰하지 못해 그 요청이 남는다 */
SW_TEST_CASE( PrefabEditorViewModelTest, RebuildWaitsForAScene )
{
    EditorSelection       selection;
    PrefabEditorViewModel model;
    SW_EXPECT_FALSE( PrefabEditorViewModel::getDocumentType().isUnknown() );
    SW_EXPECT_TRUE( model.needsRebuild( "game/a/a.prefab.xml" ) );
    SW_EXPECT_FALSE( model.rebuildRoot( nullptr, selection, "game/a/a.prefab.xml" ) );
    SW_EXPECT_TRUE( model.needsRebuild( "game/a/a.prefab.xml" ) );
    SW_EXPECT_FALSE( model.isIsolating() );
    SW_EXPECT_TRUE( model.getPrefabPath().empty() );
    model.resetForNewScene();
    SW_EXPECT_FALSE( model.isIsolating() );
}

/** @brief [PrefabEditorViewModelTest] 중첩 요청은 거절하면 까닭을 남기고 버리며, 격리 중이 아니면 들어가지 못했다고 적는다 — 요청이 없으면 상태가 그대로다 */
SW_TEST_CASE( PrefabEditorViewModelTest, NestedRequestIsRefusedOrFails )
{
    EditorSelection       selection;
    PrefabEditorViewModel model;
    model.refusePendingNested();
    model.enterPendingNested( nullptr, selection );
    SW_EXPECT_TRUE( model.getStatus().empty() );

    model.requestNested( "game/a/part.prefab.xml" );
    model.refusePendingNested();
    const string refused = model.getStatus();
    SW_EXPECT_FALSE( refused.empty() );
    model.enterPendingNested( nullptr, selection ); // 거절한 요청은 남지 않았다
    SW_EXPECT_EQUAL( refused, model.getStatus() );

    model.requestNested( "game/a/part.prefab.xml" );
    model.enterPendingNested( nullptr, selection );
    SW_EXPECT_FALSE( model.getStatus() == refused );
}

/** @brief [PrefabEditorViewModelTest] 씬이 없으면 나가기 · 고르기는 아무것도 하지 않고, 저장은 실패를 적으며, 모은 값은 비어 있다 */
SW_TEST_CASE( PrefabEditorViewModelTest, WithoutASceneNothingIsGathered )
{
    EditorSelection       selection;
    PrefabEditorViewModel model;
    SW_EXPECT_TRUE( model.exitFrame( nullptr, selection, false ) == PrefabFrameExit::Failed );
    model.selectObject( nullptr, selection, 1 );
    SW_EXPECT_FALSE( model.saveFrame( nullptr ) );
    SW_EXPECT_FALSE( model.getStatus().empty() );

    PrefabEditorViewState state;
    state._listChildRow.emplace_back();
    state._prefabPath = "old";
    model.gather( nullptr, selection, state );
    SW_EXPECT_TRUE( state._listChildRow.empty() );
    SW_EXPECT_TRUE( state._prefabPath.empty() );
    SW_EXPECT_TRUE( state._bIsolating == SW_FALSE );
    SW_EXPECT_TRUE( state._bNested == SW_FALSE );
    SW_EXPECT_EQUAL( model.getStatus(), state._status );
}

/** @brief [PrefabEditorViewModelTest] 격리 중인 프리팹 파일을 옮기면 그 단계의 저장 자리가 새 자리다 — 옛 자리면 저장이 옛 파일을 되살린다 */
SW_TEST_CASE( PrefabEditorViewModelTest, IsolationFollowsAMovedPrefab )
{
    sw::GameObjectManager manager;
    sw::GameObject* const pRoot = manager.createGameObject( sw::hashed_string( "Root" ) );
    SW_REQUIRE_NOT_NULL( pRoot );
    sw::editor::EditorPrefabIsolation isolation;
    isolation.push( manager, "game/a/inner.prefab.xml", *pRoot, true );
    isolation.remapPath( "game/a/other.prefab.xml", "game/b/other.prefab.xml" );
    SW_EXPECT_TRUE( isolation.getPrefabPath() == "game/a/inner.prefab.xml" );
    isolation.remapPath( "game/a/inner.prefab.xml", "game/b/inner.prefab.xml" );
    SW_EXPECT_TRUE( isolation.getPrefabPath() == "game/b/inner.prefab.xml" );
}
