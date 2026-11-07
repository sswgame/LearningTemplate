// GameServiceTest — 게임 서비스 창구의 범위 전환: 지금 범위 → 상위 범위 → 호스트 순서로 찾고, 중첩한 테스트 범위가 가리고 되돌리는 것.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

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
