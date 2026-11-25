// GameServiceTest — 게임 서비스 창구의 범위 전환: 지금 범위 → 상위 범위 → 호스트 순서로 찾고, 중첩한 테스트 범위가 가리고 되돌리는 것. 호스트 범위가 묶이지 않아도 호스트 전용 칸은 보이지 않는 것.
#include "pch.h"

#include "Core/File/AsyncFileIO.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Foundation/EngineScope.h"
#include "Engine/Scene/SceneObjectHash.h"

#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace
{
    /** @brief 범위에 거는 시험 서비스입니다. */
    struct ScopeTestService
    {
        int32 _value;
    };

    /** @brief 바깥 범위에만 거는 시험 서비스입니다. */
    struct OuterOnlyTestService
    {
        int32 _value;
    };
} // namespace

using namespace sw;

/** @brief [GameServiceTest] 안 범위가 같은 타입을 가리고, 안 범위에 없는 타입은 바깥 범위에서 찾는다. 안 범위를 풀면 바깥 범위가 지금 범위로 돌아온다 */
SW_TEST_CASE( GameServiceTest, NestedScopesShadowAndRestore )
{
    SubsystemScope* const pBefore = game::findGameScope();
    ScopeTestService      outerService{ 1 };
    OuterOnlyTestService  outerOnly{ 2 };
    ScopeTestService      innerService{ 3 };
    SubsystemScope        outerScope;
    SubsystemScope        innerScope;
    SW_REQUIRE_TRUE( outerScope.bind( &outerService ) );
    SW_REQUIRE_TRUE( outerScope.bind( &outerOnly ) );
    SW_REQUIRE_TRUE( innerScope.bind( &innerService ) );
    {
        const game::ScopedGameScope outer{ outerScope };
        SW_EXPECT_TRUE( game::findGameScope() == &outerScope );
        SW_EXPECT_TRUE( game::findSubsystem<ScopeTestService>() == &outerService );
        {
            const game::ScopedGameScope inner{ innerScope };
            SW_EXPECT_TRUE( game::findGameScope() == &innerScope );
            SW_EXPECT_TRUE( game::findSubsystem<ScopeTestService>() == &innerService );  // 가린다
            SW_EXPECT_TRUE( game::findSubsystem<OuterOnlyTestService>() == &outerOnly ); // 상위로 찾는다
            SW_EXPECT_EQUAL( 3, game::getSubsystem<ScopeTestService>()._value );
        }
        SW_EXPECT_TRUE( game::findGameScope() == &outerScope );
        SW_EXPECT_TRUE( game::findSubsystem<ScopeTestService>() == &outerService );
    }
    SW_EXPECT_TRUE( game::findGameScope() == pBefore );
    SW_EXPECT_NULL( game::findSubsystem<OuterOnlyTestService>() );
}

/** @brief [GameServiceTest] 게임 범위에 없는 서비스는 호스트(엔진) 서비스에서 찾는다 — 지금 범위가 있든 없든 같다 */
SW_TEST_CASE( GameServiceTest, FallsThroughToHostServices )
{
    const ISubsystemLocator* const pHost = game::internal::findHostServices();
    SW_REQUIRE_NOT_NULL( pHost );
    SubsystemScope              emptyScope;
    const game::ScopedGameScope scoped{ emptyScope };
    // 엔진이 서 있으면 호스트 표에 무엇이든 있다 — 범위에 없는 타입을 범위가 막지 않는지만 본다.
    SW_EXPECT_TRUE( game::findSubsystem<ScopeTestService>() == locateSubsystem<ScopeTestService>( pHost ) );
}

/**
 * @brief [GameServiceTest] 호스트 전용(`HostOnly`)으로 등록한 엔진 서비스는 게임 창구로 찾지 못한다 — 호스트 범위가 묶였든(게임 모듈 범위) 아니든(엔진 범위 폴백) 같다
 * @details 폴백이 엔진 범위를 그대로 보이면 묶인 게임 모듈은 nullptr 을 받는 서비스를 테스트 · 체인 단계 사이에서만 찾아 결함이 가려진다(작업 풀이 그랬다).
 *          해시 이력(`StateHashHistory`)은 키트가 줄을 달므로 게임에 보인다.
 */
SW_TEST_CASE( GameServiceTest, HostOnlyServicesStayHiddenWithoutAHostBinding )
{
    SW_EXPECT_TRUE( engine::isHostOnlyService( getSubsystemTypeHash<AsyncFileIO>() ) );
    SW_EXPECT_FALSE( engine::isHostOnlyService( getSubsystemTypeHash<StateHashHistory>() ) );
    SW_EXPECT_FALSE( engine::isHostOnlyService( getSubsystemTypeHash<ScopeTestService>() ) ); // 등록이 없는 타입은 거르지 않는다
    SW_EXPECT_NULL( game::findSubsystem<AsyncFileIO>() );
}
