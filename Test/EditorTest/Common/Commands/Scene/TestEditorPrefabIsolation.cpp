#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorPrefabIsolation.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Prefab/PrefabEditorPanel.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 프리팹 창의 미리보기 씬을 흉내 낸 씬입니다 — 환경(조명) 하나, 문서 루트 하나, 그 아래 중첩 인스턴스 둘과 숨어 있던 자식 하나. */
    struct PrefabWindowScene
    {
        SceneManager              _sceneManager;
        Scene*                    _pMain{ nullptr };
        Scene*                    _pPreview{ nullptr };
        EditorSelection           _selection;
        EditorWorkspace           _workspace{ &_selection };
        GameObject*               _pLight{ nullptr };
        GameObject*               _pDocument{ nullptr };
        GameObject*               _pNestedA{ nullptr };
        GameObject*               _pNestedB{ nullptr };
        GameObject*               _pQuiet{ nullptr };
        ScopedSceneManagerService _scopedScene{ _sceneManager };

        PrefabWindowScene()
        {
            _pMain                     = _sceneManager.createEmptyActiveScene( "Main" );
            _pPreview                  = _sceneManager.createPreviewScene( "PrefabWindow" );
            GameObjectManager& objects = *_pPreview->getObjectManager();
            _pLight                    = objects.createGameObject( hashed_string( "Light" ) );
            _pDocument                 = objects.createGameObject( hashed_string( "Document" ) );
            _pNestedA                  = objects.createGameObject( hashed_string( "NestedA" ) );
            _pNestedB                  = objects.createGameObject( hashed_string( "NestedB" ) );
            _pQuiet                    = objects.createGameObject( hashed_string( "Quiet" ) );
            objects.mergePendingAdds();
            // 부모-자식은 SceneComponent 사이에서 맺어진다 — 없으면 attachToParent 가 false 다.
            for ( GameObject* pObject : { _pLight, _pDocument, _pNestedA, _pNestedB, _pQuiet } )
            {
                pObject->addComponent<SceneComponent>();
            }
            SW_ASSERT_TRUE( _pNestedA->attachToParent( _pDocument ) );
            SW_ASSERT_TRUE( _pNestedB->attachToParent( _pDocument ) );
            SW_ASSERT_TRUE( _pQuiet->attachToParent( _pDocument ) );
            _pQuiet->setActive( false ); // 격리 전부터 꺼져 있던 오브젝트
            _workspace.setPlacedPrefabPath( *_pDocument, "game/prefabs/room.prefab.xml" );
            _workspace.setPlacedPrefabPath( *_pNestedA, "game/prefabs/crate.prefab.xml" );
            _workspace.setPlacedPrefabPath( *_pNestedB, "game/prefabs/lamp.prefab.xml" );
        }

        GameObjectManager& objects() { return *_pPreview->getObjectManager(); }
    };
} // namespace

/**
 * @brief [EditorPrefabIsolationTest] 문서 단계는 아무것도 숨기지 않고, 중첩 단계는 문서 루트 아래의 다른 오브젝트만 숨기며 나오면 처음 상태로 되살린다
 * @details 환경(조명 · 바닥)은 문서 루트 밖이라 숨기지 않는다 — 숨기면 미리보기가 어두워진다. 격리 전에 꺼져 있던 오브젝트는 되살린 뒤에도 꺼져 있어야 한다.
 */
SW_TEST_CASE( EditorPrefabIsolationTest, NestedStageHidesOnlyInsideTheDocumentRootAndRestores )
{
    PrefabWindowScene     scene;
    EditorPrefabIsolation isolation;

    isolation.push( scene.objects(), "game/prefabs/room.prefab.xml", *scene._pDocument, true );
    SW_EXPECT_TRUE( isolation.isActive() );
    SW_EXPECT_TRUE( isolation.getDocumentRootObjectID() == scene._pDocument->getObjectID() );
    SW_EXPECT_TRUE( scene._pLight->isActive() );
    SW_EXPECT_TRUE( scene._pNestedA->isActive() );
    SW_EXPECT_TRUE( scene._pNestedB->isActive() );

    isolation.push( scene.objects(), "game/prefabs/crate.prefab.xml", *scene._pNestedA, false );
    SW_EXPECT_TRUE( isolation.getDepth() == 2 );
    SW_EXPECT_TRUE( isolation.getRootObjectID() == scene._pNestedA->getObjectID() );
    SW_EXPECT_TRUE( scene._pNestedA->isActive() );
    SW_EXPECT_FALSE( scene._pNestedB->isActive() ); // 문서 루트 아래의 형제는 숨는다
    SW_EXPECT_TRUE( scene._pDocument->isActive() ); // 조상은 그대로다
    SW_EXPECT_TRUE( scene._pLight->isActive() );    // 문서 루트 밖(환경)은 그대로다

    PrefabIsolationFrame popped;
    SW_ASSERT_TRUE( isolation.pop( &scene.objects(), popped ) );
    SW_EXPECT_TRUE( popped._prefabPath == "game/prefabs/crate.prefab.xml" );
    SW_EXPECT_TRUE( scene._pNestedB->isActive() );
    SW_EXPECT_FALSE( scene._pQuiet->isActive() ); // 격리 전에 꺼져 있던 것은 꺼진 채로 돌아온다
    SW_EXPECT_TRUE( isolation.getDepth() == 1 );
}

/**
 * @brief [EditorPrefabIsolationTest] 인스턴스 찾기는 그 씬의 표를 보고, 지정한 루트 밖의 같은 프리팹은 건너뛴다
 * @details 프리팹 창의 씬 오브젝트는 활성 씬에 없다 — 활성 씬의 표로 판정하면 찾지 못해 격리 때마다 새로 스폰한다.
 */
SW_TEST_CASE( EditorPrefabIsolationTest, FindInstanceReadsThePreviewSceneTable )
{
    PrefabWindowScene scene;

    GameObject* pFound = EditorPrefabIsolation::findInstance( scene.objects(), scene._workspace, "game/prefabs/crate.prefab.xml", nullptr, nullptr );
    SW_EXPECT_TRUE( pFound == scene._pNestedA );
    GameObject* pUnderRoot = EditorPrefabIsolation::findInstance( scene.objects(), scene._workspace, "game/prefabs/crate.prefab.xml", nullptr, scene._pNestedB );
    SW_EXPECT_TRUE( pUnderRoot == nullptr ); // NestedB 아래에는 없다
    GameObject* pPreferred = EditorPrefabIsolation::findInstance( scene.objects(), scene._workspace, "game/prefabs/lamp.prefab.xml", scene._pNestedB, scene._pDocument );
    SW_EXPECT_TRUE( pPreferred == scene._pNestedB );
    GameObject* pMissing = EditorPrefabIsolation::findInstance( scene.objects(), scene._workspace, "game/prefabs/none.prefab.xml", nullptr, nullptr );
    SW_EXPECT_TRUE( pMissing == nullptr );
}

/**
 * @brief [EditorPrefabIsolationTest] 활성 씬은 격리에 닿지 않는다 — 메인 씬 오브젝트는 숨지 않고, 스택을 비워도 되살릴 것이 없다
 */
SW_TEST_CASE( EditorPrefabIsolationTest, MainSceneIsNeverTouched )
{
    PrefabWindowScene scene;
    GameObject*       pMainObject = scene._pMain->getObjectManager()->createGameObject( hashed_string( "MainObject" ) );
    SW_ASSERT_NOT_NULL( pMainObject );
    scene._pMain->getObjectManager()->mergePendingAdds();

    EditorPrefabIsolation isolation;
    isolation.push( scene.objects(), "game/prefabs/room.prefab.xml", *scene._pDocument, true );
    isolation.push( scene.objects(), "game/prefabs/lamp.prefab.xml", *scene._pNestedB, false );
    SW_EXPECT_TRUE( pMainObject->isActive() );

    isolation.clear(); // 문서를 바꿀 때 — 오브젝트는 되살리지 않고 단계만 버린다
    SW_EXPECT_FALSE( isolation.isActive() );
    SW_EXPECT_TRUE( pMainObject->isActive() );
}

/**
 * @brief [EditorPrefabIsolationTest] 문서 패널을 X 로 닫을 때 편집이 남았거나 확인 중이면 묻고, 깨끗하면 그냥 닫는다
 */
SW_TEST_CASE( EditorPrefabIsolationTest, CloseAsksOnlyWhenEditsRemain )
{
    SW_EXPECT_TRUE( EditorDocumentPanel::shouldAskBeforeClosing( false, true, false ) );   // X + 미저장
    SW_EXPECT_TRUE( EditorDocumentPanel::shouldAskBeforeClosing( true, true, true ) );     // 확인 중에는 열림을 되돌려 놓아도 계속 묻는다
    SW_EXPECT_FALSE( EditorDocumentPanel::shouldAskBeforeClosing( false, false, false ) ); // X + 깨끗
    SW_EXPECT_FALSE( EditorDocumentPanel::shouldAskBeforeClosing( true, true, false ) );   // 열려 있을 뿐
}
