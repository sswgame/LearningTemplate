// 라이브 운영 끝단(루프백) — 로그인 전 kUnauthenticated, 열린 이벤트 받기(대상 밖 · 클라이언트에 숨긴 이벤트는 빠짐), 운영이 이벤트를 넣으면 붙어 있는 클라이언트에
// 바뀜 알림이 가고 클라이언트가 스스로 다시 받는다.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

#include "GameFramework/Kits/Online/LiveOps/LiveOpsClient.h"
#include "GameFramework/Kits/Online/Server/LiveOps/LiveOpsServer.h"
#include "GameFramework/Kits/Online/Server/LiveOps/LiveOpsService.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct LiveOpsRecorder
    {
        vector<LiveOpsClientReply> _listReply{};

        void onReply( const LiveOpsClientReply& reply ) { _listReply.push_back( reply ); }

        LiveOpsClient::ReplyDelegate makeDelegate() { return LiveOpsClient::ReplyDelegate::create<&LiveOpsRecorder::onReply>( this ); }
    };

    /** @brief 서버 하나의 라이브 운영(로직 + 바인딩) — 서버보다 먼저 선언한다. */
    struct LiveOpsOnServer
    {
        LiveOpsService _service;
        LiveOpsServer  _binding;

        LiveOpsOnServer()
            : _service{}
            , _binding{}
        {
        }

        ~LiveOpsOnServer()
        {
            _binding.shutdown();
            _service.shutdown();
        }

        void start( test::OnlineTestServer& server )
        {
            SW_EXPECT_TRUE( server._host.registerService( &_binding ) );
            server.start();
            LiveOpsDependencies dependencies;
            dependencies._pStore = &server._store;
            dependencies._pBus   = &server._bus;
            _service.initialize( dependencies );
            _binding.initialize( &_service );
        }
    };

    struct LiveOpsStreamTestInternal
    {
        static LiveEventDefinition makeEvent( string_view eventId, string_view kind )
        {
            LiveEventDefinition definition;
            definition._eventId = string( eventId );
            definition._kind    = string( kind );
            definition._startMs = 0;
            definition._endMs   = 1000000;
            return definition;
        }
    };
} // namespace

SW_TEST_CASE( LiveOpsStreamTest, ClientRefreshesWhenOperationsPutAnEvent )
{
    using Internal = LiveOpsStreamTestInternal;
    LoopbackStreamNetwork   network( 3u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    LiveOpsOnServer         liveOps;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    liveOps.start( server );

    LiveOpsClient           liveOpsClient;
    test::OnlineTestClients clients( network );
    const int32             clientIndex = clients.connect( server._port, { &liveOpsClient } );
    liveOpsClient.initialize( &clients.getClient( clientIndex ) );
    test::tickAll( { &server }, clients, 1000 );

    LiveOpsRecorder recorder;
    (void)liveOpsClient.requestLiveState( "kr", 100, recorder.makeDelegate() ); // 로그인 전
    test::tickAll( { &server }, clients, 1000 );
    SW_ASSERT_EQUAL( recorder._listReply.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder._listReply[0]._errorCode, OnlineError::kUnauthenticated );

    clients.login( clientIndex, 5 );
    test::tickAll( { &server }, clients, 1000 );
    LiveEventDefinition hidden = Internal::makeEvent( "server_only", "balance" );
    hidden._bClientVisible     = SW_FALSE;
    LiveEventDefinition future = Internal::makeEvent( "future_build", "preview" );
    future._minBuildVersion    = 200;
    liveOps._service.putEvent( hidden, 0, "", 1000, 1 );
    liveOps._service.putEvent( future, 0, "", 1000, 2 );
    test::tickAll( { &server }, clients, 1000 );
    (void)liveOpsClient.requestLiveState( "kr", 100, recorder.makeDelegate() );
    test::tickAll( { &server }, clients, 1000 );
    SW_ASSERT_TRUE( recorder._listReply.back()._reply._result == LiveOpsResult::Ok );
    SW_EXPECT_TRUE( liveOpsClient.getActiveEvents().empty() ); // 숨긴 것 · 빌드 판이 낮아 대상 밖
    const uint64 revisionBefore = liveOpsClient.getRevision();

    liveOps._service.putEvent( Internal::makeEvent( "halloween", "xp_boost" ), 0, "", 1000, 3 ); // 운영(GM 도구)
    test::tickAll( { &server }, clients, 2000 );
    test::tickAll( { &server }, clients, 2000 );
    SW_EXPECT_TRUE( liveOpsClient.getRevision() > revisionBefore ); // 알림 → 스스로 다시 받음
    SW_ASSERT_EQUAL( liveOpsClient.getActiveEvents().size(), size_t( 1 ) );
    SW_EXPECT_TRUE( liveOpsClient.hasEventKind( "xp_boost" ) );
    SW_EXPECT_FALSE( liveOpsClient.hasEventKind( "balance" ) );
}
