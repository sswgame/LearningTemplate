#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/EngineServiceCollection.h"

#include "RuntimeAPI/Service/ModuleService.h"
#include "RuntimeAPI/Service/ServiceListColumns.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) EngineServiceTest — 서비스 표가 게임 모듈에 무엇을 보여 주는지
//
//    `EngineServiceList.xxx` 의 `visibility` 열은 **게임 모듈이 손댈 수 있는 것과 없는 것의
//    경계**다. 열을 잘못 바꿔도 빌드는 통과하고 아무도 모른다. 그래서 검사도 같은 목록에서 생성한다 —
//    목록이 정본이다.
// ------------------------------------------------------------------------------

/**
 * @brief [EngineServiceTest] 게임 모듈용 표에 HostOnly 인 서비스가 하나도 없는지 검증
 */
SW_TEST_CASE( EngineServiceTest, GameModuleTableHidesHostOnlyServices )
{
    ModuleService editorTable{};
    ModuleService gameTable{};
    engine::fillModuleServices( editorTable, false );
    engine::fillModuleServices( gameTable, true );

#define SW_CHECK_SERVICE_VISIBILITY( Type, visibility )                                      \
    {                                                                                        \
        const uint32 rawId = internal::toRawServiceId( internal::ModuleServiceId::Type );    \
        if constexpr ( SW_SERVICE_IS_GAME_VISIBLE( visibility ) == 0 )                       \
            SW_EXPECT_NULL( gameTable.arrServices[rawId] );                                  \
        else                                                                                 \
            SW_EXPECT_EQUAL( editorTable.arrServices[rawId], gameTable.arrServices[rawId] ); \
    }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       SW_CHECK_SERVICE_VISIBILITY( Type, visibility )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) SW_CHECK_SERVICE_VISIBILITY( Type, visibility )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                SW_CHECK_SERVICE_VISIBILITY( Type, visibility )
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
#undef SW_CHECK_SERVICE_VISIBILITY
}

/**
 * @brief [EngineServiceTest] 호스트 전용 서비스 자리는 fillModuleServices 가 건드리지 않는지 검증
 * @details `fillModuleServices` 는 표를 먼저 통째로 비운다. 그래서 호스트 서비스는 **그 뒤에**
 *          채워야 하고, ModuleHost 가 실제로 그 순서를 지킨다. 순서가 뒤집히면 에디터 모듈이
 *          모듈 컴파일러를 잃는다.
 */
SW_TEST_CASE( EngineServiceTest, FillClearsTheWholeTableFirst )
{
    ModuleService table{};
    // 호스트 서비스 자리에 미리 값을 넣어 둔다 — Engine 이 채우는 자리가 아니다.
    const uint32 hostRawId       = internal::toRawServiceId( internal::ModuleServiceId::IModuleCompiler );
    table.arrServices[hostRawId] = &table;

    engine::fillModuleServices( table, false );

    SW_EXPECT_NULL( table.arrServices[hostRawId] );
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
 * @details bind/unbind 때 한 번 센 플래그를 읽는다 — `Component::getTypeInfo()` 가 캐스트마다 부르므로 필수 서비스 칸을
 *          매번 훑으면 안 된다. 훑기와 같은 답이어야 한다: 빈 표 → false, 필수 하나가 빈 표 → false, 완전한 표 → true. 전역 표를 흔드는
 *          테스트라 **원래 표를 그대로 되돌린다** — 뒤따르는 테스트가 전부 이 답으로 게이팅된다. 바인딩은
 *          하네스의 창구(`test::rebindEngineServices`)로 한다 — 이 파일이 직접 부르면 호스트로 잡힌다.
 */
SW_TEST_CASE( EngineServiceTest, BoundFlagFollowsBindAndUnbind )
{
    SW_TEST_SUPPRESS_LOGS();

    const EngineServices saved = engine::getBoundEngineServices();
    SW_ASSERT_TRUE( engine::areEngineServicesBound() );

    engine::unbindEngineServices();
    SW_EXPECT_FALSE( engine::areEngineServicesBound() );

    EngineServices missingTask = saved;
    missingTask._pTaskManager  = nullptr;
    test::rebindEngineServices( missingTask );
    SW_EXPECT_FALSE( engine::areEngineServicesBound() );

    test::rebindEngineServices( saved );
    SW_EXPECT_TRUE( engine::areEngineServicesBound() );
}

/**
 * @brief [EngineServiceTest] 비어 있는 필수 서비스는 **이름으로** 보고된다
 * @details `areEngineServicesBound()` 가 false 라는 사실만으로는 아무도 원인을 못 짚는다 — 그 함수로
 *          게이팅되는 자리가 스무 곳이 넘고, 하나가 비면 그 스무 곳이 전부 조용히 폴백으로 간다.
 *          표를 인자로 받는 형태라 전역 바인딩을 흔들지 않고 물어볼 수 있다(흔들면 뒤따르는 테스트가
 *          전부 그 폴백을 탄다).
 */
SW_TEST_CASE( EngineServiceTest, MissingRequiredServiceIsReportedByName )
{
    // 1) 빈 표에는 반드시 빠진 것이 있다.
    const EngineServices emptyTable{};
    SW_EXPECT_NOT_NULL( engine::findUnboundRequiredServiceName( emptyTable ) );

    // 2) 지금 바인딩된 표는 완전하다 — 하네스가 다 채웠다.
    const EngineServices boundTable = engine::getBoundEngineServices();
    SW_EXPECT_NULL( engine::findUnboundRequiredServiceName( boundTable ) );

    // 3) 필수 하나를 비우면 **그 이름**이 나온다.
    EngineServices missingTask = boundTable;
    missingTask._pTaskManager  = nullptr;
    const utf8* pMissing       = engine::findUnboundRequiredServiceName( missingTask );
    SW_ASSERT_NOT_NULL( pMissing );
    SW_EXPECT_STREQ( "TaskManager", pMissing );

    // 4) 선택 서비스는 비어도 보고하지 않는다 — 그것이 선택인 이유다(툴·배포본에는 없을 수 있다).
    EngineServices missingOptional         = boundTable;
    missingOptional._pRenderTargetRegistry = nullptr;
    missingOptional._pMemoryProfiler       = nullptr;
    missingOptional._pCommandStack         = nullptr;
    SW_EXPECT_NULL( engine::findUnboundRequiredServiceName( missingOptional ) );
}

/**
 * @brief [EngineServiceTest] 저장소는 목록의 `EngineCreated` 을 전부 만들고 표에 꽂는다
 * @details 검사도 **같은 목록에서 생성한다** — 여기에 이름을 다시 적으면 그 목록이 세 번째가 된다.
 *          `HostCreated` 자리를 건드리지 않는 것도 같이 본다. 건드리면 호스트가 팩토리로 만든 것을
 *          덮어쓰고(오디오), 배포본에 없어야 할 것을 만들어 낸다(커맨드 스택).
 */
SW_TEST_CASE( EngineServiceTest, OwnedStorageFillsExactlyTheOwnedRows )
{
    EngineServiceCollection owned;
    owned.createAll();

    EngineServices table{};
    owned.bindInto( table );

#define SW_CHECK_OWNED_ROW( member, Type, creator )                                                            \
    if constexpr ( SW_SERVICE_IS_ENGINE_CREATED( creator ) == 1 )                                              \
    {                                                                                                          \
        SW_EXPECT_TRUE_MSG( table.member != nullptr, #Type " (EngineCreated) 를 저장소가 채우지 않았습니다" ); \
    }                                                                                                          \
    else                                                                                                       \
    {                                                                                                          \
        SW_EXPECT_TRUE_MSG( table.member == nullptr, #Type " (HostCreated) 를 저장소가 건드렸습니다" );        \
    }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       SW_CHECK_OWNED_ROW( member, Type, creator )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) SW_CHECK_OWNED_ROW( member, Type, creator )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                SW_CHECK_OWNED_ROW( member, Type, creator )
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
#undef SW_CHECK_OWNED_ROW

    // 표를 꽂기만 하고 비우는 것까지 본다 — 종료 경로가 이것을 쓴다.
    owned.destroyAll();
    EngineServices afterDestroy{};
    owned.bindInto( afterDestroy );
    SW_EXPECT_NULL( afterDestroy._pTaskManager );
}

/**
 * @brief [EngineServiceTest] 호스트가 먼저 만든 것을 `createAll` 이 덮지 않는다
 * @details `EngineLoop` 은 명령줄을 파싱하려고 `CommandLineManager` 와 `GlobalVariableManager` 를
 *          저장소보다 **먼저** 만든다. 덮어쓰면 파싱 결과가 통째로 사라진다.
 */
SW_TEST_CASE( EngineServiceTest, CreateAllKeepsWhatTheHostMadeFirst )
{
    EngineServiceCollection owned;

    unique_ptr<CommandLineManager> preMade   = make_unique<CommandLineManager>();
    const CommandLineManager*      pExpected = preMade.get();
    owned._pCommandLineManager               = std::move( preMade );

    owned.createAll();
    SW_EXPECT_TRUE_MSG( owned._pCommandLineManager.get() == pExpected,
                        "createAll 이 호스트가 먼저 만든 것을 덮어썼습니다 — 명령줄 파싱 결과가 사라집니다" );
    SW_EXPECT_NOT_NULL( owned._pTaskManager );
}

/**
 * @brief [EngineServiceTest] `destroyAll` 이 목록의 **역순**으로 놓는지 검증(소멸자와 같은 순서)
 * @details 기대값은 같은 목록에서 생성한다. 정방향이면 `TaskManager`(목록 앞쪽)가 `AssetStreamingQueue` 보다 먼저 사라지고,
 *          `~AssetStreamingQueue` 의 `getTaskManager().waitAll()` 이 해제된 객체를 읽는다. 명령줄 · 전역 변수가 맨 나중이다.
 */
SW_TEST_CASE( EngineServiceTest, DestroyAllReleasesInReverseListOrder )
{
    vector<const utf8*> listExpected;
#define SW_COLLECT_ENGINE_CREATED( member, creator )              \
    if constexpr ( SW_SERVICE_IS_ENGINE_CREATED( creator ) == 1 ) \
    {                                                             \
        listExpected.insert( listExpected.begin(), #member );     \
    }
#define SW_ENGINE_SERVICE( member, Tag, Type, getter, requirement, visibility, creator )       SW_COLLECT_ENGINE_CREATED( member, creator )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, requirement, visibility, creator ) SW_COLLECT_ENGINE_CREATED( member, creator )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, visibility, creator )                SW_COLLECT_ENGINE_CREATED( member, creator )
#include "RuntimeAPI/Service/EngineServiceList.xxx"
#undef SW_ENGINE_SERVICE
#undef SW_ENGINE_SERVICE_CONST
#undef SW_ENGINE_SERVICE_OPT
#undef SW_COLLECT_ENGINE_CREATED

    const vector<const utf8*> listOrder = EngineServiceCollection::makeDestroyOrder();
    SW_ASSERT_TRUE( listOrder.size() == listExpected.size() );
    size_t queueOrder = listOrder.size();
    size_t taskOrder  = listOrder.size();
    for ( size_t order = 0; order < listOrder.size(); ++order )
    {
        SW_EXPECT_STREQ( listExpected[order], listOrder[order] );
        if ( string_view{ listOrder[order] } == "_pAssetStreamingQueue" )
            queueOrder = order;
        if ( string_view{ listOrder[order] } == "_pTaskManager" )
            taskOrder = order;
    }
    SW_EXPECT_TRUE_MSG( queueOrder < taskOrder, "AssetStreamingQueue must be released before the TaskManager its destructor waits on" );
    SW_EXPECT_STREQ( "_pCommandLineManager", listOrder.back() );
}

/**
 * @brief [EngineServiceTest] 표가 꽂힌 채로 `destroyAll` 해도 `~AssetStreamingQueue` 가 살아 있는 TaskManager 를 기다리는지 검증
 * @details 저장소의 서비스를 표에 꽂고 해제한다. 해제 순서가 틀리면 소멸자가 해제된 TaskManager 를 읽는다(ASAN 구성에서 heap-use-after-free).
 */
SW_TEST_CASE( EngineServiceTest, DestroyAllWhileBoundKeepsTaskManagerForStreamingQueue )
{
    const EngineServices saved = engine::getBoundEngineServices();

    EngineServiceCollection owned;
    owned.createAll();
    EngineServices table = saved;
    owned.bindInto( table );
    test::rebindEngineServices( table );
    SW_EXPECT_TRUE( engine::areEngineServicesBound() );

    owned.destroyAll();
    test::rebindEngineServices( saved );
    SW_EXPECT_NULL( owned._pTaskManager.get() );
    SW_EXPECT_NULL( owned._pAssetStreamingQueue.get() );
}
