// 매칭 끝단(루프백) — 로그인 전 요청은 kUnauthenticated, 파티 만들기 · 초대 알림 · 수락 뒤 둘에게 파티 알림 · 떠나기, 로비 만들기 · 목록,
// 둘이 1 대 1 모드에 줄 서면 등록된 게임 서버로 배정 결과 알림(Found · 서버 주소), 끊긴 계정은 파티에서 빠져 남은 사람에게 알림.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

#include "GameFramework/Base/Online/Directory/ServerRegistration.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchQueueService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/MatchmakingServer.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Service/PartyLobbyService.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingClient.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct MatchmakingRecorder
    {
        vector<MatchmakingClientReply> _listReply{};

        void onReply( const MatchmakingClientReply& reply ) { _listReply.push_back( reply ); }

        MatchmakingClient::ReplyDelegate makeDelegate() { return MatchmakingClient::ReplyDelegate::create<&MatchmakingRecorder::onReply>( this ); }
    };

    /** @brief 서버 하나의 매칭(로직 + 바인딩) — `start` 가 서버에 올려 서버가 내려가기 전에 `stop` 한다. */
    struct MatchmakingOnServer final : public test::IOnlineTestKit
    {
        PartyLobbyService _partyLobby;
        MatchQueueService _queue;
        MatchmakingServer _binding;

        MatchmakingOnServer()
            : _partyLobby{}
            , _queue{}
            , _binding{}
        {
        }

        /** @brief 서버가 내려가기 전에 부른다(호스트 · 저장소 · 접속 상태가 살아 있다). 두 번 불려도 된다. */
        void stop() override
        {
            _binding.shutdown();
            _queue.shutdown();
            _partyLobby.shutdown();
        }

        void start( test::OnlineTestServer& server )
        {
            SW_EXPECT_TRUE( server._host.registerService( &_binding ) );
            server.start();
            server.addKit( this );
            _partyLobby.initialize( server._host.getEphemeralRouter(), server.getServerId() );
            MatchModeDefinition mode;
            mode._modeId    = "pair";
            mode._teamCount = 2;
            mode._teamSize  = 1;
            MatchQueueDependencies dependencies;
            dependencies._pRouter     = server._host.getEphemeralRouter();
            dependencies._pBus        = &server._bus;
            dependencies._pPartyLobby = &_partyLobby;
            dependencies._serverId    = server.getServerId();
            _queue.initialize( dependencies, vector<MatchModeDefinition>{ mode } );
            _binding.initialize( &_partyLobby, &_queue, nullptr, &server._presence );
        }
    };
} // namespace

SW_TEST_CASE( MatchmakingStreamTest, PartyLobbyAndQueueOverTheWire )
{
    LoopbackStreamNetwork   network( 3u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    MatchmakingOnServer     matchmaking;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    matchmaking.start( server );

    ServerRegistration registration;
    ServerDescriptor   descriptor;
    descriptor._serverId = 50;
    descriptor._kind     = "game";
    descriptor._region   = "kr";
    descriptor._address  = "10.0.0.9";
    descriptor._port     = 7777;
    descriptor._capacity = 10;
    SW_ASSERT_TRUE( registration.initialize( server._host.getEphemeralRouter(), descriptor ) );
    registration.setState( ServerState::Open );
    registration.tick( 0 );

    MatchmakingClient       clientA;
    MatchmakingClient       clientB;
    test::OnlineTestClients clients( network );
    const int32             indexA = clients.connect( server._port, { &clientA } );
    const int32             indexB = clients.connect( server._port, { &clientB } );
    clientA.initialize( &clients.getClient( indexA ) );
    clientB.initialize( &clients.getClient( indexB ) );
    test::tickAll( { &server }, clients, 0 );

    MatchmakingRecorder recorderA;
    MatchmakingRecorder recorderB;
    (void)clientA.createParty( recorderA.makeDelegate() ); // 로그인 전
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_EQUAL( recorderA._listReply.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorderA._listReply[0]._errorCode, OnlineError::kUnauthenticated );
    SW_EXPECT_TRUE( recorderA._listReply[0]._reply._result == MatchmakingResult::Unavailable );

    clients.login( indexA, 1 );
    clients.login( indexB, 2 );
    test::tickAll( { &server }, clients, 0 );
    (void)clientA.createParty( recorderA.makeDelegate() ); // 요청 번호는 쓰지 않는다 — 답은 recorder 로 확인한다
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_TRUE( recorderA._listReply.back()._reply._result == MatchmakingResult::Ok );
    const uint64 partyId = recorderA._listReply.back()._reply._party._partyId;
    SW_EXPECT_EQUAL( clientA.getParty()._partyId, partyId ); // 만든 사람에게도 파티 알림

    (void)clientA.inviteToParty( 2, recorderA.makeDelegate() );
    test::tickAll( { &server }, clients, 0 );
    vector<PartyInvite> listInvite;
    clientB.drainInvites( listInvite );
    SW_ASSERT_EQUAL( listInvite.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listInvite[0]._partyId, partyId );
    SW_EXPECT_EQUAL( listInvite[0]._inviterId, AccountId( 1 ) );
    (void)clientB.acceptPartyInvite( partyId, recorderB.makeDelegate() );
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_TRUE( recorderB._listReply.back()._reply._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( clientA.getParty()._listMemberId.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( clientB.getParty()._listMemberId.size(), size_t( 2 ) );

    (void)clientA.joinQueue( "pair", "kr", recorderA.makeDelegate() ); // 1 대 1 모드에 둘인 파티
    test::tickAll( { &server }, clients, 0 );
    SW_EXPECT_TRUE( recorderA._listReply.back()._reply._result == MatchmakingResult::PartyTooLarge );

    (void)clientB.leaveParty( recorderB.makeDelegate() );
    test::tickAll( { &server }, clients, 0 );
    SW_EXPECT_TRUE( clientB.getParty()._listMemberId.empty() );
    SW_EXPECT_EQUAL( clientA.getParty()._listMemberId.size(), size_t( 1 ) );
    (void)clientA.leaveParty( recorderA.makeDelegate() );
    test::tickAll( { &server }, clients, 0 );
    SW_EXPECT_TRUE( clientA.getParty()._listMemberId.empty() );

    LobbySnapshot lobbyRequest;
    lobbyRequest._name           = "after school";
    lobbyRequest._modeId         = "pair";
    lobbyRequest._maxMemberCount = 2;
    (void)clientA.createLobby( lobbyRequest, recorderA.makeDelegate() ); // 요청 번호는 쓰지 않는다 — 답은 recorder 로 확인한다
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_TRUE( recorderA._listReply.back()._reply._result == MatchmakingResult::Ok );
    (void)clientB.listLobbies( "pair", recorderB.makeDelegate() );
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_EQUAL( recorderB._listReply.back()._reply._listLobby.size(), size_t( 1 ) );
    SW_EXPECT_STREQ( recorderB._listReply.back()._reply._listLobby[0]._name.c_str(), "after school" );

    (void)clientA.joinQueue( "pair", "kr", recorderA.makeDelegate() );
    (void)clientB.joinQueue( "pair", "kr", recorderB.makeDelegate() );
    test::tickAll( { &server }, clients, 1000 );
    SW_EXPECT_TRUE( recorderA._listReply.back()._reply._result == MatchmakingResult::Ok );
    SW_EXPECT_NOT_EQUAL( recorderA._listReply.back()._reply._ticketId, uint64( 0 ) );
    test::tickAll( { &server }, clients, 1000 );
    vector<MatchAssignment> listMatchA;
    vector<MatchAssignment> listMatchB;
    clientA.drainMatchResults( listMatchA );
    clientB.drainMatchResults( listMatchB );
    SW_ASSERT_EQUAL( listMatchA.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( listMatchB.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listMatchA[0]._outcome == MatchQueueOutcome::Found );
    SW_EXPECT_STREQ( listMatchA[0]._address.c_str(), "10.0.0.9" );
    SW_EXPECT_EQUAL( listMatchA[0]._port, uint16( 7777 ) );
    SW_EXPECT_EQUAL( listMatchA[0]._matchId, listMatchB[0]._matchId );
    SW_EXPECT_NOT_EQUAL( listMatchA[0]._team, listMatchB[0]._team );
}

SW_TEST_CASE( MatchmakingStreamTest, DroppedAccountLeavesThePartyAndTheQueue )
{
    LoopbackStreamNetwork   network( 3u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    MatchmakingOnServer     matchmaking;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    matchmaking.start( server );

    MatchmakingClient       clientA;
    MatchmakingClient       clientB;
    test::OnlineTestClients clients( network );
    const int32             indexA = clients.connect( server._port, { &clientA } );
    const int32             indexB = clients.connect( server._port, { &clientB } );
    clientA.initialize( &clients.getClient( indexA ) );
    clientB.initialize( &clients.getClient( indexB ) );
    test::tickAll( { &server }, clients, 0 );
    clients.login( indexA, 1 );
    clients.login( indexB, 2 );
    test::tickAll( { &server }, clients, 0 );

    MatchmakingRecorder recorder;
    (void)clientA.createParty( recorder.makeDelegate() ); // 요청 번호는 쓰지 않는다 — 결과는 getParty 로 확인한다
    test::tickAll( { &server }, clients, 0 );
    const uint64 partyId = clientA.getParty()._partyId;
    (void)clientA.inviteToParty( 2, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 0 );
    (void)clientB.acceptPartyInvite( partyId, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 0 );
    (void)clientB.joinQueue( "pair", "kr", recorder.makeDelegate() ); // 장이 아니다
    test::tickAll( { &server }, clients, 0 );
    SW_EXPECT_TRUE( recorder._listReply.back()._reply._result == MatchmakingResult::NotPartyLeader );
    SW_EXPECT_EQUAL( clientB.getParty()._listMemberId.size(), size_t( 2 ) );

    clients.drop( indexA ); // 장이 끊김 — 파티에서 빠지고 B 가 장
    test::tickAll( { &server }, clients, 0, 10 );
    SW_ASSERT_EQUAL( clientB.getParty()._listMemberId.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( clientB.getParty().getLeaderId(), AccountId( 2 ) );
    SW_EXPECT_EQUAL( matchmaking._partyLobby.getPendingCount(), 0 );
}
