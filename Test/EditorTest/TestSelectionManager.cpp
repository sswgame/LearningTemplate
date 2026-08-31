#include "pch.h"

#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [SelectionManagerTest] 단일 및 다중 게임오브젝트 선택 모드 (Replace, Add, Remove, Toggle) 검증
 */
SW_TEST_CASE( SelectionManagerTest, GameObjectSelectionModes )
{
	GameObjectManager manager;
	GameObject*		  pObjA = manager.createGameObject( hashed_string( "ActorA" ) );
	GameObject*		  pObjB = manager.createGameObject( hashed_string( "ActorB" ) );
	GameObject*		  pObjC = manager.createGameObject( hashed_string( "ActorC" ) );

	SW_ASSERT_NOT_NULL( pObjA );
	SW_ASSERT_NOT_NULL( pObjB );
	SW_ASSERT_NOT_NULL( pObjC );

	GameObjectPtr ptrA{ pObjA };
	GameObjectPtr ptrB{ pObjB };
	GameObjectPtr ptrC{ pObjC };

	SelectionManager selection;
	SW_EXPECT_EQUAL( size_t( 0 ), selection.getSelectedObjectCount() );
	SW_EXPECT_FALSE( selection.getPrimaryObject().isValid() );

	// 1) Replace 모드
	selection.selectObject( ptrA, SelectionMode::Replace );
	SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
	SW_EXPECT_TRUE( selection.hasObject( ptrA ) );
	SW_EXPECT_FALSE( selection.hasObject( ptrB ) );
	SW_EXPECT_EQUAL( ptrA.get(), selection.getPrimaryObject().get() );

	// 2) Add 모드 (A + B)
	selection.selectObject( ptrB, SelectionMode::Add );
	SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedObjectCount() );
	SW_EXPECT_TRUE( selection.hasObject( ptrA ) );
	SW_EXPECT_TRUE( selection.hasObject( ptrB ) );
	SW_EXPECT_EQUAL( ptrA.get(), selection.getPrimaryObject().get() ); // Primary는 첫 번째 유지

	// 중복 Add 시 무시
	selection.selectObject( ptrB, SelectionMode::Add );
	SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedObjectCount() );

	// 3) Toggle 모드 (B 제거 -> B 추가)
	selection.selectObject( ptrB, SelectionMode::Toggle );
	SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
	SW_EXPECT_FALSE( selection.hasObject( ptrB ) );

	selection.selectObject( ptrB, SelectionMode::Toggle );
	SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedObjectCount() );
	SW_EXPECT_TRUE( selection.hasObject( ptrB ) );

	// 4) Remove 모드 (A 제거)
	selection.selectObject( ptrA, SelectionMode::Remove );
	SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
	SW_EXPECT_FALSE( selection.hasObject( ptrA ) );
	SW_EXPECT_TRUE( selection.hasObject( ptrB ) );
	SW_EXPECT_EQUAL( ptrB.get(), selection.getPrimaryObject().get() );

	// 5) 배치 선택 (SelectObjects)
	vector<GameObjectPtr> listBatch = { ptrA, ptrB, ptrC };
	selection.selectObjects( listBatch, SelectionMode::Replace );
	SW_EXPECT_EQUAL( size_t( 3 ), selection.getSelectedObjectCount() );
	SW_EXPECT_TRUE( selection.hasObject( ptrA ) );
	SW_EXPECT_TRUE( selection.hasObject( ptrB ) );
	SW_EXPECT_TRUE( selection.hasObject( ptrC ) );

	// 6) clearObjectSelection
	selection.clearObjectSelection();
	SW_EXPECT_EQUAL( size_t( 0 ), selection.getSelectedObjectCount() );
	SW_EXPECT_FALSE( selection.getPrimaryObject().isValid() );
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

	// 5) clearAssetSelection
	selection.clearAssetSelection();
	SW_EXPECT_TRUE( selection.getSelectedAssets().empty() );
}

/**
 * @brief [SelectionManagerTest] 파괴된 게임오브젝트 자동 프루닝(PruneInvalid) 검증
 */
SW_TEST_CASE( SelectionManagerTest, PruneInvalidDestroyedObjects )
{
	GameObjectManager manager;
	GameObject*		  pObjA = manager.createGameObject( hashed_string( "DeadActorA" ) );
	GameObject*		  pObjB = manager.createGameObject( hashed_string( "LiveActorB" ) );

	SW_ASSERT_NOT_NULL( pObjA );
	SW_ASSERT_NOT_NULL( pObjB );

	GameObjectPtr ptrA{ pObjA };
	GameObjectPtr ptrB{ pObjB };

	SelectionManager selection;
	selection.selectObjects( { ptrA, ptrB }, SelectionMode::Replace );
	SW_EXPECT_EQUAL( size_t( 2 ), selection.getSelectedObjectCount() );

	// A 객체 파괴 및 지연 삭제 처리
	manager.destroyObject( pObjA );
	manager.processDeferredDestruction();

	// pruneInvalid 호출 시 유효하지 않은 A가 선택 목록에서 자동 제거되어야 함
	selection.pruneInvalid();
	SW_EXPECT_EQUAL( size_t( 1 ), selection.getSelectedObjectCount() );
	SW_EXPECT_FALSE( selection.hasObject( ptrA ) );
	SW_EXPECT_TRUE( selection.hasObject( ptrB ) );
	SW_EXPECT_EQUAL( ptrB.get(), selection.getPrimaryObject().get() );
}

/**
 * @brief [SelectionManagerTest] 선택 변경 델리게이트 알림(onSelectionChanged) 검증
 */
SW_TEST_CASE( SelectionManagerTest, SelectionChangedEventBroadcast )
{
	GameObjectManager manager;
	GameObject*		  pObj = manager.createGameObject( hashed_string( "NotifyActor" ) );
	SW_ASSERT_NOT_NULL( pObj );

	SelectionManager selection;
	uint32			 eventCount = 0;

	selection.onSelectionChanged() = SW_DELEGATE_LAMBDA( Delegate<void()>, [&eventCount]()
	{
		++eventCount;
	} );

	// 객체 선택 시 알림
	selection.selectObject( GameObjectPtr{ pObj }, SelectionMode::Replace );
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
