// GameNetSessionTicketLinkTest — 씬 넷 링크의 `_bTicketRequired` 는 세션 설정(`ServerConfig::_bGameConnectTicket`)에서 온다(nogpu, 루프백 포트 0).
#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/Network/Security/Tests/FakeNetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/ServerConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
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

/** @brief [GameNetSessionTicketLinkTest] 결함: 엔진 종료가 모듈 라이브러리를 내린 뒤에 세션을 닫아, 봉인기를 만든 모듈의 사라진 코드를 불렀다 — 종료의 앞 단계(`stopSubsystem`)에서 이미 닫힌다 */
SW_TEST_CASE( GameNetSessionTicketLinkTest, EncryptedSessionClosesBeforeModulesUnload )
{
    test::FakeNetSecurityProvider provider;
    GameNetSession                session{ engine::getSubsystem<ResourceComposition>() };
    session.getConnectSecurity().setProvider( &provider );
    ServerConfig config;
    config._listenAddress      = "127.0.0.1";
    config._gamePort           = 0; // 운영체제가 고른다
    config._bGameConnectTicket = true;
    GameNetSessionParams params;
    SW_REQUIRE_TRUE( session.makeServerParams( config, params ) );
    SW_REQUIRE_TRUE( session.initialize( params ) );
    session.stopSubsystem();
    SW_EXPECT_FALSE( session.isActive() );
    session.shutdownSubsystem(); // 이미 닫혔다
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

/** @brief [GameNetSessionTicketLinkTest] 클라이언트는 붙어 있던 서버를 잃은 까닭과 종료 통지를 남기고, 붙지 못한 시도는 잃은 것이 아니다 — 게임이 다시 붙을지 정한다 */
SW_TEST_CASE( GameNetSessionTicketLinkTest, LostServerLeavesItsReason, ._timeoutSeconds = 60 )
{
    SW_EXPECT_LOG( SW_LOG_ID_NET_SESSION_LOST_SERVER );
    GameNetSession server{ engine::getSubsystem<ResourceComposition>() };
    GameNetSession client{ engine::getSubsystem<ResourceComposition>() };
    ServerConfig   config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0; // 운영체제가 고른다
    GameNetSessionParams serverParams;
    SW_REQUIRE_TRUE( server.makeServerParams( config, serverParams ) );
    SW_REQUIRE_TRUE( server.initialize( serverParams ) );
    GameNetSessionParams clientParams;
    clientParams._role          = GameNetRole::Client;
    clientParams._serverAddress = NetAddress::makeLoopback( server.getListenPort() );
    SW_REQUIRE_TRUE( client.initialize( clientParams ) );
    const auto waitFor = [&server, &client]( const auto& isDone )
    {
        for ( int32 turn = 0; turn < 2000 && isDone() == false; ++turn )
        {
            server.update();
            client.update();
            ThreadUtil::sleepForSeconds( 0.005 );
        }
        return isDone();
    };
    SW_REQUIRE_TRUE( waitFor( [&server, &client]()
    { return server.getConnectedCount() == 1 && client.getConnectedCount() == 1; } ) );
    SW_EXPECT_FALSE( client.getLostReason().has_value() );

    GameShutdownNotice notification;
    notification._reason = GameShutdownReason::Maintenance;
    server.sendShutdownNotice( notification );
    GameShutdownNotice received;
    SW_REQUIRE_TRUE( waitFor( [&client, &received]()
    { return client.tryGetShutdownNotice( received ); } ) );
    SW_EXPECT_TRUE( received._reason == GameShutdownReason::Maintenance );
    server.disconnectPeers();
    SW_REQUIRE_TRUE( waitFor( [&client]()
    { return client.getLostReason().has_value(); } ) );
    SW_EXPECT_TRUE( *client.getLostReason() == NetDisconnectReason::Remote );
    client.shutdown();
    SW_EXPECT_FALSE( client.getLostReason().has_value() );
    server.shutdown();
}

/** @brief [GameNetSessionTicketLinkTest] 결함: 서버를 잃은 클라이언트의 씬이 세션이 닫히면 혼자 하는 월드로 여겨져 서버 세계가 저장됐다 — 클라이언트로 묶였던 월드는 세션이 닫혀도 서버의 것이고, 세션을 여는 앞머리를 알린다 */
SW_TEST_CASE( GameNetSessionTicketLinkTest, ServerWorldOutlivesTheSession )
{
    GameNetSession server{ engine::getSubsystem<ResourceComposition>() };
    GameNetSession client{ engine::getSubsystem<ResourceComposition>() };
    ServerConfig   config;
    config._listenAddress = "127.0.0.1";
    config._gamePort      = 0; // 운영체제가 고른다
    GameNetSessionParams serverParams;
    SW_REQUIRE_TRUE( server.makeServerParams( config, serverParams ) );
    SW_REQUIRE_TRUE( server.initialize( serverParams ) );
    uint32               openingCount = 0;
    const Subscription   opening      = client.onClientOpening().add( SW_DELEGATE_LAMBDA( Delegate<void()>, [&openingCount, &client]()
           {
        ++openingCount;
        SW_EXPECT_FALSE( client.isActive() ); // 아직 열지 않았다
    } ) );
    GameNetSessionParams clientParams;
    clientParams._role          = GameNetRole::Client;
    clientParams._serverAddress = NetAddress::makeLoopback( server.getListenPort() );
    SW_REQUIRE_TRUE( client.initialize( clientParams ) );
    SW_EXPECT_EQUAL( 1u, openingCount );

    GameObjectManager serverWorld;
    GameObjectManager clientWorld;
    SW_REQUIRE_TRUE( serverWorld.getSubsystem<SceneNetLink>().bind( server.makeNetLink(), 0 ) );
    SW_REQUIRE_TRUE( clientWorld.getSubsystem<SceneNetLink>().bind( client.makeNetLink(), 0 ) );
    SW_EXPECT_FALSE( serverWorld.getSubsystem<SceneNetLink>().isServerWorld() ); // 서버 자신의 월드다
    SW_EXPECT_TRUE( clientWorld.getSubsystem<SceneNetLink>().isServerWorld() );
    clientWorld.getSubsystem<SceneNetLink>().unbind();
    client.shutdown();
    SW_EXPECT_FALSE( clientWorld.getSubsystem<SceneNetLink>().isBound() );
    SW_EXPECT_TRUE( clientWorld.getSubsystem<SceneNetLink>().isServerWorld() );
    serverWorld.getSubsystem<SceneNetLink>().unbind();
    server.shutdown();
}
