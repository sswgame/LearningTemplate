// GameNetSessionTicketLinkTest — 씬 넷 링크의 `_bTicketRequired` 는 세션 설정(`ServerConfig::_bGameConnectTicket`)에서 온다(nogpu, 루프백 포트 0).
#include "pch.h"

#include "Core/Network/Security/Tests/FakeNetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ServerConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Serialization/Inherit/ResourceComposition.h"

using namespace sw;

/** @brief [GameNetSessionTicketLinkTest] 접속 티켓을 켠 서버 설정으로 연 세션만 링크가 티켓을 요구한다고 알리고, 닫으면 남지 않는다 */
SW_TEST_CASE( GameNetSessionTicketLinkTest, LinkFollowsTheServerConfig )
{
    test::FakeNetSecurityProvider provider;
    GameNetSession                session{ engine::getSubsystem<ResourceComposition>() };
    session.getConnectSecurity().setProvider( &provider );
    ServerConfig config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0; // 운영체제가 고른다

    GameNetSessionParams params;
    config._bGameConnectTicket = true;
    SW_REQUIRE_TRUE( session.makeServerParams( config, params ) );
    SW_REQUIRE_TRUE( session.initialize( params ) );
    SW_EXPECT_TRUE( session.makeNetLink()._bTicketRequired == SW_TRUE );

    config._bGameConnectTicket = false;
    SW_REQUIRE_TRUE( session.makeServerParams( config, params ) );
    SW_REQUIRE_TRUE( session.initialize( params ) );
    SW_EXPECT_TRUE( session.makeNetLink()._bTicketRequired == SW_FALSE );
    session.shutdown();
    session.getConnectSecurity().setProvider( nullptr );
}

/** @brief [GameNetSessionTicketLinkTest] 닫힌 세션의 링크는 비고, 같은 세션 객체로 새로 열어도 되살아나지 않는다 — 새로 만든 링크만 쓸 수 있다 */
SW_TEST_CASE( GameNetSessionTicketLinkTest, ClosedSessionLinkStaysEmpty )
{
    GameNetSession session{ engine::getSubsystem<ResourceComposition>() };
    ServerConfig   config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0; // 운영체제가 고른다
    GameNetSessionParams params;
    SW_REQUIRE_TRUE( session.makeServerParams( config, params ) );
    SW_REQUIRE_TRUE( session.initialize( params ) );
    const NetLinkValue link = session.makeNetLink();
    SW_EXPECT_TRUE( link.isComplete() );

    session.shutdown();
    SW_EXPECT_FALSE( link.isComplete() );
    SW_REQUIRE_TRUE( session.initialize( params ) );
    SW_EXPECT_FALSE( link.isComplete() );
    SW_EXPECT_TRUE( session.makeNetLink().isComplete() );
    session.shutdown();
}
