#include "pch.h"

#include "Engine/Common/EngineServices.h"

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

#define SW_ENGINE_SERVICE( member, Tag, Type, getter, required, gameAllowed )       SW_CHECK_SERVICE_VISIBILITY( Type, gameAllowed )
#define SW_ENGINE_SERVICE_CONST( member, Tag, Type, getter, required, gameAllowed ) SW_CHECK_SERVICE_VISIBILITY( Type, gameAllowed )
#define SW_ENGINE_SERVICE_OPT( member, Tag, Type, getter, gameAllowed )             SW_CHECK_SERVICE_VISIBILITY( Type, gameAllowed )
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
