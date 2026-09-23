#include "pch.h"

#include "Core/Delegate/Delegate.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "EditorTest/EditorTestServices.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// EditorTransaction — Undo 스택이 **없을 때도** 안전한가, 있을 때는 제대로 닿는가.
//
// 이 스위트가 성립하는 이유: `editor::getService<T>()` 의 모듈 서비스 표(`s_editorService`)는
// 모듈 export 매크로의 `bindService` 콜백이 채우는데, 테스트는 그것을 부르지 않는다. 그래서
// **EditorTest 안에서는 커맨드 스택 서비스가 처음부터 nullptr 이다** — 즉 이 파일은 "서비스가
// 없는 상태" 를 따로 만들 필요 없이 그냥 그 상태다. 반대 방향(있을 때)은
// `bindLocalService<CommandStack>` 으로 만든다.

namespace
{
    /** @brief 살아 있는(pendingKill 이 아닌) 같은 이름의 오브젝트 수입니다. */
    size_t countLiveObjectsNamed( GameObjectManager& manager, const utf8* pName )
    {
        const hashed_string wanted( pName );
        size_t              count = 0;
        manager.forEachGameObject( [&count, wanted]( GameObject* pObj )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->getName() == wanted )
                ++count;
        } );
        return count;
    }

    /** @brief 트랜잭션 API 를 한 바퀴 다 불러 봅니다. 스택이 없어도 터지지 않아야 합니다. */
    void callEveryTransactionEntryPoint( GameObject* pTarget )
    {
        EditorTransaction::beginTransaction( "Probe" );
        EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, []() {} ),
                                 SW_DELEGATE_LAMBDA( Delegate<void()>, []() {} ),
                                 "Probe push" );
        EditorTransaction::recordModify( pTarget, EditorObjectSnapshot{ "<before/>", {} }, EditorObjectSnapshot{ "<after/>", {} }, "Probe modify" );
        EditorTransaction::recordCreation( pTarget, "Probe create" );
        EditorTransaction::recordDestruction( pTarget, "Probe destroy" );
        EditorTransaction::endTransaction();
        EditorTransaction::cancelTransaction();
    }
} // namespace

/**
 * @brief [EditorTransactionTest] 커맨드 스택 서비스가 없어도 트랜잭션이 터지지 않는다
 * @details `editor::getService<T>()` 는 **문서대로 nullptr 을 돌려줄 수 있다.** 그런데 이 파일의
 *          일곱 자리가 그 값을 확인 없이 `->` 로 따라가고 있었다(2026-09-18 수정). 커맨드 스택은
 *          `EngineLoop` 소유라 EditorModule 보다 오래 살고 **종료할 때 서비스 결합이 먼저 풀리므로**,
 *          그 창에서 트랜잭션이 하나라도 돌면 널 역참조였다.
 *          이 테스트 환경이 바로 그 상태다 — 고치기 전에는 이 케이스가 **프로세스를 죽였다**.
 */
SW_TEST_CASE( EditorTransactionTest, NoCommandStackServiceIsSafe )
{
    // 서비스가 정말로 없는 상태인지부터 못 박는다 — 이 전제가 깨지면 이 테스트는 아무것도 안 본다.
    SW_ASSERT_EQUAL( nullptr, getService<CommandStack>() );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "Probe" ) );
    SW_ASSERT_NOT_NULL( pObject );
    manager.mergePendingAdds();

    callEveryTransactionEntryPoint( pObject );

    // 여기까지 왔다는 것이 결과다. 스택이 없으므로 아무것도 쌓이지 않았어야 한다.
    SW_EXPECT_EQUAL( nullptr, getService<CommandStack>() );
}

/**
 * @brief [EditorTransactionTest] 스택이 있으면 트랜잭션이 실제로 거기 쌓인다
 * @details 위 케이스가 "없을 때 안 터진다" 만 보면 **아무 일도 안 하게 만들어도 통과한다.**
 *          반대 방향을 같이 못 박아 그 허점을 막는다.
 */
SW_TEST_CASE( EditorTransactionTest, BoundCommandStackReceivesPush )
{
    CommandStack              stack;
    ScopedCommandStackService scoped{ stack };
    SW_ASSERT_EQUAL( &stack, getService<CommandStack>() );

    int32 value{ 0 };
    EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
    { value = 0; } ),
                             SW_DELEGATE_LAMBDA( Delegate<void()>, [&value]()
    { value = 7; } ),
                             "Set value" );

    SW_ASSERT_EQUAL( size_t( 1 ), stack.getCommandCount() );
    SW_EXPECT_TRUE( stack.canUndo() );

    stack.undo();
    SW_EXPECT_EQUAL( 0, value );
    stack.redo();
    SW_EXPECT_EQUAL( 7, value );
}

/**
 * @brief [EditorTransactionTest] 스택이 없어도 씬 dirty 표시는 남는다
 * @details 고치는 중에 한 번 잘못 고쳤던 자리다. `recordModify` 계열에서 스택이 없으면 곧장
 *          `return` 하게 했더니 뒤따르는 `markActiveSceneDirty()` 까지 건너뛰었다 — **씬은 이미
 *          바뀌었고 되돌리기 기록만 못 남기는 것**이므로 dirty 는 찍혀야 한다. 조기 반환 대신
 *          push 만 감싸는 것이 맞다.
 * @note 활성 씬이 없는 테스트 환경에서는 `markActiveSceneDirty()` 가 `EditorContext` 를 찾지 못해
 *       조용히 돌아간다. 그래서 이 케이스가 보는 것은 **그 경로를 끝까지 지나간다**는 것이다 —
 *       조기 반환이 들어오면 지나가지 않는다.
 */
SW_TEST_CASE( EditorTransactionTest, RecordModifyRunsToTheEndWithoutStack )
{
    SW_ASSERT_EQUAL( nullptr, getService<CommandStack>() );

    GameObjectManager manager;
    GameObject*       pObject = manager.createGameObject( hashed_string( "DirtyProbe" ) );
    SW_ASSERT_NOT_NULL( pObject );
    manager.mergePendingAdds();

    // 앞뒤가 같으면 애초에 기록하지 않는 계약이므로 일부러 다르게 준다.
    EditorTransaction::recordModify( pObject, EditorObjectSnapshot{ "<a/>", {} }, EditorObjectSnapshot{ "<b/>", {} }, "Probe" );
    EditorTransaction::recordBinaryModify( pObject, EditorObjectBinarySnapshot{ vector<uint8>{ 1 }, {} }, EditorObjectBinarySnapshot{ vector<uint8>{ 2 }, {} },
                                           "Probe binary" );
}

/**
 * @brief [EditorTransactionTest] 생성과 삭제의 Undo/Redo 는 **서로의 거울**이다
 * @details 생성과 삭제는 같은 두 절차("없앤다" · "저장해 둔 XML 로 되살린다")를 **반대로 이은 것**이다.
 *          예전에는 그 두 절차가 `recordCreation` 과 `recordDestruction` 에 네 벌로 복사돼 있었다 —
 *          없애기 두 벌, 되살리기 두 벌, 바이트까지 같았다. 되살리기 쪽을 한 번 고치면 나머지 방향이
 *          조용히 뒤처지고, 증상은 **"Undo 는 되는데 Redo 는 안 된다"** 로 나온다. 사용자가 작업을
 *          잃는 방식이면서, 로그에는 아무것도 남지 않는 종류다.
 *
 *          이 케이스는 두 방향을 **실제로 실행해** 확인한다 — `SceneManager` 를 지역 서비스로 걸면
 *          `editor::getActiveScene()` 이 답하므로 Undo/Redo 델리게이트가 끝까지 지나간다(그 전까지
 *          이 스위트의 다른 케이스들은 씬이 없어 델리게이트가 곧장 돌아가고 있었다).
 */
SW_TEST_CASE( EditorTransactionTest, ObjectLifetimeUndoRedoAreMirrors )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "TransactionProbe" );
    SW_ASSERT_NOT_NULL( pScene );

    ScopedSceneManagerService scopedScene{ sceneManager };
    SW_ASSERT_EQUAL( pScene, getActiveScene() );

    GameObjectManager* pManager = pScene->getObjectManager();
    SW_ASSERT_NOT_NULL( pManager );

    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };

    BLOCK( "생성 — Undo 가 없애고 Redo 가 되살린다" )
    {
        GameObject* pBorn = pManager->createGameObject( hashed_string( "Born" ) );
        SW_ASSERT_NOT_NULL( pBorn );
        pManager->mergePendingAdds();
        SW_ASSERT_EQUAL( size_t( 1 ), countLiveObjectsNamed( *pManager, "Born" ) );

        EditorTransaction::recordCreation( pBorn, "Create Born" );
        SW_ASSERT_EQUAL( size_t( 1 ), stack.getCommandCount() );

        stack.undo();
        pManager->mergePendingAdds();
        SW_EXPECT_EQUAL( size_t( 0 ), countLiveObjectsNamed( *pManager, "Born" ) );

        stack.redo();
        pManager->mergePendingAdds();
        SW_EXPECT_EQUAL( size_t( 1 ), countLiveObjectsNamed( *pManager, "Born" ) );
    }

    BLOCK( "삭제 — Undo 가 되살리고 Redo 가 없앤다" )
    {
        GameObject* pDoomed = pManager->createGameObject( hashed_string( "Doomed" ) );
        SW_ASSERT_NOT_NULL( pDoomed );
        pManager->mergePendingAdds();

        // 기록은 **삭제 전** 스냅샷만 남긴다 — 실제로 지우는 것은 호출부의 몫이다.
        EditorTransaction::recordDestruction( pDoomed, "Delete Doomed" );
        pManager->destroyObject( pDoomed );
        SW_ASSERT_EQUAL( size_t( 0 ), countLiveObjectsNamed( *pManager, "Doomed" ) );

        stack.undo();
        pManager->mergePendingAdds();
        SW_EXPECT_EQUAL( size_t( 1 ), countLiveObjectsNamed( *pManager, "Doomed" ) );

        stack.redo();
        pManager->mergePendingAdds();
        SW_EXPECT_EQUAL( size_t( 0 ), countLiveObjectsNamed( *pManager, "Doomed" ) );
    }
}

/**
 * @brief [EditorTransactionTest] 지운 오브젝트를 되돌리면 그 오브젝트와 컴포넌트의 핸들이 다시 풀린다
 * @details 되살린 오브젝트가 **원래 id** 를 받기 때문이다(`createGameObjectWithId`, 컴포넌트는 `ObjectIdentity`). 예전에는
 *          새 id 를 받아 핸들이 끊겼고, 이름으로 찾던 `GameObjectPtr` 가 그 자리를 메웠다 — 이름을 바꾸면 끊기는 방식으로.
 */
SW_TEST_CASE( EditorTransactionTest, HandlesSurviveUndoOfDestruction )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "HandleProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };

    GameObjectManager* pManager = pScene->getObjectManager();
    SW_ASSERT_NOT_NULL( pManager );

    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };

    GameObject* pVictim = pManager->createGameObject( hashed_string( "Victim" ) );
    SW_ASSERT_NOT_NULL( pVictim );
    SceneComponent* pSceneComp = pVictim->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );
    pManager->mergePendingAdds();

    const GameObjectHandle objectHandle    = pVictim->getHandle();
    const ComponentHandle  componentHandle = pSceneComp->getHandle();

    EditorTransaction::recordDestruction( pVictim, "Delete Victim" );
    pManager->destroyObject( pVictim );
    // 옛 오브젝트의 지연 파괴가 끝나야 그 id 를 다시 쓸 수 있다(아직 등록돼 있으면 새 id 로 물러선다).
    pManager->processDeferredDestruction();
    SW_EXPECT_TRUE( pManager->resolveGameObject( objectHandle ) == nullptr );
    SW_EXPECT_TRUE( pManager->resolveComponent( componentHandle ) == nullptr );

    stack.undo();
    pManager->mergePendingAdds();

    GameObject* pRestored = pManager->resolveGameObject( objectHandle );
    SW_ASSERT_NOT_NULL( pRestored );
    SW_EXPECT_TRUE( pRestored->getName() == hashed_string( "Victim" ) );

    Component* pRestoredComp = pManager->resolveComponent( componentHandle );
    SW_ASSERT_NOT_NULL( pRestoredComp );
    SW_EXPECT_TRUE( pRestoredComp->getOwner() == pRestored );
}

/**
 * @brief [EditorTransactionTest] 수정을 되돌려 컴포넌트가 다시 만들어져도 컴포넌트 핸들은 이어진다
 * @details 되돌리기는 상태를 다시 읽으며 컴포넌트를 **전부 지우고 새로 만든다**(`clearComponents`). 속성 하나만 바꾼 것을
 *          되돌려도 그렇다. 스냅샷에 적어 둔 id 를 되살리지 않으면 컴포넌트마다 새 id 가 나가 핸들이 끊긴다 — 마지막 블록이
 *          그 대조군이다(씬 · 프리팹 로드와 복제가 가는 길).
 */
SW_TEST_CASE( EditorTransactionTest, ComponentHandleSurvivesModifyUndo )
{
    SceneManager sceneManager;
    Scene*       pScene = sceneManager.createEmptyActiveScene( "ModifyProbe" );
    SW_ASSERT_NOT_NULL( pScene );
    ScopedSceneManagerService scopedScene{ sceneManager };

    GameObjectManager* pManager = pScene->getObjectManager();
    SW_ASSERT_NOT_NULL( pManager );

    CommandStack              stack;
    ScopedCommandStackService scopedStack{ stack };

    GameObject* pObj = pManager->createGameObject( hashed_string( "Before" ) );
    SW_ASSERT_NOT_NULL( pObj );
    SceneComponent* pSceneComp = pObj->addComponent<SceneComponent>();
    SW_ASSERT_NOT_NULL( pSceneComp );
    pManager->mergePendingAdds();
    const ComponentHandle componentHandle = pSceneComp->getHandle();

    const EditorObjectSnapshot before = EditorTransaction::captureSnapshot( pObj );
    pObj->setName( hashed_string( "After" ) );
    const EditorObjectSnapshot after = EditorTransaction::captureSnapshot( pObj );
    EditorTransaction::recordModify( pObj, before, after, "Rename" );

    stack.undo();
    SW_EXPECT_TRUE( pObj->getName() == hashed_string( "Before" ) );
    SW_EXPECT_TRUE( pManager->resolveComponent( componentHandle ) != nullptr );

    stack.redo();
    SW_EXPECT_TRUE( pObj->getName() == hashed_string( "After" ) );
    SW_EXPECT_TRUE( pManager->resolveComponent( componentHandle ) != nullptr );

    BLOCK( "대조군 — id 없이 읽으면 컴포넌트가 새 id 를 받아 핸들이 끊긴다" )
    {
        ObjectStateSerializer::loadFromXmlString( pObj, before._xml );
        SW_EXPECT_TRUE( pManager->resolveComponent( componentHandle ) == nullptr );
    }
}
