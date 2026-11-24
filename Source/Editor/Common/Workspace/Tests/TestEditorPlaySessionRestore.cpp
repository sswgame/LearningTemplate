/**
 * @file TestEditorPlaySessionRestore.cpp
 * @brief Stop 이 편집 씬을 되돌릴 때 플레이가 남긴 것(도는 로드 · 예약된 파괴 · 씬 세대)이 편집 상태로 새지 않는지 봅니다(nogpu, 지역 씬 매니저).
 */
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Play/EditorSceneGenerationSync.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처(`ScopedScratchDomain`)가 엔진 서비스를 직접 쓴다
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct EditorPlaySessionRestoreTestInternal
    {
        /** @brief 지역 씬 매니저에 오브젝트 하나짜리 편집 씬을 세웁니다. 실패하면 nullptr 입니다. */
        static GameObject* openEditScene( SceneManager& sceneManager )
        {
            Scene* const             pScene   = sceneManager.createEmptyActiveScene( "Edit" );
            GameObjectManager* const pObjects = pScene != nullptr ? pScene->getObjectManager() : nullptr;
            return pObjects != nullptr ? pObjects->createGameObject( hashed_string( "Crate" ) ) : nullptr;
        }
    };
} // namespace

/** @brief [EditorPlaySessionRestoreTest] 파괴가 예약된 채(일시정지 중 삭제 · onEndPlay) Stop 하면 오브젝트가 원래 id 로 돌아온다 — 새 id 를 받으면 선택 · 되돌리기 기록이 끊긴다 */
SW_TEST_CASE( EditorPlaySessionRestoreTest, PendingDestroyKeepsTheOriginalID )
{
    SubsystemScope scope;
    SceneManager   sceneManager;
    SW_REQUIRE_TRUE( scope.bind( &sceneManager ) );
    const ScopedEditorScope scopedEditor( scope );
    GameObject* const       pCrate = EditorPlaySessionRestoreTestInternal::openEditScene( sceneManager );
    SW_REQUIRE_NOT_NULL( pCrate );
    const uint64             objectID = pCrate->getObjectID();
    GameObjectManager* const pObjects = sceneManager.findActiveObjectManager();

    PlaySessionData data;
    EditorPlaySession::captureSnapshot( data );
    pObjects->destroyGameObject( pCrate ); // 지연 파괴 — 아직 그 id 로 등록돼 있다
    EditorPlaySession::restoreSnapshot( data );

    GameObject* const pRestored = pObjects->findGameObjectByID( objectID );
    SW_REQUIRE_NOT_NULL( pRestored );
    SW_EXPECT_TRUE( pRestored->getName() == hashed_string( "Crate" ) );
}

/** @brief [EditorPlaySessionRestoreTest] 플레이가 건 씬 이동이 끝나기 전에 Stop 하면 그 로드는 취소된다 — 남으면 Stop 뒤에 끝나며 편집 씬을 저장 확인 없이 바꿔 끼운다 */
SW_TEST_CASE( EditorPlaySessionRestoreTest, StopCancelsTravelInFlight )
{
    const test::ScopedScratchDomain domain;
    SceneManager                    sceneManager;
    SubsystemScope                  scope;
    SW_REQUIRE_TRUE( scope.bind( &sceneManager ) );
    const ScopedEditorScope scopedEditor( scope );
    SW_REQUIRE_NOT_NULL( EditorPlaySessionRestoreTestInternal::openEditScene( sceneManager ) );
    SW_REQUIRE_TRUE( sceneManager.saveActiveSceneCopy( domain.makeDiskPath( "next.scene.xml" ) ) );

    PlaySessionData data;
    EditorPlaySession::captureSnapshot( data );
    const uint64       generation = sceneManager.getSceneGeneration();
    TaskFuture<Scene*> travel     = sceneManager.requestLoadFuture( test::ScopedScratchDomain::makeID( "next.scene.xml" ).str(), SceneLoadKind::Travel );
    SW_REQUIRE_TRUE( travel.isValid() );
    SW_REQUIRE_TRUE( sceneManager.isTransitioning() );
    EditorPlaySession::restoreSnapshot( data );

    SW_EXPECT_FALSE( sceneManager.isTransitioning() );
    sceneManager.updateTransitions(); // 남은 결과가 있으면 여기서 편집 씬이 바뀐다
    SW_EXPECT_EQUAL( generation, sceneManager.getSceneGeneration() );
    SW_EXPECT_TRUE( sceneManager.getActiveScene()->getName() == "Edit" );
}

/** @brief [EditorPlaySessionRestoreTest] 씬 없이 시작한 플레이의 Stop 은 게임이 연 씬을 내린다 — 남으면 플레이 상태가 편집 씬이 되어 저장이 그 레벨 파일을 덮는다 */
SW_TEST_CASE( EditorPlaySessionRestoreTest, PlayWithoutSceneEndsWithoutScene )
{
    SubsystemScope scope;
    SceneManager   sceneManager;
    SW_REQUIRE_TRUE( scope.bind( &sceneManager ) );
    const ScopedEditorScope scopedEditor( scope );
    SW_REQUIRE_TRUE( sceneManager.getActiveScene() == nullptr );

    PlaySessionData data;
    EditorPlaySession::captureSnapshot( data );
    SW_REQUIRE_NOT_NULL( sceneManager.createEmptyActiveScene( "Game" ) );
    EditorPlaySession::restoreSnapshot( data );
    SW_EXPECT_TRUE( sceneManager.getActiveScene() == nullptr );

    // 편집 씬을 연 채 다시 시작하면 앞 플레이의 "씬 없음" 이 남아 편집 씬을 내리지 않는다.
    SW_REQUIRE_NOT_NULL( EditorPlaySessionRestoreTestInternal::openEditScene( sceneManager ) );
    EditorPlaySession::captureSnapshot( data );
    EditorPlaySession::restoreSnapshot( data );
    SW_REQUIRE_NOT_NULL( sceneManager.getActiveScene() );
    SW_EXPECT_TRUE( sceneManager.getActiveScene()->getName() == "Edit" );
}

/** @brief [EditorPlaySessionRestoreTest] 모듈 리로드가 멈춘 씬 로드는 같은 약속으로 다시 떠서 끝난다 — 취소하면 이동 하나가 실패로 사라지고, 새 인스턴스는 그 요청을 다시 받는다 */
SW_TEST_CASE( EditorPlaySessionRestoreTest, SuspendedTravelFinishesAfterReload )
{
    const test::ScopedScratchDomain domain;
    SceneManager                    sceneManager;
    SubsystemScope                  scope;
    SW_REQUIRE_TRUE( scope.bind( &sceneManager ) );
    const ScopedEditorScope scopedEditor( scope );
    SW_REQUIRE_NOT_NULL( EditorPlaySessionRestoreTestInternal::openEditScene( sceneManager ) );
    SW_REQUIRE_TRUE( sceneManager.saveActiveSceneCopy( domain.makeDiskPath( "next.scene.xml" ) ) );
    const string       nextID = test::ScopedScratchDomain::makeID( "next.scene.xml" ).str();
    TaskFuture<Scene*> travel = sceneManager.requestLoadFuture( nextID, SceneLoadKind::Travel );
    SW_REQUIRE_TRUE( travel.isValid() );

    sceneManager.suspendPendingLoad();
    SW_EXPECT_TRUE( sceneManager.isTransitioning() );
    SW_EXPECT_FALSE( travel.isReady() );
    vector<string> listReplayed;
    sceneManager.replayPendingLoadRequests( SW_DELEGATE_LAMBDA( SceneLoadRequestedHandler, [&listReplayed]( const string& path, SceneLoadKind, const TaskFuture<Scene*>& )
    { listReplayed.push_back( path ); } ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listReplayed.size() );
    SW_EXPECT_TRUE( listReplayed[0] == nextID );

    sceneManager.updateTransitions(); // 다시 띄운다
    sceneManager.waitForPendingLoad();
    sceneManager.updateTransitions(); // 받는다
    SW_REQUIRE_TRUE( travel.isReady() );
    SW_EXPECT_TRUE( travel.get() != nullptr );
    SW_EXPECT_TRUE( travel.get() == sceneManager.getActiveScene() );
}

/** @brief [EditorPlaySessionRestoreTest] 플레이 중인 씬은 원본 파일에 쓰이지 않는다(사본은 쓴다) — 쓰면 플레이 상태가 레벨 파일을 덮는다 */
SW_TEST_CASE( EditorPlaySessionRestoreTest, PlayingSceneIsNotWrittenToItsSource )
{
    const test::ScopedScratchDomain domain;
    SceneManager                    sceneManager;
    SW_REQUIRE_NOT_NULL( EditorPlaySessionRestoreTestInternal::openEditScene( sceneManager ) );
    SW_REQUIRE_TRUE( sceneManager.saveActiveSceneAs( domain.makeDiskPath( "edit.scene.xml" ) ) );

    sceneManager.setWorldPlaying( true );
    SW_EXPECT_LOG( "SceneManager.SceneSaveWhilePlaying" );
    SW_EXPECT_FALSE( sceneManager.saveActiveScene() );
    SW_EXPECT_TRUE( sceneManager.saveActiveSceneCopy( domain.makeDiskPath( "copy.scene.xml" ) ) );
    sceneManager.setWorldPlaying( false );
    SW_EXPECT_TRUE( sceneManager.saveActiveScene() );
}

/** @brief [EditorPlaySessionRestoreTest] 플레이 중 레벨 이동은 편집 씬의 미저장 표시를 지우지 않고, 편집 중 씬 바꾸기는 지운다 */
SW_TEST_CASE( EditorPlaySessionRestoreTest, TravelDuringPlayKeepsTheEditSceneDirty )
{
    EditorWorkspace workspace{ nullptr };
    workspace.markSceneDirty();
    SW_EXPECT_TRUE( EditorSceneGenerationSync::apply( workspace, workspace.getObservedSceneGeneration() + 1, true, nullptr ) );
    SW_EXPECT_TRUE( workspace.isSceneDirty() );
    SW_EXPECT_TRUE( EditorSceneGenerationSync::apply( workspace, workspace.getObservedSceneGeneration() + 1, false, nullptr ) );
    SW_EXPECT_FALSE( workspace.isSceneDirty() );
}

/** @brief [EditorPlaySessionRestoreTest] 되돌린 뒤 여러 선택이 id 로 이어진다 — 예전에는 주 선택 하나를 이름으로 다시 골라 나머지를 잃고, 이름이 같은 다른 오브젝트를 골랐다 */
SW_TEST_CASE( EditorPlaySessionRestoreTest, MultiSelectionSurvivesRestore )
{
    SubsystemScope scope;
    SceneManager   sceneManager;
    SW_REQUIRE_TRUE( scope.bind( &sceneManager ) );
    const ScopedEditorScope scopedEditor( scope );
    GameObject* const       pFirst = EditorPlaySessionRestoreTestInternal::openEditScene( sceneManager );
    SW_REQUIRE_NOT_NULL( pFirst );
    GameObjectManager* const pObjects = sceneManager.findActiveObjectManager();
    GameObject* const        pSecond  = pObjects->createGameObject( hashed_string( "Barrel" ) );
    GameObject* const        pSpawned = pObjects->createGameObject( hashed_string( "Spawned" ) );
    SW_REQUIRE_NOT_NULL( pSecond );
    SW_REQUIRE_NOT_NULL( pSpawned );
    EditorSelection selection;
    EditorWorkspace workspace{ &selection };
    workspace.selectGameObjects( { pFirst, pSecond, pSpawned } );

    pObjects->destroyGameObject( pSpawned ); // 플레이가 만든 것은 Stop 이 없앤다
    workspace.refreshSelectionAfterRestore();

    vector<GameObject*> listSelected;
    selection.getSelectedObjects( listSelected );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listSelected.size() );
    SW_EXPECT_TRUE( listSelected[0] == pFirst );
    SW_EXPECT_TRUE( listSelected[1] == pSecond );
}
