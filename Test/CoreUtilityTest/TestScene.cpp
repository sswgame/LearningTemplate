/**
 * @file TestScene.cpp
 * @brief Scene / SceneManager unit tests (no RHI required)
 */
#include "TestFramework.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Object/GameObject.h"

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

SW_TEST_CASE( SceneTest, UpdateWithoutRhiIsSafe )
{
	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	sw::Scene* scene = manager.createScene( "TickWorld" );
	SW_ASSERT_NOT_NULL( scene );
	scene->getObjectManager()->createGameObject( sw::hashed_string( "EmptyActor" ) );

	manager.update( 0.016f );
	SW_EXPECT_EQUAL( size_t( 1 ), scene->getObjectManager()->getAllGameObjects().size() );

	manager.shutdown();
}
