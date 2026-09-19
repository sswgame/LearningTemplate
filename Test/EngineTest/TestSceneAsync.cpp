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
 * @brief [SceneAsyncTest] 비동기 요청 완료
 */
SW_TEST_CASE( SceneAsyncTest, AsyncRequestCompletes )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_async.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_async.bin" );
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
 * @brief [SceneAsyncTest] GPU 없이 SceneDocument 로드
 */
SW_TEST_CASE( SceneAsyncTest, DocumentLoadWithoutGpu )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_desc.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_desc.bin" );
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
 * @brief [SceneAsyncTest] SceneDocument 바이너리(SCN1) 직렬화 및 역직렬화 왕복 테스트
 */
SW_TEST_CASE( SceneAsyncTest, DocumentBinaryRoundTrip )
{
    const sw::string binPath = test::makeTempPath( "sw_test_scene_desc.bin" );

    sw::SceneDocument originalDoc{};
    originalDoc._name = "BinaryTestScene";
    sw::SceneDocument::EntityNode entA{};
    entA._name        = "Hero";
    entA._prefab      = "game/empty/prefabs/hero.prefab";
    entA._embeddedXml = "<GameObjectState><Name>Hero</Name></GameObjectState>";
    originalDoc._listEntityNode.push_back( std::move( entA ) );

    sw::SceneDocument::EntityNode entB{};
    entB._name   = "Monster";
    entB._prefab = "game/empty/prefabs/monster.prefab";
    originalDoc._listEntityNode.push_back( std::move( entB ) );

    SW_ASSERT_TRUE( originalDoc.saveBinary( binPath ) );

    sw::SceneDocument loadedDoc{};
    SW_ASSERT_TRUE( loadedDoc.loadBinary( binPath ) );
    SW_EXPECT_TRUE( loadedDoc._bValid );
    SW_EXPECT_STREQ( "BinaryTestScene", loadedDoc._name );
    SW_ASSERT_EQUAL( size_t( 2 ), loadedDoc._listEntityNode.size() );
    SW_EXPECT_STREQ( "Hero", loadedDoc._listEntityNode[0]._name );
    SW_EXPECT_STREQ( "game/empty/prefabs/hero.prefab", loadedDoc._listEntityNode[0]._prefab );
    SW_EXPECT_STREQ( "<GameObjectState><Name>Hero</Name></GameObjectState>", loadedDoc._listEntityNode[0]._embeddedXml );
    SW_EXPECT_STREQ( "Monster", loadedDoc._listEntityNode[1]._name );
    SW_EXPECT_STREQ( "game/empty/prefabs/monster.prefab", loadedDoc._listEntityNode[1]._prefab );

    sw::FileUtil::removeFile( binPath );
}

/**
 * @brief [SceneAsyncTest] 진행 중 비동기 로드는 최신 대기 경로로 대체된다
 */
SW_TEST_CASE( SceneAsyncTest, AsyncWarpSequenceQueuesLatest )
{
    const sw::string townA = test::makeTempPath( "sw_test_warp_a.xml" );
    const sw::string binA  = test::makeTempPath( "sw_test_warp_a.bin" );
    const sw::string townB = test::makeTempPath( "sw_test_warp_b.xml" );
    const sw::string binB  = test::makeTempPath( "sw_test_warp_b.bin" );
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
 * @brief [SceneAsyncTest] 비동기 스왑이 이전 활성 씬을 언로드한다(createScene 잔여 포함)
 */
SW_TEST_CASE( SceneAsyncTest, AsyncSwapUnloadsPreviousActive )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_replace.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_replace.bin" );
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
 * @brief [SceneAsyncTest] 연속된 비동기 로드 요청 및 큐잉된 씬 우선 교체 검증
 */
SW_TEST_CASE( SceneAsyncTest, SceneAsyncLoadCancellationAndRecovery )
{
    const sw::string scenePath1 = test::makeTempPath( "test_rapid_1.scene.xml" );
    const sw::string binPath1   = test::makeTempPath( "test_rapid_1.scene.bin" );
    const sw::string scenePath2 = test::makeTempPath( "test_rapid_2.scene.xml" );
    const sw::string binPath2   = test::makeTempPath( "test_rapid_2.scene.bin" );

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
 * @brief [SceneAsyncTest] TaskFuture 기반 비동기 씬 로드 및 Fluent 체이닝 검증
 */
SW_TEST_CASE( SceneAsyncTest, RequestLoadFutureChaining )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_future_scene.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_future_scene.bin" );
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

    bool                       bContinuationInvoked = false;
    sw::string                 loadedSceneName{};
    sw::TaskFuture<sw::Scene*> future = manager.requestLoadFuture( xmlPath );
    SW_ASSERT_TRUE( future.isValid() );
    SW_EXPECT_FALSE( future.isReady() );

    // .then() 모나딕 체이닝 연결
    future.then( [&bContinuationInvoked, &loadedSceneName]( sw::Scene* pScene )
    {
        if ( pScene != nullptr )
        {
            bContinuationInvoked = true;
            loadedSceneName      = pScene->getName();
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
 * @brief [SceneAsyncTest] 연속 비동기 씬 Future 발행 및 취소·복구 스트레스 테스트
 */
SW_TEST_CASE( SceneAsyncTest, RapidConcurrentFutureLoadsAndCancellationsStress )
{
    sw::vector<sw::string> listXmlPath;
    sw::vector<sw::string> listBinPath;

    for ( int32 index = 0; index < 3; ++index )
    {
        const sw::string name    = sw::string( "StressScene_" ) + sw::string( std::to_string( index ).c_str() );
        const sw::string xmlPath = test::makeTempPath( ( name + ".xml" ).c_str() );
        const sw::string binPath = test::makeTempPath( ( name + ".bin" ).c_str() );

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

/**
 * @brief [SceneAsyncTest] 대기열에 들어간 요청도 자기 씬을 받는다
 * @details 이미 로드가 도는 중에 다시 요청하면 그 요청은 대기열로 간다. 그런데 돌려주던
 *          future 는 **도는 중인 로드의 것**이었고, `tickTransitions` 는 대기열 때문에 그
 *          로드를 버리면서 같은 약속에 nullptr 을 넣었다 — 그래서 대기열에 넣은 쪽은
 *          자기 씬이 멀쩡히 활성이 되는데도 "실패" 를 받았다. 그리고 세 번째 요청이 오면
 *          두 번째는 `_queuedPath` 가 덮이면서 **아무 통지도 없이** 사라졌다.
 */
SW_TEST_CASE( SceneAsyncTest, QueuedRequestGetsItsOwnScene )
{
    const sw::string pathA = test::makeTempPath( "sw_test_queued_a.xml" );
    const sw::string binA  = test::makeTempPath( "sw_test_queued_a.bin" );
    const sw::string pathB = test::makeTempPath( "sw_test_queued_b.xml" );
    const sw::string binB  = test::makeTempPath( "sw_test_queued_b.bin" );
    const sw::string pathC = test::makeTempPath( "sw_test_queued_c.xml" );
    const sw::string binC  = test::makeTempPath( "sw_test_queued_c.bin" );

    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [=]()
    {
        for ( const sw::string& p : { pathA, binA, pathB, binB, pathC, binC } )
            sw::FileUtil::removeFile( p );
    } ) );

    for ( const auto& nameAndPath : {
              sw::pair<const utf8*, const sw::string*>{"QueuedA", &binA},
              sw::pair<const utf8*, const sw::string*>{"QueuedB", &binB},
              sw::pair<const utf8*, const sw::string*>{"QueuedC", &binC}
    } )
    {
        sw::SceneDocument doc{};
        doc._name = nameAndPath.first;
        sw::SceneDocument::EntityNode node{};
        node._name = "Root";
        doc._listEntityNode.push_back( std::move( node ) );
        SW_ASSERT_TRUE( doc.saveBinary( *nameAndPath.second ) );
    }

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    // A 가 돌고, B 가 대기열로, C 가 B 를 밀어낸다.
    sw::TaskFuture<sw::Scene*> futA = manager.requestLoadFuture( pathA );
    sw::TaskFuture<sw::Scene*> futB = manager.requestLoadFuture( pathB );
    sw::TaskFuture<sw::Scene*> futC = manager.requestLoadFuture( pathC );
    SW_ASSERT_TRUE( futA.isValid() );
    SW_ASSERT_TRUE( futB.isValid() );
    SW_ASSERT_TRUE( futC.isValid() );

    sw::drainSceneTransitions( manager );

    SW_ASSERT_NOT_NULL( manager.getActiveScene() );
    SW_EXPECT_STREQ( "QueuedC", manager.getActiveScene()->getName() );

    // 셋 다 답을 받아야 한다 — 끝나지 않는 future 를 쥐고 있는 요청자는 없다.
    SW_ASSERT_TRUE( futA.isReady() );
    SW_ASSERT_TRUE( futB.isReady() );
    SW_ASSERT_TRUE( futC.isReady() );

    // A 와 B 는 밀려났으므로 nullptr, C 만 자기 씬을 받는다.
    SW_EXPECT_NULL( futA.get() );
    SW_EXPECT_NULL( futB.get() );
    SW_EXPECT_EQUAL( manager.getActiveScene(), futC.get() );

    manager.shutdown();
}
