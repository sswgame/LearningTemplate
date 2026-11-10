// ServerDirectoryServiceTest — 서버 디렉터리 서비스(메모리 저장소 · 캐시): 배정 · 목록, 점검의 막기와 허용 계정, 변경 규칙 · 완료 · 감사 기록, 저장소 실패와 손상 레코드에서 이전 값 지키기,
// 다시 읽기 일정(주기 · 알림 · 쓰기 뒤), 보이는 상태의 바뀜 표시, GM 창구의 결과 변환. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryService.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Directory/Server/ServerRegistration.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs    = 1700000000000ll;
    constexpr AccountID kPlayer   = 22;
    constexpr AccountID kTester   = 23;
    constexpr uint64    kServerID = 5;

    ServerDescriptor makeDescriptor( uint64 serverID = kServerID )
    {
        ServerDescriptor descriptor;
        descriptor._serverID     = serverID;
        descriptor._kind         = "game";
        descriptor._region       = "kr";
        descriptor._address      = "10.0.0.1";
        descriptor._buildVersion = 7;
        descriptor._capacity     = 100;
        descriptor._port         = 7000;
        descriptor._portCount    = 1;
        return descriptor;
    }

    MaintenanceWindow makeWindow( string_view scope = "game", int64 startMs = kNowMs - 1, int64 endMs = kNowMs + 1000 )
    {
        MaintenanceWindow window;
        window._scope      = string( scope );
        window._messageKey = "maint.key";
        window._startMs    = startMs;
        window._endMs      = endMs;
        return window;
    }

    ServiceAnnouncement makeNotice( uint64 noticeID = 1, int64 startMs = kNowMs - 1, int64 endMs = kNowMs + 1000, int32 priority = 0 )
    {
        ServiceAnnouncement notice;
        notice._text     = "notice.key";
        notice._noticeID = noticeID;
        notice._startMs  = startMs;
        notice._endMs    = endMs;
        notice._priority = priority;
        return notice;
    }

    ServerAssignmentRequest makeAssignment( string_view kind = "game", string_view region = "kr" )
    {
        ServerAssignmentRequest request;
        request._kind   = string( kind );
        request._region = string( region );
        return request;
    }

    struct DirectoryServiceFixture
    {
        ManualClock             _clock;
        MemoryEphemeralDatabase _cacheData;
        MemoryEphemeralStore    _cache;
        EphemeralStoreRouter    _router;
        MemoryServiceDatabase   _database;
        MemoryServiceStore      _store;
        ServerRegistration      _registration;
        ServerDirectoryService  _service;

        DirectoryServiceFixture()
            : _clock{ MonotonicMilliseconds{ 0 } }
            , _cacheData{ _clock }
            , _cache{ &_cacheData }
            , _router{}
            , _database{}
            , _store{ &_database }
            , _registration{}
            , _service{}
        {
            _router.initialize( &_cache );
            ServerDirectoryDependencies dependencies;
            dependencies._pStore  = &_store;
            dependencies._pRouter = &_router;
            ServerDirectoryServiceConfig settings;
            settings._listServerKind.push_back( "game" );
            settings._registryRefreshPeriodMs = 1;
            _service.initialize( dependencies, settings );
        }

        ~DirectoryServiceFixture()
        {
            _service.shutdown();
            _registration.shutdown();
            _router.shutdown();
        }

        /** @brief 서비스와 등록을 한 번 돌리고 캐시 · 저장소 답을 끝까지 나눠 줍니다. */
        void tick( int64 nowMs )
        {
            _service.update( UnixMilliseconds{ nowMs } );
            if ( _registration.getStatus()._descriptor._serverID != 0 )
                _registration.update( UnixMilliseconds{ nowMs } );
            pump();
        }

        void pump()
        {
            for ( int32 round = 0; round < 8; ++round )
            {
                const int32 routerWork = _router.pump();
                const int32 storeWork  = _store.pollCompletions();
                if ( routerWork == 0 && storeWork == 0 && _cache.getPendingCount() == 0 )
                    return;
            }
        }

        /** @brief 서버 하나를 올리고 서비스의 서버 목록까지 닿게 합니다. */
        void startServer( ServerState state = ServerState::Open, int32 load = 0 )
        {
            SW_REQUIRE_TRUE( _registration.initialize( &_router, makeDescriptor() ) );
            _registration.setState( state );
            _registration.setLoad( load );
            refreshRegistry();
        }

        void refreshRegistry( int64 nowMs = kNowMs )
        {
            // 색인 → 기록 두 왕복과 읽기 주기를 지나게 시각을 조금씩 민다
            for ( int64 offset = 0; offset < 4; ++offset )
            {
                tick( nowMs + offset );
            }
        }

        /** @brief 지금 시각으로 배정합니다. @p pOutAssignment 가 있으면 배정을 받습니다. */
        ServerDirectoryOutcome assign( AccountID accountID, const ServerAssignmentRequest& request, ServerAssignment* pOutAssignment = nullptr )
        {
            ServerAssignment             assignment;
            const ServerDirectoryOutcome outcome = _service.assignServer( accountID, request, UnixMilliseconds{ kNowMs }, assignment );
            if ( pOutAssignment != nullptr )
                *pOutAssignment = assignment;
            return outcome;
        }

        ServerDirectoryOutcome finishOne()
        {
            pump();
            vector<ServerDirectoryCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            return listCompletion.empty() ? ServerDirectoryResult::Count : listCompletion.back()._result;
        }

        ServerDirectoryOutcome setMaintenance( const MaintenanceWindow& window )
        {
            _service.setMaintenance( window, 7, "memo", UnixMilliseconds{ kNowMs }, 1 );
            return finishOne();
        }

        ServerDirectoryOutcome postNotice( const ServiceAnnouncement& notice )
        {
            _service.postNotice( notice, 7, UnixMilliseconds{ kNowMs }, 1 );
            return finishOne();
        }

        /** @brief 다른 서버가 쓴 것처럼 서비스를 거치지 않고 점검을 넣습니다. */
        void writeMaintenanceDirectly( const MaintenanceWindow& window )
        {
            vector<uint8> bytes;
            SW_REQUIRE_TRUE( ServerDirectoryProtocol::encodeMaintenance( window, bytes ) );
            ServiceTransaction transaction;
            transaction.put( hashed_string{ "sd_maintenance" }, window._scope, bytes, 0 );
            SW_REQUIRE_TRUE( _database.commit( transaction ) == ServiceStoreResult::Ok );
        }
    };
} // namespace

/** @brief [ServerDirectoryServiceTest] 배정: 등록된 서버를 고른다. 모르는 종류 · 틀린 지역 이름은 Invalid, 시작 중인 서버뿐이면 NoServer 이다 */
SW_TEST_CASE( ServerDirectoryServiceTest, AssignPicksRegisteredServer )
{
    DirectoryServiceFixture rig;
    SW_EXPECT_TRUE( rig.assign( kPlayer, makeAssignment() ) == ServerDirectoryResult::NoServer );
    SW_EXPECT_TRUE( rig.assign( kPlayer, makeAssignment( "unknown" ) ).hasError( ServerDirectoryError::kUnknownKind ) );
    SW_EXPECT_TRUE( rig.assign( kPlayer, makeAssignment( "game", "KR!" ) ).hasError( ServerDirectoryError::kUnknownKind ) );

    rig.startServer();
    ServerAssignment assignment;
    SW_EXPECT_FALSE( rig.assign( kPlayer, makeAssignment(), &assignment ).isFailed() );
    SW_EXPECT_TRUE( assignment._result == ServerDirectoryResult::Ok );
    SW_EXPECT_EQUAL( kServerID, assignment._serverID );
    SW_EXPECT_EQUAL( string( "10.0.0.1" ), assignment._address );
    SW_EXPECT_EQUAL( uint16( 7000 ), assignment._port );
}

/** @brief [ServerDirectoryServiceTest] 목록: 찬 비율을 계산하고 오래된 서버는 뺀다. 모르는 종류는 Invalid 이다 */
SW_TEST_CASE( ServerDirectoryServiceTest, ListServersSkipsStale )
{
    DirectoryServiceFixture rig;
    rig.startServer( ServerState::Open, 25 );

    vector<ServerListEntry> listEntry;
    SW_EXPECT_TRUE( rig._service.listServers( "unknown", UnixMilliseconds{ kNowMs }, listEntry ).hasError( ServerDirectoryError::kUnknownKind ) );
    SW_EXPECT_TRUE( rig._service.listServers( "game", UnixMilliseconds{ kNowMs }, listEntry ) == ServerDirectoryResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), listEntry.size() );
    SW_EXPECT_EQUAL( uint8( 25 ), listEntry[0]._fillPercent );
    SW_EXPECT_EQUAL( string( "kr" ), listEntry[0]._region );

    listEntry.clear();
    SW_EXPECT_TRUE( rig._service.listServers( "game", UnixMilliseconds{ kNowMs + 60000 }, listEntry ) == ServerDirectoryResult::Ok );
    SW_EXPECT_TRUE( listEntry.empty() );
}

/** @brief [ServerDirectoryServiceTest] 점검: 쓰기 뒤 다시 읽어 막는다. 끝 시각과 안내 키를 주고, 범위 밖 종류와 기간 밖은 막지 않으며, 지우면 풀린다 */
SW_TEST_CASE( ServerDirectoryServiceTest, MaintenanceBlocksAssignment )
{
    DirectoryServiceFixture rig;
    rig.startServer();
    const UnixMilliseconds nowMs{ kNowMs };

    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    SW_EXPECT_TRUE( rig.assign( kPlayer, makeAssignment() ) == ServerDirectoryResult::Ok ); // 아직 다시 읽기 전이다
    rig.tick( kNowMs );

    ServerAssignment blocked;
    SW_EXPECT_FALSE( rig.assign( kPlayer, makeAssignment(), &blocked ).isFailed() );
    SW_EXPECT_TRUE( blocked._result == ServerDirectoryResult::Maintenance );
    SW_EXPECT_EQUAL( kNowMs + 1000, blocked._maintenanceEndMs );
    SW_EXPECT_EQUAL( string( "maint.key" ), blocked._messageKey );

    MaintenanceWindow window;
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs, &window ) );
    SW_EXPECT_EQUAL( string( "game" ), window._scope );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "game", kPlayer, UnixMilliseconds{ kNowMs + 1000 } ) );

    rig._service.clearMaintenance( "game", 7, "memo", nowMs, 2 );
    SW_REQUIRE_TRUE( rig.finishOne() == ServerDirectoryResult::Ok );
    rig.tick( kNowMs );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
}

/** @brief [ServerDirectoryServiceTest] 허용 계정: 점검 중에도 배정받고, 점검 상태의 서버도 후보다. 다른 계정은 막히며 상태에 허용 목록이 보이지 않는 것은 형식이 보장한다 */
SW_TEST_CASE( ServerDirectoryServiceTest, AllowedAccountPassesMaintenance )
{
    DirectoryServiceFixture rig;
    rig.startServer( ServerState::Maintenance );
    SW_EXPECT_TRUE( rig.assign( kTester, makeAssignment() ) == ServerDirectoryResult::NoServer ); // 점검이 걸리지 않았으면 점검 서버는 후보가 아니다

    MaintenanceWindow window = makeWindow( MaintenanceWindow::kScopeAll );
    window._listAllowedAccount.push_back( kTester );
    SW_REQUIRE_TRUE( rig.setMaintenance( window ) == ServerDirectoryResult::Ok );
    rig.tick( kNowMs );

    SW_EXPECT_TRUE( rig.assign( kTester, makeAssignment() ) == ServerDirectoryResult::Ok );
    SW_EXPECT_TRUE( rig.assign( kPlayer, makeAssignment() ) == ServerDirectoryResult::Maintenance );
}

/** @brief [ServerDirectoryServiceTest] 변경 규칙: 틀린 범위 · 뒤집힌 기간 · 빈 공지 · id 0 · 너무 큰 값은 Invalid 이고 저장소가 그대로다 */
SW_TEST_CASE( ServerDirectoryServiceTest, InvalidChangesTouchNothing )
{
    DirectoryServiceFixture rig;
    const uint64            hashBefore = rig._database.computeContentHash();

    SW_EXPECT_TRUE( rig.setMaintenance( makeWindow( "Bad.Scope" ) ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.setMaintenance( makeWindow( "game", 10, 10 ) ).hasError( OnlineError::kInvalidRequest ) );
    MaintenanceWindow tooMany = makeWindow();
    tooMany._listAllowedAccount.assign( static_cast<size_t>( ServerDirectoryLimit::kMaxAllowedAccountCount ) + 1, kPlayer );
    SW_EXPECT_TRUE( rig.setMaintenance( tooMany ).hasError( OnlineError::kInvalidRequest ) );
    rig._service.clearMaintenance( "Bad.Scope", 7, "memo", UnixMilliseconds{ kNowMs }, 1 );
    SW_EXPECT_TRUE( rig.finishOne().hasError( OnlineError::kInvalidRequest ) );

    SW_EXPECT_TRUE( rig.postNotice( makeNotice( 0 ) ).hasError( OnlineError::kInvalidRequest ) );
    ServiceAnnouncement emptyText = makeNotice();
    emptyText._text.clear();
    SW_EXPECT_TRUE( rig.postNotice( emptyText ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.postNotice( makeNotice( 1, 10, 5 ) ).hasError( OnlineError::kInvalidRequest ) );

    SW_EXPECT_EQUAL( hashBefore, rig._database.computeContentHash() );
}

/** @brief [ServerDirectoryServiceTest] 없는 것을 지우면 NotFound 이고, 있는 공지를 지우면 Ok 이며 쓴 변경마다 감사 기록이 남는다 */
SW_TEST_CASE( ServerDirectoryServiceTest, RemoveAndAudit )
{
    DirectoryServiceFixture rig;
    rig._service.removeNotice( 9, 7, UnixMilliseconds{ kNowMs }, 1 );
    SW_EXPECT_TRUE( rig.finishOne() == ServerDirectoryResult::NotFound );
    rig._service.clearMaintenance( "game", 7, "memo", UnixMilliseconds{ kNowMs }, 2 );
    SW_EXPECT_TRUE( rig.finishOne() == ServerDirectoryResult::NotFound );

    SW_REQUIRE_TRUE( rig.postNotice( makeNotice( 9 ) ) == ServerDirectoryResult::Ok );
    rig._service.removeNotice( 9, 7, UnixMilliseconds{ kNowMs }, 3 );
    SW_EXPECT_TRUE( rig.finishOne() == ServerDirectoryResult::Ok );

    vector<ServiceAuditEntry> listEntry;
    string                    nextCursor;
    SW_REQUIRE_TRUE( ServiceAuditLog::listEntries( rig._database, "sd/", "", 10, listEntry, nextCursor ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( size_t( 2 ), listEntry.size() ); // 찾지 못한 지우기는 기록하지 않는다
}

/** @brief [ServerDirectoryServiceTest] 저장소 실패: 쓰기 실패는 Unavailable 이고 다시 읽지 않는다. 읽기 실패는 있던 값을 둔다 */
SW_TEST_CASE( ServerDirectoryServiceTest, StoreFailuresKeepPreviousState )
{
    DirectoryServiceFixture rig;
    const UnixMilliseconds  nowMs{ kNowMs };
    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    rig.tick( kNowMs );
    SW_REQUIRE_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );

    rig._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.setMaintenance( makeWindow( "chat" ) ).hasError( OnlineError::kUnavailable ) );
    rig.tick( kNowMs );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );

    rig._database.armFault( ServiceStoreFault::RejectRead );
    rig._service.notifyChanged();
    rig.tick( kNowMs );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );
}

/** @brief [ServerDirectoryServiceTest] 손상 레코드: 규칙을 어긴 레코드가 있으면 그 읽기 전체를 버리고 있던 값을 지킨다 */
SW_TEST_CASE( ServerDirectoryServiceTest, BadRecordKeepsPreviousState )
{
    SW_EXPECT_LOG( "ServerDirectoryService.ServerDirectoryMaintenanceRecord" );
    DirectoryServiceFixture rig;
    const UnixMilliseconds  nowMs{ kNowMs };
    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    rig.tick( kNowMs );

    rig.writeMaintenanceDirectly( makeWindow( "chat", 10, 5 ) ); // 기간이 뒤집혔다
    rig._service.notifyChanged();
    rig.tick( kNowMs );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );
}

/** @brief [ServerDirectoryServiceTest] 다시 읽기: 다른 서버의 변경은 알림이나 주기로 들어온다 */
SW_TEST_CASE( ServerDirectoryServiceTest, ReloadOnNotifyAndPeriod )
{
    DirectoryServiceFixture rig;
    const UnixMilliseconds  nowMs{ kNowMs };
    rig.tick( kNowMs );

    rig.writeMaintenanceDirectly( makeWindow( "game" ) );
    rig.tick( kNowMs + 1 );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
    rig._service.notifyChanged();
    rig.tick( kNowMs + 2 );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );

    rig.writeMaintenanceDirectly( makeWindow( "chat" ) );
    rig.tick( kNowMs + 3 );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );
    rig.tick( kNowMs + 2 + 30000 );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );
}

/** @brief [ServerDirectoryServiceTest] 상태: 기간 안의 것만 우선순위 순서로 보인다 */
SW_TEST_CASE( ServerDirectoryServiceTest, StatusShowsActiveByPriority )
{
    DirectoryServiceFixture rig;
    SW_REQUIRE_TRUE( rig.postNotice( makeNotice( 1, kNowMs - 1, 0, 1 ) ) == ServerDirectoryResult::Ok );
    SW_REQUIRE_TRUE( rig.postNotice( makeNotice( 2, kNowMs - 1, 0, 9 ) ) == ServerDirectoryResult::Ok );
    SW_REQUIRE_TRUE( rig.postNotice( makeNotice( 3, kNowMs + 500, 0, 99 ) ) == ServerDirectoryResult::Ok ); // 아직 시작 전이다
    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    rig.tick( kNowMs );

    const ServerDirectoryStatus status = rig._service.makeStatus( UnixMilliseconds{ kNowMs } );
    SW_REQUIRE_EQUAL( size_t( 2 ), status._listNotice.size() );
    SW_EXPECT_EQUAL( uint64( 2 ), status._listNotice[0]._noticeID );
    SW_EXPECT_EQUAL( uint64( 1 ), status._listNotice[1]._noticeID );
    SW_REQUIRE_EQUAL( size_t( 1 ), status._listMaintenance.size() );
    SW_EXPECT_EQUAL( string( "game" ), status._listMaintenance[0]._scope );
}

/** @brief [ServerDirectoryServiceTest] 바뀜 표시: 처음 읽은 빈 상태는 알리지 않고, 변경과 기간 경계에서 한 번씩 켜지며 읽으면 꺼진다 */
SW_TEST_CASE( ServerDirectoryServiceTest, StatusChangeMarksOnVisibleChange )
{
    DirectoryServiceFixture rig;
    rig.tick( kNowMs );
    SW_EXPECT_FALSE( rig._service.takeStatusChange() );

    SW_REQUIRE_TRUE( rig.postNotice( makeNotice( 1, kNowMs + 100, kNowMs + 200 ) ) == ServerDirectoryResult::Ok );
    rig.tick( kNowMs );
    SW_EXPECT_FALSE( rig._service.takeStatusChange() ); // 아직 기간 전이라 보이지 않는다

    rig.tick( kNowMs + 100 );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );
    SW_EXPECT_FALSE( rig._service.takeStatusChange() );
    const uint64 revision = rig._service.getStatusRevision();

    rig.tick( kNowMs + 200 );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );
    SW_EXPECT_EQUAL( revision + 1, rig._service.getStatusRevision() );
}

/** @brief [ServerDirectoryServiceTest] 바뀜 표시: 클라이언트가 받는 칸이면 시각이나 문구 방식만 바뀌어도 알린다 */
SW_TEST_CASE( ServerDirectoryServiceTest, StatusChangeCoversEveryVisibleField )
{
    DirectoryServiceFixture rig;
    SW_REQUIRE_TRUE( rig.postNotice( makeNotice( 1, kNowMs - 1, kNowMs + 1000 ) ) == ServerDirectoryResult::Ok );
    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    rig.tick( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );

    SW_REQUIRE_TRUE( rig.postNotice( makeNotice( 1, kNowMs - 1, kNowMs + 2000 ) ) == ServerDirectoryResult::Ok ); // 끝 시각만 늘렸다
    rig.tick( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );

    SW_REQUIRE_TRUE( rig.postNotice( makeNotice( 1, kNowMs - 2, kNowMs + 2000 ) ) == ServerDirectoryResult::Ok ); // 시작 시각만 바꿨다
    rig.tick( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );

    ServiceAnnouncement literal = makeNotice( 1, kNowMs - 2, kNowMs + 2000 );
    literal._bLiteralText       = SW_TRUE;
    SW_REQUIRE_TRUE( rig.postNotice( literal ) == ServerDirectoryResult::Ok );
    rig.tick( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );

    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow( "game", kNowMs - 2 ) ) == ServerDirectoryResult::Ok ); // 점검 시작 시각만 바꿨다
    rig.tick( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );
}

/** @brief [ServerDirectoryServiceTest] GM 창구: 변경 값이 그대로 저장되고 결과가 창구의 형식으로 바뀐다 */
SW_TEST_CASE( ServerDirectoryServiceTest, ControlInterfaceMapsResults )
{
    DirectoryServiceFixture  rig;
    IServerDirectoryControl& control = rig._service;
    const UnixMilliseconds   nowMs{ kNowMs };

    ServerDirectoryMaintenanceChange maintenance;
    maintenance._scope      = "game";
    maintenance._messageKey = "maint.key";
    maintenance._startMs    = kNowMs - 1;
    control.setMaintenance( maintenance, 7, "memo", nowMs, 11 );
    ServerDirectoryMaintenanceChange badScope;
    badScope._scope = "Bad.Scope";
    control.setMaintenance( badScope, 7, "memo", nowMs, 12 );
    control.removeNotice( 77, 7, nowMs, 13 );
    ServerDirectoryNoticeChange notice;
    notice._text         = "literal";
    notice._noticeID     = 3;
    notice._startMs      = kNowMs - 1;
    notice._priority     = 2;
    notice._bLiteralText = SW_TRUE;
    control.postNotice( notice, 7, nowMs, 14 );
    rig._database.armFault( ServiceStoreFault::RejectCommit );
    control.clearMaintenance( "game", 7, "memo", nowMs, 15 );
    rig.pump();

    vector<ServerDirectoryControlCompletion> listCompletion;
    control.takeControlCompletions( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 5 ), listCompletion.size() );
    const auto resultOf = [&listCompletion]( uint64 requestTag )
    {
        for ( const ServerDirectoryControlCompletion& completion : listCompletion )
        {
            if ( completion._requestTag == requestTag )
                return completion._result;
        }
        return ServerDirectoryControlResult::Unavailable;
    };
    SW_EXPECT_TRUE( resultOf( 11 ) == ServerDirectoryControlResult::Ok );
    SW_EXPECT_TRUE( resultOf( 12 ) == ServerDirectoryControlResult::Invalid );
    SW_EXPECT_TRUE( resultOf( 13 ) == ServerDirectoryControlResult::NotFound );
    SW_EXPECT_TRUE( resultOf( 14 ) == ServerDirectoryControlResult::Ok );
    SW_EXPECT_TRUE( resultOf( 15 ) == ServerDirectoryControlResult::Unavailable );

    rig.tick( kNowMs );
    const ServerDirectoryStatus status = rig._service.makeStatus( nowMs );
    SW_REQUIRE_EQUAL( size_t( 1 ), status._listNotice.size() );
    SW_EXPECT_EQUAL( string( "literal" ), status._listNotice[0]._text );
    SW_EXPECT_EQUAL( 2, status._listNotice[0]._priority );
    SW_EXPECT_EQUAL( SW_TRUE, status._listNotice[0]._bLiteralText );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
}
