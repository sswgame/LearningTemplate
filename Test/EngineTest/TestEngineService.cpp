#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/File/AsyncFileIO.h"
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServiceListColumns.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Resource/AssetStreamingQueue.h"
#include "Engine/Scene/SceneManager.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) EngineServiceTest — 서비스 표가 게임 모듈에 무엇을 보여 주는지
//
//    `EngineServiceList.xxx` 의 `visibility` 열은 **게임 모듈이 손댈 수 있는 것과 없는 것의
//    경계**다. 열을 잘못 바꿔도 빌드는 통과하고 아무도 모른다. 그래서 검사도 같은 목록에서 생성한다 —
//    목록이 원본이다.
// ------------------------------------------------------------------------------

/**
 * @brief [EngineServiceTest] 게임 모듈의 범위에는 HostOnly 인 서비스가 하나도 없다
 * @details 목록의 `visibility` 칸이 게임 모듈이 손댈 수 있는 것과 없는 것의 경계다. 칸을 잘못 바꿔도 빌드는 통과하므로 검사도 같은 목록에서 생성한다.
 */
SW_TEST_CASE( EngineServiceTest, GameModuleScopeHidesHostOnlyServices )
{
    SubsystemScope editorScope;
    SubsystemScope gameScope;
    engine::bindModuleServices( editorScope, false );
    engine::bindModuleServices( gameScope, true );

#define SW_CHECK_SERVICE_VISIBILITY( Type, visibility )                          \
    {                                                                            \
        if constexpr ( SW_SERVICE_IS_GAME_VISIBLE( visibility ) == 0 )           \
            SW_EXPECT_NULL( gameScope.find<Type>() );                            \
        else                                                                     \
            SW_EXPECT_EQUAL( editorScope.find<Type>(), gameScope.find<Type>() ); \
    }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility )       SW_CHECK_SERVICE_VISIBILITY( Type, visibility )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility ) SW_CHECK_SERVICE_VISIBILITY( Type, visibility )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility )                SW_CHECK_SERVICE_VISIBILITY( Type, visibility )
#include "Engine/Common/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
#undef SW_CHECK_SERVICE_VISIBILITY

    // 게임에 보이는 필수 행은 실제로 걸린다(비어 있으면 위 비교가 nullptr == nullptr 로 지나간다).
    SW_EXPECT_NOT_NULL( gameScope.find<SceneManager>() );
    SW_EXPECT_NULL( gameScope.find<TaskManager>() ); // HostOnly
}

/**
 * @brief [EngineServiceTest] 테스트 하네스가 필수 서비스를 모두 바인딩했는지 검증
 * @details 많은 테스트가 `areEngineServicesBound()` 로 자기 본문을 게이팅한다. 이것이 false 면
 *          그 테스트들이 **통과한 것처럼 보이면서 아무것도 하지 않는다.**
 */
SW_TEST_CASE( EngineServiceTest, TestHarnessBindsEveryRequiredService )
{
    SW_EXPECT_TRUE( engine::areEngineServicesBound() );
}

/**
 * @brief [EngineServiceTest] `areEngineServicesBound()` 는 bind/unbind 를 따라오는 플래그 하나다
 * @details bind/unbind 때 한 번 센 플래그를 읽는다 — `Component::getTypeInfo()` 가 캐스트마다 부르므로 필수 서비스 칸을 매번 훑으면 안 된다.
 *          훑기와 같은 답이어야 한다: 풀린 표 → false, 필수가 빠진 범위 → false, 하네스의 범위 → true. 전역 표를 흔드는 테스트라 **원래 범위로
 *          되돌린다** — 뒤따르는 테스트가 전부 이 답으로 게이팅된다. 바인딩은 하네스의 창구(`test::rebindEngineServices`)로 한다.
 */
SW_TEST_CASE( EngineServiceTest, BoundFlagFollowsBindAndUnbind )
{
    const ISubsystemLocator* pSaved = engine::getEngineSubsystems();
    SW_ASSERT_NOT_NULL( pSaved );
    SW_ASSERT_TRUE( engine::areEngineServicesBound() );

    engine::unbindEngineServices();
    SW_EXPECT_FALSE( engine::areEngineServicesBound() );
    SW_EXPECT_NULL( engine::getEngineSubsystems() );

    const SubsystemScope empty;
    SW_EXPECT_NOT_NULL( test::rebindEngineServices( empty ) );
    SW_EXPECT_FALSE( engine::areEngineServicesBound() );

    SW_EXPECT_NULL( test::rebindEngineServices( *pSaved ) );
    SW_EXPECT_TRUE( engine::areEngineServicesBound() );
}

/**
 * @brief [EngineServiceTest] 비어 있는 필수 서비스는 **이름으로** 보고된다
 * @details `areEngineServicesBound()` 가 false 라는 사실만으로는 아무도 원인을 못 짚는다 — 그 함수로 게이팅되는 자리가 스무 곳이 넘고, 하나가 비면
 *          그 스무 곳이 전부 조용히 폴백으로 간다. 그래서 엔진 루프는 이 이름이 nullptr 가 아니면 서지 않는다. 범위를 인자로 받는 형태라 전역 바인딩을
 *          흔들지 않고 물어볼 수 있다. 행 순서가 곧 보고 순서다(목록의 첫 행은 명령줄).
 */
SW_TEST_CASE( EngineServiceTest, MissingRequiredServiceIsReportedByName )
{
    const SubsystemScope empty;
    const utf8*          pMissing = engine::findUnboundRequiredServiceName( empty );
    SW_ASSERT_NOT_NULL( pMissing );
    SW_EXPECT_STREQ( "CommandLineManager", pMissing );

    // 하네스의 범위를 부모로 두면 빠진 것이 없다 — 부모 범위까지 찾는다.
    const SubsystemScope child{ engine::getEngineSubsystems() };
    SW_EXPECT_NULL( engine::findUnboundRequiredServiceName( child ) );
    SW_EXPECT_NULL( engine::findUnboundRequiredServiceName() );
}

/**
 * @brief [EngineServiceTest] 같은 이름을 넣은 범위는 부모의 서비스를 가린다 — 테스트가 서비스 하나를 갈아 끼우는 길
 */
SW_TEST_CASE( EngineServiceTest, OverrideScopeShadowsTheParentService )
{
    TaskManager* pHarnessTaskManager = &engine::getTaskManager();
    TaskManager* pOverride           = nullptr;
    {
        test::ScopedEngineServiceOverride scope;
        pOverride = &scope.add( sw::make_unique<TaskManager>() );
        SW_ASSERT_TRUE( scope.bind() == nullptr );
        SW_EXPECT_TRUE( &engine::getTaskManager() == pOverride );
        SW_EXPECT_TRUE( &engine::getAssetManager() == engine::getBoundEngineServices()._pAssetManager ); // 나머지는 부모의 것
    }
    SW_EXPECT_TRUE( &engine::getTaskManager() == pHarnessTaskManager );
}

/**
 * @brief [EngineServiceTest] 스트리밍 큐는 그 소멸자가 기다리는 태스크 매니저보다 먼저 소멸한다
 * @details 서비스의 소멸 순서는 서브시스템의 의존이 정한다(역순 소멸). 의존이 빠지면 이름 순으로 놓여 `~AssetStreamingQueue` 의
 *          `getTaskManager().waitAll()` 이 해제된 객체를 읽는다(ASAN 구성에서 heap-use-after-free).
 */
SW_TEST_CASE( EngineServiceTest, StreamingQueueIsDestroyedBeforeTheTaskManagerItWaitsOn )
{
    SubsystemCollection collection;
    collection.add( sw::make_unique<AssetStreamingQueue>() );
    collection.add( sw::make_unique<AsyncFileIO>() );
    collection.add( sw::make_unique<TaskManager>() );
    SW_ASSERT_TRUE( collection.sort() );

    size_t queueOrder = 0;
    size_t taskOrder  = 0;
    for ( size_t order = 0; order < collection.getInitializeOrder().size(); ++order )
    {
        const string_view name{ collection.getSubsystem( collection.getInitializeOrder()[order] ).getSubsystemName() };
        if ( name == AssetStreamingQueue::kSubsystemName )
            queueOrder = order;
        if ( name == TaskManager::kSubsystemName )
            taskOrder = order;
    }
    // 소멸은 초기화의 역순이다 — 큐가 뒤에 서야 먼저 사라진다.
    SW_EXPECT_TRUE_MSG( taskOrder < queueOrder, "AssetStreamingQueue must be released before the TaskManager its destructor waits on" );
}
