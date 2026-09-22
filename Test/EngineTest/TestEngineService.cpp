#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/EngineOwnedServices.h"

#include "RuntimeAPI/Service/ModuleService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) EngineServiceTest — 서비스 표가 게임 모듈에 무엇을 보여 주는지
//
//    `EngineServiceList.xxx` 의 `gameAllowed` 열은 **게임 모듈이 손댈 수 있는 것과 없는 것의
//    경계**인데, 그때까지 아무 테스트도 그 경계를 보고 있지 않았다. 열을 잘못 바꿔도 빌드는
//    통과하고 아무도 모른다. 그래서 검사도 같은 목록에서 생성한다 — 목록이 정본이다.
// ------------------------------------------------------------------------------

/**
 * @brief [EngineServiceTest] 게임 모듈용 표에 gameAllowed=0 인 서비스가 하나도 없는지 검증
 */
SW_TEST_CASE( EngineServiceTest, GameModuleTableHidesHostOnlyServices )
{
    ModuleService editorTable{};
    ModuleService gameTable{};
    engine::fillModuleServices( editorTable, false );
    engine::fillModuleServices( gameTable, true );

#define SW_CHECK_SERVICE_VISIBILITY( Type, gameAllowed )                                     \
    {                                                                                        \
        const uint32 rawId = internal::toRawServiceId( internal::ModuleServiceId::Type );    \
        if constexpr ( ( gameAllowed ) == 0 )                                                \
            SW_EXPECT_NULL( gameTable.arrServices[rawId] );                                  \
        else                                                                                 \
            SW_EXPECT_EQUAL( editorTable.arrServices[rawId], gameTable.arrServices[rawId] ); \
    }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )       SW_CHECK_SERVICE_VISIBILITY( Type, gameAllowed )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) SW_CHECK_SERVICE_VISIBILITY( Type, gameAllowed )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned )             SW_CHECK_SERVICE_VISIBILITY( Type, gameAllowed )
#include "Engine/Common/EngineServiceList.xxx"
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
 * @details 예전엔 부를 때마다 필수 서비스 22 칸을 훑었다 — `Component::getTypeInfo()` 가 캐스트마다 그것을
 *          불러 캐스트 한 번의 절반이 이 훑기였다. 이제 bind/unbind 때 한 번 센 플래그를 읽는다. 훑기와 같은
 *          답이어야 한다: 빈 표 → false, 필수 하나가 빈 표 → false, 완전한 표 → true. 전역 표를 흔드는
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
 * @brief [EngineServiceTest] 저장소는 목록의 `owned=1` 을 전부 만들고 표에 꽂는다
 * @details 검사도 **같은 목록에서 생성한다** — 여기에 이름을 다시 적으면 그 목록이 세 번째가 된다.
 *          `owned=0` 자리를 건드리지 않는 것도 같이 본다. 건드리면 호스트가 팩토리로 만든 것을
 *          덮어쓰고(오디오), 배포본에 없어야 할 것을 만들어 낸다(커맨드 스택).
 */
SW_TEST_CASE( EngineServiceTest, OwnedStorageFillsExactlyTheOwnedRows )
{
    EngineOwnedServices owned;
    owned.createAll();

    EngineServices table{};
    owned.bindInto( table );

#define SW_CHECK_OWNED_ROW( member, Type, owned )                                                        \
    if constexpr ( ( owned ) == 1 )                                                                      \
    {                                                                                                    \
        SW_EXPECT_TRUE_MSG( table.member != nullptr, #Type " (owned=1) 을 저장소가 채우지 않았습니다" ); \
    }                                                                                                    \
    else                                                                                                 \
    {                                                                                                    \
        SW_EXPECT_TRUE_MSG( table.member == nullptr, #Type " (owned=0) 을 저장소가 건드렸습니다" );      \
    }

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed, owned )       SW_CHECK_OWNED_ROW( member, Type, owned )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed, owned ) SW_CHECK_OWNED_ROW( member, Type, owned )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed, owned )             SW_CHECK_OWNED_ROW( member, Type, owned )
#include "Engine/Common/EngineServiceList.xxx"
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
 *          저장소보다 **먼저** 만든다. 덮어썼다면 파싱 결과가 통째로 사라진다 — 이 저장소를 처음
 *          붙였을 때 실제로 그렇게 될 뻔했다.
 */
SW_TEST_CASE( EngineServiceTest, CreateAllKeepsWhatTheHostMadeFirst )
{
    EngineOwnedServices owned;

    unique_ptr<CommandLineManager> preMade   = make_unique<CommandLineManager>();
    const CommandLineManager*      pExpected = preMade.get();
    owned._pCommandLineManager               = std::move( preMade );

    owned.createAll();
    SW_EXPECT_TRUE_MSG( owned._pCommandLineManager.get() == pExpected,
                        "createAll 이 호스트가 먼저 만든 것을 덮어썼습니다 — 명령줄 파싱 결과가 사라집니다" );
    SW_EXPECT_NOT_NULL( owned._pTaskManager );
}
