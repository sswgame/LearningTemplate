/**
 * @file TestSceneAsync.cpp
 * @brief SceneManager async load tests (no GPU)
 */
#include "TestFramework.h"
#include "Core/Common/CoreServices.h"
#include "Core/Game/Scene/SceneManager.h"
#include "Core/Game/Scene/SceneDescriptor.h"
#include "Core/Object/GameObjectManager.h"
#include "Core/Utility/Task/TaskManager.h"
#include "Core/Utility/File/FileUtil.h"

#include <filesystem>
#include <thread>
#include <chrono>

SW_TEST_CASE( SceneTest, AsyncRequestCompletes )
{
	const std::filesystem::path xmlPath = std::filesystem::temp_directory_path() / "sw_test_scene_async.xml";
	const std::string			xmlStr	=
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<SceneDescriptor>\n"
		"  <name>AsyncTown</name>\n"
		"  <entities>\n"
		"    <entity name=\"PlayerSpawn\" prefab=\"Game/Prefabs/sample.prefab.xml\"/>\n"
		"    <entity name=\"Npc\"/>\n"
		"  </entities>\n"
		"</SceneDescriptor>\n";

	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath.string(),
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );
	SW_EXPECT_FALSE( manager.isTransitioning() );

	SW_ASSERT_TRUE( manager.requestLoadAsync( xmlPath.string() ) );
	SW_EXPECT_TRUE( manager.isTransitioning() );

	sw::TaskManager& tasks = sw::core::getTaskManager();
	tasks.dispatch();
	tasks.waitAll();

	// Worker sets ready flag; main thread swap happens in tickTransitions.
	for ( int i = 0; i < 100 && manager.isTransitioning(); ++i )
	{
		manager.tickTransitions();
		if ( manager.isTransitioning() )
			std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
	}

	manager.tickTransitions();
	SW_EXPECT_FALSE( manager.isTransitioning() );
	SW_ASSERT_NOT_NULL( manager.getActiveScene() );
	SW_EXPECT_STREQ( "AsyncTown", manager.getActiveScene()->getName() );
	SW_EXPECT_EQUAL( size_t( 2 ), manager.getActiveScene()->getObjectManager()->getAllGameObjects().size() );

	manager.shutdown();
	std::filesystem::remove( xmlPath );
}

SW_TEST_CASE( SceneTest, DescriptorLoadWithoutGpu )
{
	const std::filesystem::path xmlPath = std::filesystem::temp_directory_path() / "sw_test_scene_desc.xml";
	const std::string			xmlStr	=
		"<SceneDescriptor><name>DescOnly</name><entities><entity name=\"A\"/></entities></SceneDescriptor>";
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath.string(),
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneDescriptor desc{};
	SW_ASSERT_TRUE( sw::loadSceneDescriptorFromXml( xmlPath.string(), desc ) );
	SW_EXPECT_TRUE( desc._bValid );
	SW_EXPECT_STREQ( "DescOnly", desc._name );
	SW_EXPECT_EQUAL( size_t( 1 ), desc._entities.size() );

	std::filesystem::remove( xmlPath );
}
