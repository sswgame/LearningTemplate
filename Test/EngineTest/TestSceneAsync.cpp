#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Scene/SceneDocument.h"

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
		"<Scene formatVersion=\"0\" name=\"AsyncTown\">\n"
		"  <entities>\n"
		"    <entity name=\"PlayerSpawn\"/>\n"
		"    <entity name=\"Npc\"/>\n"
		"  </entities>\n"
		"</Scene>\n";

	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneDocument doc{};
	doc._name = "AsyncTown";
	sw::SceneDocument::EntityNode entA{};
	entA._name = "PlayerSpawn";
	doc._listEntityNode.push_back( std::move( entA ) );
	sw::SceneDocument::EntityNode entB{};
	entB._name = "Npc";
	doc._listEntityNode.push_back( std::move( entB ) );
	SW_ASSERT_TRUE( doc.saveBinary( binPath ) );

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
 * @brief [SceneTest] GPU 없이 SceneDocument 로드
 */
SW_TEST_CASE( SceneTest, DocumentLoadWithoutGpu )
{
	const sw::string xmlPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_desc.xml" );
	const sw::string binPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_desc.bin" );
	const sw::string xmlStr =
		"<Scene formatVersion=\"0\" name=\"DescOnly\"><entities><entity name=\"A\"/></entities></Scene>";
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneDocument docSetup{};
	docSetup._name = "DescOnly";
	sw::SceneDocument::EntityNode entA{};
	entA._name = "A";
	docSetup._listEntityNode.push_back( std::move( entA ) );
	SW_ASSERT_TRUE( docSetup.saveBinary( binPath ) );

	sw::SceneDocument doc{};
	SW_ASSERT_TRUE( doc.load( xmlPath ) );
	SW_EXPECT_TRUE( doc._bValid );
	SW_EXPECT_STREQ( "DescOnly", doc._name );
	SW_EXPECT_EQUAL( size_t( 1 ), doc._listEntityNode.size() );

	sw::FileUtil::removeFile( xmlPath );
	sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [SceneTest] SceneDocument 바이너리(SCN1) 직렬화 및 역직렬화 왕복 테스트
 */
SW_TEST_CASE( SceneTest, DocumentBinaryRoundTrip )
{
	const sw::string binPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_scene_desc.bin" );

	sw::SceneDocument originalDoc{};
	originalDoc._name = "BinaryTestScene";
	sw::SceneDocument::EntityNode entA{};
	entA._name		  = "Hero";
	entA._prefab	  = "game/demo/prefabs/Hero.prefab";
	entA._embeddedXml = "<GameObjectState><Name>Hero</Name></GameObjectState>";
	originalDoc._listEntityNode.push_back( std::move( entA ) );

	sw::SceneDocument::EntityNode entB{};
	entB._name	 = "Monster";
	entB._prefab = "game/demo/prefabs/Monster.prefab";
	originalDoc._listEntityNode.push_back( std::move( entB ) );

	SW_ASSERT_TRUE( originalDoc.saveBinary( binPath ) );

	sw::SceneDocument loadedDoc{};
	SW_ASSERT_TRUE( loadedDoc.loadBinary( binPath ) );
	SW_EXPECT_TRUE( loadedDoc._bValid );
	SW_EXPECT_STREQ( "BinaryTestScene", loadedDoc._name );
	SW_ASSERT_EQUAL( size_t( 2 ), loadedDoc._listEntityNode.size() );
	SW_EXPECT_STREQ( "Hero", loadedDoc._listEntityNode[0]._name );
	SW_EXPECT_STREQ( "game/demo/prefabs/Hero.prefab", loadedDoc._listEntityNode[0]._prefab );
	SW_EXPECT_STREQ( "<GameObjectState><Name>Hero</Name></GameObjectState>", loadedDoc._listEntityNode[0]._embeddedXml );
	SW_EXPECT_STREQ( "Monster", loadedDoc._listEntityNode[1]._name );
	SW_EXPECT_STREQ( "game/demo/prefabs/Monster.prefab", loadedDoc._listEntityNode[1]._prefab );

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
		"<Scene formatVersion=\"0\" name=\"TownA\"><entities><entity name=\"A\"/></entities></Scene>";
	const sw::string xmlB =
		"<Scene formatVersion=\"0\" name=\"TownB\"><entities><entity name=\"B1\"/><entity name=\"B2\"/></entities></Scene>";

	SW_ASSERT_TRUE( sw::FileUtil::writeFile( townA,
											 reinterpret_cast<const uint8*>( xmlA.data() ),
											 static_cast<uint64>( xmlA.size() ) ) );
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( townB,
											 reinterpret_cast<const uint8*>( xmlB.data() ),
											 static_cast<uint64>( xmlB.size() ) ) );

	sw::SceneDocument docA{};
	docA._name = "TownA";
	sw::SceneDocument::EntityNode entA{};
	entA._name = "A";
	docA._listEntityNode.push_back( std::move( entA ) );
	SW_ASSERT_TRUE( docA.saveBinary( binA ) );

	sw::SceneDocument docB{};
	docB._name = "TownB";
	sw::SceneDocument::EntityNode entB1{};
	entB1._name = "B1";
	docB._listEntityNode.push_back( std::move( entB1 ) );
	sw::SceneDocument::EntityNode entB2{};
	entB2._name = "B2";
	docB._listEntityNode.push_back( std::move( entB2 ) );
	SW_ASSERT_TRUE( docB.saveBinary( binB ) );

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
		"<Scene formatVersion=\"0\" name=\"Replaced\"><entities><entity name=\"Only\"/></entities></Scene>";
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneDocument doc{};
	doc._name = "Replaced";
	sw::SceneDocument::EntityNode ent{};
	ent._name = "Only";
	doc._listEntityNode.push_back( std::move( ent ) );
	SW_ASSERT_TRUE( doc.saveBinary( binPath ) );

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
	const sw::string binPath1	= sw::FileUtil::joinPath( tempDir, "test_rapid_1.scene.bin" );
	const sw::string scenePath2 = sw::FileUtil::joinPath( tempDir, "test_rapid_2.scene.xml" );
	const sw::string binPath2	= sw::FileUtil::joinPath( tempDir, "test_rapid_2.scene.bin" );

	const sw::string xmlStr1 =
		"<Scene formatVersion=\"0\" name=\"SceneFirst\"><entities><entity name=\"E1\"/></entities></Scene>";
	const sw::string xmlStr2 =
		"<Scene formatVersion=\"0\" name=\"SceneSecond\"><entities><entity name=\"E2\"/></entities></Scene>";

	SW_ASSERT_TRUE( sw::FileUtil::writeFile( scenePath1, reinterpret_cast<const uint8*>( xmlStr1.data() ), xmlStr1.size() ) );
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( scenePath2, reinterpret_cast<const uint8*>( xmlStr2.data() ), xmlStr2.size() ) );

	sw::SceneDocument doc1{};
	doc1._name = "SceneFirst";
	sw::SceneDocument::EntityNode ent1{};
	ent1._name = "E1";
	doc1._listEntityNode.push_back( std::move( ent1 ) );
	SW_ASSERT_TRUE( doc1.saveBinary( binPath1 ) );

	sw::SceneDocument doc2{};
	doc2._name = "SceneSecond";
	sw::SceneDocument::EntityNode ent2{};
	ent2._name = "E2";
	doc2._listEntityNode.push_back( std::move( ent2 ) );
	SW_ASSERT_TRUE( doc2.saveBinary( binPath2 ) );

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
	sw::FileUtil::removeFile( binPath1 );
	sw::FileUtil::removeFile( scenePath2 );
	sw::FileUtil::removeFile( binPath2 );
}

/**
 * @brief [SceneTest] TaskFuture 기반 비동기 씬 로드 및 Fluent 체이닝 검증
 */
SW_TEST_CASE( SceneTest, RequestLoadFutureChaining )
{
	const sw::string xmlPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_future_scene.xml" );
	const sw::string binPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_future_scene.bin" );
	const sw::string xmlStr =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<Scene formatVersion=\"0\" name=\"FutureTown\">\n"
		"  <entities>\n"
		"    <entity name=\"Hero\"/>\n"
		"  </entities>\n"
		"</Scene>\n";

	SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
											 reinterpret_cast<const uint8*>( xmlStr.data() ),
											 static_cast<uint64>( xmlStr.size() ) ) );

	sw::SceneDocument doc{};
	doc._name = "FutureTown";
	sw::SceneDocument::EntityNode ent{};
	ent._name = "Hero";
	doc._listEntityNode.push_back( std::move( ent ) );
	SW_ASSERT_TRUE( doc.saveBinary( binPath ) );

	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	bool					   bContinuationInvoked = false;
	sw::string				   loadedSceneName{};
	sw::TaskFuture<sw::Scene*> future = manager.requestLoadFuture( xmlPath );
	SW_ASSERT_TRUE( future.isValid() );
	SW_EXPECT_FALSE( future.isReady() );

	// .then() 모나딕 체이닝 연결
	future.then( [&bContinuationInvoked, &loadedSceneName]( sw::Scene* pScene )
	{
		if ( pScene != nullptr )
		{
			bContinuationInvoked = true;
			loadedSceneName		 = pScene->getName();
		}
	} );

	sw::drainSceneTransitions( manager );

	SW_EXPECT_TRUE( future.isReady() );
	SW_EXPECT_TRUE( bContinuationInvoked );
	SW_EXPECT_STREQ( "FutureTown", loadedSceneName.c_str() );
	SW_ASSERT_NOT_NULL( future.get() );
	SW_EXPECT_EQUAL( future.get(), manager.getActiveScene() );

	manager.shutdown();
	sw::FileUtil::removeFile( xmlPath );
	sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [AssetStreamingTest] TaskFuture 및 LockFreeQueue 기반 에셋 스트리밍 완료 검증
 */
SW_TEST_CASE( AssetStreamingTest, StreamingFutureAndLockFreeQueue )
{
	const sw::string tempFile = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "sw_test_streaming_asset.dat" );
	const sw::string testData = "STREAMING_TEST_PAYLOAD";
	SW_ASSERT_TRUE( sw::FileUtil::writeFile( tempFile,
											 reinterpret_cast<const uint8*>( testData.data() ),
											 static_cast<uint64>( testData.size() ) ) );

	sw::AssetStreamingQueue queue;
	queue.initialize();

	sw::TaskFuture<bool> futSuccess = queue.requestAssetFuture( tempFile, sw::StreamingPriority::High );
	SW_ASSERT_TRUE( futSuccess.isValid() );

	bool bCallbackRan = false;
	futSuccess.then( [&bCallbackRan]( bool bResult )
	{
		if ( bResult )
			bCallbackRan = true;
	} );

	// 동기/비동기 태스크 완료 대기 및 락-프리 큐 펌프
	if ( sw::engine::areEngineServicesBound() )
		sw::engine::getTaskManager().waitAll();

	queue.update( 32 );

	SW_EXPECT_TRUE( futSuccess.isReady() );
	SW_EXPECT_TRUE( futSuccess.get() );
	SW_EXPECT_TRUE( bCallbackRan );
	SW_EXPECT_TRUE( queue.isLoaded( tempFile ) );

	queue.shutdown();
	sw::FileUtil::removeFile( tempFile );
}

/**
 * @brief [AssetStreamingTest] 다중 스레드 동시 스트리밍 및 락-프리 큐 펌프 스트레스 테스트
 */
SW_TEST_CASE( AssetStreamingTest, MultiThreadedConcurrentStreamingStress )
{
	constexpr int32 kFileCount = 8;
	constexpr int32 kWorkers   = 4;

	sw::vector<sw::string> listTempFile;
	listTempFile.reserve( kFileCount );

	const sw::string tempDir = sw::FileUtil::getTempDirectory();
	for ( int32 fileIndex = 0; fileIndex < kFileCount; ++fileIndex )
	{
		const sw::string path = sw::FileUtil::joinPath( tempDir, sw::string( "sw_stress_asset_" ) + sw::string( std::to_string( fileIndex ).c_str() ) + ".dat" );
		const sw::string data = "STRESS_DATA_BLOCK";
		SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( data.data() ), data.size() ) );
		listTempFile.push_back( path );
	}

	sw::AssetStreamingQueue queue;
	queue.initialize();

	sw::atomic<int32> countCallback{ 0 };

	// 4개 생산자 스레드가 동시에 8개 파일에 대해 비동기 Future 요청 발행
	sw::vector<std::thread> listThread;
	listThread.reserve( kWorkers );

	for ( int32 threadIndex = 0; threadIndex < kWorkers; ++threadIndex )
	{
		listThread.emplace_back( [&queue, &listTempFile, &countCallback]()
		{
			for ( int32 cycle = 0; cycle < 10; ++cycle )
			{
				for ( const sw::string& path : listTempFile )
				{
					sw::TaskFuture<bool> fut = queue.requestAssetFuture( path );
					fut.then( [&countCallback]( bool bSuccess )
					{
						if ( bSuccess )
							countCallback.fetch_add( 1, std::memory_order_relaxed );
					} );
				}
			}
		} );
	}

	for ( auto& workerThread : listThread )
		workerThread.join();

	// 백그라운드 태스크 대기 및 락-프리 완료 큐 드레인
	if ( sw::engine::areEngineServicesBound() )
		sw::engine::getTaskManager().waitAll();

	for ( int32 drainStep = 0; drainStep < 50; ++drainStep )
	{
		queue.update( 64 );
		std::this_thread::yield();
	}

	for ( const sw::string& path : listTempFile )
	{
		SW_EXPECT_TRUE( queue.isLoaded( path ) );
		sw::FileUtil::removeFile( path );
	}

	SW_EXPECT_TRUE( countCallback.load() > 0 );
	queue.shutdown();
}

/**
 * @brief [SceneTest] 연속 비동기 씬 Future 발행 및 취소·복구 스트레스 테스트
 */
SW_TEST_CASE( SceneTest, RapidConcurrentFutureLoadsAndCancellationsStress )
{
	const sw::string	   tempDir = sw::FileUtil::getTempDirectory();
	sw::vector<sw::string> listXmlPath;
	sw::vector<sw::string> listBinPath;

	for ( int32 index = 0; index < 3; ++index )
	{
		const sw::string name	 = sw::string( "StressScene_" ) + sw::string( std::to_string( index ).c_str() );
		const sw::string xmlPath = sw::FileUtil::joinPath( tempDir, name + ".xml" );
		const sw::string binPath = sw::FileUtil::joinPath( tempDir, name + ".bin" );

		const sw::string xmlStr = "<Scene formatVersion=\"0\" name=\"" + name + "\"><entities><entity name=\"E\"/></entities></Scene>";
		SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlStr.data() ), xmlStr.size() ) );

		sw::SceneDocument doc{};
		doc._name = name;
		sw::SceneDocument::EntityNode ent{};
		ent._name = "E";
		doc._listEntityNode.push_back( std::move( ent ) );
		SW_ASSERT_TRUE( doc.saveBinary( binPath ) );

		listXmlPath.push_back( xmlPath );
		listBinPath.push_back( binPath );
	}

	sw::SceneManager manager;
	SW_ASSERT_TRUE( manager.initialize() );

	for ( int32 cycle = 0; cycle < 5; ++cycle )
	{
		sw::TaskFuture<sw::Scene*> fut1 = manager.requestLoadFuture( listXmlPath[0] );
		sw::TaskFuture<sw::Scene*> fut2 = manager.requestLoadFuture( listXmlPath[1] );
		sw::TaskFuture<sw::Scene*> fut3 = manager.requestLoadFuture( listXmlPath[2] );

		sw::drainSceneTransitions( manager );

		SW_EXPECT_FALSE( manager.isTransitioning() );
		SW_ASSERT_NOT_NULL( manager.getActiveScene() );
		SW_EXPECT_STREQ( "StressScene_2", manager.getActiveScene()->getName() );
	}

	manager.shutdown();

	for ( size_t index = 0; index < listXmlPath.size(); ++index )
	{
		sw::FileUtil::removeFile( listXmlPath[index] );
		sw::FileUtil::removeFile( listBinPath[index] );
	}
}
