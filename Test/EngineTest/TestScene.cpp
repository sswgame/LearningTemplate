#include "pch.h"

#include "Core/Uuid/Uuid.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) SceneTest — 활성 씬·비동기 로드
// ------------------------------------------------------------------------------
/**
 * @brief [SceneTest] 빈 매니저에서 createScene 이 활성 씬을 설정
 */

SW_TEST_CASE( SceneTest, CreateSceneSetsActiveWhenEmpty )
{
	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );
	SW_EXPECT_NULL( manager.getActiveScene() );
	SW_EXPECT_EQUAL( size_t( 0 ), manager.getLoadedScenes().size() );

	sw::Scene* scene = manager.createScene( "Main" );
	SW_ASSERT_NOT_NULL( scene );
	SW_EXPECT_STREQ( "Main", scene->getName() );
	SW_EXPECT_EQUAL( scene, manager.getActiveScene() );
	SW_EXPECT_EQUAL( size_t( 1 ), manager.getLoadedScenes().size() );

	manager.shutdown();
}

/**
 * @brief [SceneTest] 여러 씬이 있어도 첫 활성 씬 유지
 */
SW_TEST_CASE( SceneTest, MultipleScenesKeepFirstActive )
{
	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	sw::Scene* first  = manager.createScene( "Level_A" );
	sw::Scene* second = manager.createScene( "Level_B" );
	SW_ASSERT_NOT_NULL( first );
	SW_ASSERT_NOT_NULL( second );
	SW_EXPECT_EQUAL( first, manager.getActiveScene() );
	SW_EXPECT_EQUAL( size_t( 2 ), manager.getLoadedScenes().size() );
	SW_EXPECT_STREQ( "Level_B", second->getName() );

	manager.shutdown();
}

/**
 * @brief [SceneTest] GameObjectManager 소유
 */
SW_TEST_CASE( SceneTest, OwnsGameObjectManager )
{
	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	sw::Scene* scene = manager.createScene( "World" );
	SW_ASSERT_NOT_NULL( scene );
	SW_ASSERT_NOT_NULL( scene->getObjectManager() );

	sw::GameObject* obj = scene->getObjectManager()->createGameObject( sw::hashed_string( "Hero" ) );
	SW_ASSERT_NOT_NULL( obj );
	SW_EXPECT_EQUAL( obj, scene->getObjectManager()->findGameObjectByName( sw::hashed_string( "Hero" ) ) );

	manager.shutdown();
}

/**
 * @brief [SceneTest] RHI 없이 update 안전
 */
SW_TEST_CASE( SceneTest, UpdateWithoutRhiIsSafe )
{
	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	sw::Scene* scene = manager.createScene( "TickWorld" );
	SW_ASSERT_NOT_NULL( scene );
	scene->getObjectManager()->createGameObject( sw::hashed_string( "EmptyActor" ) );

	manager.tick( 0.016f );
	SW_EXPECT_EQUAL( size_t( 1 ), scene->getObjectManager()->getAllGameObjects().size() );

	manager.shutdown();
}

/**
 * @brief [SceneTest] 씬 내 엔티티 수명 및 셧다운 후 정리 검증
 */
SW_TEST_CASE( SceneTest, SceneEntityLifecycleAndShutdownCleanup )
{
	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	sw::Scene* sceneA = manager.createScene( "DungeonLevel" );
	SW_ASSERT_NOT_NULL( sceneA );

	// 오브젝트 여러 개 생성
	sw::GameObject* hero	= sceneA->getObjectManager()->createGameObject( sw::hashed_string( "Hero" ) );
	sw::GameObject* monster = sceneA->getObjectManager()->createGameObject( sw::hashed_string( "Monster" ) );
	SW_ASSERT_NOT_NULL( hero );
	SW_ASSERT_NOT_NULL( monster );

	SW_EXPECT_EQUAL( 2u, sceneA->getObjectManager()->getAllGameObjects().size() );

	// 틱 실행 및 정상 상태 검증
	manager.tick( 0.016f );
	SW_EXPECT_EQUAL( 2u, sceneA->getObjectManager()->getAllGameObjects().size() );

	// 셧다운 시 모든 씬 및 오브젝트 정리
	manager.shutdown();
	SW_EXPECT_NULL( manager.getActiveScene() );
	SW_EXPECT_EQUAL( 0u, manager.getLoadedScenes().size() );
}

/**
 * @brief [SceneTest] serializeToDocument 가 프리팹 소스 경로를 씁니다
 */
SW_TEST_CASE( SceneTest, SerializeWritesPrefabSourcePath )
{
	sw::Scene scene{ "PrefabRoundTrip" };
	SW_ASSERT_NOT_NULL( scene.getObjectManager() );

	sw::GameObject* pHero = scene.getObjectManager()->createGameObject( sw::hashed_string( "Hero" ) );
	SW_ASSERT_NOT_NULL( pHero );
	scene.setEntityPrefabPath( pHero->getObjectId(), "prefabs/hero.prefab.xml" );

	sw::SceneDocument doc{};
	SW_ASSERT_TRUE( scene.serializeToDocument( doc ) );
	SW_EXPECT_FALSE( doc._listEntityNode.empty() );

	bool bFoundPrefab{ false };
	for ( const sw::SceneDocument::EntityNode& node : doc._listEntityNode )
	{
		if ( node._name != "Hero" )
			continue;
		SW_EXPECT_STREQ( "prefabs/hero.prefab.xml", node._prefab.c_str() );
		bFoundPrefab = true;
	}
	SW_EXPECT_TRUE( bFoundPrefab );
}

/**
 * @brief [SceneTest] createEmptyActiveScene 이 세대를 올리고 이전 씬을 내립니다
 */
SW_TEST_CASE( SceneTest, CreateEmptyActiveSceneBumpsGeneration )
{
	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	sw::Scene* pFirst = manager.createScene( "First" );
	SW_ASSERT_NOT_NULL( pFirst );
	const uint64 firstGeneration = manager.getSceneGeneration();
	SW_EXPECT_TRUE( firstGeneration > 0 );

	sw::Scene* pEmpty = manager.createEmptyActiveScene( "Untitled" );
	SW_ASSERT_NOT_NULL( pEmpty );
	SW_EXPECT_EQUAL( pEmpty, manager.getActiveScene() );
	SW_EXPECT_TRUE( manager.getSceneGeneration() > firstGeneration );
	SW_EXPECT_STREQ( "Untitled", pEmpty->getName() );

	manager.shutdown();
}

/**
 * @brief [SceneTest] SceneDocument가 prefabGuid를 정상 직렬화하고 AssetDatabase를 통해 새 경로로 자동 복원합니다
 */
SW_TEST_CASE( SceneTest, PrefabGuidRoundtripAndResolve )
{
	sw::SceneDocument doc{};
	doc._name = "GuidTestScene";

	sw::SceneDocument::EntityNode node{};
	node._name				= "HeroInstance";
	node._prefab			= "prefabs/old_hero.prefab.xml";
	const sw::Uuid heroGuid = sw::Uuid::generate();
	node._prefabGuid		= heroGuid.toString();
	doc._listEntityNode.push_back( node );

	const sw::string tempSceneXml = sw::FileUtil::joinPath( sw::FileUtil::getDirectoryPart( sw::FileUtil::getExecutablePath() ), "temp_guid_scene.scene.xml" );
	SW_ASSERT_TRUE( doc.saveXml( tempSceneXml ) );

	if ( sw::engine::areEngineServicesBound() )
	{
		sw::engine::getResourceManager().getAssetDatabase()._mapGuidToPath[heroGuid] = "prefabs/new_hero.prefab.xml";
	}

	sw::SceneDocument loadedDoc{};
	SW_ASSERT_TRUE( loadedDoc.loadXml( tempSceneXml ) );
	SW_ASSERT_TRUE( loadedDoc._listEntityNode.empty() == false );
	SW_EXPECT_STREQ( "HeroInstance", loadedDoc._listEntityNode[0]._name.c_str() );
	SW_EXPECT_STREQ( heroGuid.toString().c_str(), loadedDoc._listEntityNode[0]._prefabGuid.c_str() );
	if ( sw::engine::areEngineServicesBound() )
	{
		SW_EXPECT_STREQ( "prefabs/new_hero.prefab.xml", loadedDoc._listEntityNode[0]._prefab.c_str() );
	}

	sw::FileUtil::removeFile( tempSceneXml );
}
