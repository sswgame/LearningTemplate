#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Viewport/EditorPreviewPick.h"
#include "Editor/Viewport/EditorViewportRayPick.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// 창 범위 선택 — 선택은 메인 씬의 것과 미리보기 창마다의 것이 따로 있고, 호출부는 이름 있는 접근자로 어느 것인지 고른다.
// 메인 선택만 타입으로 찾던 때는 미리보기 창이 겨누는데 명령이 메인 선택을 고쳤다(옛 D3-7 (2), EditorMultiWorld).

static_assert( kIsAccessorOnlySubsystem<EditorSelection>, "the selection must not be reachable by type lookup - several instances exist" );
static_assert( kIsAccessorOnlySubsystem<const EditorSelection>, "a const lookup must be blocked too" );
static_assert( kIsAccessorOnlySubsystem<EditorWorkspace> == false, "other subsystems keep their type lookup" );

namespace
{
    /** @brief @p manager 에 주 씬 컴포넌트를 단 오브젝트를 월드 @p position 에 놓습니다. */
    GameObject* placeSceneObject( GameObjectManager& manager, const utf8* pName, const float3& position )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        SceneComponent* pScene = pObject->addComponent<SceneComponent>();
        if ( pScene == nullptr )
            return nullptr;
        manager.mergePendingAdds();
        pScene->setWorldPosition( position );
        return pObject;
    }
} // namespace

/**
 * @brief [EditorSelectionScopeTest] 접근자가 고르는 선택 — 메인 · 겨누는 창 · 오브젝트가 사는 씬
 * @details 미리보기 선택이 바뀌면 그것이 겨누는 선택이 되고, 메인 선택이 바뀌면 메인으로 돌아온다. `findSelectionOf` 는 포커스와 상관없이
 *          씬으로 고른다. 묶음을 풀면 미리보기 목록에서 빠진다(빈 포인터가 남지 않는다).
 */
SW_TEST_CASE( EditorSelectionScopeTest, NamedAccessorsPickTheRightSelection )
{
    SceneManager sceneManager;
    Scene*       pLevel   = sceneManager.createEmptyActiveScene( "ScopeLevel" );
    Scene*       pPreview = sceneManager.createPreviewScene( "ScopeWindow" );
    SW_ASSERT_NOT_NULL( pLevel );
    SW_ASSERT_NOT_NULL( pPreview );
    ScopedSceneManagerService scopedScene{ sceneManager };

    EditorSelection                 mainSelection;
    ScopedMainSceneSelectionService scopedMain{ mainSelection };
    SW_EXPECT_TRUE( &getMainSceneSelection() == &mainSelection );
    SW_EXPECT_TRUE( getFocusedSelection() == &mainSelection );

    CommandStack    previewStack;
    EditorSelection previewSelection;
    previewSelection.bindPreviewScope( *pPreview, previewStack );
    SW_EXPECT_EQUAL( size_t( 1 ), getPreviewSelections().size() );
    SW_EXPECT_TRUE( findSelectionOf( *pPreview->getObjectManager() ) == &previewSelection );
    SW_EXPECT_TRUE( findSelectionOf( *pLevel->getObjectManager() ) == &mainSelection );

    GameObject* pSubject = placeSceneObject( *pPreview->getObjectManager(), "PreviewSubject", float3{} );
    GameObject* pProp    = placeSceneObject( *pLevel->getObjectManager(), "LevelProp", float3{} );
    SW_ASSERT_NOT_NULL( pSubject );
    SW_ASSERT_NOT_NULL( pProp );

    previewSelection.selectObject( pSubject );
    SW_EXPECT_TRUE( getFocusedSelection() == &previewSelection );
    SW_EXPECT_TRUE( getMainSceneSelection().getPrimaryObject() == nullptr ); // 창에서 고른 것은 메인 선택에 들지 않는다

    mainSelection.selectObject( pProp );
    SW_EXPECT_TRUE( getFocusedSelection() == &mainSelection );

    previewSelection.clearObjectSelection(); // 창의 선택이 바뀌어 다시 창이 겨눈다
    SW_EXPECT_TRUE( getFocusedSelection() == &previewSelection );
    previewSelection.unbindPreviewScope();
    SW_EXPECT_TRUE( getPreviewSelections().empty() );
    SW_EXPECT_TRUE( getFocusedSelection() == &mainSelection ); // 겨누던 창이 내려가면 메인으로
    SW_EXPECT_TRUE( findSelectionOf( *pPreview->getObjectManager() ) == nullptr );
}

/**
 * @brief [EditorSelectionScopeTest] 전역 명령(정렬)은 겨누는 창의 선택을 고치고, 기록은 그 창의 스택에만 남는다
 * @details 정렬이 메인 선택을 읽던 때는 미리보기 창에서 두 오브젝트를 골라도 메인 씬의 것을 옮겼다. 기록이 전역 스택으로 가면 메인 씬이
 *          dirty 가 되고 Ctrl+Z(메인)가 창의 편집을 되돌린다 — 기록의 스택은 오브젝트가 사는 씬이 고른다(`EditorTransaction`).
 */
SW_TEST_CASE( EditorSelectionScopeTest, AlignOnFocusedPreviewSelectionStaysInThePreviewStack )
{
    SceneManager sceneManager;
    Scene*       pLevel   = sceneManager.createEmptyActiveScene( "AlignLevel" );
    Scene*       pPreview = sceneManager.createPreviewScene( "AlignWindow" );
    SW_ASSERT_NOT_NULL( pLevel );
    SW_ASSERT_NOT_NULL( pPreview );
    ScopedSceneManagerService scopedScene{ sceneManager };

    CommandStack                    mainStack;
    ScopedCommandStackService       scopedStack{ mainStack };
    EditorSelection                 mainSelection;
    ScopedMainSceneSelectionService scopedMain{ mainSelection };
    EditorWorkspace                 workspace{ &mainSelection };
    ScopedWorkspaceService          scopedWorkspace{ workspace };

    GameObject* pLevelLeft  = placeSceneObject( *pLevel->getObjectManager(), "LevelLeft", float3{ -8.0f, 0.0f, 0.0f } );
    GameObject* pLevelRight = placeSceneObject( *pLevel->getObjectManager(), "LevelRight", float3{ 8.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pLevelLeft );
    SW_ASSERT_NOT_NULL( pLevelRight );
    mainSelection.selectObjects( { pLevelLeft, pLevelRight } );

    CommandStack    previewStack;
    EditorSelection previewSelection;
    previewSelection.bindPreviewScope( *pPreview, previewStack );
    GameObject* pLeft  = placeSceneObject( *pPreview->getObjectManager(), "PreviewLeft", float3{ 0.0f, 0.0f, 0.0f } );
    GameObject* pRight = placeSceneObject( *pPreview->getObjectManager(), "PreviewRight", float3{ 4.0f, 0.0f, 0.0f } );
    SW_ASSERT_NOT_NULL( pLeft );
    SW_ASSERT_NOT_NULL( pRight );
    previewSelection.selectObjects( { pLeft, pRight } );
    workspace.clearSceneDirty();

    EditorTransformCommands::alignSelectedObjects( AlignAxis::X, AlignType::Center );

    SW_EXPECT_NEAR_EQUAL( 2.0f, pLeft->getPrimarySceneComponent()->getWorldPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, pRight->getPrimarySceneComponent()->getWorldPosition()._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( -8.0f, pLevelLeft->getPrimarySceneComponent()->getWorldPosition()._x, 1e-4f ); // 메인 씬은 그대로
    SW_EXPECT_EQUAL( size_t( 1 ), previewStack.getCommandCount() );                                      // 한 묶음
    SW_EXPECT_EQUAL( size_t( 0 ), mainStack.getCommandCount() );
    SW_EXPECT_FALSE( workspace.isSceneDirty() );

    previewStack.undo();
    GameObject* pRestored = pPreview->getObjectManager()->findGameObjectByID( pRight->getObjectID() );
    SW_ASSERT_NOT_NULL( pRestored );
    SW_EXPECT_NEAR_EQUAL( 4.0f, pRestored->getPrimarySceneComponent()->getWorldPosition()._x, 1e-4f );
    previewSelection.unbindPreviewScope();
}

/**
 * @brief [EditorSelectionScopeTest] 미리보기 씬의 오브젝트를 지우면 그 창의 선택에서 빠지고 기록은 창의 스택에 남는다
 * @details 지운 것을 메인 선택에서만 빼면 창의 선택(인스펙터 · 기즈모의 대상)이 지운 오브젝트를 계속 잡는다.
 */
SW_TEST_CASE( EditorSelectionScopeTest, DestroyInPreviewLeavesThePreviewSelection )
{
    SceneManager sceneManager;
    Scene*       pLevel   = sceneManager.createEmptyActiveScene( "DestroyLevel" );
    Scene*       pPreview = sceneManager.createPreviewScene( "DestroyWindow" );
    SW_ASSERT_NOT_NULL( pLevel );
    SW_ASSERT_NOT_NULL( pPreview );
    ScopedSceneManagerService scopedScene{ sceneManager };

    CommandStack                    mainStack;
    ScopedCommandStackService       scopedStack{ mainStack };
    EditorSelection                 mainSelection;
    ScopedMainSceneSelectionService scopedMain{ mainSelection };

    CommandStack    previewStack;
    EditorSelection previewSelection;
    previewSelection.bindPreviewScope( *pPreview, previewStack );
    GameObjectManager* pPreviewObjects = pPreview->getObjectManager();
    GameObject*        pDoomed         = placeSceneObject( *pPreviewObjects, "PreviewDoomed", float3{} );
    SW_ASSERT_NOT_NULL( pDoomed );
    const uint64 doomedID = pDoomed->getObjectID();
    previewSelection.selectObject( pDoomed );

    SW_ASSERT_TRUE( EditorSceneCommands::destroy( pPreviewObjects, pDoomed ) );
    pPreviewObjects->processDeferredDestruction();
    SW_EXPECT_EQUAL( size_t( 0 ), previewSelection.getSelectedObjectCount() );
    SW_EXPECT_EQUAL( size_t( 1 ), previewStack.getCommandCount() );
    SW_EXPECT_EQUAL( size_t( 0 ), mainStack.getCommandCount() );

    previewStack.undo();
    pPreviewObjects->mergePendingAdds();
    SW_EXPECT_NOT_NULL( pPreviewObjects->findGameObjectByID( doomedID ) );
    previewSelection.unbindPreviewScope();
}

/**
 * @brief [EditorSelectionScopeTest] 미리보기 뷰포트의 클릭은 창의 씬을 집어 창 선택에 놓는다 — 메인 선택은 그대로다
 * @details 메인 뷰포트의 피킹 길(`EditorViewportPicker`)은 활성 씬을 집어 메인 선택에 놓는다. 창은 `EditorPreviewPickUtil` 로 자기 씬만 본다.
 */
SW_TEST_CASE( EditorSelectionScopeTest, PreviewClickPicksIntoTheWindowSelection )
{
    SceneManager sceneManager;
    Scene*       pLevel   = sceneManager.createEmptyActiveScene( "PickLevel" );
    Scene*       pPreview = sceneManager.createPreviewScene( "PickWindow" );
    SW_ASSERT_NOT_NULL( pLevel );
    SW_ASSERT_NOT_NULL( pPreview );
    ScopedSceneManagerService scopedScene{ sceneManager };

    EditorSelection                 mainSelection;
    ScopedMainSceneSelectionService scopedMain{ mainSelection };
    // 같은 자리에 메인 씬 오브젝트도 둔다 — 창의 클릭이 활성 씬을 보면 이것을 집는다.
    SW_ASSERT_NOT_NULL( placeSceneObject( *pLevel->getObjectManager(), "LevelDecoy", float3{} ) );

    CommandStack    previewStack;
    EditorSelection previewSelection;
    previewSelection.bindPreviewScope( *pPreview, previewStack );
    GameObject* pSubject = placeSceneObject( *pPreview->getObjectManager(), "PreviewSubject", float3{} );
    SW_ASSERT_NOT_NULL( pSubject );

    EditorPickRay ray{};
    ray._origin    = float3{ 0.0f, 0.0f, -5.0f };
    ray._direction = float3{ 0.0f, 0.0f, 1.0f };
    SW_EXPECT_TRUE( EditorPreviewPickUtil::pickIntoSelection( previewSelection, ray, SelectionMode::Replace ) );
    SW_EXPECT_TRUE( previewSelection.getPrimaryObject() == pSubject );
    SW_EXPECT_EQUAL( size_t( 0 ), mainSelection.getSelectedObjectCount() );
    SW_EXPECT_TRUE( getFocusedSelection() == &previewSelection );

    // 빈 곳을 누르면 창 선택을 비운다.
    EditorPickRay miss{};
    miss._origin    = float3{ 50.0f, 50.0f, -5.0f };
    miss._direction = float3{ 0.0f, 0.0f, 1.0f };
    SW_EXPECT_FALSE( EditorPreviewPickUtil::pickIntoSelection( previewSelection, miss, SelectionMode::Replace ) );
    SW_EXPECT_EQUAL( size_t( 0 ), previewSelection.getSelectedObjectCount() );

    // 묶이지 않은 선택(메인)에는 놓지 않는다 — 메인 뷰포트의 피킹이 따로 있다.
    SW_EXPECT_FALSE( EditorPreviewPickUtil::pickIntoSelection( mainSelection, ray, SelectionMode::Replace ) );
    SW_EXPECT_EQUAL( size_t( 0 ), mainSelection.getSelectedObjectCount() );
    previewSelection.unbindPreviewScope();
}

/**
 * @brief [EditorSelectionScopeTest] 인스펙터의 구조 편집(컴포넌트 제거)도 미리보기 씬이면 창의 스택에 남고 메인 씬은 dirty 가 아니다
 * @details 인스펙터의 미리보기 갈래는 메인과 같은 명령(`EditorSceneCommands::destroyComponent`)을 부른다 — 기록의 스택은 오브젝트의 씬이 고른다.
 */
SW_TEST_CASE( EditorSelectionScopeTest, RemovingAPreviewComponentStaysInThePreviewStack )
{
    SceneManager sceneManager;
    Scene*       pLevel   = sceneManager.createEmptyActiveScene( "StructureLevel" );
    Scene*       pPreview = sceneManager.createPreviewScene( "StructureWindow" );
    SW_ASSERT_NOT_NULL( pLevel );
    SW_ASSERT_NOT_NULL( pPreview );
    ScopedSceneManagerService scopedScene{ sceneManager };

    CommandStack                    mainStack;
    ScopedCommandStackService       scopedStack{ mainStack };
    EditorSelection                 mainSelection;
    ScopedMainSceneSelectionService scopedMain{ mainSelection };
    EditorWorkspace                 workspace{ &mainSelection };
    ScopedWorkspaceService          scopedWorkspace{ workspace };

    CommandStack    previewStack;
    EditorSelection previewSelection;
    previewSelection.bindPreviewScope( *pPreview, previewStack );
    GameObjectManager* pPreviewObjects = pPreview->getObjectManager();
    GameObject*        pOwner          = placeSceneObject( *pPreviewObjects, "PreviewOwner", float3{} );
    SW_ASSERT_NOT_NULL( pOwner );
    BoxCollider2DComponent* pCollider = pOwner->addComponent<BoxCollider2DComponent>();
    SW_ASSERT_NOT_NULL( pCollider );
    pPreviewObjects->mergePendingAdds();
    workspace.clearSceneDirty();

    SW_ASSERT_TRUE( EditorSceneCommands::destroyComponent( pPreviewObjects, pOwner, pCollider ) );
    pPreviewObjects->processDeferredDestruction();
    SW_EXPECT_TRUE( pOwner->getComponent<BoxCollider2DComponent>() == nullptr );
    SW_EXPECT_EQUAL( size_t( 1 ), previewStack.getCommandCount() );
    SW_EXPECT_EQUAL( size_t( 0 ), mainStack.getCommandCount() );
    SW_EXPECT_FALSE( workspace.isSceneDirty() );

    previewStack.undo();
    pPreviewObjects->mergePendingAdds();
    GameObject* pRestored = pPreviewObjects->findGameObjectByName( hashed_string( "PreviewOwner" ) );
    SW_ASSERT_NOT_NULL( pRestored );
    SW_EXPECT_TRUE( pRestored->getComponent<BoxCollider2DComponent>() != nullptr );
    previewSelection.unbindPreviewScope();
}
