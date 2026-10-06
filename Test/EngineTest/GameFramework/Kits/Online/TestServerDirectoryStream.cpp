// 서버 디렉터리 끝단(루프백) — 로그인 전 상태 · 목록, 로그인 뒤 배정(전엔 kUnauthenticated), 모르는 종류는 키트 오류,
// 점검을 걸면 붙어 있는 클라이언트에 상태 알림, 다른 서버에서 건 점검도 버스로 와서 이 서버의 클라이언트에 알린다.
#include "pch.h"

#include "Core/Network/Transport/LoopbackStreamTransport.h"

#include "EngineTest/GameFramework/Online/OnlineHostTestUtil.h"

#include "GameFramework/Base/Online/Directory/ServerRegistration.h"
#include "GameFramework/Kits/Online/Server/ServerDirectory/ServerDirectoryServer.h"
#include "GameFramework/Kits/Online/Server/ServerDirectory/ServerDirectoryService.h"
#include "GameFramework/Kits/Online/ServerDirectory/ServerDirectoryClient.h"
#include "GameFramework/Kits/Online/ServerDirectory/ServerDirectoryProtocol.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct DirectoryRecorder
    {
        vector<ServerAssignment>        _listAssignment{};
        vector<uint16>                  _listAssignmentError{};
        vector<vector<ServerListEntry>> _listServerList{};
        vector<uint16>                  _listServerListError{};
        vector<uint16>                  _listStatusError{};

        void onAssignment( uint16 errorCode, const ServerAssignment& assignment )
        {
            _listAssignmentError.push_back( errorCode );
            _listAssignment.push_back( assignment );
        }

        void onServerList( uint16 errorCode, const vector<ServerListEntry>& listEntry )
        {
            _listServerListError.push_back( errorCode );
            _listServerList.push_back( listEntry );
        }

        void onStatus( uint16 errorCode, const ServerDirectoryStatus& status )
        {
            (void)status;
            _listStatusError.push_back( errorCode );
        }
    };

    /** @brief 서버 하나의 디렉터리(로직 + 바인딩) — `start` 가 서버에 올려 서버가 내려가기 전에 `stop` 한다. */
    struct DirectoryOnServer final : public test::IOnlineTestKit
    {
        ServerDirectoryService _service;
        ServerDirectoryServer  _binding;

        DirectoryOnServer()
            : _service{}
            , _binding{}
        {
        }

        /** @brief 서버가 내려가기 전에 부른다(호스트 · 저장소 · 접속 상태가 살아 있다). 두 번 불려도 된다. */
        void stop() override
        {
            _binding.shutdown();
            _service.shutdown();
        }

        /** @brief 서버를 띄우기 전에 바인딩을 올리고, 띄운 뒤 로직을 호스트의 저장소 · 라우터 · 버스에 잇습니다. */
        void start( test::OnlineTestServer& server )
        {
            SW_EXPECT_TRUE( server._host.registerService( &_binding ) );
            server.start();
            server.addKit( this );
            ServerDirectoryDependencies dependencies;
            dependencies._pStore  = &server._store;
            dependencies._pRouter = server._host.getEphemeralRouter();
            dependencies._pBus    = &server._bus;
            ServerDirectorySettings settings;
            settings._listServerKind.push_back( "game" );
            _service.initialize( dependencies, settings );
            _binding.initialize( &_service );
        }
    };

    struct ServerDirectoryStreamTestInternal
    {
        static ServerAssignmentRequest makeRequest( string_view kind )
        {
            ServerAssignmentRequest request;
            request._kind   = string( kind );
            request._region = "kr";
            return request;
        }
    };
} // namespace

SW_TEST_CASE( ServerDirectoryStreamTest, ListBeforeLoginAssignAfterLoginAndMaintenancePush )
{
    using Internal = ServerDirectoryStreamTestInternal;
    LoopbackStreamNetwork   network( 3u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    DirectoryOnServer       directory;
    test::OnlineTestServer  server( network, &database, &cacheDatabase, &hub, 1 );
    directory.start( server );

    ServerRegistration registration;
    ServerDescriptor   descriptor;
    descriptor._serverId = 10;
    descriptor._kind     = "game";
    descriptor._region   = "kr";
    descriptor._address  = "10.0.0.1";
    descriptor._port     = 7777;
    descriptor._capacity = 10;
    SW_ASSERT_TRUE( registration.initialize( server._host.getEphemeralRouter(), descriptor ) );
    registration.setState( ServerState::Open );
    registration.tick( 0 );

    ServerDirectoryClient   directoryClient;
    test::OnlineTestClients clients( network );
    const int32             clientIndex = clients.connect( server._port, { &directoryClient } );
    directoryClient.initialize( &clients.getClient( clientIndex ) );
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_TRUE( clients.getClient( clientIndex ).isReady() );

    DirectoryRecorder recorder;
    (void)directoryClient.requestStatus( ServerDirectoryClient::StatusDelegate::create<&DirectoryRecorder::onStatus>( &recorder ) );
    (void)directoryClient.requestServerList( "game", ServerDirectoryClient::ServerListDelegate::create<&DirectoryRecorder::onServerList>( &recorder ) );
    (void)directoryClient.requestServerList( "chat", ServerDirectoryClient::ServerListDelegate::create<&DirectoryRecorder::onServerList>( &recorder ) );
    (void)directoryClient.requestAssignment( Internal::makeRequest( "game" ),
                                             ServerDirectoryClient::AssignmentDelegate::create<&DirectoryRecorder::onAssignment>( &recorder ) );
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_EQUAL( recorder._listStatusError.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder._listStatusError[0], OnlineError::kOk ); // 익명 메서드
    SW_ASSERT_EQUAL( recorder._listServerListError.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( recorder._listServerListError[0], OnlineError::kOk );
    SW_ASSERT_EQUAL( recorder._listServerList[0].size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder._listServerList[0][0]._port, uint16( 7777 ) );
    SW_EXPECT_EQUAL( recorder._listServerListError[1], ServerDirectoryError::kUnknownKind );
    SW_ASSERT_EQUAL( recorder._listAssignmentError.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder._listAssignmentError[0], OnlineError::kUnauthenticated ); // 배정은 로그인 뒤

    clients.login( clientIndex, 500 );
    test::tickAll( { &server }, clients, 0 );
    (void)directoryClient.requestAssignment( Internal::makeRequest( "game" ),
                                             ServerDirectoryClient::AssignmentDelegate::create<&DirectoryRecorder::onAssignment>( &recorder ) );
    test::tickAll( { &server }, clients, 0 );
    SW_ASSERT_EQUAL( recorder._listAssignment.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( recorder._listAssignmentError[1], OnlineError::kOk );
    SW_EXPECT_TRUE( recorder._listAssignment[1]._result == ServerDirectoryResult::Ok );
    SW_EXPECT_EQUAL( recorder._listAssignment[1]._port, uint16( 7777 ) );
    SW_EXPECT_TRUE( recorder._listAssignment[1]._address == "10.0.0.1" );

    const uint64      revisionBefore = directoryClient.getStatusRevision();
    MaintenanceWindow window;
    window._scope      = MaintenanceWindow::kScopeAll;
    window._messageKey = "notice.maintenance";
    directory._service.setMaintenance( window, 1, "", 0, 1 );
    test::tickAll( { &server }, clients, 10 );
    SW_ASSERT_EQUAL( directoryClient.getLastStatus()._listMaintenance.size(), size_t( 1 ) ); // 알림이 왔다
    SW_EXPECT_TRUE( directoryClient.getLastStatus()._listMaintenance[0]._messageKey == "notice.maintenance" );
    SW_EXPECT_EQUAL( directoryClient.getStatusRevision(), revisionBefore + 1 );

    (void)directoryClient.requestAssignment( Internal::makeRequest( "game" ),
                                             ServerDirectoryClient::AssignmentDelegate::create<&DirectoryRecorder::onAssignment>( &recorder ) );
    test::tickAll( { &server }, clients, 10 );
    SW_ASSERT_EQUAL( recorder._listAssignment.size(), size_t( 3 ) );
    SW_EXPECT_TRUE( recorder._listAssignment[2]._result == ServerDirectoryResult::Maintenance ); // 점검은 오류가 아니라 답이다
}

SW_TEST_CASE( ServerDirectoryStreamTest, MaintenanceSetOnAnotherServerIsPushedToThisServersClients )
{
    LoopbackStreamNetwork   network( 4u );
    MemoryServiceDatabase   database;
    MemoryEphemeralDatabase cacheDatabase;
    LocalServerBusHub       hub;
    DirectoryOnServer       firstDirectory;
    DirectoryOnServer       secondDirectory;
    test::OnlineTestServer  first( network, &database, &cacheDatabase, &hub, 1 );
    test::OnlineTestServer  second( network, &database, &cacheDatabase, &hub, 2 );
    firstDirectory.start( first );
    secondDirectory.start( second );

    ServerDirectoryClient   directoryClient;
    test::OnlineTestClients clients( network );
    const int32             clientIndex = clients.connect( second._port, { &directoryClient } ); // 두 번째 서버에 붙었다
    directoryClient.initialize( &clients.getClient( clientIndex ) );
    test::tickAll( { &first, &second }, clients, 0 );
    clients.login( clientIndex, 700 );
    test::tickAll( { &first, &second }, clients, 0 );

    MaintenanceWindow window;
    window._scope = "game";
    firstDirectory._service.setMaintenance( window, 1, "", 0, 1 ); // 첫 번째 서버의 GM 도구가 건다
    test::tickAll( { &first, &second }, clients, 10, 10 );
    SW_ASSERT_EQUAL( directoryClient.getLastStatus()._listMaintenance.size(), size_t( 1 ) ); // 버스 → 다시 읽기 → 알림
    SW_EXPECT_TRUE( directoryClient.getLastStatus()._listMaintenance[0]._scope == "game" );
}
