#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/TagID.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorPlaySession.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/AssetFormat.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 지울 표시가 없는 같은 이름의 오브젝트 수입니다(지운 오브젝트는 프레임 끝까지 목록에 남는다). */
    size_t countLivePlayObjectsNamed( GameObjectManager& manager, const utf8* pName )
    {
        const hashed_string wanted( pName );
        size_t              count = 0;
        manager.forEachGameObject( [&count, wanted]( GameObject* pObj )
        {
            if ( pObj != nullptr && pObj->isPendingDestroy() == false && pObj->getName() == wanted )
                ++count;
        } );
        return count;
    }

    /** @brief 엔티티 하나가 있는 씬 파일을 씁니다(비동기 로드용). */
    sw::string writeQueuedPlaySceneFile( const utf8* pFileName, const utf8* pSceneName )
    {
        const sw::string path = test::makeTempPath( pFileName );
        const sw::string text = sw::string( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Scene formatVersion=\"1\" name=\"" ) + pSceneName +
                                "\">\n  <entities>\n    <entity id=\"101\" name=\"Boss\"/>\n  </entities>\n</Scene>\n";
        // 쿠킹한 바이너리도 쿠커와 같은 이름으로 둔다(`AssetCookPath`) — Shipping 은 바이너리 씬(.scene.bin)만 읽는다.
        sw::SceneDocument                  doc{};
        sw::SceneDocument::SceneObjectNode boss{};
        doc._name  = pSceneName;
        boss._name = "Boss";
        doc._listSceneObjectNode.push_back( std::move( boss ) );
        const bool bWritten = sw::FileUtil::writeTextFile( path, text ) && doc.saveBinary( sw::AssetCookPath::toCookedPath( path ) );
        return bWritten ? path : sw::string{};
    }

    /** @brief 에디터 프레임을 흉내 냅니다 — 태스크를 비우고, 끝난 로드를 들이고(tickTransitions), 플레이 세션을 갱신합니다. */
    void runEditorFramesUntil( SceneManager& sceneManager, PlaySessionData& data, bool ( *pfnDone )( const SceneManager&, const PlaySessionData& ) )
    {
        for ( int32 frame = 0; frame < 2000 && pfnDone( sceneManager, data ) == false; ++frame )
        {
            sw::engine::getTaskManager().waitAll();
            sceneManager.tickTransitions();
            EditorPlaySession::update( data );
            if ( pfnDone( sceneManager, data ) == false )
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
    }
} // namespace

/**
 * @brief [EditorPlaySessionTest] 플레이 중에 활성 씬이 바뀌면 Stop 이 편집하던 씬을 다시 세워 되돌린다 — 지금 씬에 섞지 않는다
 * @details 스냅샷은 Play 때의 활성 씬을 찍는다. Stop 이 그때 활성인 씬(플레이 중에 게임 코드가 연 다음 레벨)에 그대로 되돌리면 두 씬의
 *          오브젝트가 섞이고, 활성 씬은 그 레벨의 소스 경로를 든 채라 저장하면 **그 레벨 파일**을 편집하던 씬의 내용으로 덮어쓴다.
 *          다시 세운 씬에는 오브젝트가 원래 id 로 돌아오고, 씬이 매어 둔 프리팹 연결도 돌아와야 한다(저장하면 프리팹 참조로 나간다).
 */
SW_TEST_CASE( EditorPlaySessionTest, StopAfterSceneChangeRebuildsTheEditedScene )
{
    SceneManager sceneManager;
    Scene*       pEdited = sceneManager.createEmptyActiveScene( "EditedLevel" );
    SW_ASSERT_NOT_NULL( pEdited );
    pEdited->setSourcePath( "game/scenes/editedlevel.scene" );
    ScopedSceneManagerService scopedScene{ sceneManager };

    GameObject* pHero = pEdited->getObjectManager()->createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pHero );
    pEdited->getObjectManager()->mergePendingAdds();
    const uint64 heroId = pHero->getObjectId();
    pEdited->setEntityPrefabPath( heroId, "game/prefabs/hero.prefab" );

    PlaySessionData data;
    EditorPlaySession::captureSnapshot( data );
    SW_ASSERT_TRUE( data._listSnapshot.empty() == false );

    // 플레이 중에 게임 코드가 다음 레벨을 연다 — 편집하던 씬은 내려간다.
    Scene* pNext = sceneManager.createEmptyActiveScene( "NextLevel" );
    SW_ASSERT_NOT_NULL( pNext );
    pNext->setSourcePath( "game/scenes/nextlevel.scene" );
    SW_ASSERT_NOT_NULL( pNext->getObjectManager()->createGameObject( hashed_string( "Boss" ) ) );
    pNext->getObjectManager()->mergePendingAdds();

    EditorPlaySession::restoreSnapshot( data );

    Scene* pActive = sceneManager.getActiveScene();
    SW_ASSERT_NOT_NULL( pActive );
    // 저장하면 이 경로로 나간다 — 다음 레벨의 파일이면 그것을 편집하던 씬으로 덮어쓴다.
    SW_EXPECT_TRUE_MSG( pActive->getSourcePath() == "game/scenes/editedlevel.scene", pActive->getSourcePath().c_str() );
    SW_EXPECT_TRUE_MSG( pActive->getName() == "EditedLevel", pActive->getName().c_str() );

    GameObjectManager* pObjects = pActive->getObjectManager();
    SW_ASSERT_NOT_NULL( pObjects );
    pObjects->processDeferredDestruction();
    pObjects->mergePendingAdds();
    GameObject* pRestoredHero = pObjects->findGameObjectById( heroId );
    SW_ASSERT_NOT_NULL( pRestoredHero );
    SW_EXPECT_TRUE( pRestoredHero->getName() == hashed_string( "Hero" ) );
    SW_EXPECT_EQUAL( size_t( 0 ), countLivePlayObjectsNamed( *pObjects, "Boss" ) );
    SW_EXPECT_TRUE( pActive->getEntityPrefabPath( heroId ) == "game/prefabs/hero.prefab" );
}

/**
 * @brief [EditorPlaySessionTest] 씬이 그대로면 Stop 은 같은 씬에 되돌린다 — 플레이 중에 생긴 것은 지우고 지운 것은 원래 id 로 되살린다
 * @details 위 케이스의 대조군이다. 씬을 다시 세우는 길은 활성 씬이 **바뀌었을 때만** 타야 한다(세대로 가른다).
 */
SW_TEST_CASE( EditorPlaySessionTest, StopWithoutSceneChangeRestoresInPlace )
{
    SceneManager sceneManager;
    Scene*       pEdited = sceneManager.createEmptyActiveScene( "EditedLevel" );
    SW_ASSERT_NOT_NULL( pEdited );
    ScopedSceneManagerService scopedScene{ sceneManager };

    GameObjectManager* pObjects = pEdited->getObjectManager();
    GameObject*        pHero    = pObjects->createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pHero );
    pObjects->mergePendingAdds();
    const uint64 heroId = pHero->getObjectId();

    PlaySessionData data;
    EditorPlaySession::captureSnapshot( data );

    // 플레이 중: 영웅이 죽고 총알이 생긴다.
    pObjects->destroyObject( pHero );
    SW_ASSERT_NOT_NULL( pObjects->createGameObject( hashed_string( "Bullet" ) ) );
    pObjects->processDeferredDestruction();
    pObjects->mergePendingAdds();

    EditorPlaySession::restoreSnapshot( data );

    SW_EXPECT_TRUE_MSG( sceneManager.getActiveScene() == pEdited, "씬이 그대로인데 다시 세웠습니다" );
    pObjects->processDeferredDestruction();
    pObjects->mergePendingAdds();
    SW_EXPECT_NOT_NULL( pObjects->findGameObjectById( heroId ) );
    SW_EXPECT_EQUAL( size_t( 0 ), countLivePlayObjectsNamed( *pObjects, "Bullet" ) );
}

/**
 * @brief [EditorPlaySessionTest] 플레이 중에 부모와 자식이 모두 지워져도 Stop 이 계층째 되살린다 — 자식이 부모보다 먼저 되살아나도
 * @details 되살리기는 스냅샷 순서(만든 순서)대로 오브젝트를 읽는다. 읽을 때 부모를 붙이면 자식을 먼저 만든 경우 자식을 읽는 순간 부모가 아직
 *          없어 루트로 남는다. 그래서 씬 로드(`Scene::instantiate`)와 같이, 모두 읽은 뒤 계층을 다시 잇는다.
 */
SW_TEST_CASE( EditorPlaySessionTest, StopRestoresAHierarchyWhoseChildWasCreatedFirst )
{
    SceneManager sceneManager;
    Scene*       pEdited = sceneManager.createEmptyActiveScene( "EditedLevel" );
    SW_ASSERT_NOT_NULL( pEdited );
    ScopedSceneManagerService scopedScene{ sceneManager };

    GameObjectManager* pObjects = pEdited->getObjectManager();
    GameObject*        pChild   = pObjects->createGameObject( hashed_string( "Rider" ) );
    GameObject*        pParent  = pObjects->createGameObject( hashed_string( "Horse" ) );
    SW_ASSERT_NOT_NULL( pChild->addComponent<SceneComponent>() );
    SW_ASSERT_NOT_NULL( pParent->addComponent<SceneComponent>() );
    SW_ASSERT_TRUE( pChild->attachToParent( pParent ) );
    pObjects->mergePendingAdds();
    const uint64 childId  = pChild->getObjectId();
    const uint64 parentId = pParent->getObjectId();

    PlaySessionData data;
    EditorPlaySession::captureSnapshot( data );

    // 플레이 중: 둘 다 지워진다.
    pObjects->destroyObject( pParent, true );
    pObjects->processDeferredDestruction();
    pObjects->mergePendingAdds();
    SW_ASSERT_TRUE( pObjects->findGameObjectById( childId ) == nullptr );

    EditorPlaySession::restoreSnapshot( data );
    pObjects->processDeferredDestruction();
    pObjects->mergePendingAdds();

    GameObject* pRestoredChild  = pObjects->findGameObjectById( childId );
    GameObject* pRestoredParent = pObjects->findGameObjectById( parentId );
    SW_ASSERT_NOT_NULL( pRestoredChild );
    SW_ASSERT_NOT_NULL( pRestoredParent );
    SW_EXPECT_TRUE_MSG( pRestoredChild->getParent() == pRestoredParent, "자식이 부모보다 먼저 되살아나 루트로 남았습니다" );
}

/**
 * @brief [EditorPlaySessionTest] 플레이 중에 이름을 서로 바꿔도 Stop 이 자식을 원래 부모에 붙이고 이름도 되돌린다
 * @details 되살리기가 부모를 이름으로 찾으면, 플레이 중 A("Left") 가 "Tmp" 로, B("Right") 가 "Left" 로 바뀐 경우 A 를 되돌리는 순간 "Left" 는
 *          아직 B 의 것이라 A 는 `Left_2` 가 되고, A 의 자식은 이름 "Left" 로 **B** 를 찾아 붙는다. 부모는 원래 id 로 찾고, 모두 읽은 뒤 비어 있는
 *          저장된 이름을 되찾는다.
 */
SW_TEST_CASE( EditorPlaySessionTest, StopRestoresParentsByIdAfterPlayRenames )
{
    SceneManager sceneManager;
    Scene*       pEdited = sceneManager.createEmptyActiveScene( "RenamedLevel" );
    SW_ASSERT_NOT_NULL( pEdited );
    ScopedSceneManagerService scopedScene{ sceneManager };

    GameObjectManager* pObjects = pEdited->getObjectManager();
    GameObject*        pLeft    = pObjects->createGameObject( hashed_string( "Left" ) );
    GameObject*        pChild   = pObjects->createGameObject( hashed_string( "LeftHand" ) );
    GameObject*        pRight   = pObjects->createGameObject( hashed_string( "Right" ) );
    for ( GameObject* pObj : { pLeft, pChild, pRight } )
        SW_ASSERT_NOT_NULL( pObj->addComponent<SceneComponent>() );
    SW_ASSERT_TRUE( pChild->attachToParent( pLeft ) );
    pObjects->mergePendingAdds();
    const uint64 leftId  = pLeft->getObjectId();
    const uint64 childId = pChild->getObjectId();
    const uint64 rightId = pRight->getObjectId();

    PlaySessionData data;
    EditorPlaySession::captureSnapshot( data );

    // 플레이 중: 이름을 서로 바꾸고, 자식을 떼어 오른쪽에 붙인다.
    pLeft->setName( hashed_string( "Tmp" ) );
    pRight->setName( hashed_string( "Left" ) );
    SW_ASSERT_TRUE( pChild->attachToParent( pRight ) );

    EditorPlaySession::restoreSnapshot( data );
    pObjects->processDeferredDestruction();
    pObjects->mergePendingAdds();

    GameObject* pRestoredLeft  = pObjects->findGameObjectById( leftId );
    GameObject* pRestoredChild = pObjects->findGameObjectById( childId );
    GameObject* pRestoredRight = pObjects->findGameObjectById( rightId );
    SW_ASSERT_NOT_NULL( pRestoredLeft );
    SW_ASSERT_NOT_NULL( pRestoredChild );
    SW_ASSERT_NOT_NULL( pRestoredRight );
    SW_EXPECT_TRUE_MSG( pRestoredChild->getParent() == pRestoredLeft, "자식이 플레이 중 이름을 가져간 다른 오브젝트에 붙었습니다" );
    SW_EXPECT_TRUE( pRestoredLeft->getName() == hashed_string( "Left" ) );
    SW_EXPECT_TRUE( pRestoredRight->getName() == hashed_string( "Right" ) );
}

/**
 * @brief [EditorPlaySessionTest] 멈춤에서 일시정지를 거쳐 Play 해도 월드가 켜지고 스냅샷이 찍힌다 — Stop 이 편집 씬을 되돌린다
 * @details 멈춤 → 플레이에서만 월드를 켜면, 멈춤 → 일시정지 → 플레이는 스냅샷도 onBeginPlay 도 없이 플레이가 돌고 Stop 이 편집 씬을
 *          되돌리지 못한다. 상태 전환 본체가 상태를 인자로 받으므로 EditorContext 없이 시험한다.
 */
SW_TEST_CASE( EditorPlaySessionTest, PauseFromStoppedThenPlayStartsTheWorld )
{
    SceneManager sceneManager;
    Scene*       pEdited = sceneManager.createEmptyActiveScene( "EditedLevel" );
    SW_ASSERT_NOT_NULL( pEdited );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager*        pObjects = pEdited->getObjectManager();
    GameObject*               pHero    = pObjects->createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pHero );
    pObjects->mergePendingAdds();
    const uint64 heroId = pHero->getObjectId();

    PlaySessionData data;
    EditorPlaySession::setState( data, PlaySessionState::Paused );
    SW_EXPECT_TRUE( data._bHasSnapshot == SW_TRUE );
    SW_EXPECT_TRUE( sceneManager.isWorldPlaying() );
    EditorPlaySession::setState( data, PlaySessionState::Playing );
    SW_EXPECT_TRUE( sceneManager.isWorldPlaying() );

    // 플레이 중에 영웅이 죽는다 — Stop 이 되살려야 한다.
    pObjects->destroyObject( pHero );
    pObjects->processDeferredDestruction();
    EditorPlaySession::setState( data, PlaySessionState::Stopped );
    SW_EXPECT_FALSE( sceneManager.isWorldPlaying() );
    pObjects->processDeferredDestruction();
    pObjects->mergePendingAdds();
    SW_EXPECT_NOT_NULL( pObjects->findGameObjectById( heroId ) );
}

/**
 * @brief [EditorPlaySessionTest] 씬을 여는 중에 누른 Play 는 미뤄졌다가 로드가 끝난 프레임에 그 씬으로 시작한다
 * @details 로드 중에 바로 시작하면 스냅샷이 곧 내려갈 씬을 찍고 플레이 중에 로드가 끝나 씬이 바뀐다. 언리얼 `RequestPlaySession` 처럼 요청을 걸어
 *          두고 로드가 끝난 다음 틱에 시작한다. 이 시험이 돌 수 있게 상태 전환 본체가 상태를 인자로 받는다(EditorTest 는 EditorContext 를 만들 수 없다).
 */
SW_TEST_CASE( EditorPlaySessionTest, PlayRequestedWhileASceneLoadsStartsAfterTheLoad )
{
    const sw::string scenePath = writeQueuedPlaySceneFile( "queuedplay.scene.xml", "LoadedLevel" );
    SW_ASSERT_FALSE( scenePath.empty() );

    SceneManager sceneManager;
    SW_ASSERT_TRUE( sceneManager.initialize() );
    SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "EditedLevel" ) );
    ScopedSceneManagerService scopedScene{ sceneManager };

    PlaySessionData data;
    SW_ASSERT_TRUE( sceneManager.requestLoadAsync( scenePath ) );
    SW_ASSERT_TRUE( sceneManager.isTransitioning() );

    EditorPlaySession::setState( data, PlaySessionState::Playing );
    SW_EXPECT_TRUE_MSG( data._state == PlaySessionState::Stopped, "씬을 여는 중인데 곧 내려갈 씬으로 플레이를 시작했습니다" );
    SW_EXPECT_TRUE_MSG( data._bStartQueued != SW_FALSE, "씬을 여는 중에 누른 Play 를 미뤄 두지 않았습니다" );
    // 로드가 아직 들어오지 않았으면(tickTransitions 전) 프레임이 돌아도 시작하지 않는다.
    EditorPlaySession::update( data );
    SW_EXPECT_TRUE_MSG( data._state == PlaySessionState::Stopped, "씬 로드가 끝나기 전에 미룬 플레이를 시작했습니다" );

    runEditorFramesUntil( sceneManager, data, []( const SceneManager&, const PlaySessionData& state )
    { return state._state != PlaySessionState::Stopped; } );
    SW_ASSERT_TRUE_MSG( data._state == PlaySessionState::Playing, "로드가 끝났는데 미룬 플레이가 시작하지 않았습니다" );
    SW_EXPECT_TRUE( data._bStartQueued == SW_FALSE );
    SW_EXPECT_TRUE_MSG( data._sceneName == "LoadedLevel", data._sceneName.c_str() ); // 스냅샷은 로드한 씬의 것이다
    SW_EXPECT_TRUE( sceneManager.isWorldPlaying() );

    EditorPlaySession::setState( data, PlaySessionState::Stopped );
    SW_ASSERT_NOT_NULL( sceneManager.getActiveScene() );
    SW_EXPECT_TRUE( sceneManager.getActiveScene()->getName() == "LoadedLevel" );
    sceneManager.shutdown();
}

/**
 * @brief [EditorPlaySessionTest] 미룬 Play 는 Stop 으로 거둔다 — 로드가 끝나도 저절로 시작하지 않는다
 */
SW_TEST_CASE( EditorPlaySessionTest, StopCancelsAQueuedPlay )
{
    const sw::string scenePath = writeQueuedPlaySceneFile( "cancelledplay.scene.xml", "LoadedLevel" );
    SW_ASSERT_FALSE( scenePath.empty() );

    SceneManager sceneManager;
    SW_ASSERT_TRUE( sceneManager.initialize() );
    SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "EditedLevel" ) );
    ScopedSceneManagerService scopedScene{ sceneManager };

    PlaySessionData data;
    SW_ASSERT_TRUE( sceneManager.requestLoadAsync( scenePath ) );
    EditorPlaySession::setState( data, PlaySessionState::Playing );
    SW_ASSERT_TRUE( data._bStartQueued != SW_FALSE );
    EditorPlaySession::setState( data, PlaySessionState::Stopped );
    SW_EXPECT_TRUE( data._bStartQueued == SW_FALSE );

    runEditorFramesUntil( sceneManager, data, []( const SceneManager& manager, const PlaySessionData& )
    { return manager.isTransitioning() == false; } );
    EditorPlaySession::update( data );
    SW_EXPECT_TRUE_MSG( data._state == PlaySessionState::Stopped, "거둔 Play 가 로드 뒤에 시작했습니다" );
    SW_EXPECT_FALSE( sceneManager.isWorldPlaying() );
    sceneManager.shutdown();
}

/**
 * @brief [EditorPlaySessionTest] Simulate 는 월드를 시작하지만 플레이어 세션은 아니다 — 도중에 Play 로 바꿀 수 있다
 * @details 호스트는 `isPlayerActive`(에디터 API 의 isPlaying)로 게임 모듈 업데이트 · 게임 입력 · 게임 카메라를 켠다. Simulate 가 Play 와 같으면
 *          에디터 카메라로 월드만 지켜보는 자리가 없다.
 */
SW_TEST_CASE( EditorPlaySessionTest, SimulateRunsTheWorldWithoutThePlayer )
{
    SceneManager sceneManager;
    SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "EditedLevel" ) );
    ScopedSceneManagerService scopedScene{ sceneManager };

    PlaySessionData data;
    EditorPlaySession::startSession( data, true );
    SW_EXPECT_TRUE( data._state == PlaySessionState::Playing );
    SW_EXPECT_TRUE( sceneManager.isWorldPlaying() );
    SW_EXPECT_FALSE_MSG( EditorPlaySession::isPlayerActive( data ), "Simulate 가 플레이어 세션으로 답했습니다(게임 모듈 · 입력이 켜진다)" );

    EditorPlaySession::startSession( data, false );
    SW_EXPECT_TRUE( EditorPlaySession::isPlayerActive( data ) );
    SW_EXPECT_TRUE( data._bHasSnapshot == SW_TRUE ); // 바꿔도 스냅샷은 처음 것 그대로

    EditorPlaySession::setState( data, PlaySessionState::Stopped );
    SW_EXPECT_FALSE( EditorPlaySession::isPlayerActive( data ) );
    SW_EXPECT_FALSE( sceneManager.isWorldPlaying() );
}

/**
 * @brief [EditorPlaySessionTest] Step N 은 N 프레임 동안 플레이어 세션으로 돌고 그 뒤 일시정지한다
 */
SW_TEST_CASE( EditorPlaySessionTest, StepFramesAdvancesThatManyFramesThenPauses )
{
    SceneManager sceneManager;
    SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "EditedLevel" ) );
    ScopedSceneManagerService scopedScene{ sceneManager };

    PlaySessionData data;
    EditorPlaySession::setState( data, PlaySessionState::Paused );
    EditorPlaySession::stepFrames( data, 3 );
    for ( uint32 frameIndex = 0; frameIndex < 3; ++frameIndex )
    {
        SW_EXPECT_TRUE_MSG( EditorPlaySession::isPlayerActive( data ), "Step 이 남았는데 플레이가 멈췄습니다" );
        EditorPlaySession::consumePendingStep( data );
    }
    SW_EXPECT_FALSE( EditorPlaySession::isPlayerActive( data ) );
    SW_EXPECT_TRUE( data._state == PlaySessionState::Paused );

    // 멈춤에서 Step N 은 플레이를 시작해 진행하고, N 이 다 되면 일시정지다.
    EditorPlaySession::setState( data, PlaySessionState::Stopped );
    EditorPlaySession::stepFrames( data, 2 );
    SW_EXPECT_TRUE( data._state == PlaySessionState::Playing );
    EditorPlaySession::consumePendingStep( data );
    SW_EXPECT_TRUE( data._state == PlaySessionState::Playing );
    EditorPlaySession::consumePendingStep( data );
    SW_EXPECT_TRUE( data._state == PlaySessionState::Paused );

    EditorPlaySession::stepFrames( data, 0 ); // 0 은 무시한다
    SW_EXPECT_EQUAL( 0u, data._pendingStepCount );
    EditorPlaySession::setState( data, PlaySessionState::Stopped );
}

/**
 * @brief [EditorPlaySessionTest] 카메라에서 시작은 'Player' 태그 오브젝트를, 없으면 게임 카메라를 든 오브젝트의 맨 위 조상을 옮긴다
 * @details 시작할 때(월드 시작 직후) 한 번, 첫 프레임 뒤(`update`) 한 번 더 옮긴다 — 첫 틱에 스폰 자리로 되돌리는 게임이 있다. Simulate 는 옮기지 않는다.
 */
SW_TEST_CASE( EditorPlaySessionTest, StartAtCameraMovesThePlayer )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "EditedLevel" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager*        pObjects = pScene->getObjectManager();

    // 태그가 없으면 카메라를 든 오브젝트의 맨 위 조상이다(1 인칭 카메라가 몸의 자식).
    GameObject* pBody = pObjects->createGameObject( hashed_string( "Body" ) );
    GameObject* pHead = pObjects->createGameObject( hashed_string( "Head" ) );
    SW_ASSERT_NOT_NULL( pBody->addComponent<SceneComponent>() );
    CameraComponent* pCamera = pHead->addComponent<CameraComponent>();
    SW_ASSERT_NOT_NULL( pCamera );
    SW_ASSERT_TRUE( pHead->attachToParent( pBody ) );
    pObjects->mergePendingAdds();
    SW_EXPECT_TRUE( EditorPlaySession::findStartObject( *pObjects, pCamera ) == pBody );
    SW_EXPECT_TRUE( EditorPlaySession::findStartObject( *pObjects, nullptr ) == nullptr );

    // 'Player' 태그가 있으면 그것이 먼저다.
    GameObject*     pPlayer     = pObjects->createGameObject( hashed_string( "Hero" ) );
    SceneComponent* pPlayerRoot = pPlayer->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pPlayerRoot );
    pPlayer->addTag( TagID::request( EditorPlaySession::kPlayerStartTag ) );
    pObjects->mergePendingAdds();
    SW_ASSERT_TRUE( EditorPlaySession::findStartObject( *pObjects, pCamera ) == pPlayer );

    const float3    start{ 4.0f, 5.0f, 6.0f };
    PlaySessionData data;
    EditorPlaySession::setStartPosition( data, start );
    EditorPlaySession::setState( data, PlaySessionState::Playing );
    SW_EXPECT_NEAR_EQUAL( 5.0f, pPlayerRoot->getWorldPosition()._y, 1e-4f );

    // 게임이 첫 틱에 스폰 자리로 되돌렸다 — 첫 프레임 끝(update)에 다시 옮긴다.
    pPlayerRoot->setWorldPosition( float3{} );
    EditorPlaySession::update( data );
    SW_EXPECT_NEAR_EQUAL( 6.0f, pPlayerRoot->getWorldPosition()._z, 1e-4f );
    EditorPlaySession::setState( data, PlaySessionState::Stopped );

    // Simulate 는 옮기지 않는다.
    pPlayerRoot->setWorldPosition( float3{} );
    EditorPlaySession::startSession( data, true );
    SW_EXPECT_NEAR_EQUAL( 0.0f, pPlayerRoot->getWorldPosition()._y, 1e-4f );
    EditorPlaySession::setState( data, PlaySessionState::Stopped );
}
