#include "pch.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [EditorWorkspaceTest] 프리팹 인스턴스 경로는 오브젝트가 사는 씬이 원본이다 — 워크스페이스는 사본을 들지 않는다
 * @details 씬에서 연결을 끊으면(`Scene::setEntityPrefabPath` 를 부르는 엔진 경로) 워크스페이스도 같은 답을 해야 한다. 활성 씬이 바뀌어도(플레이 세션이 씬을 다시 세운다)
 *          새 씬의 같은 id 오브젝트에 옛 씬의 경로가 남지 않는다 — 사본을 들면 프리팹 적용 · 되돌리기 메뉴가 연결 없는 오브젝트에 뜬다.
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
    workspace.setPlacedPrefabPath( *pHero, "game/prefabs/hero.prefab" );
    SW_EXPECT_TRUE( pFirst->getEntityPrefabPath( heroID ) == "game/prefabs/hero.prefab" );
    SW_EXPECT_TRUE( workspace.getPlacedPrefabPath( *pHero ) == "game/prefabs/hero.prefab" );

    // 씬에서 끊은 연결은 워크스페이스에서도 끊겨 보인다.
    pFirst->setEntityPrefabPath( heroID, "" );
    SW_EXPECT_TRUE_MSG( workspace.getPlacedPrefabPath( *pHero ).empty(), sw::string{ workspace.getPlacedPrefabPath( *pHero ) }.c_str() );

    // 활성 씬이 바뀌면 옛 씬의 연결은 새 씬의 같은 id 오브젝트에 보이지 않는다.
    workspace.setPlacedPrefabPath( *pHero, "game/prefabs/hero.prefab" );
    Scene* pSecond = sceneManager.createEmptyActiveScene( "Second" );
    SW_ASSERT_NOT_NULL( pSecond );
    GameObject* pOther = pSecond->getObjectManager()->createGameObject( hashed_string( "Other" ) );
    SW_ASSERT_NOT_NULL( pOther );
    pSecond->getObjectManager()->mergePendingAdds();
    SW_EXPECT_TRUE_MSG( workspace.getPlacedPrefabPath( *pOther ).empty(), sw::string{ workspace.getPlacedPrefabPath( *pOther ) }.c_str() );
}

/**
 * @brief [EditorWorkspaceTest] 미리보기 씬에 놓은 인스턴스의 프리팹 경로는 그 미리보기 씬에 기록되고 읽힌다 — 활성 씬의 같은 id 와 섞이지 않는다
 * @details 프리팹 창(`PrefabEditorPanel`)은 메인 씬이 아니라 미리보기 씬에 프리팹을 스폰한다. 표를 활성 씬의 id 로만 읽으면 그 인스턴스를 찾지 못해 중첩 격리가 깨진다.
 */
SW_TEST_CASE( EditorWorkspaceTest, PrefabPathOfAPreviewSceneObjectLivesInThePreviewScene )
{
    SceneManager sceneManager;
    Scene*       pMain = sceneManager.createEmptyActiveScene( "Main" );
    SW_ASSERT_NOT_NULL( pMain );
    ScopedSceneManagerService scopedScene{ sceneManager };
    const ScenePreviewPolicy  policy{};
    Scene*                    pPreview = sceneManager.createPreviewScene( "PrefabWindow", policy );
    SW_ASSERT_NOT_NULL( pPreview );

    GameObject* pInMain    = pMain->getObjectManager()->createGameObject( hashed_string( "InMain" ) );
    GameObject* pInPreview = pPreview->getObjectManager()->createGameObject( hashed_string( "InPreview" ) );
    SW_ASSERT_NOT_NULL( pInMain );
    SW_ASSERT_NOT_NULL( pInPreview );
    pMain->getObjectManager()->mergePendingAdds();
    pPreview->getObjectManager()->mergePendingAdds();

    EditorSelection editorSelection;
    EditorWorkspace workspace{ &editorSelection };
    workspace.setPlacedPrefabPath( *pInPreview, "game/prefabs/crate.prefab.xml" );
    SW_EXPECT_TRUE( pPreview->getEntityPrefabPath( pInPreview->getObjectID() ) == "game/prefabs/crate.prefab.xml" );
    SW_EXPECT_TRUE( workspace.findPrefabPath( pInPreview ) == "game/prefabs/crate.prefab.xml" );
    SW_EXPECT_TRUE( workspace.findPrefabPath( pInMain ).empty() );
    SW_EXPECT_TRUE( pMain->getEntityPrefabPath( pInPreview->getObjectID() ).empty() ); // 활성 씬의 표에는 들어가지 않는다
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
    workspace.setPlacedPrefabPath( *pPost, "game/prefabs/post.prefab.xml" );
    objects.getSubsystem<PrefabInstanceRegistry>().registerNestedPart( pLamp->getObjectID(), pPost->getObjectID(), "game/prefabs/lamp.prefab.xml", "0" );

    SW_EXPECT_TRUE( workspace.findPrefabPath( pPost ) == "game/prefabs/post.prefab.xml" );
    SW_EXPECT_TRUE( workspace.findPrefabPath( pLamp ) == "game/prefabs/lamp.prefab.xml" ); // 씬 컴포넌트가 없는 부품 — 바깥 인스턴스가 살아 있으면 부품이다
    SW_EXPECT_TRUE( workspace.isNestedPrefabPart( pLamp ) );
    SW_EXPECT_TRUE( workspace.getPlacedPrefabPath( *pLamp ).empty() );
    SW_EXPECT_TRUE( workspace.findPrefabPath( pLone ).empty() );
    SW_EXPECT_FALSE( workspace.isNestedPrefabPart( pPost ) );

    // 바깥 인스턴스가 사라지면 그 부품은 더는 부품이 아니다.
    objects.destroyObject( pPost );
    SW_EXPECT_TRUE( workspace.findPrefabPath( pLamp ).empty() );
}

/**
 * @brief [EditorWorkspaceTest] 미리보기 씬에 묶인 선택은 그 씬에서 풀고, 바뀌면 겨누는 선택이 되며, 메인 선택이 바뀌거나 풀리면 메인으로 돌아온다
 * @details 인스펙터는 `getFocusedSelection` 하나로 대상을 묻는다. 창 선택이 활성 씬에서 풀리면 같은 id 의 다른 오브젝트를 가리키거나 비어 보인다.
 */
SW_TEST_CASE( EditorWorkspaceTest, PreviewSelectionResolvesInItsSceneAndBecomesTheFocusedSelection )
{
    SceneManager sceneManager;
    Scene*       pMain    = sceneManager.createEmptyActiveScene( "Main" );
    Scene*       pPreview = sceneManager.createScene( "PreviewWorld" );
    SW_ASSERT_TRUE( pMain != nullptr && pPreview != nullptr );
    ScopedSceneManagerService scopedScene{ sceneManager };

    GameObject* pBall = pPreview->getObjectManager()->createGameObject( hashed_string( "Ball" ) );
    SW_ASSERT_NOT_NULL( pBall );
    pPreview->getObjectManager()->mergePendingAdds();

    CommandStack    windowStack;
    EditorSelection mainSelection;
    EditorSelection previewSelection;
    previewSelection.bindPreviewScope( *pPreview, windowStack );
    SW_EXPECT_TRUE( previewSelection.isPreviewScope() );
    SW_EXPECT_TRUE( previewSelection.getScene() == pPreview );

    previewSelection.selectObject( pBall );
    SW_EXPECT_TRUE( previewSelection.getPrimaryObject() == pBall ); // 활성 씬이 아니라 묶인 씬에서 푼다
    SW_EXPECT_TRUE( getFocusedSelection() == &previewSelection );

    // 메인 선택이 바뀌면 메인으로 돌아온다(포인터가 아니라 nullptr 로 걸어 메인을 뜻한다).
    mainSelection.clearAll();
    mainSelection.selectObject( nullptr );
    SW_EXPECT_TRUE( getFocusedSelection() != &previewSelection );

    previewSelection.selectObject( pBall, SelectionMode::Toggle );
    previewSelection.selectObject( pBall, SelectionMode::Toggle );
    SW_EXPECT_TRUE( getFocusedSelection() == &previewSelection );
    previewSelection.unbindPreviewScope();
    SW_EXPECT_TRUE( getFocusedSelection() != &previewSelection );
    SW_EXPECT_FALSE( previewSelection.isPreviewScope() );
    SW_EXPECT_EQUAL( size_t( 0 ), previewSelection.getSelectedObjectCount() );
}

/**
 * @brief [EditorServiceTest] 필수 조회 `getSubsystem<T>()` 는 있으면 참조를 돌려주고, 없으면 오류 로그와 단언으로 멈춘다
 * @details 없을 때의 `getSubsystem` 은 단언 뒤 참조를 돌려줘야 하므로(null 역참조) 직접 부르지 않고, 없을 때 `getSubsystem` 이 부르는 보고 함수를 검사한다.
 */
SW_TEST_CASE( EditorServiceTest, RequiredLookupReturnsServiceAndReportsWhenMissing )
{
    SceneManager sceneManager;
    {
        ScopedSceneManagerService scopedScene{ sceneManager };
        SW_EXPECT_TRUE( &getSubsystem<SceneManager>() == &sceneManager );
    }

    // 없으면 모든 구성에서 오류 로그를 남기고 멈춘다. 멈춤을 가로채 세는 것은 Debug 에만 있어서, 프로세스 안에서 볼 수 있는 구성에서만 부른다.
    test::ScopedAssertCapture assertCapture;
    if ( test::ScopedAssertCapture::kAssertsAreActive )
    {
        SW_TEST_DEFENSIVE_SCOPE( "a required editor service is missing" );
        sw::internal::reportMissingRequiredService( "test lookup" ); // 에디터 · 게임 · 엔진 · 범위 조회가 함께 쓰는 Core 의 보고 함수
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
    }
}

/**
 * @brief [EditorServiceTest] 에디터 컨텍스트가 없는 범위에서 `findSubsystem<EditorContext>()` 는 nullptr 을 돌려준다
 * @details `getSubsystem<EditorContext>()` 는 같은 상황에서 멈추므로(`reportMissingRequiredService`) 직접 부르지 않는다. 없을 수 있는 쪽(`findSubsystem`)만 이 테스트로 본다.
 */
SW_TEST_CASE( EditorServiceTest, EditorContextLookupReturnsNullWithoutContext )
{
    SceneManager              sceneManager;
    ScopedSceneManagerService scopedScene{ sceneManager }; // 컨텍스트 없이 서비스 하나만 건 테스트 범위

    SW_EXPECT_TRUE( findSubsystem<EditorContext>() == nullptr );
}
