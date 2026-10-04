#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Scene/SceneDocument.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

namespace sw
{
    /** @brief 씬을 짓는 **동안** 올라오는 모듈의 컴포넌트입니다. 로드를 띄울 때는 표에 없습니다. */
    class LateModuleComponent : public Component
    {
    public:
        REFLECT_BODY();
        const TypeInfo* getTypeInfo() const override { return StaticType(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 같은 경로가 대기열에 있는 경우를 따로 보는 둘째 모듈의 컴포넌트입니다(배포 구성은 타입을 내리지 못해 이름을 나눈다). */
    class LateQueuedModuleComponent : public Component
    {
    public:
        REFLECT_BODY();
        const TypeInfo* getTypeInfo() const override { return StaticType(); }
    };

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

        /** @brief 모듈의 코드젠이 만드는 것과 같은 모양의 TypeInfo(이름 · 크기 · 생성 함수)입니다. 표에 올리지는 않습니다. */
        TypeInfo makeLateTypeInfo( const utf8* pName, const utf8* pFqn, size_t size, Component* ( *addComponent )(GameObject*))
        {
            TypeInfo info{};
            info._name               = hashed_string( pName );
            info._fullyQualifiedName = hashed_string( pFqn );
            info._size               = size;
            info._addComponent       = addComponent;
            return info;
        }

        /** @brief 씬 로드 도중에 올라오는 모듈의 타입(생성 함수 포함)을 표에 올립니다. 모듈 DLL 의 TypeRegistrar 사슬과 같은 경로입니다. */
        void registerLateModuleTypeInternal( TypeRegistry& registry )
        {
            registry.registerClass( *LateModuleComponent::StaticType() );
        }

        void registerLateQueuedModuleTypeInternal( TypeRegistry& registry )
        {
            registry.registerClass( *LateQueuedModuleComponent::StaticType() );
        }

        /** @brief 이름 하나짜리 컴포넌트를 든 씬 XML 을 쓰고 옆에 구운 바이너리도 둡니다(배포 구성은 바이너리만 읽는다). 성공하면 true 입니다. */
        bool writeSceneWithComponent( const string& xmlPath, const string& binPath, const utf8* pSceneName, const utf8* pComponentName )
        {
            string xmlStr = "<Scene formatVersion=\"1\" name=\"";
            xmlStr += pSceneName;
            xmlStr += "\">\n  <entities>\n    <entity id=\"1\" name=\"Speaker\">\n      <GameObject _schemaVersion=\"0\" _name=\"Speaker\" _bActive=\"true\">\n"
                      "        <_listComponent>\n          <";
            xmlStr += pComponentName;
            xmlStr += " />\n        </_listComponent>\n      </GameObject>\n    </entity>\n  </entities>\n</Scene>\n";
            if ( FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlStr.data() ), static_cast<uint64>( xmlStr.size() ) ) == false )
                return false;
            SceneDocument cooked{};
            return cooked.loadXml( xmlPath ) && cooked.saveBinary( binPath );
        }
    } // namespace

    const TypeInfo* LateModuleComponent::StaticType()
    {
        static const TypeInfo s_info = makeLateTypeInfo( "LateModuleComponent", "sw::LateModuleComponent", sizeof( LateModuleComponent ),
                                                         &GameObject::addComponentTo<LateModuleComponent> );
        return &s_info;
    }

    const TypeInfo* LateQueuedModuleComponent::StaticType()
    {
        static const TypeInfo s_info = makeLateTypeInfo( "LateQueuedModuleComponent", "sw::LateQueuedModuleComponent", sizeof( LateQueuedModuleComponent ),
                                                         &GameObject::addComponentTo<LateQueuedModuleComponent> );
        return &s_info;
    }
} // namespace sw

// ------------------------------------------------------------------------------
// 1) SceneTest — 활성 씬·비동기 로드
// ------------------------------------------------------------------------------
/**
 * @brief [SceneAsyncTest] 비동기 요청 완료
 */
SW_TEST_CASE( SceneAsyncTest, AsyncRequestCompletes )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_async.scene.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_async.scene.bin" );
    const sw::string xmlStr =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Scene formatVersion=\"1\" name=\"AsyncTown\">\n"
        "  <entities>\n"
        "    <entity id=\"101\" name=\"PlayerSpawn\"/>\n"
        "    <entity id=\"102\" name=\"Npc\"/>\n"
        "  </entities>\n"
        "</Scene>\n";

    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
                                             reinterpret_cast<const uint8*>( xmlStr.data() ),
                                             static_cast<uint64>( xmlStr.size() ) ) );

    sw::SceneDocument doc{};
    doc._name = "AsyncTown";
    sw::SceneDocument::SceneObjectNode entA{};
    entA._name   = "PlayerSpawn";
    entA._fileId = 1;
    doc._listSceneObjectNode.push_back( std::move( entA ) );
    sw::SceneDocument::SceneObjectNode entB{};
    entB._name   = "Npc";
    entB._fileId = 2;
    doc._listSceneObjectNode.push_back( std::move( entB ) );
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
}

/**
 * @brief [SceneAsyncTest] 들어오는 씬에 같은 이름의 오브젝트가 있어도 옮겨 심은 영속 루트의 자식은 제 부모(소켓)에 붙는다
 * @details 옮겨 심을 때 다른 오브젝트로의 부착을 **컴포넌트 id** 로 되붙인다. 이름(`_attachOwner`)으로 찾으면 들어오는 씬의 같은 이름 오브젝트에
 *          붙거나(그쪽에는 그 소켓이 없다) 떨어진다 — 새 매니저가 옮겨 온 루트의 이름을 유일하게 바꾸기 때문이다.
 */
SW_TEST_CASE( SceneAsyncTest, CarriedChildKeepsItsParentWhenTheNextSceneHasTheSameName )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_carry.scene.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_carry.scene.bin" );
    const sw::string xmlStr  = "<Scene formatVersion=\"1\" name=\"Dungeon\"><entities><entity id=\"103\" name=\"MusicPlayer\"/></entities></Scene>";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlStr.data() ), static_cast<uint64>( xmlStr.size() ) ) );
    sw::SceneDocument doc{};
    doc._name = "Dungeon";
    sw::SceneDocument::SceneObjectNode decoy{};
    decoy._name   = "MusicPlayer";
    decoy._fileId = 3;
    doc._listSceneObjectNode.push_back( std::move( decoy ) );
    SW_ASSERT_TRUE( doc.saveBinary( binPath ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    manager.setWorldPlaying( true );
    sw::Scene* pTown = manager.createEmptyActiveScene( "Town" );
    SW_ASSERT_NOT_NULL( pTown );
    sw::GameObjectManager* pObjects = pTown->getObjectManager();
    sw::GameObject*        pKeeper  = pObjects->createGameObject( sw::hashed_string( "MusicPlayer" ) );
    sw::SceneComponent*    pRoot    = pKeeper->addComponent<sw::SceneComponent>();
    sw::SceneComponent*    pMount   = pKeeper->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRoot );
    SW_ASSERT_NOT_NULL( pMount );
    SW_ASSERT_TRUE( pMount->attachToComponent( pRoot ) );
    sw::GameObject*     pSpeaker      = pObjects->createGameObject( sw::hashed_string( "Speaker" ) );
    sw::SceneComponent* pSpeakerScene = pSpeaker->addComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pSpeakerScene );
    SW_ASSERT_TRUE( pSpeakerScene->attachToComponent( pMount ) );
    manager.markPersistent( pKeeper );
    const sw::ComponentHandle mountHandle   = pMount->getHandle();
    const sw::ComponentHandle speakerHandle = pSpeakerScene->getHandle();

    SW_ASSERT_TRUE( manager.requestLoadAsync( xmlPath ) );
    sw::drainSceneTransitions( manager );
    sw::Scene* pDungeon = manager.getActiveScene();
    SW_ASSERT_NOT_NULL( pDungeon );
    SW_EXPECT_STREQ( "Dungeon", pDungeon->getName() );
    sw::GameObjectManager* pNext           = pDungeon->getObjectManager();
    sw::Component*         pCarriedMount   = pNext->resolveComponent( mountHandle );
    auto*                  pCarriedSpeaker = static_cast<sw::SceneComponent*>( pNext->resolveComponent( speakerHandle ) );
    SW_ASSERT_NOT_NULL( pCarriedMount );
    SW_ASSERT_NOT_NULL( pCarriedSpeaker );
    SW_EXPECT_TRUE( pCarriedSpeaker->getParent() == pCarriedMount );

    manager.setWorldPlaying( false );
    manager.shutdown();
}

/**
 * @brief [SceneAsyncTest] GPU 없이 SceneDocument 로드
 */
SW_TEST_CASE( SceneAsyncTest, DocumentLoadWithoutGpu )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_desc.scene.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_desc.scene.bin" );
    const sw::string xmlStr =
        "<Scene formatVersion=\"1\" name=\"DescOnly\"><entities><entity id=\"1\" name=\"A\"/></entities></Scene>";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
                                             reinterpret_cast<const uint8*>( xmlStr.data() ),
                                             static_cast<uint64>( xmlStr.size() ) ) );

    sw::SceneDocument docSetup{};
    docSetup._name = "DescOnly";
    sw::SceneDocument::SceneObjectNode entA{};
    entA._name   = "A";
    entA._fileId = 4;
    docSetup._listSceneObjectNode.push_back( std::move( entA ) );
    SW_ASSERT_TRUE( docSetup.saveBinary( binPath ) );

    sw::SceneDocument doc{};
    SW_ASSERT_TRUE( doc.load( xmlPath ) );
    SW_EXPECT_TRUE( doc._bValid );
    SW_EXPECT_STREQ( "DescOnly", doc._name );
    SW_EXPECT_EQUAL( size_t( 1 ), doc._listSceneObjectNode.size() );
}

/**
 * @brief [SceneAsyncTest] SceneDocument 바이너리(SCN1) 직렬화 및 역직렬화 왕복 테스트
 */
SW_TEST_CASE( SceneAsyncTest, DocumentBinaryRoundTrip )
{
    const sw::string binPath = test::makeTempPath( "sw_test_scene_desc.scene.bin" );

    sw::SceneDocument originalDoc{};
    originalDoc._name = "BinaryTestScene";
    sw::SceneDocument::SceneObjectNode entA{};
    entA._name        = "Hero";
    entA._fileId      = 5;
    entA._prefab      = "game/empty/prefabs/hero.prefab";
    entA._embeddedXml = "<GameObjectState><Name>Hero</Name></GameObjectState>";
    originalDoc._listSceneObjectNode.push_back( std::move( entA ) );

    sw::SceneDocument::SceneObjectNode entB{};
    entB._name   = "Monster";
    entB._fileId = 6;
    entB._prefab = "game/empty/prefabs/monster.prefab";
    originalDoc._listSceneObjectNode.push_back( std::move( entB ) );

    SW_ASSERT_TRUE( originalDoc.saveBinary( binPath ) );

    sw::SceneDocument loadedDoc{};
    SW_ASSERT_TRUE( loadedDoc.loadBinary( binPath ) );
    SW_EXPECT_TRUE( loadedDoc._bValid );
    SW_EXPECT_STREQ( "BinaryTestScene", loadedDoc._name );
    SW_ASSERT_EQUAL( size_t( 2 ), loadedDoc._listSceneObjectNode.size() );
    SW_EXPECT_STREQ( "Hero", loadedDoc._listSceneObjectNode[0]._name );
    SW_EXPECT_STREQ( "game/empty/prefabs/hero.prefab", loadedDoc._listSceneObjectNode[0]._prefab );
    SW_EXPECT_STREQ( "<GameObjectState><Name>Hero</Name></GameObjectState>", loadedDoc._listSceneObjectNode[0]._embeddedXml );
    SW_EXPECT_STREQ( "Monster", loadedDoc._listSceneObjectNode[1]._name );
    SW_EXPECT_STREQ( "game/empty/prefabs/monster.prefab", loadedDoc._listSceneObjectNode[1]._prefab );
}

/**
 * @brief [SceneAsyncTest] 진행 중 비동기 로드는 최신 대기 경로로 대체된다
 */
SW_TEST_CASE( SceneAsyncTest, AsyncWarpSequenceQueuesLatest )
{
    const sw::string townA = test::makeTempPath( "sw_test_warp_a.scene.xml" );
    const sw::string binA  = test::makeTempPath( "sw_test_warp_a.scene.bin" );
    const sw::string townB = test::makeTempPath( "sw_test_warp_b.scene.xml" );
    const sw::string binB  = test::makeTempPath( "sw_test_warp_b.scene.bin" );
    const sw::string xmlA =
        "<Scene formatVersion=\"1\" name=\"TownA\"><entities><entity id=\"1\" name=\"A\"/></entities></Scene>";
    const sw::string xmlB =
        "<Scene formatVersion=\"1\" name=\"TownB\"><entities><entity id=\"104\" name=\"B1\"/><entity id=\"105\" name=\"B2\"/></entities></Scene>";

    SW_ASSERT_TRUE( sw::FileUtil::writeFile( townA,
                                             reinterpret_cast<const uint8*>( xmlA.data() ),
                                             static_cast<uint64>( xmlA.size() ) ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( townB,
                                             reinterpret_cast<const uint8*>( xmlB.data() ),
                                             static_cast<uint64>( xmlB.size() ) ) );

    sw::SceneDocument docA{};
    docA._name = "TownA";
    sw::SceneDocument::SceneObjectNode entA{};
    entA._name   = "A";
    entA._fileId = 7;
    docA._listSceneObjectNode.push_back( std::move( entA ) );
    SW_ASSERT_TRUE( docA.saveBinary( binA ) );

    sw::SceneDocument docB{};
    docB._name = "TownB";
    sw::SceneDocument::SceneObjectNode entB1{};
    entB1._name   = "B1";
    entB1._fileId = 8;
    docB._listSceneObjectNode.push_back( std::move( entB1 ) );
    sw::SceneDocument::SceneObjectNode entB2{};
    entB2._name   = "B2";
    entB2._fileId = 9;
    docB._listSceneObjectNode.push_back( std::move( entB2 ) );
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
}

/**
 * @brief [SceneAsyncTest] 비동기 스왑이 이전 활성 씬을 언로드한다(createScene 잔여 포함)
 */
SW_TEST_CASE( SceneAsyncTest, AsyncSwapUnloadsPreviousActive )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_replace.scene.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_replace.scene.bin" );
    const sw::string xmlStr =
        "<Scene formatVersion=\"1\" name=\"Replaced\"><entities><entity id=\"106\" name=\"Only\"/></entities></Scene>";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
                                             reinterpret_cast<const uint8*>( xmlStr.data() ),
                                             static_cast<uint64>( xmlStr.size() ) ) );

    sw::SceneDocument doc{};
    doc._name = "Replaced";
    sw::SceneDocument::SceneObjectNode ent{};
    ent._name   = "Only";
    ent._fileId = 10;
    doc._listSceneObjectNode.push_back( std::move( ent ) );
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
        "<Scene formatVersion=\"1\" name=\"SceneFirst\"><entities><entity id=\"107\" name=\"E1\"/></entities></Scene>";
    const sw::string xmlStr2 =
        "<Scene formatVersion=\"1\" name=\"SceneSecond\"><entities><entity id=\"108\" name=\"E2\"/></entities></Scene>";

    SW_ASSERT_TRUE( sw::FileUtil::writeFile( scenePath1, reinterpret_cast<const uint8*>( xmlStr1.data() ), xmlStr1.size() ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( scenePath2, reinterpret_cast<const uint8*>( xmlStr2.data() ), xmlStr2.size() ) );

    sw::SceneDocument doc1{};
    doc1._name = "SceneFirst";
    sw::SceneDocument::SceneObjectNode ent1{};
    ent1._name   = "E1";
    ent1._fileId = 11;
    doc1._listSceneObjectNode.push_back( std::move( ent1 ) );
    SW_ASSERT_TRUE( doc1.saveBinary( binPath1 ) );

    sw::SceneDocument doc2{};
    doc2._name = "SceneSecond";
    sw::SceneDocument::SceneObjectNode ent2{};
    ent2._name   = "E2";
    ent2._fileId = 12;
    doc2._listSceneObjectNode.push_back( std::move( ent2 ) );
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
}

/**
 * @brief [SceneAsyncTest] TaskFuture 기반 비동기 씬 로드 및 Fluent 체이닝 검증
 */
SW_TEST_CASE( SceneAsyncTest, RequestLoadFutureChaining )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_future_scene.scene.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_future_scene.scene.bin" );
    const sw::string xmlStr =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<Scene formatVersion=\"1\" name=\"FutureTown\">\n"
        "  <entities>\n"
        "    <entity id=\"109\" name=\"Hero\"/>\n"
        "  </entities>\n"
        "</Scene>\n";

    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath,
                                             reinterpret_cast<const uint8*>( xmlStr.data() ),
                                             static_cast<uint64>( xmlStr.size() ) ) );

    sw::SceneDocument doc{};
    doc._name = "FutureTown";
    sw::SceneDocument::SceneObjectNode ent{};
    ent._name   = "Hero";
    ent._fileId = 13;
    doc._listSceneObjectNode.push_back( std::move( ent ) );
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
        const sw::string xmlPath = test::makeTempPath( ( name + ".scene.xml" ).c_str() );
        const sw::string binPath = test::makeTempPath( ( name + ".scene.bin" ).c_str() );

        const sw::string xmlStr = "<Scene formatVersion=\"1\" name=\"" + name + "\"><entities><entity id=\"110\" name=\"E\"/></entities></Scene>";
        SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlStr.data() ), xmlStr.size() ) );

        sw::SceneDocument doc{};
        doc._name = name;
        sw::SceneDocument::SceneObjectNode ent{};
        ent._name   = "E";
        ent._fileId = 14;
        doc._listSceneObjectNode.push_back( std::move( ent ) );
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
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( listXmlPath[index] ) );
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( listBinPath[index] ) );
    }
}

/**
 * @brief [SceneAsyncTest] 대기열에 들어간 요청도 자기 씬을 받는다
 * @details 이미 로드가 도는 중에 다시 요청하면 그 요청은 대기열로 간다. 돌려주는 future 가 **도는 중인 로드의 것**이면
 *          `tickTransitions` 가 대기열 때문에 그 로드를 버리면서 같은 약속에 nullptr 을 넣어, 대기열에 넣은 쪽은 자기 씬이
 *          멀쩡히 활성이 되는데도 "실패" 를 받는다. 세 번째 요청이 와서 `_queuedPath` 가 덮이면 두 번째는 **아무 통지도 없이**
 *          사라지면 안 된다.
 */
SW_TEST_CASE( SceneAsyncTest, QueuedRequestGetsItsOwnScene )
{
    const sw::string pathA = test::makeTempPath( "sw_test_queued_a.scene.xml" );
    const sw::string binA  = test::makeTempPath( "sw_test_queued_a.scene.bin" );
    const sw::string pathB = test::makeTempPath( "sw_test_queued_b.scene.xml" );
    const sw::string binB  = test::makeTempPath( "sw_test_queued_b.scene.bin" );
    const sw::string pathC = test::makeTempPath( "sw_test_queued_c.scene.xml" );
    const sw::string binC  = test::makeTempPath( "sw_test_queued_c.scene.bin" );

    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [=]()
    {
        for ( const sw::string& p : { pathA, binA, pathB, binB, pathC, binC } )
            SW_EXPECT_TRUE( sw::FileUtil::removeFile( p ) );
    } ) );

    for ( const auto& nameAndPath : {
              sw::pair<const utf8*, const sw::string*>{"QueuedA", &binA},
              sw::pair<const utf8*, const sw::string*>{"QueuedB", &binB},
              sw::pair<const utf8*, const sw::string*>{"QueuedC", &binC}
    } )
    {
        sw::SceneDocument doc{};
        doc._name = nameAndPath.first;
        sw::SceneDocument::SceneObjectNode node{};
        node._name   = "Root";
        node._fileId = 15;
        doc._listSceneObjectNode.push_back( std::move( node ) );
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

/**
 * @brief [SceneAsyncTest] 씬을 짓는 동안 모듈 타입이 올라오면 그 씬을 다시 짓는다 — 새 모듈의 컴포넌트가 빠지지 않는다
 * @details 에디터가 시작 씬을 여는 동안 키트 · SWGame 이 올라오면, 워커는 그때의 타입 표로 씬을 지어 그 모듈의 컴포넌트를 만들지 못한다.
 *          그 씬을 그대로 쓰면 저장할 때 컴포넌트가 사라진다. 로드를 띄울 때와 끝날 때 타입 표 세대(`TypeRegistry::getGeneration`)를 견준다.
 */
SW_TEST_CASE( SceneAsyncTest, TypesRegisteredDuringLoadAreNotLost )
{
    static sw::TypeRegistrar* s_pLateHead{ nullptr };
    static sw::TypeRegistrar  s_lateRegistrar{ &sw::registerLateModuleTypeInternal, s_pLateHead };

    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_late_type.scene.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_late_type.scene.bin" );
    SW_ASSERT_TRUE( sw::writeSceneWithComponent( xmlPath, binPath, "LateType", "LateModuleComponent" ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    SW_ASSERT_TRUE( manager.requestLoadAsync( xmlPath ) );
    // 워커가 씬을 다 지은 뒤에(그 타입 없이) 모듈이 올라온다.
    sw::engine::getTaskManager().waitAll();
    sw::engine::getTypeRegistry().registerPendingTypes( "LateModule", s_pLateHead, nullptr );

    sw::drainSceneTransitions( manager );
    SW_ASSERT_NOT_NULL( manager.getActiveScene() );
    sw::GameObject* pSpeaker = manager.getActiveScene()->getObjectManager()->findGameObjectByName( sw::hashed_string( "Speaker" ) );
    SW_ASSERT_NOT_NULL( pSpeaker );
    SW_EXPECT_TRUE_MSG( pSpeaker->findComponentByTypeName( sw::hashed_string( "LateModuleComponent" ) ) != nullptr,
                        "로드 도중에 등록된 모듈의 컴포넌트가 씬에서 빠졌습니다" );

    manager.shutdown();
#if !defined( SW_SHIPPING )
    sw::engine::getTypeRegistry().unregisterTypesByModule( "LateModule" );
#endif
}

/**
 * @brief [SceneAsyncTest] 같은 씬이 대기열에 있어도, 짓는 동안 모듈 타입이 올라오면 다시 짓는다 — 두 요청자 모두 새 씬을 받는다
 * @details 같은 경로가 대기열에 있으면 그 결과를 그대로 활성으로 쓰는 길이 따로 있다. 그 길에서도 세대를 견줘야 새 모듈의 컴포넌트가 빠진
 *          씬이 남지 않는다 — 에디터가 시작 씬을 두 번 여는 흔한 순서다.
 */
SW_TEST_CASE( SceneAsyncTest, TypesRegisteredDuringLoadAreNotLostWithSamePathQueued )
{
    static sw::TypeRegistrar* s_pLateHead{ nullptr };
    static sw::TypeRegistrar  s_lateRegistrar{ &sw::registerLateQueuedModuleTypeInternal, s_pLateHead };

    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_late_type_queued.scene.xml" );
    const sw::string binPath = test::makeTempPath( "sw_test_scene_late_type_queued.scene.bin" );
    SW_ASSERT_TRUE( sw::writeSceneWithComponent( xmlPath, binPath, "LateTypeQueued", "LateQueuedModuleComponent" ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    sw::TaskFuture<sw::Scene*> first  = manager.requestLoadFuture( xmlPath );
    sw::TaskFuture<sw::Scene*> second = manager.requestLoadFuture( xmlPath ); // 같은 씬이 대기열로
    SW_ASSERT_TRUE( first.isValid() );
    SW_ASSERT_TRUE( second.isValid() );
    // 워커가 씬을 다 지은 뒤에(그 타입 없이) 모듈이 올라온다.
    sw::engine::getTaskManager().waitAll();
    sw::engine::getTypeRegistry().registerPendingTypes( "LateModuleQueued", s_pLateHead, nullptr );

    sw::drainSceneTransitions( manager );
    SW_ASSERT_NOT_NULL( manager.getActiveScene() );
    sw::GameObject* pSpeaker = manager.getActiveScene()->getObjectManager()->findGameObjectByName( sw::hashed_string( "Speaker" ) );
    SW_ASSERT_NOT_NULL( pSpeaker );
    SW_EXPECT_TRUE_MSG( pSpeaker->findComponentByTypeName( sw::hashed_string( "LateQueuedModuleComponent" ) ) != nullptr,
                        "같은 씬이 대기열에 있을 때 로드 도중에 등록된 모듈의 컴포넌트가 빠졌습니다" );
    SW_ASSERT_TRUE( first.isReady() );
    SW_ASSERT_TRUE( second.isReady() );
    SW_EXPECT_EQUAL( manager.getActiveScene(), first.get() );
    SW_EXPECT_EQUAL( manager.getActiveScene(), second.get() );

    manager.shutdown();
#if !defined( SW_SHIPPING )
    sw::engine::getTypeRegistry().unregisterTypesByModule( "LateModuleQueued" );
#endif
}

/**
 * @brief [SceneAsyncTest] 저장이 막혀 있으면 활성 씬을 저장하지 않고, 풀면 저장한다
 * @details 게임 모듈 리로드가 게임 컴포넌트를 걷어 낸 채 실패하면 호스트가 막는다. 그 상태로 저장하면 컴포넌트가 빠진 씬이 저장된다.
 */
SW_TEST_CASE( SceneAsyncTest, SaveIsRefusedWhileBlocked )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_save_block.scene.xml" );
    const sw::string xmlStr  = "<Scene formatVersion=\"1\" name=\"SaveBlock\"><entities><entity id=\"1\" name=\"A\"/></entities></Scene>";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlStr.data() ), static_cast<uint64>( xmlStr.size() ) ) );
    const sw::string  binPath = test::makeTempPath( "sw_test_scene_save_block.scene.bin" );
    sw::SceneDocument cooked{};
    SW_ASSERT_TRUE( cooked.loadXml( xmlPath ) );
    SW_ASSERT_TRUE( cooked.saveBinary( binPath ) );
    const sw::string savePath = test::makeTempPath( "sw_test_scene_save_block_out.scene.xml" );
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( savePath ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    SW_ASSERT_TRUE( manager.requestLoadAsync( xmlPath ) );
    sw::drainSceneTransitions( manager );
    SW_ASSERT_NOT_NULL( manager.getActiveScene() );

    manager.setSaveBlockReason( "test: components removed" );
    SW_EXPECT_TRUE( manager.isSaveBlocked() );
    SW_EXPECT_FALSE( manager.saveActiveScene( savePath ) );
    SW_EXPECT_FALSE( sw::FileUtil::fileExists( savePath ) );

    manager.setSaveBlockReason( {} );
    SW_EXPECT_FALSE( manager.isSaveBlocked() );
    SW_EXPECT_TRUE( manager.saveActiveScene( savePath ) );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( savePath ) );

    manager.shutdown();
}

/**
 * @brief [SceneAsyncTest] 씬은 쿠커가 굽는 이름으로만 저장된다 — 확장자 없는 이름은 `.scene.xml` 이 붙고, 경로가 전혀 없으면 쓰지 않는다
 * @details 저장 대화상자에 "level" 을 적은 이름을 그대로 저장하면 에디터에서는 열리는데 쿠커가 굽지 않아 배포본에 없다. 경로도 출처도 없는 씬에
 *          `Resource/` 밖의 이름을 지어내 쓰면 안 된다.
 */
SW_TEST_CASE( SceneAsyncTest, SavedSceneNamesAreCookable )
{
    const sw::string xmlPath = test::makeTempPath( "sw_test_scene_save_name.scene.xml" );
    const sw::string xmlStr  = "<Scene formatVersion=\"1\" name=\"SaveName\"><entities><entity id=\"1\" name=\"A\"/></entities></Scene>";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( xmlPath, reinterpret_cast<const uint8*>( xmlStr.data() ), static_cast<uint64>( xmlStr.size() ) ) );
    const sw::string  binPath = test::makeTempPath( "sw_test_scene_save_name.scene.bin" );
    sw::SceneDocument cooked{};
    SW_ASSERT_TRUE( cooked.loadXml( xmlPath ) );
    SW_ASSERT_TRUE( cooked.saveBinary( binPath ) );

    sw::SceneManager manager;
    SW_ASSERT_TRUE( manager.initialize() );
    SW_ASSERT_TRUE( manager.requestLoadAsync( xmlPath ) );
    sw::drainSceneTransitions( manager );
    SW_ASSERT_NOT_NULL( manager.getActiveScene() );

    const sw::string bareName = test::makeTempPath( "level" );
    {
        test::ScopedDefensiveTestLog expected( "a scene saved under a name the cooker does not bake" );
        SW_EXPECT_TRUE( manager.saveActiveScene( bareName ) );
    }
    SW_EXPECT_FALSE( sw::FileUtil::fileExists( bareName ) );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( bareName + ".scene.xml" ) );
    SW_EXPECT_STREQ( ( bareName + ".scene.xml" ).c_str(), manager.getActiveScene()->getSourcePath().c_str() ); // 다음 저장도 그 이름이다

    manager.getActiveScene()->setSourcePath( {} );
    {
        test::ScopedDefensiveTestLog expected( "a scene with no path at all" );
        SW_EXPECT_FALSE( manager.saveActiveScene( {} ) );
    }
    SW_EXPECT_FALSE( sw::FileUtil::fileExists( "Assets/Scenes/DefaultScene.scene" ) );

    manager.shutdown();
}
