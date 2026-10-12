/**
 * @file TestGameService.cpp
 * @brief 게임 로컬 서비스(`game::bindLocalService`)의 겹침 규칙 테스트입니다 — 키트 둘이 같은 타입을 서비스로 걸면 나중 것이 앞 것을 소리 없이 덮지 않는다.
 */
#include "pch.h"

#include "Core/Module/ModuleImageUtil.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 로컬 서비스로 걸 테스트 전용 타입입니다(다른 테스트의 서비스와 타입이 겹치지 않게). */
    struct GameServiceProbeCatalog
    {
        int32 _id{ 0 };
    };

    /** @brief 테스트가 끝나면 걸어 둔 것을 풉니다(어서션이 빠져나가도). */
    struct ScopedProbeServiceUnbind
    {
        ScopedProbeServiceUnbind() = default;
        ~ScopedProbeServiceUnbind() { game::unbindLocalService<GameServiceProbeCatalog>(); }

        ScopedProbeServiceUnbind( const ScopedProbeServiceUnbind& )            = delete;
        ScopedProbeServiceUnbind& operator=( const ScopedProbeServiceUnbind& ) = delete;
    };

    /** @brief 프레임워크 기반 클래스가 대신 거는 게임 인스턴스를 흉내 냅니다. vtable 이 이 테스트 이미지에 있다. */
    struct GameServiceProbeInstance
    {
        virtual ~GameServiceProbeInstance() = default;
        GameServiceProbeCatalog _catalog{ 1 };
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

/**
 * @brief [GameServiceTest] 풀지 않고 내려간 이미지가 건 로컬 서비스는 언로드 훑기가 회수한다 — 핫 리로드의 새 인스턴스가 겹침 규칙에 걸리지 않는다
 * @details 이 테스트 이미지가 건 칸을 그 이미지의 첫 바이트만 덮는 범위로 훑는다(다른 리스너의 등록은 코드 영역을 가리키므로 건드리지 않는다).
 */
SW_TEST_CASE( GameServiceTest, UnloadSweepReleasesLocalServicesOfTheImage )
{
    ScopedProbeServiceUnbind unbind;
    GameServiceProbeCatalog  oldInstance{ 1 };
    GameServiceProbeCatalog  newInstance{ 2 };

    const void* pTestImageBegin = nullptr;
    const void* pTestImageEnd   = nullptr;
    if ( ModuleImageUtil::findLoadedImageRange( reinterpret_cast<const void*>( static_cast<void ( * )( GameServiceProbeCatalog* )>( &game::bindLocalService<GameServiceProbeCatalog> ) ), pTestImageBegin, pTestImageEnd ) == false )
        return; // 이미지 범위를 읽을 수 없는 플랫폼

    // 옛 인스턴스가 풀지 않은 채 언로드 차례가 온다.
    game::bindLocalService<GameServiceProbeCatalog>( &oldInstance );
    vector<IModuleUnloadListener::ReleaseResult> listResult;
    IModuleUnloadListener::removeAllWithin( pTestImageBegin, static_cast<const uint8*>( pTestImageBegin ) + 1, listResult );
    SW_EXPECT_TRUE( game::getService<GameServiceProbeCatalog>() == nullptr );

    game::bindLocalService<GameServiceProbeCatalog>( &newInstance ); // 새 인스턴스는 거절되지 않는다
    SW_EXPECT_TRUE( game::getService<GameServiceProbeCatalog>() == &newInstance );
}

/**
 * @brief [GameServiceTest] 소유자를 명시해 건 칸은 반환 주소가 아니라 그 소유자 이미지를 따른다 — 프레임워크 코드가 건 게임 객체의 서비스도 게임 이미지 언로드 때 회수된다
 * @details 앞 절반은 소유자를 다른 이미지(GameFramework 의 함수 주소)로 명시해, 부른 곳(테스트 이미지)의 훑기가 회수하지 않음을 본다.
 *          뒤 절반은 `GameInstanceBase::initialize` 처럼 인스턴스 vtable 을 소유자로 넘겨, 그 이미지의 훑기가 회수함을 본다.
 */
SW_TEST_CASE( GameServiceTest, ExplicitOwnerDecidesWhichImageReleasesTheService )
{
    ScopedProbeServiceUnbind unbind;
    GameServiceProbeInstance instance;

    const void* pTestImageBegin = nullptr;
    const void* pTestImageEnd   = nullptr;
    if ( ModuleImageUtil::findLoadedImageRange( game::getPolymorphicOwnerAddress( instance ), pTestImageBegin, pTestImageEnd ) == false )
        return; // 이미지 범위를 읽을 수 없는 플랫폼
    vector<IModuleUnloadListener::ReleaseResult> listResult;

    const void* pFrameworkAddress = reinterpret_cast<const void*>( &game::internal::getRawLocalService );
    if ( pFrameworkAddress < pTestImageBegin || pFrameworkAddress >= pTestImageEnd ) // GameFramework 를 정적으로 묶은 빌드는 건너뛴다
    {
        game::bindLocalService<GameServiceProbeCatalog>( &instance._catalog, pFrameworkAddress );
        IModuleUnloadListener::removeAllWithin( pTestImageBegin, static_cast<const uint8*>( pTestImageBegin ) + 1, listResult );
        SW_EXPECT_TRUE( game::getService<GameServiceProbeCatalog>() == &instance._catalog ); // 부른 이미지가 아니라 소유자 이미지를 따른다
        game::unbindLocalService<GameServiceProbeCatalog>();
    }

    game::bindLocalService<GameServiceProbeCatalog>( &instance._catalog, game::getPolymorphicOwnerAddress( instance ) );
    IModuleUnloadListener::removeAllWithin( pTestImageBegin, static_cast<const uint8*>( pTestImageBegin ) + 1, listResult );
    SW_EXPECT_TRUE( game::getService<GameServiceProbeCatalog>() == nullptr );
    SW_EXPECT_TRUE( game::isServiceBound<GameServiceProbeCatalog>() == false );
}
