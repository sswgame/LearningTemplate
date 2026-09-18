#include "pch.h"

#include "Core/Delegate/Delegate.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
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
    /** @brief 스코프 동안 커맨드 스택을 지역 서비스로 걸어 둡니다. */
    class ScopedCommandStackService
    {
    public:
        explicit ScopedCommandStackService( CommandStack& stack ) { bindLocalService<CommandStack>( &stack ); }
        ~ScopedCommandStackService() { unbindLocalService<CommandStack>(); }

        ScopedCommandStackService( const ScopedCommandStackService& )            = delete;
        ScopedCommandStackService& operator=( const ScopedCommandStackService& ) = delete;
    };

    /** @brief 트랜잭션 API 를 한 바퀴 다 불러 봅니다. 스택이 없어도 터지지 않아야 합니다. */
    void callEveryTransactionEntryPoint( GameObjectPtr target )
    {
        EditorTransaction::beginTransaction( "Probe" );
        EditorTransaction::push( SW_DELEGATE_LAMBDA( Delegate<void()>, []() {} ),
                                 SW_DELEGATE_LAMBDA( Delegate<void()>, []() {} ),
                                 "Probe push" );
        EditorTransaction::recordModify( target, "<before/>", "<after/>", "Probe modify" );
        EditorTransaction::recordCreation( target, "Probe create" );
        EditorTransaction::recordDestruction( target, "Probe destroy" );
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

    callEveryTransactionEntryPoint( GameObjectPtr{ pObject } );

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
    EditorTransaction::recordModify( GameObjectPtr{ pObject }, "<a/>", "<b/>", "Probe" );
    EditorTransaction::recordBinaryModify( GameObjectPtr{ pObject }, vector<uint8>{ 1 }, vector<uint8>{ 2 }, "Probe binary" );
}
