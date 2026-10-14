/**
 * @file TestGameService.cpp
 * @brief 게임 서비스 창구(`game::getService`)의 범위 규칙 테스트입니다 — 지금 게임 범위에서 찾고, 범위를 겹쳐 걸면 가까운 범위가 이기고, 풀면 앞 범위로 돌아간다.
 */
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 게임 범위에 걸 테스트 전용 타입입니다(다른 테스트의 서비스와 타입이 겹치지 않게). */
    struct GameServiceProbeCatalog
    {
        int32 _id{ 0 };
    };
} // namespace

/**
 * @brief [GameServiceTest] 지금 게임 범위에 건 것을 찾고, 범위를 풀면 다시 없다 — 같은 범위에 같은 타입을 두 번 걸지 않는다
 */
SW_TEST_CASE( GameServiceTest, ServiceIsFoundInTheCurrentGameScope )
{
    SW_EXPECT_TRUE( game::findSubsystem<GameServiceProbeCatalog>() == nullptr );
    GameServiceProbeCatalog first{ 1 };
    GameServiceProbeCatalog second{ 2 };
    {
        SubsystemScope        scope;
        game::ScopedGameScope activeScope( scope );
        SW_EXPECT_TRUE( scope.bind( &first ) );
        SW_EXPECT_FALSE( scope.bind( &second ) ); // 같은 타입은 거절 — 나중 것이 앞 것을 소리 없이 덮지 않는다
        SW_EXPECT_TRUE( game::findSubsystem<GameServiceProbeCatalog>() == &first );
        SW_EXPECT_NOT_NULL( game::findSubsystem<GameServiceProbeCatalog>() );
    }
    SW_EXPECT_TRUE( game::findSubsystem<GameServiceProbeCatalog>() == nullptr ); // 범위가 사라지면 칸도 없다(풀 일이 없다)
    SW_EXPECT_NULL( game::findSubsystem<GameServiceProbeCatalog>() );
}

/**
 * @brief [GameServiceTest] 범위를 겹쳐 걸면 가까운 범위가 이기고, 거기 없는 것은 앞 범위에서 찾으며, 풀면 앞 범위가 다시 지금 범위다
 * @details 핫 리로드에서 새 게임 인스턴스가 옛 것보다 먼저 범위를 거는 경우와 테스트가 겹쳐 거는 경우가 이 규칙을 탄다.
 */
SW_TEST_CASE( GameServiceTest, NestedScopesShadowAndRestore )
{
    GameServiceProbeCatalog outerCatalog{ 1 };
    GameServiceProbeCatalog innerCatalog{ 2 };
    SubsystemScope          outerScope;
    game::ScopedGameScope   activeOuter( outerScope );
    SW_EXPECT_TRUE( outerScope.bind( &outerCatalog ) );
    {
        SubsystemScope        innerScope;
        game::ScopedGameScope activeInner( innerScope );
        SW_EXPECT_TRUE( game::findGameScope() == &innerScope );
        SW_EXPECT_TRUE( game::findSubsystem<GameServiceProbeCatalog>() == &outerCatalog ); // 가까운 범위에 없으면 앞 범위
        SW_EXPECT_TRUE( innerScope.bind( &innerCatalog ) );
        SW_EXPECT_TRUE( game::findSubsystem<GameServiceProbeCatalog>() == &innerCatalog ); // 가까운 범위가 이긴다
    }
    SW_EXPECT_TRUE( game::findGameScope() == &outerScope );
    SW_EXPECT_TRUE( game::findSubsystem<GameServiceProbeCatalog>() == &outerCatalog );
}
