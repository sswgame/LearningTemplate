#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorPlaySession.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
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
        const sw::string text = sw::string( "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Scene formatVersion=\"0\" name=\"" ) + pSceneName +
                                "\">\n  <entities>\n    <entity name=\"Boss\"/>\n  </entities>\n</Scene>\n";
        // 쿠킹한 바이너리도 같은 이름으로 둔다 — Shipping 은 바이너리 씬(.bin)만 읽는다.
        sw::SceneDocument             doc{};
        sw::SceneDocument::EntityNode boss{};
        doc._name  = pSceneName;
        boss._name = "Boss";
        doc._listEntityNode.push_back( std::move( boss ) );
        const bool bWritten = sw::FileUtil::writeTextFile( path, text ) && doc.saveBinary( sw::FileUtil::replaceExtension( path, ".bin" ) );
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
 * @details 스냅샷은 Play 때의 활성 씬을 찍는다. 예전에는 Stop 이 그때 활성인 씬(플레이 중에 게임 코드가 연 다음 레벨)에 그대로 되돌려 두 씬의
 *          오브젝트가 섞였고, 활성 씬은 그 레벨의 소스 경로를 든 채라 저장하면 **그 레벨 파일**을 편집하던 씬의 내용으로 덮어썼다.
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
 * @details 되살리기는 스냅샷 순서(만든 순서)대로 오브젝트를 읽고, 읽을 때 부모를 이름으로 찾아 붙인다. 자식을 먼저 만들었으면 자식을 읽는 순간
 *          부모가 아직 없어 루트로 남았다. 오브젝트를 다 읽은 뒤 계층을 한 번 더 잇는 단계는 XML 폴백에만 있었는데, 그 폴백은 바이너리 저장이 실패할
 *          때만(오브젝트가 null) 타서 실제로는 돌지 않았다. 씬 로드(`Scene::instantiate`)와 같이, 모두 읽은 뒤 계층을 다시 잇는다.
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
 * @brief [EditorPlaySessionTest] 멈춤에서 일시정지를 거쳐 Play 해도 월드가 켜지고 스냅샷이 찍힌다 — Stop 이 편집 씬을 되돌린다
 * @details 예전에는 멈춤 → 플레이만 월드를 켜서, 멈춤 → 일시정지 → 플레이는 스냅샷도 onBeginPlay 도 없이 플레이가 돌았고 Stop 이 편집 씬을
 *          되돌리지 못했다(고쳤지만 EditorContext 를 세울 수 없어 시험이 없었다 — 상태 전환 본체가 상태를 인자로 받으면서 생겼다).
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
 * @details 지금 시작하면 스냅샷이 곧 내려갈 씬을 찍고 플레이 중에 로드가 끝나 씬이 바뀐다. 예전(㊿)에는 그때 거절했고(경고), 그 전에는 그냥 시작했다.
 *          언리얼 `RequestPlaySession` 처럼 요청을 걸어 두고 다음 틱에 시작한다. 이 시험이 돌 수 있게 상태 전환 본체가 상태를 인자로 받는다(EditorTest 는
 *          EditorContext 를 만들 수 없다).
 */
SW_TEST_CASE( EditorPlaySessionTest, PlayRequestedWhileASceneLoadsStartsAfterTheLoad )
{
    const sw::string scenePath = writeQueuedPlaySceneFile( "queuedplay.xml", "LoadedLevel" );
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
    const sw::string scenePath = writeQueuedPlaySceneFile( "cancelledplay.xml", "LoadedLevel" );
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
