// ServerDirectoryServiceTest — 서버 디렉터리 서비스(메모리 저장소 · 캐시): 배정 · 목록, 점검의 막기와 허용 계정, 변경 규칙 · 완료 · 감사 기록, 저장소 실패와 손상 레코드에서 이전 값 지키기,
// 다시 읽기 일정(주기 · 알림 · 쓰기 뒤), 보이는 상태의 바뀜 표시, GM 창구의 결과 변환. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryProtocol.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Server/ServerDirectoryService.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Cache/Server/EphemeralStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryEphemeralStore.h"
#include "GameFramework/Runtime/Online/Directory/Server/ServerRegistration.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceRecordTable.h"
#include "GameFramework/Runtime/Online/Store/ServiceKeyUtil.h"

using namespace sw;

namespace
{
    constexpr int64     kNowMs    = 1700000000000ll;
    constexpr AccountID kPlayer   = 22;
    constexpr AccountID kTester   = 23;
    constexpr uint64    kServerID = 5;
    constexpr AccountID kGM       = 7;
    constexpr AccountID kStranger = 8;

    /** @brief 늘 허락하는 가드입니다(등급 읽기는 Admin 키트의 테스트가 본다). */
    class AllowGuard final : public IServiceWriteGuard
    {
    public:
        ServiceStoreResult validateAllowed( IServiceStoreConnection& /*connection*/ ) const override { return ServiceStoreResult::Ok; }
    };

    /** @brief 운영자가 아닌 계정의 가드입니다(허락할 근거가 없다). */
    class DenyGuard final : public IServiceWriteGuard
    {
    public:
        ServiceStoreResult validateAllowed( IServiceStoreConnection& /*connection*/ ) const override { return ServiceStoreResult::NotFound; }
    };

    ServiceOperator makeGM()
    {
        return ServiceOperator{ kGM, make_shared<const AllowGuard>() };
    }

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

    ServiceAnnouncement makeAnnouncement( uint64 announcementID = 1, int64 startMs = kNowMs - 1, int64 endMs = kNowMs + 1000, int32 priority = 0 )
    {
        ServiceAnnouncement announcement;
        announcement._text           = "notice.key";
        announcement._announcementID = announcementID;
        announcement._startMs        = startMs;
        announcement._endMs          = endMs;
        announcement._priority       = priority;
        return announcement;
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
            settings._reloadPeriodMs          = 30000;
            settings._registryRefreshPeriodMs = 2000;
            settings._staleMs                 = 15000;
            settings._registryRefreshPeriodMs = 1;
            settings._seatReservationTTLMs    = 30000;
            _service.initialize( dependencies, settings );
        }

        ~DirectoryServiceFixture()
        {
            _service.shutdown();
            _registration.shutdown();
            _router.shutdown();
        }

        /** @brief 서비스와 등록을 한 번 돌리고 캐시 · 저장소 답을 끝까지 나눠 줍니다. */
        void update( int64 nowMs )
        {
            _service.update( UnixMilliseconds{ nowMs } );
            if ( _registration.getStatus()._descriptor._serverID != 0 )
                _registration.update( UnixMilliseconds{ nowMs } );
            poll();
        }

        void poll()
        {
            for ( int32 round = 0; round < 8; ++round )
            {
                const int32 routerJob = _router.poll();
                const int32 storeJob  = _store.pollCompletions();
                if ( routerJob == 0 && storeJob == 0 && _cache.getPendingCount() == 0 )
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
                update( nowMs + offset );
            }
        }

        /** @brief 지금 시각으로 배정합니다. @p pOutAssignment 가 있으면 배정을 받습니다. */
        ServerDirectoryServiceResult assign( AccountID accountID, const ServerAssignmentRequest& request, ServerAssignment* pOutAssignment = nullptr )
        {
            ServerAssignment                   assignment;
            const ServerDirectoryServiceResult result = _service.assignServer( accountID, request, UnixMilliseconds{ kNowMs }, assignment );
            if ( pOutAssignment != nullptr )
                *pOutAssignment = assignment;
            return result;
        }

        ServerDirectoryServiceResult finishOne()
        {
            poll();
            vector<ServerDirectoryCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            return listCompletion.empty() ? ServerDirectoryResult::Count : listCompletion.back()._result;
        }

        ServerDirectoryServiceResult setMaintenance( const MaintenanceWindow& window )
        {
            _service.setMaintenance( window, makeGM(), "memo", UnixMilliseconds{ kNowMs }, 1 );
            return finishOne();
        }

        ServerDirectoryServiceResult postAnnouncement( const ServiceAnnouncement& announcement )
        {
            _service.postAnnouncement( announcement, makeGM(), UnixMilliseconds{ kNowMs }, 1 );
            return finishOne();
        }

        /** @brief 다른 서버가 쓴 것처럼 서비스를 거치지 않고 점검을 넣습니다. */
        void writeMaintenanceDirectly( const MaintenanceWindow& window )
        {
            static const ServiceRecordTable<MaintenanceWindow> s_maintenanceTable{ "sd_maintenance" };
            ServiceTransaction                                 transaction;
            transaction.put( s_maintenanceTable, window._scope, window, 0 );
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
    rig.update( kNowMs );

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

    rig._service.clearMaintenance( "game", makeGM(), "memo", nowMs, 2 );
    SW_REQUIRE_TRUE( rig.finishOne() == ServerDirectoryResult::Ok );
    rig.update( kNowMs );
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
    rig.update( kNowMs );

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
    rig._service.clearMaintenance( "Bad.Scope", makeGM(), "memo", UnixMilliseconds{ kNowMs }, 1 );
    SW_EXPECT_TRUE( rig.finishOne().hasError( OnlineError::kInvalidRequest ) );

    SW_EXPECT_TRUE( rig.postAnnouncement( makeAnnouncement( 0 ) ).hasError( OnlineError::kInvalidRequest ) );
    ServiceAnnouncement emptyText = makeAnnouncement();
    emptyText._text.clear();
    SW_EXPECT_TRUE( rig.postAnnouncement( emptyText ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.postAnnouncement( makeAnnouncement( 1, 10, 5 ) ).hasError( OnlineError::kInvalidRequest ) );

    SW_EXPECT_EQUAL( hashBefore, rig._database.computeContentHash() );
}

/** @brief [ServerDirectoryServiceTest] 없는 것을 지우면 NotFound 이고, 있는 공지를 지우면 Ok 이며 쓴 변경마다 감사 기록이 남는다 */
SW_TEST_CASE( ServerDirectoryServiceTest, RemoveAndAudit )
{
    DirectoryServiceFixture rig;
    rig._service.removeAnnouncement( 9, makeGM(), UnixMilliseconds{ kNowMs }, 1 );
    SW_EXPECT_TRUE( rig.finishOne() == ServerDirectoryResult::NotFound );
    rig._service.clearMaintenance( "game", makeGM(), "memo", UnixMilliseconds{ kNowMs }, 2 );
    SW_EXPECT_TRUE( rig.finishOne() == ServerDirectoryResult::NotFound );

    SW_REQUIRE_TRUE( rig.postAnnouncement( makeAnnouncement( 9 ) ) == ServerDirectoryResult::Ok );
    rig._service.removeAnnouncement( 9, makeGM(), UnixMilliseconds{ kNowMs }, 3 );
    SW_EXPECT_TRUE( rig.finishOne() == ServerDirectoryResult::Ok );

    vector<ServiceAuditEntry> listEntry;
    string                    nextCursor;
    SW_REQUIRE_TRUE( ServiceAuditLog::listEntries( rig._database, "sd/", "", 10, listEntry, nextCursor ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( size_t( 2 ), listEntry.size() ); // 찾지 못한 지우기는 기록하지 않는다
}

/** @brief [ServerDirectoryServiceTest] 운영자 권한: 가드가 거절하거나 없는 변경은 Forbidden 이고 표를 건드리지 않으며 거절 감사 줄(`<동작>.denied`)을 남긴다. 운영자는 통과한다 */
SW_TEST_CASE( ServerDirectoryServiceTest, OperatorGuardRefusesNonOperators )
{
    SW_EXPECT_LOG( "ServiceAuditLog.WriteWithoutGuard" ); // 권한 확인 없는 운영자 쓰기는 감사 기록이 거절하며 오류로 남긴다
    DirectoryServiceFixture rig;
    const UnixMilliseconds  nowMs{ kNowMs };
    const ServiceOperator   stranger{ kStranger, make_shared<const DenyGuard>() };
    const ServiceOperator   withoutGuard{ kStranger, nullptr };

    rig._service.setMaintenance( makeWindow(), stranger, "memo", nowMs, 1 );
    SW_EXPECT_TRUE( rig.finishOne().hasError( OnlineError::kForbidden ) );
    rig._service.postAnnouncement( makeAnnouncement( 4 ), withoutGuard, nowMs, 2 );
    SW_EXPECT_TRUE( rig.finishOne().hasError( OnlineError::kForbidden ) );

    IServerDirectoryControl& control = rig._service;
    control.clearMaintenance( "game", stranger, "memo", nowMs, 3 );
    control.removeAnnouncement( 4, stranger, nowMs, 4 );
    rig.poll();
    vector<ServerDirectoryControlCompletion> listCompletion;
    control.takeControlCompletions( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 2 ), listCompletion.size() );
    for ( const ServerDirectoryControlCompletion& completion : listCompletion )
    {
        SW_EXPECT_TRUE( completion._result == ServerDirectoryControlResult::Forbidden );
    }

    ServiceRecord record;
    SW_EXPECT_TRUE( rig._database.readRecord( hashed_string( "sd_maintenance" ), "game", record ) == ServiceStoreResult::NotFound );
    SW_EXPECT_TRUE( rig._database.readRecord( hashed_string( "sd_notice" ), ServiceKeyUtil::makeHex64( 4 ), record ) == ServiceStoreResult::NotFound );

    vector<ServiceAuditEntry> listEntry;
    string                    nextCursor;
    SW_REQUIRE_TRUE( ServiceAuditLog::listEntries( rig._database, "sd/", "", 10, listEntry, nextCursor ) == ServiceStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 4 ), listEntry.size() );
    const auto countAction = [&listEntry]( string_view action )
    {
        int32 count = 0;
        for ( const ServiceAuditEntry& entry : listEntry )
        {
            if ( entry._action == action )
                ++count;
        }
        return count;
    };
    SW_EXPECT_EQUAL( 1, countAction( "sd.maintenance.set.denied" ) );
    SW_EXPECT_EQUAL( 1, countAction( "sd.notice.post.denied" ) );
    SW_EXPECT_EQUAL( 1, countAction( "sd.maintenance.clear.denied" ) );
    SW_EXPECT_EQUAL( 1, countAction( "sd.notice.remove.denied" ) );
    for ( const ServiceAuditEntry& entry : listEntry )
    {
        SW_EXPECT_EQUAL( ServiceAuditLog::makeOperatorActor( kStranger ), entry._actor );
    }

    // 운영자는 통과하고 효과 줄이 남는다
    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    rig.update( kNowMs );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
    listEntry.clear();
    SW_REQUIRE_TRUE( ServiceAuditLog::listEntries( rig._database, "sd/", "", 10, listEntry, nextCursor ) == ServiceStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 5 ), listEntry.size() );
    SW_EXPECT_EQUAL( 1, countAction( "sd.maintenance.set" ) );
}

/** @brief [ServerDirectoryServiceTest] 저장소 실패: 쓰기 실패는 Unavailable 이고 다시 읽지 않는다. 읽기 실패는 있던 값을 둔다 */
SW_TEST_CASE( ServerDirectoryServiceTest, StoreFailuresKeepPreviousState )
{
    DirectoryServiceFixture rig;
    const UnixMilliseconds  nowMs{ kNowMs };
    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    rig.update( kNowMs );
    SW_REQUIRE_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );

    rig._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.setMaintenance( makeWindow( "chat" ) ).hasError( OnlineError::kUnavailable ) );
    rig.update( kNowMs );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );

    rig._database.armFault( ServiceStoreFault::RejectRead );
    rig._service.notifyChanged();
    rig.update( kNowMs );
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
    rig.update( kNowMs );

    rig.writeMaintenanceDirectly( makeWindow( "chat", 10, 5 ) ); // 기간이 뒤집혔다
    rig._service.notifyChanged();
    rig.update( kNowMs );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );
}

/** @brief [ServerDirectoryServiceTest] 다시 읽기: 다른 서버의 변경은 알림이나 주기로 들어온다 */
SW_TEST_CASE( ServerDirectoryServiceTest, ReloadOnNotifyAndPeriod )
{
    DirectoryServiceFixture rig;
    const UnixMilliseconds  nowMs{ kNowMs };
    rig.update( kNowMs );

    rig.writeMaintenanceDirectly( makeWindow( "game" ) );
    rig.update( kNowMs + 1 );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
    rig._service.notifyChanged();
    rig.update( kNowMs + 2 );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );

    rig.writeMaintenanceDirectly( makeWindow( "chat" ) );
    rig.update( kNowMs + 3 );
    SW_EXPECT_FALSE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );
    rig.update( kNowMs + 2 + 30000 );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "chat", kPlayer, nowMs ) );
}

/** @brief [ServerDirectoryServiceTest] 상태: 기간 안의 것만 우선순위 순서로 보인다 */
SW_TEST_CASE( ServerDirectoryServiceTest, StatusShowsActiveByPriority )
{
    DirectoryServiceFixture rig;
    SW_REQUIRE_TRUE( rig.postAnnouncement( makeAnnouncement( 1, kNowMs - 1, 0, 1 ) ) == ServerDirectoryResult::Ok );
    SW_REQUIRE_TRUE( rig.postAnnouncement( makeAnnouncement( 2, kNowMs - 1, 0, 9 ) ) == ServerDirectoryResult::Ok );
    SW_REQUIRE_TRUE( rig.postAnnouncement( makeAnnouncement( 3, kNowMs + 500, 0, 99 ) ) == ServerDirectoryResult::Ok ); // 아직 시작 전이다
    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    rig.update( kNowMs );

    const ServerDirectoryStatus status = rig._service.makeStatus( UnixMilliseconds{ kNowMs } );
    SW_REQUIRE_EQUAL( size_t( 2 ), status._listAnnouncement.size() );
    SW_EXPECT_EQUAL( uint64( 2 ), status._listAnnouncement[0]._announcementID );
    SW_EXPECT_EQUAL( uint64( 1 ), status._listAnnouncement[1]._announcementID );
    SW_REQUIRE_EQUAL( size_t( 1 ), status._listMaintenance.size() );
    SW_EXPECT_EQUAL( string( "game" ), status._listMaintenance[0]._scope );
}

/** @brief [ServerDirectoryServiceTest] 바뀜 표시: 처음 읽은 빈 상태는 알리지 않고, 변경과 기간 경계에서 한 번씩 켜지며 읽으면 꺼진다 */
SW_TEST_CASE( ServerDirectoryServiceTest, StatusChangeMarksOnVisibleChange )
{
    DirectoryServiceFixture rig;
    rig.update( kNowMs );
    SW_EXPECT_FALSE( rig._service.takeStatusChange() );

    SW_REQUIRE_TRUE( rig.postAnnouncement( makeAnnouncement( 1, kNowMs + 100, kNowMs + 200 ) ) == ServerDirectoryResult::Ok );
    rig.update( kNowMs );
    SW_EXPECT_FALSE( rig._service.takeStatusChange() ); // 아직 기간 전이라 보이지 않는다

    rig.update( kNowMs + 100 );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );
    SW_EXPECT_FALSE( rig._service.takeStatusChange() );
    const uint64 revision = rig._service.getStatusRevision();

    rig.update( kNowMs + 200 );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );
    SW_EXPECT_EQUAL( revision + 1, rig._service.getStatusRevision() );
}

/** @brief [ServerDirectoryServiceTest] 바뀜 표시: 클라이언트가 받는 칸이면 시각이나 문구 방식만 바뀌어도 알린다 */
SW_TEST_CASE( ServerDirectoryServiceTest, StatusChangeCoversEveryVisibleField )
{
    DirectoryServiceFixture rig;
    SW_REQUIRE_TRUE( rig.postAnnouncement( makeAnnouncement( 1, kNowMs - 1, kNowMs + 1000 ) ) == ServerDirectoryResult::Ok );
    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow() ) == ServerDirectoryResult::Ok );
    rig.update( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );

    SW_REQUIRE_TRUE( rig.postAnnouncement( makeAnnouncement( 1, kNowMs - 1, kNowMs + 2000 ) ) == ServerDirectoryResult::Ok ); // 끝 시각만 늘렸다
    rig.update( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );

    SW_REQUIRE_TRUE( rig.postAnnouncement( makeAnnouncement( 1, kNowMs - 2, kNowMs + 2000 ) ) == ServerDirectoryResult::Ok ); // 시작 시각만 바꿨다
    rig.update( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );

    ServiceAnnouncement literal = makeAnnouncement( 1, kNowMs - 2, kNowMs + 2000 );
    literal._bLiteralText       = SW_TRUE;
    SW_REQUIRE_TRUE( rig.postAnnouncement( literal ) == ServerDirectoryResult::Ok );
    rig.update( kNowMs );
    SW_EXPECT_TRUE( rig._service.takeStatusChange() );

    SW_REQUIRE_TRUE( rig.setMaintenance( makeWindow( "game", kNowMs - 2 ) ) == ServerDirectoryResult::Ok ); // 점검 시작 시각만 바꿨다
    rig.update( kNowMs );
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
    control.setMaintenance( maintenance, makeGM(), "memo", nowMs, 11 );
    ServerDirectoryMaintenanceChange badScope;
    badScope._scope = "Bad.Scope";
    control.setMaintenance( badScope, makeGM(), "memo", nowMs, 12 );
    control.removeAnnouncement( 77, makeGM(), nowMs, 13 );
    ServerDirectoryAnnouncementChange announcement;
    announcement._text           = "literal";
    announcement._announcementID = 3;
    announcement._startMs        = kNowMs - 1;
    announcement._priority       = 2;
    announcement._bLiteralText   = SW_TRUE;
    control.postAnnouncement( announcement, makeGM(), nowMs, 14 );
    rig._database.armFault( ServiceStoreFault::RejectCommit );
    control.clearMaintenance( "game", makeGM(), "memo", nowMs, 15 );
    rig.poll();

    vector<ServerDirectoryControlCompletion> listCompletion;
    control.takeControlCompletions( listCompletion );
    SW_REQUIRE_EQUAL( size_t( 5 ), listCompletion.size() );
    const auto getResultOf = [&listCompletion]( uint64 requestTag )
    {
        for ( const ServerDirectoryControlCompletion& completion : listCompletion )
        {
            if ( completion._requestTag == requestTag )
                return completion._result;
        }
        return ServerDirectoryControlResult::Unavailable;
    };
    SW_EXPECT_TRUE( getResultOf( 11 ) == ServerDirectoryControlResult::Ok );
    SW_EXPECT_TRUE( getResultOf( 12 ) == ServerDirectoryControlResult::Invalid );
    SW_EXPECT_TRUE( getResultOf( 13 ) == ServerDirectoryControlResult::NotFound );
    SW_EXPECT_TRUE( getResultOf( 14 ) == ServerDirectoryControlResult::Ok );
    SW_EXPECT_TRUE( getResultOf( 15 ) == ServerDirectoryControlResult::Unavailable );

    rig.update( kNowMs );
    const ServerDirectoryStatus status = rig._service.makeStatus( nowMs );
    SW_REQUIRE_EQUAL( size_t( 1 ), status._listAnnouncement.size() );
    SW_EXPECT_EQUAL( string( "literal" ), status._listAnnouncement[0]._text );
    SW_EXPECT_EQUAL( 2, status._listAnnouncement[0]._priority );
    SW_EXPECT_EQUAL( SW_TRUE, status._listAnnouncement[0]._bLiteralText );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
}

/** @brief [ServerDirectoryServiceTest] 점검 표는 첫 쪽(64 행) 뒤까지 다 읽는다 — 앞 범위의 점검이 많아도 뒤 범위(game)의 점검이 빠져 열리지 않는다(감사 O-18) */
SW_TEST_CASE( ServerDirectoryServiceTest, MaintenanceBeyondTheFirstPageStillBlocks )
{
    DirectoryServiceFixture rig;
    const UnixMilliseconds  nowMs{ kNowMs };
    for ( int32 index = 0; index < 70; ++index )
    {
        fixed_string<constant::buffer::kMax16> scope;
        formatstring( scope.data(), scope.capacity(), "aa%#", 100 + index );
        rig.writeMaintenanceDirectly( makeWindow( scope.view() ) ); // 키 순서로 game 앞에 온다
    }
    rig.writeMaintenanceDirectly( makeWindow( "game" ) );
    rig._service.notifyChanged();
    rig.update( kNowMs );
    SW_EXPECT_TRUE( rig._service.isBlockedByMaintenance( "game", kPlayer, nowMs ) );
}
