#include "pch.h"

#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorWorkspaceTest] 프리팹 인스턴스 경로는 활성 씬이 정본이다 — 워크스페이스는 사본을 들지 않는다
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
    const uint64 heroId = pHero->getObjectId();

    SelectionManager selectionManager;
    EditorWorkspace  workspace{ &selectionManager };
    workspace.setGameObjectPrefabPath( heroId, "game/prefabs/hero.prefab" );
    SW_EXPECT_TRUE( pFirst->getEntityPrefabPath( heroId ) == "game/prefabs/hero.prefab" );
    SW_EXPECT_TRUE( workspace.getGameObjectPrefabPath( heroId ) == "game/prefabs/hero.prefab" );

    // 씬에서 끊은 연결은 워크스페이스에서도 끊겨 보인다.
    pFirst->setEntityPrefabPath( heroId, "" );
    SW_EXPECT_TRUE_MSG( workspace.getGameObjectPrefabPath( heroId ).empty(), workspace.getGameObjectPrefabPath( heroId ).c_str() );

    // 활성 씬이 바뀌면 옛 씬의 연결은 보이지 않는다.
    workspace.setGameObjectPrefabPath( heroId, "game/prefabs/hero.prefab" );
    SW_ASSERT_NOT_NULL( sceneManager.createEmptyActiveScene( "Second" ) );
    SW_EXPECT_TRUE_MSG( workspace.getGameObjectPrefabPath( heroId ).empty(), workspace.getGameObjectPrefabPath( heroId ).c_str() );
}
