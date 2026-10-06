// 서버 등록 · 읽기 — 등록한 서버가 다른 서버의 스냅숏에 보이고, 하트비트가 멈추면 시한 뒤 사라지며 색인도 정리되고, 정상 종료는 바로 사라진다.
// 고른 몫은 다음 읽기 전까지 얹힌다. 읽는 쪽이 라우터보다 먼저 내려가도 기다리던 답이 사라진 객체를 부르지 않는다. 기록 형식은 왕복하고 다른 판은 읽지 않는다.
#include "pch.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Directory/ServerRegistration.h"
#include "GameFramework/Base/Online/Directory/ServerRegistryReader.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 서버 프로세스 하나의 캐시 쪽 — 앞 + 라우터. */
    struct RegistryNode
    {
        MemoryEphemeralStore _store;
        EphemeralStoreRouter _router;

        explicit RegistryNode( MemoryEphemeralDatabase* pDatabase )
            : _store{ pDatabase }
            , _router{}
        {
            _router.initialize( &_store );
        }

        ~RegistryNode() { _router.shutdown(); }
    };

    struct IndexRecorder
    {
        vector<EphemeralScoredMember> _listMember{};
        uint8                         _bAnswered{ SW_FALSE };

        void onReply( const EphemeralReply& reply )
        {
            _listMember = reply._listMember;
            _bAnswered  = SW_TRUE;
        }
    };

    struct ServerRegistryTestInternal
    {
        static ServerDescriptor makeDescriptor( uint64 serverId )
        {
            ServerDescriptor descriptor;
            descriptor._serverId = serverId;
            descriptor._kind     = "game";
            descriptor._region   = "kr";
            descriptor._address  = "127.0.0.1";
            descriptor._port     = static_cast<uint16>( 7000 + serverId );
            descriptor._capacity = 10;
            return descriptor;
        }

        /** @brief 읽기를 끝까지 돌립니다(메모리 앞은 맡기는 자리에서 실행하므로 pump 몇 번이면 끝난다). */
        static void refreshFully( RegistryNode& node, ServerRegistryReader& reader, int64 nowMs )
        {
            reader.requestRefresh();
            reader.tick( nowMs );
            for ( int32 pumpIndex = 0; pumpIndex < 4 && reader.isRefreshing(); ++pumpIndex )
                (void)node._router.pump();
            (void)node._router.pump(); // 정리 쓰기의 답
        }

        /** @brief 종류 색인의 멤버 수입니다. */
        static size_t countIndexMembers( RegistryNode& node )
        {
            IndexRecorder recorder;
            (void)node._router.submit( EphemeralRequest::makeScoreRange( ServerRecord::makeIndexKey( "game" ), 0, 64 ),
                                       EphemeralStoreRouter::ReplyDelegate::create<&IndexRecorder::onReply>( &recorder ) );
            (void)node._router.pump();
            SW_EXPECT_TRUE( recorder._bAnswered == SW_TRUE );
            return recorder._listMember.size();
        }
    };
} // namespace

SW_TEST_CASE( ServerRegistryTest, RegisteredServerIsSeenAndExpiresWhenHeartbeatStops )
{
    using Internal = ServerRegistryTestInternal;
    MemoryEphemeralDatabase database;
    database.setManualTimeMs( 0 );
    RegistryNode       gameNode( &database );
    RegistryNode       lobbyNode( &database );
    ServerRegistration registration;
    SW_ASSERT_TRUE( registration.initialize( &gameNode._router, Internal::makeDescriptor( 1 ) ) );
    registration.setState( ServerState::Open );
    registration.tick( 0 );
    (void)gameNode._router.pump();

    ServerRegistryReader reader;
    reader.initialize( &lobbyNode._router, "game", 2000 );
    Internal::refreshFully( lobbyNode, reader, 0 );
    SW_ASSERT_EQUAL( reader.getSnapshot().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( reader.getSnapshot()[0]._descriptor._port, uint16( 7001 ) );
    SW_EXPECT_TRUE( reader.getSnapshot()[0]._state == ServerState::Open );
    SW_EXPECT_TRUE( reader.getSnapshot()[0]._descriptor._address == "127.0.0.1" );

    registration.tick( ServerRegistration::kHeartbeatPeriodMs - 1 ); // 주기 전 — 다시 쓰지 않는다
    database.advanceTimeMs( ServerRegistration::kRecordTtlMs );      // 하트비트 없이 시한이 지났다
    Internal::refreshFully( lobbyNode, reader, ServerRegistration::kRecordTtlMs );
    SW_EXPECT_EQUAL( reader.getSnapshot().size(), size_t( 0 ) );
    SW_EXPECT_EQUAL( Internal::countIndexMembers( lobbyNode ), size_t( 0 ) ); // 죽은 멤버를 색인에서 지웠다
    reader.shutdown();
}

SW_TEST_CASE( ServerRegistryTest, HeartbeatKeepsTheRecordAliveAndStateChangesAreWrittenAtOnce )
{
    using Internal = ServerRegistryTestInternal;
    MemoryEphemeralDatabase database;
    database.setManualTimeMs( 0 );
    RegistryNode       node( &database );
    ServerRegistration registration;
    SW_ASSERT_TRUE( registration.initialize( &node._router, Internal::makeDescriptor( 3 ) ) );
    registration.setState( ServerState::Open );
    ServerRegistryReader reader;
    reader.initialize( &node._router, "game", 2000 );
    for ( int64 nowMs = 0; nowMs <= 3 * ServerRegistration::kRecordTtlMs; nowMs += 1000 )
    {
        database.setManualTimeMs( nowMs );
        registration.tick( nowMs );
        (void)node._router.pump();
    }
    const int64 nowMs = 3 * ServerRegistration::kRecordTtlMs;
    Internal::refreshFully( node, reader, nowMs );
    SW_ASSERT_EQUAL( reader.getSnapshot().size(), size_t( 1 ) ); // 시한 셋을 지나도 하트비트가 살렸다

    registration.setState( ServerState::Maintenance );
    registration.setLoad( 4 );
    registration.tick( nowMs + 1 ); // 주기 전이어도 바뀐 것은 바로
    (void)node._router.pump();
    Internal::refreshFully( node, reader, nowMs + 1 );
    SW_ASSERT_EQUAL( reader.getSnapshot().size(), size_t( 1 ) );
    SW_EXPECT_TRUE( reader.getSnapshot()[0]._state == ServerState::Maintenance );
    SW_EXPECT_EQUAL( reader.getSnapshot()[0]._load, 4 );
    reader.shutdown();
}

SW_TEST_CASE( ServerRegistryTest, ShutdownRemovesImmediatelyAndPickAddsPendingLoad )
{
    using Internal = ServerRegistryTestInternal;
    MemoryEphemeralDatabase database;
    database.setManualTimeMs( 0 );
    RegistryNode       node( &database );
    ServerRegistration first;
    ServerRegistration second;
    SW_ASSERT_TRUE( first.initialize( &node._router, Internal::makeDescriptor( 1 ) ) );
    SW_ASSERT_TRUE( second.initialize( &node._router, Internal::makeDescriptor( 2 ) ) );
    first.setState( ServerState::Open );
    second.setState( ServerState::Open );
    first.tick( 0 );
    second.tick( 0 );
    (void)node._router.pump();

    ServerRegistryReader reader;
    reader.initialize( &node._router, "game", 2000 );
    Internal::refreshFully( node, reader, 0 );
    ServerSelectionQuery query;
    query._kind      = "game";
    query._region    = "kr";
    query._seatCount = 6;
    ServerStatus picked;
    SW_ASSERT_TRUE( reader.pickServer( query, 0, picked ) );
    SW_EXPECT_EQUAL( picked._descriptor._serverId, uint64( 1 ) );
    SW_ASSERT_TRUE( reader.pickServer( query, 0, picked ) ); // 1 은 6/10 이 얹혀 2 가 덜 찼다
    SW_EXPECT_EQUAL( picked._descriptor._serverId, uint64( 2 ) );
    SW_EXPECT_FALSE( reader.pickServer( query, 0, picked ) ); // 둘 다 6 + 6 > 10

    first.shutdown();
    (void)node._router.pump();
    Internal::refreshFully( node, reader, 1 );
    SW_ASSERT_EQUAL( reader.getSnapshot().size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( reader.getSnapshot()[0]._descriptor._serverId, uint64( 2 ) );
    SW_EXPECT_EQUAL( reader.getSnapshot()[0]._load, 0 ); // 새 스냅숏 — 얹은 몫은 사라졌다
    reader.shutdown();
}

SW_TEST_CASE( ServerRegistryTest, ReaderShutDownBeforeTheRouterIsNeverCalledBack )
{
    using Internal = ServerRegistryTestInternal;
    MemoryEphemeralDatabase database;
    RegistryNode            node( &database );
    ServerRegistration      registration;
    SW_ASSERT_TRUE( registration.initialize( &node._router, Internal::makeDescriptor( 1 ) ) );
    registration.tick( 0 );
    (void)node._router.pump();
    {
        ServerRegistryReader reader;
        reader.initialize( &node._router, "game", 2000 );
        reader.tick( 0 );
        SW_EXPECT_TRUE( reader.isRefreshing() );
    } // 읽기가 끝나기 전에 내려간다
    SW_EXPECT_EQUAL( node._router.getPendingCount(), 0 ); // 색인 읽기를 취소했다
    (void)node._router.pump();                            // 답이 와도 아무도 부르지 않는다
}

SW_TEST_CASE( ServerRegistryTest, RecordRoundTripsAndRejectsBadInput )
{
    using Internal = ServerRegistryTestInternal;
    ServerStatus status;
    status._descriptor               = Internal::makeDescriptor( 0xABCD );
    status._descriptor._buildVersion = 12;
    status._heartbeatMs              = 1234567;
    status._load                     = 3;
    status._state                    = ServerState::Draining;
    vector<uint8> bytes              = ServerRecord::encode( status );
    ServerStatus  decoded;
    SW_ASSERT_TRUE( ServerRecord::decode( bytes, decoded ) );
    SW_EXPECT_EQUAL( decoded._descriptor._serverId, uint64( 0xABCD ) );
    SW_EXPECT_EQUAL( decoded._descriptor._buildVersion, uint32( 12 ) );
    SW_EXPECT_EQUAL( decoded._heartbeatMs, int64( 1234567 ) );
    SW_EXPECT_TRUE( decoded._state == ServerState::Draining );
    SW_EXPECT_TRUE( decoded._descriptor._region == "kr" );

    bytes[0] = static_cast<uint8>( ServerRecord::kFormatVersion + 1 );
    SW_EXPECT_FALSE( ServerRecord::decode( bytes, decoded ) ); // 다른 판

    uint64 serverId = 0;
    SW_EXPECT_TRUE( ServerRecord::parseMember( ServerRecord::makeMember( 77 ), serverId ) );
    SW_EXPECT_EQUAL( serverId, uint64( 77 ) );
    SW_EXPECT_TRUE( ServerRecord::isValidName( "eu-west" ) );
    SW_EXPECT_FALSE( ServerRecord::isValidName( "Game" ) );
    SW_EXPECT_FALSE( ServerRecord::isValidName( "" ) );

    MemoryEphemeralDatabase database;
    RegistryNode            node( &database );
    ServerRegistration      registration;
    ServerDescriptor        descriptor = Internal::makeDescriptor( 1 );
    descriptor._kind                   = "Game";
    {
        SW_TEST_DEFENSIVE_SCOPE( "invalid server descriptors are refused" );
        SW_EXPECT_FALSE( registration.initialize( &node._router, descriptor ) );
        SW_EXPECT_FALSE( registration.initialize( &node._router, Internal::makeDescriptor( 0 ) ) );
    }
}
