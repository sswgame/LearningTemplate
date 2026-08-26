#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/SceneDescriptor.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
	namespace
	{
		/** @brief 비동기 씬 전환이 끝날 때까지 태스크를 비웁니다. */
		void drainSceneTransitions( sw::SceneManager& manager )
		{
			sw::TaskManager& tasks = sw::engine::getTaskManager();
			for ( int32 stepIndex = 0; stepIndex < 200 && manager.isTransitioning(); ++stepIndex )
			{
				tasks.waitAll();
				manager.tickTransitions();
				if ( manager.isTransitioning() )
					std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
			}
			manager.tickTransitions();
		}

	} // namespace
} // namespace sw

// ------------------------------------------------------------------------------
// 1) SceneTest — 활성 씬·비동기 로드
// ------------------------------------------------------------------------------
/**
 * @brief [SceneTest] 비동기 요청 완료
 */
SW_TEST_CASE( SceneTest, AsyncRequestCompletes )
{
	const sw::string xmlPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_async.xml" );
	const sw::string binPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_async.bin" );
	const sw::string xmlStr =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<SceneDescriptor>\n"
		"  <name>AsyncTown</name>\n"
		"  <entities>\n"
		"    <entity name=\"PlayerSpawn\"/>\n"
		"    <entity name=\"Npc\"/>\n"
		"  </entities>\n"
		"</SceneDescriptor>\n";

	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneDescriptor desc{};
	desc._name = "AsyncTown";
	sw::SceneEntityPlaceholder entA{};
	entA._name = "PlayerSpawn";
	desc._listEntities.push_back( std::move( entA ) );
	sw::SceneEntityPlaceholder entB{};
	entB._name = "Npc";
	desc._listEntities.push_back( std::move( entB ) );
	SW_ASSERT_TRUE( sw::saveSceneDescriptorToBinary( binPath, desc ) );

	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );
	SW_EXPECT_FALSE( manager.isTransitioning() );

	SW_ASSERT_TRUE( manager.requestLoadAsync( xmlPath ) );
	SW_EXPECT_TRUE( manager.isTransitioning() );

	sw::drainSceneTransitions( manager );
	SW_EXPECT_FALSE( manager.isTransitioning() );
	SW_ASSERT_NOT_NULL( manager.getActiveScene() );
	SW_EXPECT_STREQ( "AsyncTown", manager.getActiveScene()->getName() );
	manager.getActiveScene()->getObjectManager()->tick( 0.0f );
	SW_EXPECT_EQUAL( size_t( 2 ), manager.getActiveScene()->getObjectManager()->getAllGameObjects().size() );

	manager.shutdown();
	sw::FileUtil::removeFile( xmlPath );
	sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [SceneTest] GPU 없이 Descriptor 로드
 */
SW_TEST_CASE( SceneTest, DescriptorLoadWithoutGpu )
{
	const sw::string xmlPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_desc.xml" );
	const sw::string binPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_desc.bin" );
	const sw::string xmlStr =
		"<SceneDescriptor><name>DescOnly</name><entities><entity name=\"A\"/></entities></SceneDescriptor>";
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneDescriptor descSetup{};
	descSetup._name = "DescOnly";
	sw::SceneEntityPlaceholder entA{};
	entA._name = "A";
	descSetup._listEntities.push_back( std::move( entA ) );
	SW_ASSERT_TRUE( sw::saveSceneDescriptorToBinary( binPath, descSetup ) );

	sw::SceneDescriptor desc{};
	SW_ASSERT_TRUE( sw::loadSceneDescriptor( xmlPath, desc ) );
	SW_EXPECT_TRUE( desc._bValid );
	SW_EXPECT_STREQ( "DescOnly", desc._name );
	SW_EXPECT_EQUAL( size_t( 1 ), desc._listEntities.size() );

	sw::FileUtil::removeFile( xmlPath );
	sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [SceneTest] SceneDescriptor 바이너리(SCN1) 직렬화 및 역직렬화 왕복 테스트
 */
SW_TEST_CASE( SceneTest, DescriptorBinaryRoundTrip )
{
	const sw::string binPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_desc.bin" );

	sw::SceneDescriptor originalDesc{};
	originalDesc._name = "BinaryTestScene";
	sw::SceneEntityPlaceholder entA{};
	entA._name		  = "Hero";
	entA._prefab	  = "game/demo/prefabs/Hero.prefab";
	entA._embeddedXml = "<GameObjectState><Name>Hero</Name></GameObjectState>";
	originalDesc._listEntities.push_back( std::move( entA ) );

	sw::SceneEntityPlaceholder entB{};
	entB._name	 = "Monster";
	entB._prefab = "game/demo/prefabs/Monster.prefab";
	originalDesc._listEntities.push_back( std::move( entB ) );

	SW_ASSERT_TRUE( sw::saveSceneDescriptorToBinary( binPath, originalDesc ) );

	sw::SceneDescriptor loadedDesc{};
	SW_ASSERT_TRUE( sw::loadSceneDescriptorFromBinary( binPath, loadedDesc ) );
	SW_EXPECT_TRUE( loadedDesc._bValid );
	SW_EXPECT_STREQ( "BinaryTestScene", loadedDesc._name );
	SW_ASSERT_EQUAL( size_t( 2 ), loadedDesc._listEntities.size() );
	SW_EXPECT_STREQ( "Hero", loadedDesc._listEntities[0]._name );
	SW_EXPECT_STREQ( "game/demo/prefabs/Hero.prefab", loadedDesc._listEntities[0]._prefab );
	SW_EXPECT_STREQ( "<GameObjectState><Name>Hero</Name></GameObjectState>", loadedDesc._listEntities[0]._embeddedXml );
	SW_EXPECT_STREQ( "Monster", loadedDesc._listEntities[1]._name );
	SW_EXPECT_STREQ( "game/demo/prefabs/Monster.prefab", loadedDesc._listEntities[1]._prefab );

	sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [SceneTest] 진행 중 비동기 로드는 최신 대기 경로로 대체된다
 */
SW_TEST_CASE( SceneTest, AsyncWarpSequenceQueuesLatest )
{
	const sw::string townA = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_warp_a.xml" );
	const sw::string binA  = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_warp_a.bin" );
	const sw::string townB = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_warp_b.xml" );
	const sw::string binB  = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_warp_b.bin" );
	const sw::string xmlA =
		"<SceneDescriptor><name>TownA</name><entities><entity name=\"A\"/></entities></SceneDescriptor>";
	const sw::string xmlB =
		"<SceneDescriptor><name>TownB</name><entities><entity name=\"B1\"/><entity name=\"B2\"/></entities></SceneDescriptor>";

	SW_ASSERT_TRUE( sw::FileUtil::writeFile( townA,
											 reinterpret_cast<const uint8*>( xmlA.data() ),
											 static_cast<uint64>( xmlA.size() ) ) );
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( townB,
											 reinterpret_cast<const uint8*>( xmlB.data() ),
											 static_cast<uint64>( xmlB.size() ) ) );

	sw::SceneDescriptor descA{};
	descA._name = "TownA";
	sw::SceneEntityPlaceholder entA{};
	entA._name = "A";
	descA._listEntities.push_back( std::move( entA ) );
	SW_ASSERT_TRUE( sw::saveSceneDescriptorToBinary( binA, descA ) );

	sw::SceneDescriptor descB{};
	descB._name = "TownB";
	sw::SceneEntityPlaceholder entB1{};
	entB1._name = "B1";
	descB._listEntities.push_back( std::move( entB1 ) );
	sw::SceneEntityPlaceholder entB2{};
	entB2._name = "B2";
	descB._listEntities.push_back( std::move( entB2 ) );
	SW_ASSERT_TRUE( sw::saveSceneDescriptorToBinary( binB, descB ) );

	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );
	SW_ASSERT_TRUE( manager.requestLoadAsync( townA ) );
	SW_EXPECT_TRUE( manager.requestLoadAsync( townB ) ); // 대기열에 넣고 목적지를 버리지 않음

	sw::drainSceneTransitions( manager );
	SW_ASSERT_NOT_NULL( manager.getActiveScene() );
	SW_EXPECT_STREQ( "TownB", manager.getActiveScene()->getName() );
	SW_EXPECT_EQUAL( size_t( 1 ), manager.getLoadedScenes().size() );
	manager.getActiveScene()->getObjectManager()->tick( 0.0f );
	SW_EXPECT_EQUAL( size_t( 2 ), manager.getActiveScene()->getObjectManager()->getAllGameObjects().size() );

	manager.shutdown();
	sw::FileUtil::removeFile( townA );
	sw::FileUtil::removeFile( binA );
	sw::FileUtil::removeFile( townB );
	sw::FileUtil::removeFile( binB );
}

/**
 * @brief [SceneTest] 비동기 스왑이 이전 활성 씬을 언로드한다(createScene 잔여 포함)
 */
SW_TEST_CASE( SceneTest, AsyncSwapUnloadsPreviousActive )
{
	const sw::string xmlPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_replace.xml" );
	const sw::string binPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_replace.bin" );
	const sw::string xmlStr =
		"<SceneDescriptor><name>Replaced</name><entities><entity name=\"Only\"/></entities></SceneDescriptor>";
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneDescriptor desc{};
	desc._name = "Replaced";
	sw::SceneEntityPlaceholder ent{};
	ent._name = "Only";
	desc._listEntities.push_back( std::move( ent ) );
	SW_ASSERT_TRUE( sw::saveSceneDescriptorToBinary( binPath, desc ) );

	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );
	sw::Scene* bootstrap = manager.createScene( "Bootstrap" );
	SW_ASSERT_NOT_NULL( bootstrap );
	SW_EXPECT_EQUAL( size_t( 1 ), manager.getLoadedScenes().size() );

	SW_ASSERT_TRUE( manager.requestLoadAsync( xmlPath ) );
	sw::drainSceneTransitions( manager );

	SW_ASSERT_NOT_NULL( manager.getActiveScene() );
	SW_EXPECT_STREQ( "Replaced", manager.getActiveScene()->getName() );
	SW_EXPECT_EQUAL( size_t( 1 ), manager.getLoadedScenes().size() );

	manager.shutdown();
	sw::FileUtil::removeFile( xmlPath );
	sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [SceneTest] 연속된 비동기 로드 요청 및 큐잉된 씬 우선 교체 검증
 */
SW_TEST_CASE( SceneTest, SceneAsyncLoadCancellationAndRecovery )
{
	const sw::string tempDir	= sw::FileUtil::getTempDirectory();
	const sw::string scenePath1 = sw::FileUtil::joinPath( tempDir, "test_rapid_1.scene.xml" );
	const sw::string scenePath2 = sw::FileUtil::joinPath( tempDir, "test_rapid_2.scene.xml" );

	const sw::string xmlStr1 =
		"<SceneDescriptor><name>SceneFirst</name><entities><entity name=\"E1\"/></entities></SceneDescriptor>";
	const sw::string xmlStr2 =
		"<SceneDescriptor><name>SceneSecond</name><entities><entity name=\"E2\"/></entities></SceneDescriptor>";

	SW_ASSERT_TRUE( sw::FileUtil::writeFile( scenePath1, reinterpret_cast<const uint8*>( xmlStr1.data() ), xmlStr1.size() ) );
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( scenePath2, reinterpret_cast<const uint8*>( xmlStr2.data() ), xmlStr2.size() ) );

	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	// 1차 비동기 요청 후 즉시 2차 비동기 요청 발행 (큐잉 전환)
	SW_ASSERT_TRUE( manager.requestLoadAsync( scenePath1 ) );
	SW_ASSERT_TRUE( manager.requestLoadAsync( scenePath2 ) );

	sw::drainSceneTransitions( manager );

	SW_EXPECT_FALSE( manager.isTransitioning() );
	SW_ASSERT_NOT_NULL( manager.getActiveScene() );
	SW_EXPECT_STREQ( "SceneSecond", manager.getActiveScene()->getName() );

	manager.shutdown();
	sw::FileUtil::removeFile( scenePath1 );
	sw::FileUtil::removeFile( scenePath2 );
}
