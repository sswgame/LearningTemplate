// 서버 디렉터리 로직 — 배정, 전체 점검(허용 계정은 점검 서버까지), 점검 걸기가 버스로 다른 서버에 퍼짐(주기 다시 읽기 전), 공지 기간 경계의 상태 변화,
// 저장소 실패는 아무것도 남기지 않음, 없는 공지 지우기는 NotFound, 규칙 밖 바꾸기는 Invalid.
#include "pch.h"

#include "EngineTest/GameFramework/Online/OnlineMessageTestUtil.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Directory/ServerRegistration.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryService.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Shared/ServerDirectoryProtocol.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 서버 프로세스 하나의 로직 — 저장소 앞 · 캐시 앞 · 라우터 · 로컬 버스 · 디렉터리. 버스 메시지는 호스트 대신 테스트가 비워 notifyChanged 로 넘긴다. */
    struct DirectoryNode
    {
        MemoryServiceStore                _store;
        MemoryEphemeralStore              _cache;
        EphemeralStoreRouter              _router;
        LocalServerBus                    _bus;
        ServerBus                         _busFront;
        ServerDirectoryService            _service;
        vector<ServerDirectoryCompletion> _listCompletion;
        int32                             _statusChangeCount;

        DirectoryNode( MemoryServiceDatabase* pDatabase, MemoryEphemeralDatabase* pCacheDatabase, LocalServerBusHub* pHub, uint64 serverID )
            : _store{ pDatabase }
            , _cache{ pCacheDatabase }
            , _router{}
            , _bus{ pHub, serverID }
            , _busFront{ _bus }
            , _service{}
            , _listCompletion{}
            , _statusChangeCount{ 0 }
        {
            _router.initialize( &_cache );
            _bus.subscribe( ServerDirectoryBus::kChangedTopic.getName() );
            ServerDirectoryDependencies dependencies;
            dependencies._pStore  = &_store;
            dependencies._pRouter = &_router;
            dependencies._pBus    = &_busFront;
            ServerDirectoryServiceConfig settings;
            settings._listServerKind.push_back( "game" );
            _service.initialize( dependencies, settings );
        }

        ~DirectoryNode()
        {
            _service.shutdown();
            _store.shutdown();
            (void)_store.pollCompletions();
            _router.shutdown();
        }

        /** @brief 틱 + 저장 · 캐시 · 버스 왕복을 끝까지. */
        void step( int64 nowMs )
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                vector<ServerBusMessage> listMessage;
                (void)_bus.pollMessages( listMessage );
                for ( const ServerBusMessage& message : listMessage )
                {
                    ServerDirectoryChanged changed;
                    const bool             bFromOtherServer = message.getOriginServerID() != _bus.getServerID();
                    if ( bFromOtherServer && ServerDirectoryBus::kChangedTopic.read( message, changed ) == ServerBusReadResult::Read )
                        _service.notifyChanged();
                }
                _service.update( nowMs );
                (void)_store.pollCompletions();
                (void)_router.pump();
                _statusChangeCount += _service.takeStatusChange() ? 1 : 0;
            }
            _service.drainCompletions( _listCompletion );
        }
    };

    struct ServerDirectoryServiceTestInternal
    {
        static void registerGameServer( EphemeralStoreRouter& router, ServerRegistration& registration, uint64 serverID, ServerState state )
        {
            ServerDescriptor descriptor;
            descriptor._serverID = serverID;
            descriptor._kind     = "game";
            descriptor._region   = "kr";
            descriptor._address  = "10.0.0.1";
            descriptor._port     = 7777;
            descriptor._capacity = 100;
            SW_EXPECT_TRUE( registration.initialize( &router, descriptor ) );
            registration.setState( state );
            registration.update( 0 );
            (void)router.pump();
        }

        static ServerAssignmentRequest makeRequest()
        {
            ServerAssignmentRequest request;
            request._kind   = "game";
            request._region = "kr";
            return request;
        }
    };
} // namespace

SW_TEST_CASE( ServerDirectoryServiceTest, AssignsOpenServerAndHonoursMaintenanceAllowList )
{
    using Internal = ServerDirectoryServiceTestInternal;
    MemoryServiceDatabase   database;
    ManualClock             cacheClock{ 0 };
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    DirectoryNode           node( &database, &cacheDatabase, &hub, 1 );
    ServerRegistration      openServer;
    ServerRegistration      maintenanceServer;
    Internal::registerGameServer( node._router, openServer, 10, ServerState::Open );
    Internal::registerGameServer( node._router, maintenanceServer, 11, ServerState::Maintenance );
    node.step( 0 );

    ServerAssignment assignment = node._service.assignServer( 500, Internal::makeRequest(), 0 );
    SW_ASSERT_TRUE( assignment._result == ServerDirectoryResult::Ok );
    SW_EXPECT_EQUAL( assignment._serverID, uint64( 10 ) );
    SW_EXPECT_EQUAL( assignment._port, uint16( 7777 ) );
    ServerAssignmentRequest unknownKind = Internal::makeRequest();
    unknownKind._kind                   = "chat";
    SW_EXPECT_TRUE( node._service.assignServer( 500, unknownKind, 0 )._result == ServerDirectoryResult::Invalid );

    MaintenanceWindow window;
    window._scope      = MaintenanceWindow::kScopeAll;
    window._messageKey = "notice.maintenance.weekly";
    window._startMs    = 100;
    window._endMs      = 5000;
    window._listAllowedAccount.push_back( 900 );
    node._service.setMaintenance( window, 1, "weekly", 50, 77 );
    node.step( 100 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._listCompletion[0]._requestTag, uint64( 77 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._result == ServerDirectoryResult::Ok );

    assignment = node._service.assignServer( 500, Internal::makeRequest(), 200 );
    SW_EXPECT_TRUE( assignment._result == ServerDirectoryResult::Maintenance );
    SW_EXPECT_EQUAL( assignment._maintenanceEndMs, int64( 5000 ) );
    SW_EXPECT_TRUE( assignment._messageKey == "notice.maintenance.weekly" );

    openServer.setState( ServerState::Maintenance ); // 점검 동안 모든 서버가 점검 상태
    openServer.update( 200 );
    (void)node._router.pump();
    node.step( 2500 );                                                                                                             // 목록 다시 읽기(2 초 주기)
    SW_EXPECT_TRUE( node._service.assignServer( 900, Internal::makeRequest(), 2500 )._result == ServerDirectoryResult::Ok );       // 허용 계정은 점검 서버로
    SW_EXPECT_TRUE( node._service.assignServer( 500, Internal::makeRequest(), 6000 )._result == ServerDirectoryResult::NoServer ); // 점검은 끝났지만 서버가 아직 점검 상태
}

SW_TEST_CASE( ServerDirectoryServiceTest, MaintenanceSetOnOneServerReachesTheOtherBeforeThePeriodicReload )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    DirectoryNode           first( &database, &cacheDatabase, &hub, 1 );
    DirectoryNode           second( &database, &cacheDatabase, &hub, 2 );
    first.step( 0 );
    second.step( 0 );
    const int32 changesBefore = second._statusChangeCount;

    MaintenanceWindow window;
    window._scope = "game";
    window._listAllowedAccount.push_back( 900 );
    first._service.setMaintenance( window, 1, "", 10, 1 );
    first.step( 10 );  // 쓰기 완료 → 버스 발행
    second.step( 20 ); // 버스 → 다시 읽기(주기 30 초 전)
    SW_EXPECT_TRUE( second._service.isBlockedByMaintenance( "game", 500, 20 ) );
    SW_EXPECT_FALSE( second._service.isBlockedByMaintenance( "game", 900, 20 ) );
    SW_EXPECT_FALSE( second._service.isBlockedByMaintenance( "chat", 500, 20 ) ); // 범위 밖 종류
    SW_EXPECT_EQUAL( second._statusChangeCount, changesBefore + 1 );
    const ServerDirectoryStatus status = second._service.makeStatus( 20 );
    SW_ASSERT_EQUAL( status._listMaintenance.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( status._listMaintenance[0]._scope == "game" ); // 보이는 타입(`MaintenanceView`)에는 허용 목록 필드가 없다

    second._service.clearMaintenance( "game", 1, "done", 30, 2 );
    second.step( 30 );
    first.step( 40 );
    SW_EXPECT_FALSE( first._service.isBlockedByMaintenance( "game", 500, 40 ) ); // 풀기도 건너간다
}

SW_TEST_CASE( ServerDirectoryServiceTest, NoticeAppearsAndDisappearsAtItsWindowEdges )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    DirectoryNode           node( &database, &cacheDatabase, &hub, 1 );
    ServiceNotice           notice;
    notice._noticeID = 3;
    notice._text     = "notice.event.halloween";
    notice._startMs  = 1000;
    notice._endMs    = 2000;
    ServiceNotice urgent;
    urgent._noticeID = 4;
    urgent._text     = "notice.urgent";
    urgent._priority = 10;
    node._service.postNotice( notice, 1, 0, 1 );
    node._service.postNotice( urgent, 1, 0, 2 );
    node.step( 0 );
    SW_ASSERT_EQUAL( node._service.makeStatus( 500 )._listNotice.size(), size_t( 1 ) ); // 아직 기간 전 — 급한 공지만
    const int32 changesBefore = node._statusChangeCount;

    node.step( 1000 ); // 시작 경계 — 다시 읽지 않아도 바뀜
    const ServerDirectoryStatus during = node._service.makeStatus( 1000 );
    SW_ASSERT_EQUAL( during._listNotice.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( during._listNotice[0]._noticeID, uint64( 4 ) ); // 우선순위가 위
    SW_EXPECT_EQUAL( node._statusChangeCount, changesBefore + 1 );
    node.step( 2000 ); // 끝 경계
    SW_EXPECT_EQUAL( node._service.makeStatus( 2000 )._listNotice.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( node._statusChangeCount, changesBefore + 2 );
    node.step( 2500 ); // 보이는 것이 같으면 알리지 않는다
    SW_EXPECT_EQUAL( node._statusChangeCount, changesBefore + 2 );
}

SW_TEST_CASE( ServerDirectoryServiceTest, StoreFailureLeavesNothingAndBadInputIsRefused )
{
    MemoryServiceDatabase   database;
    SystemMonotonicClock    cacheClock;
    MemoryEphemeralDatabase cacheDatabase{ cacheClock };
    LocalServerBusHub       hub;
    DirectoryNode           node( &database, &cacheDatabase, &hub, 1 );
    node.step( 0 );
    const uint64 hashBefore = database.computeContentHash();
    database.armFault( ServiceStoreFault::RejectCommit );
    MaintenanceWindow window;
    window._scope = MaintenanceWindow::kScopeAll;
    node._service.setMaintenance( window, 1, "", 10, 9 );
    node.step( 10 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._result == ServerDirectoryResult::Unavailable );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashBefore ); // 레코드도 감사 줄도 없다
    SW_EXPECT_FALSE( node._service.isBlockedByMaintenance( "game", 500, 10 ) );

    node._service.removeNotice( 12345, 1, 20, 10 ); // 없는 공지
    MaintenanceWindow badScope;
    badScope._scope = "Game!";
    node._service.setMaintenance( badScope, 1, "", 20, 11 );
    ServiceNotice backwards;
    backwards._noticeID = 5;
    backwards._text     = "x";
    backwards._startMs  = 100;
    backwards._endMs    = 50;
    node._service.postNotice( backwards, 1, 20, 12 );
    node.step( 20 );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 4 ) );
    for ( const ServerDirectoryCompletion& completion : node._listCompletion )
    {
        if ( completion._requestTag == 10 )
            SW_EXPECT_TRUE( completion._result == ServerDirectoryResult::NotFound );
        else if ( completion._requestTag == 11 || completion._requestTag == 12 )
            SW_EXPECT_TRUE( completion._result == ServerDirectoryResult::Invalid );
    }
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 0 );
}

/**
 * @brief [ServerDirectoryProtocolTest] 허용 계정 목록의 원소를 `AccountID` 별칭으로 적어도 저장소 레코드의 바이트는 그대로다
 * @details 생성기가 컨테이너 원소의 별칭을 벗기기 전에는 `uint64` 로 적어 피했다. 골든은 `uint64` 로 적던 빌드가 쓴 바이트다.
 */
SW_TEST_CASE( ServerDirectoryProtocolTest, MaintenanceRecordBytesStayTheSameWithTheAccountIDElement )
{
    MaintenanceWindow window;
    window._scope      = "game";
    window._messageKey = "notice.maintenance";
    window._listAllowedAccount.push_back( 900 );
    window._listAllowedAccount.push_back( 0x0102030405060708ull );
    vector<uint8> bytes;
    SW_ASSERT_TRUE( ServerDirectoryProtocol::encodeMaintenance( window, bytes ) );
    const string hex = test::OnlineMessageTestUtil::toHexString( bytes );
    SW_EXPECT_TRUE_MSG( hex == "0100000005000001f94ba75516ce124f140000000200000084030000000000000807060504030201aae70c7a3865c117080000000400000067616d65c4854d94"
                               "3865c11716000000120000006e6f746963652e6d61696e74656e616e6365ce98ab1d6423d20308000000000000000000000013507a986423d203080000000000000000000000",
                        hex.c_str() );
    MaintenanceWindow decoded;
    SW_ASSERT_TRUE( ServerDirectoryProtocol::decodeMaintenance( "game", bytes, decoded ) );
    SW_EXPECT_TRUE( decoded.allowsAccount( 900 ) && decoded.allowsAccount( 0x0102030405060708ull ) );
}

/**
 * @brief [ServerDirectoryProtocolTest] 서버 디렉터리 메시지는 상한까지 왕복하고, 점검 창과 공지 레코드는 자기 기술형으로 왕복하며, 범위 밖 인원은 쓰지 않는다
 */
SW_TEST_CASE( ServerDirectoryProtocolTest, MessagesAndRecordsRoundTrip )
{
    using Messages = ServerDirectoryProtocol::MessageTypes;
    using Util     = test::OnlineMessageTestUtil;
    ServerDirectoryStatus status;
    status._listMaintenance.assign( 16, MaintenanceView{ "all", string( 64, 'm' ), 1, 0 } );
    status._listNotice.assign( 32, ServiceNotice{ string( 1024, 'n' ), 3, 0, 9, -2, SW_TRUE } );
    SW_EXPECT_TRUE( Util::roundTrips<Messages>( status ) );
    ServerListReply list;
    list._listEntry.assign( 256, ServerListEntry{ "kr", string( 128, 'a' ), 5, 65535, 100, ServerState::Maintenance } );
    SW_EXPECT_TRUE( Util::roundTrips<Messages>( list ) );
    SW_EXPECT_TRUE( Util::roundTrips<Messages>( ServerAssignment{ "127.0.0.1", "notice.maintenance", 4, 99, 7777, ServerDirectoryResult::Maintenance } ) );
    SW_EXPECT_TRUE( Util::refusesEveryPrefix<Messages>( ServerAssignmentRequest{ "game", "kr", 3, 64 } ) );

    MaintenanceWindow window;
    window._scope      = "game";
    window._messageKey = "notice.maintenance";
    window._listAllowedAccount.push_back( 900 );
    MaintenanceWindow decodedWindow;
    vector<uint8>     windowBytes;
    SW_ASSERT_TRUE( ServerDirectoryProtocol::encodeMaintenance( window, windowBytes ) );
    SW_ASSERT_TRUE( ServerDirectoryProtocol::decodeMaintenance( "game", windowBytes, decodedWindow ) );
    SW_EXPECT_TRUE( decodedWindow.allowsAccount( 900 ) && decodedWindow._scope == "game" );
    SW_EXPECT_TRUE( window.makeView()._messageKey == "notice.maintenance" );
    {
        SW_TEST_DEFENSIVE_SCOPE( "out-of-range values are not written" );
        SW_EXPECT_TRUE( Util::refusesToWrite<Messages>( ServerAssignmentRequest{ "game", "kr", 0, 0 } ) );
        SW_EXPECT_TRUE( Util::refusesToWrite<Messages>( ServerAssignmentRequest{ "game", "kr", 0, 65 } ) );
    }
}
