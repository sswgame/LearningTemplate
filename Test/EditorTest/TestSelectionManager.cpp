#include "pch.h"

#include "Editor/Common/Workspace/SelectionManager.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// 선택은 핸들로 보관하고 편집 중인 씬에서 풀기 때문에(`editor::findGameObject`), 오브젝트를 다루는 케이스는
// 씬 매니저를 지역 서비스로 걸고 그 활성 씬에서 오브젝트를 만든다.

/**
 * @brief [SelectionManagerTest] 단일 및 다중 게임오브젝트 선택 모드 (Replace, Add, Remove, Toggle) 검증
 */
SW_TEST_CASE( SelectionManagerTest, GameObjectSelectionModes )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "SelectionProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager&        manager = *pScene->getObjectManager();

    GameObject* pObjA = manager.createGameObject( hashed_string( "ActorA" ) );
    GameObject* pObjB = manager.createGameObject( hashed_string( "ActorB" ) );
    GameObject* pObjC = manager.createGameObject( hashed_string( "ActorC" ) );

    SW_ASSERT_NOT_NULL( pObjA );
    SW_ASSERT_NOT_NULL( pObjB );
    SW_ASSERT_NOT_NULL( pObjC );

    SelectionManager selection;
    SW_EXPECT_EQUAL( size_t( 0 ), selection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == nullptr );

    // 1) Replace 모드
    selection.selectObject( pObjA, SelectionMode::Replace );
    SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( selection.hasObject( pObjA ) );
    SW_EXPECT_FALSE( selection.hasObject( pObjB ) );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == pObjA );

    // 2) Add 모드 (A + B)
    selection.selectObject( pObjB, SelectionMode::Add );
    SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( selection.hasObject( pObjA ) );
    SW_EXPECT_TRUE( selection.hasObject( pObjB ) );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == pObjA ); // Primary는 첫 번째 유지

    // 중복 Add 시 무시
    selection.selectObject( pObjB, SelectionMode::Add );
    SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedObjectCount() );

    // 3) Toggle 모드 (B 제거 -> B 추가)
    selection.selectObject( pObjB, SelectionMode::Toggle );
    SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
    SW_EXPECT_FALSE( selection.hasObject( pObjB ) );

    selection.selectObject( pObjB, SelectionMode::Toggle );
    SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( selection.hasObject( pObjB ) );

    // 4) Remove 모드 (A 제거)
    selection.selectObject( pObjA, SelectionMode::Remove );
    SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
    SW_EXPECT_FALSE( selection.hasObject( pObjA ) );
    SW_EXPECT_TRUE( selection.hasObject( pObjB ) );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == pObjB );

    // 5) 배치 선택 (SelectObjects)
    const vector<GameObject*> listBatch = { pObjA, pObjB, pObjC };
    selection.selectObjects( listBatch, SelectionMode::Replace );
    SW_EXPECT_EQUAL( size_t( 3 ), selection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( selection.hasObject( pObjA ) );
    SW_EXPECT_TRUE( selection.hasObject( pObjB ) );
    SW_EXPECT_TRUE( selection.hasObject( pObjC ) );

    // 풀어 받는 목록은 선택한 순서다.
    vector<GameObject*> listResolved;
    selection.getSelectedObjects( listResolved );
    SW_ASSERT_EQUAL( size_t( 3 ), listResolved.size() );
    SW_EXPECT_TRUE( listResolved[0] == pObjA );
    SW_EXPECT_TRUE( listResolved[2] == pObjC );

    // 6) 배치 Toggle은 기존 선택을 제거하고 신규 선택을 추가
    selection.selectObjects( { pObjB, pObjC }, SelectionMode::Toggle );
    SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( selection.hasObject( pObjA ) );
    SW_EXPECT_FALSE( selection.hasObject( pObjB ) );
    SW_EXPECT_FALSE( selection.hasObject( pObjC ) );

    // 7) clearObjectSelection
    selection.clearObjectSelection();
    SW_EXPECT_EQUAL( size_t( 0 ), selection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == nullptr );
}

/**
 * @brief [SelectionManagerTest] 애셋 경로 선택 모드 (Replace, Add, Remove, Toggle) 검증
 */
SW_TEST_CASE( SelectionManagerTest, AssetPathSelectionModes )
{
    SelectionManager selection;

    const string pathA = "content/maps/overworld.scene.xml";
    const string pathB = "content/textures/hero_albedo.png";
    const string pathC = "content/prefabs/enemy.prefab.xml";

    // 1) Replace
    selection.selectAsset( pathA, SelectionMode::Replace );
    SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedAssets().size() );
    SW_EXPECT_TRUE( selection.hasAsset( pathA ) );
    SW_EXPECT_EQUAL( pathA, string( selection.getPrimaryAsset() ) );

    // 2) Add
    selection.selectAsset( pathB, SelectionMode::Add );
    SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedAssets().size() );
    SW_EXPECT_TRUE( selection.hasAsset( pathA ) );
    SW_EXPECT_TRUE( selection.hasAsset( pathB ) );

    // 3) Toggle
    selection.selectAsset( pathA, SelectionMode::Toggle );
    SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedAssets().size() );
    SW_EXPECT_FALSE( selection.hasAsset( pathA ) );
    SW_EXPECT_TRUE( selection.hasAsset( pathB ) );

    // 4) 배치 선택
    vector<string> listBatch = { pathA, pathB, pathC };
    selection.selectAssets( listBatch, SelectionMode::Replace );
    SW_EXPECT_EQUAL( size_t( 3 ), selection.getSelectedAssets().size() );

    // 5) 배치 Toggle은 기존 선택을 제거하고 신규 선택을 추가
    selection.selectAssets( { pathB, pathC }, SelectionMode::Toggle );
    SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedAssets().size() );
    SW_EXPECT_TRUE( selection.hasAsset( pathA ) );
    SW_EXPECT_FALSE( selection.hasAsset( pathB ) );
    SW_EXPECT_FALSE( selection.hasAsset( pathC ) );

    // 6) clearAssetSelection
    selection.clearAssetSelection();
    SW_EXPECT_TRUE( selection.getSelectedAssets().empty() );
}

/**
 * @brief [SelectionManagerTest] 파괴된 게임오브젝트 자동 프루닝(PruneInvalid) 검증
 */
SW_TEST_CASE( SelectionManagerTest, PruneInvalidDestroyedObjects )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "PruneProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager&        manager = *pScene->getObjectManager();

    GameObject* pObjA = manager.createGameObject( hashed_string( "DeadActorA" ) );
    GameObject* pObjB = manager.createGameObject( hashed_string( "LiveActorB" ) );

    SW_ASSERT_NOT_NULL( pObjA );
    SW_ASSERT_NOT_NULL( pObjB );

    SelectionManager selection;
    selection.selectObjects( { pObjA, pObjB }, SelectionMode::Replace );
    SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedObjectCount() );

    // A 객체 파괴 및 지연 삭제 처리. 핸들로 비교하므로 A 의 핸들은 파괴 뒤에도 적어 둔 값으로 본다.
    const GameObjectHandle handleA = pObjA->getHandle();
    manager.destroyObject( pObjA );
    manager.processDeferredDestruction();

    // pruneInvalid 호출 시 유효하지 않은 A가 선택 목록에서 자동 제거되어야 함
    selection.pruneInvalid();
    SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( selection.getSelectedHandles().front() != handleA );
    SW_EXPECT_TRUE( selection.hasObject( pObjB ) );
    SW_EXPECT_TRUE( selection.getPrimaryObject() == pObjB );
}

/**
 * @brief [SelectionManagerTest] 이름을 바꿔도 선택이 유지되고, 옛 이름의 새 오브젝트로 옮겨 가지 않는다
 * @details 이름으로 찾던 `GameObjectPtr` 로 선택을 들던 때에는 둘 다 틀렸다 — 이름을 바꾸면 선택이 풀렸고, 옛 이름으로
 *          새 오브젝트를 만들면 선택이 조용히 그쪽을 가리켰다. 지금은 objectId 로 들고, id 는 다시 쓰지 않는다.
 */
SW_TEST_CASE( SelectionManagerTest, SelectionSurvivesRename )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "RenameProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager&        manager = *pScene->getObjectManager();

    GameObject* pObj = manager.createGameObject( hashed_string( "Original" ) );
    SW_ASSERT_NOT_NULL( pObj );

    SelectionManager selection;
    selection.selectObject( pObj, SelectionMode::Replace );

    pObj->setName( hashed_string( "Renamed" ) );
    GameObject* pImpostor = manager.createGameObject( hashed_string( "Original" ) );
    SW_ASSERT_NOT_NULL( pImpostor );
    SW_ASSERT_TRUE( pImpostor->getName() == hashed_string( "Original" ) );

    SW_EXPECT_TRUE( selection.getPrimaryObject() == pObj );
    SW_EXPECT_TRUE( selection.hasObject( pObj ) );
    SW_EXPECT_FALSE( selection.hasObject( pImpostor ) );
}

/**
 * @brief [SelectionManagerTest] 선택 변경 델리게이트 알림(onSelectionChanged) 검증
 */
SW_TEST_CASE( SelectionManagerTest, SelectionChangedEventBroadcast )
{
    GameObjectManager manager;
    GameObject*       pObj = manager.createGameObject( hashed_string( "NotifyActor" ) );
    SW_ASSERT_NOT_NULL( pObj );

    SelectionManager selection;
    uint32           eventCount = 0;

    selection.onSelectionChanged() = SW_DELEGATE_LAMBDA( Delegate<void()>, [&eventCount]()
    {
        ++eventCount;
    } );

    // 객체 선택 시 알림
    selection.selectObject( pObj, SelectionMode::Replace );
    SW_EXPECT_EQUAL( 1u, eventCount );

    // 애셋 선택 시 알림
    selection.selectAsset( "content/materials/wood.mat", SelectionMode::Add );
    SW_EXPECT_EQUAL( 2u, eventCount );

    // ClearAll 호출 시 알림
    selection.clearAll();
    SW_EXPECT_EQUAL( 3u, eventCount );

    // 빈 상태에서 ClearAll 호출 시 알림 미발생
    selection.clearAll();
    SW_EXPECT_EQUAL( 3u, eventCount );
}
