#include "pch.h"

#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Commands/Scene/EditorSiblingOrder.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// EditorSiblingOrder — 계층 창의 형제 순서 · 끌어 놓기 · 폴더(ImGui 없음).

namespace
{
    struct EditorSiblingOrderTestInternal
    {
        /** @brief SceneComponent 를 가진 루트 오브젝트를 이름 순서대로 만듭니다. */
        static void spawnRoots( GameObjectManager& manager, std::initializer_list<const utf8*> listName )
        {
            for ( const utf8* pName : listName )
            {
                GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
                if ( pObject != nullptr )
                    (void)pObject->addComponent<SceneComponent>();
            }
            manager.mergePendingAdds();
        }

        static string rootNames( GameObjectManager& manager )
        {
            manager.mergePendingAdds();
            manager.processDeferredDestruction();
            vector<GameObject*> listRoot;
            EditorSceneCommands::collectRootsInOrder( manager, listRoot );
            string names;
            for ( const GameObject* pRoot : listRoot )
            {
                names += pRoot->getName().c_str();
            }
            return names;
        }

        static string childNames( GameObjectManager& manager, const utf8* pParentName )
        {
            manager.mergePendingAdds();
            manager.processDeferredDestruction();
            const GameObject*   pParent = manager.findGameObjectByName( hashed_string( pParentName ) );
            vector<GameObject*> listChild;
            if ( pParent != nullptr )
                EditorSiblingOrder::collectChildrenInOrder( *pParent, listChild );
            string names;
            for ( const GameObject* pChild : listChild )
            {
                names += pChild->getName().c_str();
            }
            return names;
        }

        static GameObject* find( GameObjectManager& manager, const utf8* pName )
        {
            return manager.findGameObjectByName( hashed_string( pName ) );
        }
    };
} // namespace

/**
 * @brief [EditorSiblingOrderTest] 끌어 놓는 자리는 행의 위쪽 가장자리 · 가운데 · 아래쪽 가장자리로 갈린다
 */
SW_TEST_CASE( EditorSiblingOrderTest, DropZoneFollowsTheCursorInsideTheRow )
{
    SW_EXPECT_TRUE( EditorSiblingOrder::computeDropZone( 100.0f, 100.0f, 120.0f ) == SiblingDropZone::Before );
    SW_EXPECT_TRUE( EditorSiblingOrder::computeDropZone( 107.0f, 100.0f, 120.0f ) == SiblingDropZone::Onto );
    SW_EXPECT_TRUE( EditorSiblingOrder::computeDropZone( 110.0f, 100.0f, 120.0f ) == SiblingDropZone::Onto );
    SW_EXPECT_TRUE( EditorSiblingOrder::computeDropZone( 119.0f, 100.0f, 120.0f ) == SiblingDropZone::After );
    SW_EXPECT_TRUE( EditorSiblingOrder::computeDropZone( 110.0f, 100.0f, 100.0f ) == SiblingDropZone::Onto ); // 높이가 0 이면 가운데로 본다
}

/**
 * @brief [EditorSiblingOrderTest] 번호가 모두 같으면 오브젝트 id 순(옛 씬의 파일 순서)이고, 번호가 있으면 번호 순이다
 */
SW_TEST_CASE( EditorSiblingOrderTest, IndexBeatsIDAndTiesFallBackToIDs )
{
    using Internal = EditorSiblingOrderTestInternal;
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "SiblingIndexProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    GameObjectManager* pManager = pScene->getObjectManager();
    Internal::spawnRoots( *pManager, { "A", "B", "C" } );
    SW_EXPECT_EQUAL( string( "ABC" ), Internal::rootNames( *pManager ) );

    Internal::find( *pManager, "C" )->setSiblingIndex( 0 );
    Internal::find( *pManager, "A" )->setSiblingIndex( 1 );
    Internal::find( *pManager, "B" )->setSiblingIndex( 2 );
    SW_EXPECT_EQUAL( string( "CAB" ), Internal::rootNames( *pManager ) );

    Internal::find( *pManager, "B" )->setSiblingIndex( 0 ); // C 와 B 가 같은 번호면 id 순(B 가 먼저 만들어졌다)
    SW_EXPECT_EQUAL( string( "BCA" ), Internal::rootNames( *pManager ) );
}

/**
 * @brief [EditorSiblingOrderTest] 끌어 놓은 순서는 되돌리기 한 단계이고 되돌리면 처음 순서다
 */
SW_TEST_CASE( EditorSiblingOrderTest, MoveBesideIsOneUndoStep )
{
    using Internal = EditorSiblingOrderTestInternal;
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "MoveBesideProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };
    GameObjectManager*        pManager = pScene->getObjectManager();
    Internal::spawnRoots( *pManager, { "A", "B", "C" } );

    SW_ASSERT_TRUE( EditorSiblingOrder::moveBeside( *pManager, Internal::find( *pManager, "C" ), Internal::find( *pManager, "A" ), false ) );
    SW_EXPECT_EQUAL( string( "CAB" ), Internal::rootNames( *pManager ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCommandCount() );

    stack.undo();
    SW_EXPECT_EQUAL( string( "ABC" ), Internal::rootNames( *pManager ) );
    stack.redo();
    SW_EXPECT_EQUAL( string( "CAB" ), Internal::rootNames( *pManager ) );

    // 뒤로 옮기기
    SW_ASSERT_TRUE( EditorSiblingOrder::moveBeside( *pManager, Internal::find( *pManager, "C" ), Internal::find( *pManager, "B" ), true ) );
    SW_EXPECT_EQUAL( string( "ABC" ), Internal::rootNames( *pManager ) );
    // 자기 자신 옆으로는 옮기지 않는다
    SW_EXPECT_FALSE( EditorSiblingOrder::moveBeside( *pManager, Internal::find( *pManager, "A" ), Internal::find( *pManager, "A" ), true ) );
}

/**
 * @brief [EditorSiblingOrderTest] 다른 부모의 형제 옆에 놓으면 그 부모의 자식이 되어 그 자리에 선다 — 자기 후손 옆으로는 옮기지 않는다
 */
SW_TEST_CASE( EditorSiblingOrderTest, MoveBesideAcrossParentsReparents )
{
    using Internal = EditorSiblingOrderTestInternal;
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "MoveAcrossParentsProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };
    GameObjectManager*        pManager = pScene->getObjectManager();
    Internal::spawnRoots( *pManager, { "P", "X", "Y", "Z" } );
    SW_ASSERT_TRUE( Internal::find( *pManager, "X" )->attachToParent( Internal::find( *pManager, "P" ) ) );
    SW_ASSERT_TRUE( Internal::find( *pManager, "Y" )->attachToParent( Internal::find( *pManager, "P" ) ) );
    SW_EXPECT_EQUAL( string( "XY" ), Internal::childNames( *pManager, "P" ) );
    SW_EXPECT_EQUAL( string( "PZ" ), Internal::rootNames( *pManager ) );

    SW_ASSERT_TRUE( EditorSiblingOrder::moveBeside( *pManager, Internal::find( *pManager, "Z" ), Internal::find( *pManager, "Y" ), false ) );
    SW_EXPECT_EQUAL( string( "XZY" ), Internal::childNames( *pManager, "P" ) );
    SW_EXPECT_EQUAL( string( "P" ), Internal::rootNames( *pManager ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCommandCount() );

    // 부모를 자기 자식 옆으로 옮기면 순환이다
    SW_EXPECT_FALSE( EditorSiblingOrder::moveBeside( *pManager, Internal::find( *pManager, "P" ), Internal::find( *pManager, "X" ), true ) );

    stack.undo();
    SW_EXPECT_EQUAL( string( "XY" ), Internal::childNames( *pManager, "P" ) );
    SW_EXPECT_EQUAL( string( "PZ" ), Internal::rootNames( *pManager ) );
}

/**
 * @brief [EditorSiblingOrderTest] 한 칸씩 옮기기는 끝에서 멈춘다
 */
SW_TEST_CASE( EditorSiblingOrderTest, MoveStepStopsAtTheEnds )
{
    using Internal = EditorSiblingOrderTestInternal;
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "MoveStepProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };
    GameObjectManager*        pManager = pScene->getObjectManager();
    Internal::spawnRoots( *pManager, { "A", "B", "C" } );

    SW_EXPECT_FALSE( EditorSiblingOrder::moveStep( *pManager, Internal::find( *pManager, "A" ), false ) );
    SW_EXPECT_TRUE( EditorSiblingOrder::moveStep( *pManager, Internal::find( *pManager, "A" ), true ) );
    SW_EXPECT_EQUAL( string( "BAC" ), Internal::rootNames( *pManager ) );
    SW_EXPECT_TRUE( EditorSiblingOrder::moveStep( *pManager, Internal::find( *pManager, "A" ), true ) );
    SW_EXPECT_EQUAL( string( "BCA" ), Internal::rootNames( *pManager ) );
    SW_EXPECT_FALSE( EditorSiblingOrder::moveStep( *pManager, Internal::find( *pManager, "A" ), true ) );
    SW_EXPECT_TRUE( EditorSiblingOrder::moveStep( *pManager, Internal::find( *pManager, "A" ), false ) );
    SW_EXPECT_EQUAL( string( "BAC" ), Internal::rootNames( *pManager ) );
}

/**
 * @brief [EditorSiblingOrderTest] 형제 순서 옮기기 · 이름 바꾸기와 그 되돌리기 · 다시 하기가 모두 구조 세대를 올린다 — 계층 창의 행 모델이 이 세대로만 다시 짓는다
 */
SW_TEST_CASE( EditorSiblingOrderTest, ReorderAndRenameBumpStructureGenerationThroughUndo )
{
    using Internal = EditorSiblingOrderTestInternal;
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "StructureGenerationProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };
    GameObjectManager*        pManager = pScene->getObjectManager();
    Internal::spawnRoots( *pManager, { "A", "B", "C" } );

    uint64 generation = pManager->getStructureGeneration();
    SW_ASSERT_TRUE( EditorSiblingOrder::moveStep( *pManager, Internal::find( *pManager, "A" ), true ) );
    SW_EXPECT_TRUE( pManager->getStructureGeneration() != generation );
    SW_EXPECT_EQUAL( string( "BAC" ), Internal::rootNames( *pManager ) );

    generation = pManager->getStructureGeneration();
    stack.undo();
    SW_EXPECT_TRUE( pManager->getStructureGeneration() != generation );
    SW_EXPECT_EQUAL( string( "ABC" ), Internal::rootNames( *pManager ) );
    generation = pManager->getStructureGeneration();
    stack.redo();
    SW_EXPECT_TRUE( pManager->getStructureGeneration() != generation );

    generation = pManager->getStructureGeneration();
    SW_ASSERT_TRUE( EditorSceneCommands::rename( Internal::find( *pManager, "B" ), "Bee" ) );
    SW_EXPECT_TRUE( pManager->getStructureGeneration() != generation );
    SW_EXPECT_NOT_NULL( Internal::find( *pManager, "Bee" ) );

    generation = pManager->getStructureGeneration();
    stack.undo();
    SW_EXPECT_TRUE( pManager->getStructureGeneration() != generation );
    SW_EXPECT_NOT_NULL( Internal::find( *pManager, "B" ) );
}

/**
 * @brief [EditorSiblingOrderTest] 폴더는 태그가 붙은 빈 오브젝트이고 만들기는 되돌리기 한 단계다 — 자식을 폴더에 넣을 수 있다
 */
SW_TEST_CASE( EditorSiblingOrderTest, FolderIsATaggedEmptyObject )
{
    using Internal = EditorSiblingOrderTestInternal;
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "FolderProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };
    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };
    GameObjectManager*        pManager = pScene->getObjectManager();
    Internal::spawnRoots( *pManager, { "Lamp" } );

    GameObject* pFolder = EditorSiblingOrder::createFolder( *pManager, nullptr );
    SW_ASSERT_NOT_NULL( pFolder );
    SW_EXPECT_TRUE( EditorSiblingOrder::isFolder( *pFolder ) );
    SW_EXPECT_FALSE( EditorSiblingOrder::isFolder( *Internal::find( *pManager, "Lamp" ) ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, stack.getCommandCount() );
    SW_EXPECT_TRUE( pFolder->getParent() == nullptr );

    // 폴더에 오브젝트를 넣는다 — 폴더의 자식이 되고, 폴더는 되돌리기로 한 번에 사라진다.
    const uint64 folderID = pFolder->getObjectID();
    GameObject*  pLamp    = Internal::find( *pManager, "Lamp" );
    SW_ASSERT_TRUE( EditorSceneCommands::reparent( pLamp, pManager->findGameObjectByID( folderID ) ) );
    GameObject* pFolderAgain = pManager->findGameObjectByID( folderID );
    SW_ASSERT_NOT_NULL( pFolderAgain );
    vector<GameObject*> listChild;
    EditorSiblingOrder::collectChildrenInOrder( *pFolderAgain, listChild );
    SW_EXPECT_EQUAL( size_t{ 1 }, listChild.size() );
    SW_EXPECT_TRUE( EditorSiblingOrder::isFolder( *pFolderAgain ) );
}
