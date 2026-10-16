#include "pch.h"

#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorWorkspaceTest] 프리팹 인스턴스 경로는 활성 씬이 원본이다 — 워크스페이스는 사본을 들지 않는다
 * @details 씬에서 연결을 끊거나(`Scene::setEntityPrefabPath` 를 부르는 엔진 경로) 활성 씬이 바뀌면(플레이 세션이 씬을 다시 세운다) 워크스페이스도
 *          같은 답을 해야 한다. 사본을 들면 옛 씬의 경로가 새 씬의 같은 id 에 남아, 프리팹 적용 · 되돌리기 메뉴가 연결 없는 오브젝트에 뜬다.
 */
SW_TEST_CASE( EditorWorkspaceTest, PrefabPathIsReadFromTheActiveScene )
{
    SceneManager sceneManager;
    Scene*       pFirst = sceneManager.createEmptyActiveScene( "First" );
    SW_ASSERT_NOT_NULL( pFirst );
    ScopedSceneManagerService scopedScene{ sceneManager };

    GameObject* pHero = pFirst->getObjectManager()->createGameObject( hashed_string( "Hero" ) );
    SW_ASSERT_NOT_NULL( pHero );
    pFirst->getObjectManager()->mergePendingAdds();
    const uint64 heroID = pHero->getObjectID();

    EditorSelection editorSelection;
    EditorWorkspace workspace{ &editorSelection };
    workspace.setPlacedPrefabPath( heroID, "game/prefabs/hero.prefab" );
    SW_EXPECT_TRUE( pFirst->getEntityPrefabPath( heroID ) == "game/prefabs/hero.prefab" );
    SW_EXPECT_TRUE( workspace.getPlacedPrefabPath( heroID ) == "game/prefabs/hero.prefab" );

    // 씬에서 끊은 연결은 워크스페이스에서도 끊겨 보인다.
    pFirst->setEntityPrefabPath( heroID, "" );
    SW_EXPECT_TRUE_MSG( workspace.getPlacedPrefabPath( heroID ).empty(), sw::string{ workspace.getPlacedPrefabPath( heroID ) }.c_str() );

    // 활성 씬이 바뀌면 옛 씬의 연결은 보이지 않는다.
    workspace.setPlacedPrefabPath( heroID, "game/prefabs/hero.prefab" );
    SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "Second" ) );
    SW_EXPECT_TRUE_MSG( workspace.getPlacedPrefabPath( heroID ).empty(), sw::string{ workspace.getPlacedPrefabPath( heroID ) }.c_str() );
}

/**
 * @brief [EditorWorkspaceTest] 프리팹 인스턴스 판정은 두 기록을 함께 본다 — 놓은 인스턴스와, 바깥 인스턴스 아래에 붙은 중첩 부품
 * @details 놓은 인스턴스 기록만 보면 다른 프리팹이 스폰한 부품이 인스펙터 프리팹 줄 · 계층의 파란 이름 · Apply 대상에서 빠진다.
 *          바깥 인스턴스 밖으로 옮긴 부품은 더는 부품이 아니다(씬이 그 오브젝트를 따로 저장한다).
 */
SW_TEST_CASE( EditorWorkspaceTest, PrefabPathCoversPlacedInstancesAndNestedParts )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "Nested" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    GameObjectManager&        objects = *pScene->getObjectManager();

    GameObject* pPost = objects.createGameObject( hashed_string( "Post" ) );
    GameObject* pLamp = objects.createGameObject( hashed_string( "Lamp" ) );
    GameObject* pLone = objects.createGameObject( hashed_string( "Lone" ) );
    objects.mergePendingAdds();

    EditorSelection editorSelection;
    EditorWorkspace workspace{ &editorSelection };
    workspace.setPlacedPrefabPath( pPost->getObjectID(), "game/prefabs/post.prefab.xml" );
    objects.getSubsystem<PrefabInstanceRegistry>().registerNestedPart( pLamp->getObjectID(), pPost->getObjectID(), "game/prefabs/lamp.prefab.xml" );

    SW_EXPECT_TRUE( workspace.findPrefabPath( pPost ) == "game/prefabs/post.prefab.xml" );
    SW_EXPECT_TRUE( workspace.findPrefabPath( pLamp ) == "game/prefabs/lamp.prefab.xml" ); // 씬 컴포넌트가 없는 부품 — 바깥 인스턴스가 살아 있으면 부품이다
    SW_EXPECT_TRUE( workspace.isNestedPrefabPart( pLamp ) );
    SW_EXPECT_TRUE( workspace.getPlacedPrefabPath( pLamp->getObjectID() ).empty() );
    SW_EXPECT_TRUE( workspace.findPrefabPath( pLone ).empty() );
    SW_EXPECT_FALSE( workspace.isNestedPrefabPart( pPost ) );

    // 바깥 인스턴스가 사라지면 그 부품은 더는 부품이 아니다.
    objects.destroyObject( pPost );
    SW_EXPECT_TRUE( workspace.findPrefabPath( pLamp ).empty() );
}
