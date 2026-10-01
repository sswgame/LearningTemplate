#include "pch.h"

#include "Editor/Common/Workspace/EditorPlaySession.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
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
