// SkirmishLockstepLinkTest — 락스텝 연결이 넷 세션의 수명을 따른다(nogpu, 루프백 포트 0).
#include "pch.h"

#define SW_TEST_INTERNAL // 세션을 직접 세우느라 엔진 서비스(`ResourceComposition`)를 찾는다
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ServerConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

#include "Games/Playground/StarSkirmish/SkirmishLockstepLink.h"

using namespace sw;

/** @brief [SkirmishLockstepLinkTest] 판 도중에 세션이 닫히면 락스텝을 한 번 놓는다 — 명령을 쌓으며 영영 기다리지 않고, 새 세션에서 다시 열린다 */
SW_TEST_CASE( SkirmishLockstepLinkTest, ClosedSessionReleasesTheLockstep )
{
    GameNetSession session{ engine::getSubsystem<ResourceComposition>() };
    ServerConfig   config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0; // 운영체제가 고른다
    GameNetSessionParams params;
    SW_REQUIRE_TRUE( session.makeServerParams( config, params ) );
    SW_REQUIRE_TRUE( session.initialize( params ) );
    SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind<GameNetSession>( &session ) );
    game::ScopedGameScope scopedScope( scope );

    SkirmishLockstepLink link;
    SW_REQUIRE_TRUE( link.open() );
    SW_EXPECT_TRUE( link.isActive() );
    SW_EXPECT_FALSE( link.releaseLostSession() );

    session.shutdown();
    SW_EXPECT_FALSE( link.isActive() );
    SW_EXPECT_FALSE( link.queueCommand( RTSCommand{} ) );
    SW_EXPECT_TRUE( link.releaseLostSession() );
    SW_EXPECT_FALSE( link.releaseLostSession() );

    SW_REQUIRE_TRUE( session.initialize( params ) );
    SW_REQUIRE_TRUE( link.open() );
    SW_EXPECT_TRUE( link.isActive() );
    link.close();
    session.shutdown();
}
