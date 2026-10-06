/**
 * @file TestGameService.cpp
 * @brief 게임 로컬 서비스(`game::bindLocalService`)의 겹침 규칙 시험입니다 — 키트 둘이 같은 타입을 서비스로 걸면 나중 것이 앞 것을 소리 없이 덮지 않는다.
 */
#include "pch.h"

#include "GameFramework/Base/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 로컬 서비스로 걸 시험 전용 타입입니다(다른 시험의 서비스와 타입이 겹치지 않게). */
    struct GameServiceProbeCatalog
    {
        int32 _id{ 0 };
    };

    /** @brief 시험이 끝나면 걸어 둔 것을 풉니다(어서션이 빠져나가도). */
    struct ScopedProbeServiceUnbind
    {
        ScopedProbeServiceUnbind() = default;
        ~ScopedProbeServiceUnbind() { game::unbindLocalService<GameServiceProbeCatalog>(); }

        ScopedProbeServiceUnbind( const ScopedProbeServiceUnbind& )            = delete;
        ScopedProbeServiceUnbind& operator=( const ScopedProbeServiceUnbind& ) = delete;
    };
} // namespace

/**
 * @brief [GameServiceTest] 같은 타입의 게임 로컬 서비스를 다른 인스턴스로 다시 걸면 거절한다(앞 것이 남는다) — 같은 인스턴스를 다시 걸거나 푼 뒤 거는 것은 된다
 */
SW_TEST_CASE( GameServiceTest, BindingASecondInstanceOfOneTypeIsRefused )
{
    ScopedProbeServiceUnbind unbind;
    GameServiceProbeCatalog  first{ 1 };
    GameServiceProbeCatalog  second{ 2 };

    game::bindLocalService<GameServiceProbeCatalog>( &first );
    game::bindLocalService<GameServiceProbeCatalog>( &first ); // 같은 인스턴스 — 그대로
    SW_EXPECT_TRUE( game::getService<GameServiceProbeCatalog>() == &first );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a second instance of one local service type" );
        game::bindLocalService<GameServiceProbeCatalog>( &second );
    }
    SW_EXPECT_TRUE( game::getService<GameServiceProbeCatalog>() == &first ); // 앞 것이 남는다

    game::unbindLocalService<GameServiceProbeCatalog>();
    SW_EXPECT_TRUE( game::getService<GameServiceProbeCatalog>() == nullptr );
    game::bindLocalService<GameServiceProbeCatalog>( &second ); // 푼 뒤에는 된다(핫 리로드의 새 인스턴스)
    SW_EXPECT_TRUE( game::getService<GameServiceProbeCatalog>() == &second );
}
