// SkirmishLockstepSessionTest — 락스텝 연결이 넷 세션의 수명을 따른다(nogpu, 루프백 포트 0).
#include "pch.h"

#define SW_TEST_INTERNAL // 세션을 직접 세우느라 엔진 서비스(`AssetComposition`)를 찾는다
#include "Core/Subsystem/SubsystemScope.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Config/ServerConfig.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Serialization/Compose/AssetComposition.h"

#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

#include "Games/Playground/StarSkirmish/SkirmishLockstepSession.h"

using namespace sw;

/** @brief [SkirmishLockstepSessionTest] 판의 월드에 들어왔던 상대가 나가면 락스텝을 한 번 놓는다 — 기다리던 상대(아직 들어오지 않음)는 나간 것이 아니다 */
SW_TEST_CASE( SkirmishLockstepSessionTest, PeerLeavingTheWorldReleasesTheLockstep )
{
    GameNetSession session{ engine::getSubsystem<AssetComposition>() };
    session.setRunWireContent( GameNetWireContent{} );
    ServerConfig config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0; // 운영체제가 고른다
    GameNetSessionParams params;
    SW_REQUIRE_TRUE( session.makeServerParams( config, params ) );
    SW_REQUIRE_TRUE( session.initialize( params ) );
    SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind<GameNetSession>( &session ) );
    game::ScopedGameScope scopedScope( scope );

    SkirmishLockstepSession link;
    SW_REQUIRE_TRUE( link.open() );
    SW_EXPECT_FALSE( link.releaseLostSession() ); // 아직 아무도 들어오지 않았다
    session.getPeers().setInWorld( 0, true );
    SW_EXPECT_FALSE( link.releaseLostSession() );
    session.getPeers().setInWorld( 0, false ); // 끊겼거나 다른 씬으로 갔다 — 세션은 열려 있다
    SW_EXPECT_TRUE( link.releaseLostSession() );
    SW_EXPECT_FALSE( link.isActive() );
    SW_EXPECT_FALSE( link.releaseLostSession() );
    session.shutdown();
}

/** @brief [SkirmishLockstepSessionTest] 판 도중에 세션이 닫히면 락스텝을 한 번 놓는다 — 명령을 쌓으며 영영 기다리지 않고, 새 세션에서 다시 열린다 */
SW_TEST_CASE( SkirmishLockstepSessionTest, ClosedSessionReleasesTheLockstep )
{
    GameNetSession session{ engine::getSubsystem<AssetComposition>() };
    session.setRunWireContent( GameNetWireContent{} );
    ServerConfig config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0; // 운영체제가 고른다
    GameNetSessionParams params;
    SW_REQUIRE_TRUE( session.makeServerParams( config, params ) );
    SW_REQUIRE_TRUE( session.initialize( params ) );
    SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind<GameNetSession>( &session ) );
    game::ScopedGameScope scopedScope( scope );

    SkirmishLockstepSession link;
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
