// AdminServiceTest — GM 서비스(메모리 저장소 · 가짜 포트): 호출 입구, 조회의 접속 여부 · 이름 색인, 지급의 잔액 알림, 제재의 세션 끊기(재시도는 다시 끊지 않는다),
// 점검 · 공지 · 라이브 이벤트 명령 넘기기와 완료 되받기, 첫 관리자 넣기, 종료 회수. 저장 규칙은 AdminStoreLogicTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Admin/Runtime/AdminProtocol.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Config/LiveEventControl.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Directory/ServerDirectoryControl.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Ledger/IBalanceChangedListener.h"
#include "GameFramework/Runtime/Online/Identity/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr AccountID kSuper    = 6;
    constexpr AccountID kOperator = 7;
    constexpr AccountID kViewer   = 8;
    constexpr AccountID kPlayer   = 99;
    constexpr int64     kNowMs    = 1700000000000ll;

    /** @brief 세션 끊기 호출을 셉니다. */
    class FakeSessionControl final : public IAccountSessionControl
    {
    public:
        vector<AccountID> _listRevoked{};

        void revokeAccountSessions( AccountID accountID, string_view /*reasonCode*/, UnixMilliseconds /*nowMs*/ ) override { _listRevoked.push_back( accountID ); }
    };

    /** @brief 잔액 알림 호출을 셉니다. */
    class FakeBalanceNotice final : public IBalanceChangedListener
    {
    public:
        vector<AccountID> _listAccount{};

        void notifyBalancesChanged( AccountID accountID, const vector<LedgerBalance>& /*listBalance*/ ) override { _listAccount.push_back( accountID ); }
    };

    /** @brief kPlayer 만 접속해 있는 디렉터리입니다. */
    class FakeDirectory final : public IAccountDirectory
    {
    public:
        bool findIdentity( AccountID /*accountID*/, AccountIdentity& /*outIdentity*/ ) const override { return false; }
        bool findIdentityByDisplayName( string_view /*displayName*/, AccountIdentity& /*outIdentity*/ ) const override { return false; }
        bool isAccountOnline( AccountID accountID ) const override { return accountID == kPlayer; }
    };

    /** @brief "player" 와 kPlayer 만 아는 이름 색인입니다. */
    class FakeNameIndex final : public IAccountNameIndex
    {
    public:
        ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& /*connection*/, string_view displayName, AccountIdentity& outIdentity ) const override
        {
            if ( displayName != "player" )
                return ServiceStoreResult::NotFound;
            outIdentity._accountID = kPlayer;
            return ServiceStoreResult::Ok;
        }

        ServiceStoreResult readIdentity( IServiceStoreConnection& /*connection*/, AccountID accountID, AccountIdentity& outIdentity ) const override
        {
            if ( accountID != kPlayer )
                return ServiceStoreResult::NotFound;
            outIdentity._accountID = accountID;
            return ServiceStoreResult::Ok;
        }
    };

    /** @brief 넘긴 명령을 적어 두고, 시험이 정한 결과로 끝냅니다. */
    class FakeDirectoryControl final : public IServerDirectoryControl
    {
    public:
        vector<ServerDirectoryControlCompletion> _listCompletion{};
        vector<uint64>                           _listTag{};
        ServerDirectoryMaintenanceChange         _maintenance{};
        ServerDirectoryNoticeChange              _notice{};
        string                                   _clearedScope{};
        uint64                                   _removedNoticeID{ 0 };

        void setMaintenance( const ServerDirectoryMaintenanceChange& change, AccountID /*actorID*/, string_view /*memo*/, UnixMilliseconds /*nowMs*/, uint64 requestTag ) override
        {
            _maintenance = change;
            _listTag.push_back( requestTag );
        }

        void clearMaintenance( string_view scope, AccountID /*actorID*/, string_view /*memo*/, UnixMilliseconds /*nowMs*/, uint64 requestTag ) override
        {
            _clearedScope = string( scope );
            _listTag.push_back( requestTag );
        }

        void postNotice( const ServerDirectoryNoticeChange& change, AccountID /*actorID*/, UnixMilliseconds /*nowMs*/, uint64 requestTag ) override
        {
            _notice = change;
            _listTag.push_back( requestTag );
        }

        void removeNotice( uint64 noticeID, AccountID /*actorID*/, UnixMilliseconds /*nowMs*/, uint64 requestTag ) override
        {
            _removedNoticeID = noticeID;
            _listTag.push_back( requestTag );
        }

        void takeControlCompletions( vector<ServerDirectoryControlCompletion>& outListCompletion ) override
        {
            for ( const ServerDirectoryControlCompletion& completion : _listCompletion )
            {
                outListCompletion.push_back( completion );
            }
            _listCompletion.clear();
        }

        void finishLast( ServerDirectoryControlResult result ) { _listCompletion.push_back( ServerDirectoryControlCompletion{ _listTag.back(), result } ); }
    };

    /** @brief 라이브 이벤트 명령을 적어 두고, 시험이 정한 결과로 끝냅니다. */
    class FakeLiveEventControl final : public ILiveEventControl
    {
    public:
        vector<LiveEventControlCompletion> _listCompletion{};
        vector<uint64>                     _listTag{};
        vector<string>                     _listEventJSON{};
        string                             _putJSON{};
        string                             _removedEventID{};

        void putEventJSON( string_view definitionJSON, AccountID /*actorID*/, string_view /*memo*/, UnixMilliseconds /*nowMs*/, uint64 requestTag ) override
        {
            _putJSON = string( definitionJSON );
            _listTag.push_back( requestTag );
        }

        void removeEvent( string_view eventID, AccountID /*actorID*/, string_view /*memo*/, UnixMilliseconds /*nowMs*/, uint64 requestTag ) override
        {
            _removedEventID = string( eventID );
            _listTag.push_back( requestTag );
        }

        void collectEventJSON( vector<string>& outListJSON ) const override { outListJSON = _listEventJSON; }

        void takeControlCompletions( vector<LiveEventControlCompletion>& outListCompletion ) override
        {
            for ( const LiveEventControlCompletion& completion : _listCompletion )
            {
                outListCompletion.push_back( completion );
            }
            _listCompletion.clear();
        }

        void finishLast( LiveEventControlResult result ) { _listCompletion.push_back( LiveEventControlCompletion{ _listTag.back(), result } ); }
    };

    struct AdminReplySink
    {
        vector<OnlineResult<AdminReply>> _listReply{};

        void onReply( const OnlineResult<AdminReply>& reply ) { _listReply.push_back( reply ); }
    };

    /** @brief 답 하나입니다. `_outcome` 이 결과이고(공통 범주면 그 오류), 업무 결과는 그 안의 `_result` 다 — 실패한 답의 `_result` 기본값(Ok)을 결과로 읽지 않게 한다. */
    struct AdminTestReply : AdminReply
    {
        AdminOutcome _outcome{};
    };

    struct AdminFixture
    {
        MemoryServiceDatabase _database;
        MemoryServiceStore    _store;
        FakeSessionControl    _sessions;
        FakeBalanceNotice     _balances;
        FakeDirectory         _directory;
        FakeNameIndex         _nameIndex;
        FakeDirectoryControl  _directoryControl;
        FakeLiveEventControl  _liveControl;
        AdminReplySink        _sink;
        AdminService          _service;
        uint64                _nextKey{ 1 };

        explicit AdminFixture( bool bPorts = true )
            : _database{}
            , _store{ &_database }
            , _sessions{}
            , _balances{}
            , _directory{}
            , _nameIndex{}
            , _directoryControl{}
            , _liveControl{}
            , _sink{}
            , _service{}
        {
            SW_EXPECT_TRUE( AdminStoreLogic::seedRole( _database, kSuper, AdminRole::Super, kNowMs ) == AdminResult::Ok );
            SW_EXPECT_TRUE( AdminStoreLogic::seedRole( _database, kOperator, AdminRole::Operator, kNowMs ) == AdminResult::Ok );
            SW_EXPECT_TRUE( AdminStoreLogic::seedRole( _database, kViewer, AdminRole::Viewer, kNowMs ) == AdminResult::Ok );
            AdminServiceConfig settings;
            if ( bPorts )
            {
                settings._pDirectory        = &_directory;
                settings._pNameIndex        = &_nameIndex;
                settings._pSessionControl   = &_sessions;
                settings._pDirectoryControl = &_directoryControl;
                settings._pBalanceNotice    = &_balances;
                settings._pLiveEventControl = &_liveControl;
            }
            SW_EXPECT_TRUE( _service.initialize( &_store, settings ) );
        }

        ~AdminFixture() { _service.shutdown(); }

        void send( AccountID adminID, uint16 method, const AdminRequest& request, uint64 keyLow = 0 )
        {
            const NetIdempotencyKey key{ 1, keyLow != 0 ? keyLow : _nextKey++ };
            _service.submitCall( adminID, method, request, key, UnixMilliseconds{ kNowMs }, AdminService::ReplyDelegate::create<&AdminReplySink::onReply>( &_sink ) );
            while ( _store.pollCompletions() > 0 )
            {
            }
        }

        /** @brief 응답 하나를 꺼냅니다. 없으면 Ok 도 공통 범주도 아닌 `kInternal` 로 알립니다. */
        AdminTestReply take()
        {
            SW_EXPECT_EQUAL( size_t( 1 ), _sink._listReply.size() );
            AdminTestReply reply;
            reply._outcome = AdminOutcome::fail( OnlineError::kInternal );
            if ( _sink._listReply.empty() == false )
            {
                const OnlineResult<AdminReply>& last = _sink._listReply.back();
                static_cast<AdminReply&>( reply )    = last._reply;
                reply._outcome                       = last._errorCode != OnlineError::kOk ? AdminOutcome::fail( last._errorCode ) : AdminOutcome{ last._reply._result };
            }
            _sink._listReply.clear();
            return reply;
        }

        AdminTestReply call( AccountID adminID, uint16 method, const AdminRequest& request, uint64 keyLow = 0 )
        {
            send( adminID, method, request, keyLow );
            return take();
        }

        void tick() { _service.update( UnixMilliseconds{ kNowMs } ); }
    };

    AdminRequest makeRequest()
    {
        AdminRequest request;
        request._memo = "ticket-1";
        return request;
    }

    AdminRequest makeSanction( ServiceSanctionKind kind, int64 untilMs )
    {
        AdminRequest request  = makeRequest();
        request._accountID    = kPlayer;
        request._sanctionKind = kind;
        request._untilMs      = untilMs;
        return request;
    }
} // namespace

/** @brief [AdminServiceTest] 입구: 로그인 전은 NotSignedIn, 모르는 명령은 Forbidden, 저장소 없이 시작하면 서지 않고 Unavailable 이다 */
SW_TEST_CASE( AdminServiceTest, EntryRules )
{
    SW_EXPECT_LOG( "AdminService.AdminServiceNeedsStore" );
    AdminFixture rig;
    SW_EXPECT_TRUE( rig.call( kInvalidAccountID, AdminMethod::LookupAccount::kID, makeRequest() )._outcome.hasError( OnlineError::kUnauthenticated ) );
    SW_EXPECT_TRUE( rig.call( kSuper, 9999, makeRequest() )._outcome.hasError( OnlineError::kForbidden ) );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCallCount() );

    AdminService   unstarted;
    AdminReplySink sink;
    SW_EXPECT_FALSE( unstarted.initialize( nullptr, AdminServiceConfig{} ) );
    unstarted.submitCall( kSuper, AdminMethod::LookupAccount::kID, makeRequest(), NetIdempotencyKey{ 1, 1 }, UnixMilliseconds{ kNowMs }, AdminService::ReplyDelegate::create<&AdminReplySink::onReply>( &sink ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), sink._listReply.size() );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, sink._listReply[0]._errorCode );
    unstarted.shutdown();
}

/** @brief [AdminServiceTest] 조회: id 나 이름으로 찾고 접속 여부를 이 프로세스의 디렉터리로 채운다. 없는 계정과 색인 없는 이름 조회는 UnknownAccount 이다 */
SW_TEST_CASE( AdminServiceTest, LookupFillsOnlineAndUsesNameIndex )
{
    AdminFixture rig;

    AdminRequest byName  = makeRequest();
    byName._displayName  = "player";
    AdminTestReply reply = rig.call( kViewer, AdminMethod::LookupAccount::kID, byName );
    SW_EXPECT_TRUE( reply._outcome == AdminResult::Ok );
    SW_EXPECT_EQUAL( kPlayer, reply._identity._accountID );
    SW_EXPECT_EQUAL( SW_TRUE, reply._bOnline );

    AdminRequest byID = makeRequest();
    byID._accountID   = kOperator;
    SW_EXPECT_TRUE( rig.call( kViewer, AdminMethod::LookupAccount::kID, byID )._outcome == AdminResult::UnknownAccount ); // 색인이 모르는 id 이다

    AdminRequest unknownName = makeRequest();
    unknownName._displayName = "nobody";
    SW_EXPECT_TRUE( rig.call( kViewer, AdminMethod::LookupAccount::kID, unknownName )._outcome == AdminResult::UnknownAccount );

    AdminFixture noPorts( false );
    SW_EXPECT_TRUE( noPorts.call( kViewer, AdminMethod::LookupAccount::kID, byName )._outcome == AdminResult::UnknownAccount );
    AdminRequest playerID = makeRequest();
    playerID._accountID   = kPlayer;
    reply                 = noPorts.call( kViewer, AdminMethod::LookupAccount::kID, playerID );
    SW_EXPECT_TRUE( reply._outcome == AdminResult::Ok );
    SW_EXPECT_EQUAL( SW_FALSE, reply._bOnline );
}

/** @brief [AdminServiceTest] 지급: 바뀐 잔액을 그 계정에 알린다. 같은 키의 재시도와 실패는 알리지 않는다 */
SW_TEST_CASE( AdminServiceTest, AdjustNotifiesBalanceOnce )
{
    AdminFixture rig;
    AdminRequest request = makeRequest();
    request._accountID   = kPlayer;
    request._assetID     = "cur.gold";
    request._amount      = 100;

    SW_EXPECT_TRUE( rig.call( kOperator, AdminMethod::AdjustAsset::kID, request, 50 )._outcome == AdminResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._balances._listAccount.size() );
    SW_EXPECT_EQUAL( kPlayer, rig._balances._listAccount[0] );

    const AdminTestReply replay = rig.call( kOperator, AdminMethod::AdjustAsset::kID, request, 50 );
    SW_EXPECT_TRUE( replay._outcome == AdminResult::Ok );
    SW_EXPECT_EQUAL( SW_TRUE, replay._bReplayed );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._balances._listAccount.size() );

    SW_EXPECT_TRUE( rig.call( kViewer, AdminMethod::AdjustAsset::kID, request )._outcome.hasError( OnlineError::kForbidden ) );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._balances._listAccount.size() );
}

/** @brief [AdminServiceTest] 제재: 정지가 새로 걸리면 세션을 끊고, 재시도 · 채팅 금지 · 해제는 끊지 않는다 */
SW_TEST_CASE( AdminServiceTest, SuspendRevokesSessionsOnce )
{
    AdminFixture       rig;
    const AdminRequest suspend = makeSanction( ServiceSanctionKind::Suspend, kNowMs + 1000 );
    SW_EXPECT_TRUE( rig.call( kOperator, AdminMethod::SetSanction::kID, suspend, 60 )._outcome == AdminResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._sessions._listRevoked.size() );
    SW_EXPECT_EQUAL( kPlayer, rig._sessions._listRevoked[0] );

    SW_EXPECT_TRUE( rig.call( kOperator, AdminMethod::SetSanction::kID, suspend, 60 )._outcome == AdminResult::Ok );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._sessions._listRevoked.size() );

    SW_EXPECT_TRUE( rig.call( kOperator, AdminMethod::SetSanction::kID, makeSanction( ServiceSanctionKind::ChatMute, kNowMs + 1000 ) )._outcome == AdminResult::Ok );
    SW_EXPECT_TRUE( rig.call( kOperator, AdminMethod::SetSanction::kID, makeSanction( ServiceSanctionKind::Suspend, 0 ) )._outcome == AdminResult::Ok );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._sessions._listRevoked.size() );
}

/** @brief [AdminServiceTest] 영구 정지: 요청의 시각이 이미 지났어도 영구로 저장되므로 세션을 끊는다 */
SW_TEST_CASE( AdminServiceTest, BanRevokesEvenWithPastTime )
{
    AdminFixture rig;
    SW_EXPECT_TRUE( rig.call( kOperator, AdminMethod::SetSanction::kID, makeSanction( ServiceSanctionKind::Ban, 1 ) )._outcome.hasError( OnlineError::kForbidden ) ); // 영구 정지는 Super 몫이다
    SW_EXPECT_TRUE( rig._sessions._listRevoked.empty() );

    const AdminTestReply reply = rig.call( kSuper, AdminMethod::SetSanction::kID, makeSanction( ServiceSanctionKind::Ban, 1 ) );
    SW_EXPECT_TRUE( reply._outcome == AdminResult::Ok );
    SW_EXPECT_EQUAL( ServiceSanctionState::kPermanentMs, reply._sanction._arrUntilMs[static_cast<int32>( ServiceSanctionKind::Ban )] );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._sessions._listRevoked.size() );
}

/** @brief [AdminServiceTest] 점검 · 공지 명령: 등급을 확인한 뒤 넘기고, 응답은 넘긴 키트의 완료가 온 뒤에 간다. 값과 결과가 그대로 전해진다 */
SW_TEST_CASE( AdminServiceTest, DirectoryCommandsForwardAndFinishOnCompletion )
{
    AdminFixture rig;

    AdminRequest maintenance = makeRequest();
    maintenance._scope       = "game";
    maintenance._messageKey  = "maint.key";
    maintenance._startMs     = kNowMs;
    maintenance._endMs       = kNowMs + 10;
    maintenance._listAccountID.push_back( kPlayer );

    SW_EXPECT_TRUE( rig.call( kViewer, AdminMethod::SetMaintenance::kID, maintenance )._outcome.hasError( OnlineError::kForbidden ) );
    SW_EXPECT_TRUE( rig._directoryControl._listTag.empty() );

    rig.send( kOperator, AdminMethod::SetMaintenance::kID, maintenance );
    SW_EXPECT_TRUE( rig._sink._listReply.empty() ); // 아직 끝나지 않았다
    SW_EXPECT_EQUAL( 1, rig._service.getPendingCallCount() );
    SW_EXPECT_EQUAL( string( "game" ), rig._directoryControl._maintenance._scope );
    SW_EXPECT_EQUAL( string( "maint.key" ), rig._directoryControl._maintenance._messageKey );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._directoryControl._maintenance._listAllowedAccount.size() );
    rig._directoryControl.finishLast( ServerDirectoryControlResult::Ok );
    rig.tick();
    SW_EXPECT_TRUE( rig.take()._outcome == AdminResult::Ok );

    AdminRequest notice  = makeRequest();
    notice._noticeID     = 5;
    notice._body         = "hello";
    notice._priority     = 3;
    notice._bLiteralText = SW_TRUE;
    rig.send( kOperator, AdminMethod::PostNotice::kID, notice );
    SW_EXPECT_EQUAL( string( "hello" ), rig._directoryControl._notice._text );
    SW_EXPECT_EQUAL( 3, rig._directoryControl._notice._priority );
    SW_EXPECT_EQUAL( SW_TRUE, rig._directoryControl._notice._bLiteralText );
    rig._directoryControl.finishLast( ServerDirectoryControlResult::Invalid );
    rig.tick();
    SW_EXPECT_TRUE( rig.take()._outcome.hasError( OnlineError::kInvalidRequest ) );

    rig.send( kOperator, AdminMethod::RemoveNotice::kID, notice );
    SW_EXPECT_EQUAL( uint64( 5 ), rig._directoryControl._removedNoticeID );
    rig._directoryControl.finishLast( ServerDirectoryControlResult::NotFound );
    rig.tick();
    SW_EXPECT_TRUE( rig.take()._outcome == AdminResult::NotFound );

    rig.send( kOperator, AdminMethod::ClearMaintenance::kID, maintenance );
    SW_EXPECT_EQUAL( string( "game" ), rig._directoryControl._clearedScope );
    rig._directoryControl.finishLast( ServerDirectoryControlResult::Unavailable );
    rig.tick();
    SW_EXPECT_TRUE( rig.take()._outcome.hasError( OnlineError::kUnavailable ) );
}

/** @brief [AdminServiceTest] 라이브 이벤트 명령: 목록은 바로 답하되 너무 큰 정의는 뺀다. 바꾸기는 넘기고 완료에서 답한다 */
SW_TEST_CASE( AdminServiceTest, LiveEventCommandsForward )
{
    AdminFixture rig;
    rig._liveControl._listEventJSON.push_back( "{\"id\":\"a\"}" );
    rig._liveControl._listEventJSON.push_back( string( static_cast<size_t>( AdminProtocol::kMaxEventJSONSize ) + 1, 'x' ) );
    const AdminTestReply listed = rig.call( kViewer, AdminMethod::ListLiveEvents::kID, makeRequest() );
    SW_EXPECT_TRUE( listed._outcome == AdminResult::Ok );
    SW_EXPECT_EQUAL( size_t( 1 ), listed._listLiveEvent.size() );

    AdminRequest put = makeRequest();
    put._eventJSON   = "{\"id\":\"b\"}";
    rig.send( kOperator, AdminMethod::PutLiveEvent::kID, put );
    SW_EXPECT_TRUE( rig._sink._listReply.empty() );
    SW_EXPECT_EQUAL( put._eventJSON, rig._liveControl._putJSON );
    rig._liveControl.finishLast( LiveEventControlResult::Invalid );
    rig.tick();
    SW_EXPECT_TRUE( rig.take()._outcome.hasError( OnlineError::kInvalidRequest ) );

    AdminRequest removal = makeRequest();
    removal._eventID     = "b";
    rig.send( kOperator, AdminMethod::RemoveLiveEvent::kID, removal );
    SW_EXPECT_EQUAL( string( "b" ), rig._liveControl._removedEventID );
    rig._liveControl.finishLast( LiveEventControlResult::NotFound );
    rig.tick();
    SW_EXPECT_TRUE( rig.take()._outcome == AdminResult::NotFound );
}

/** @brief [AdminServiceTest] 넘길 키트가 없으면 등급이 맞아도 Unavailable 이고 기다리는 호출이 남지 않는다 */
SW_TEST_CASE( AdminServiceTest, ForwardWithoutKitIsUnavailable )
{
    AdminFixture rig( false );

    AdminRequest maintenance = makeRequest();
    maintenance._scope       = "game";
    SW_EXPECT_TRUE( rig.call( kOperator, AdminMethod::SetMaintenance::kID, maintenance )._outcome.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( rig.call( kViewer, AdminMethod::ListLiveEvents::kID, makeRequest() )._outcome.hasError( OnlineError::kUnavailable ) );

    AdminRequest removal = makeRequest();
    removal._eventID     = "b";
    SW_EXPECT_TRUE( rig.call( kOperator, AdminMethod::RemoveLiveEvent::kID, removal )._outcome.hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCallCount() );
}

/** @brief [AdminServiceTest] 모르는 호출의 완료는 경고만 남기고 지나간다 */
SW_TEST_CASE( AdminServiceTest, UnknownForwardedCompletionWarns )
{
    SW_EXPECT_LOG( "ServiceServerCallTable.UnknownCall" );
    AdminFixture rig;
    rig._directoryControl._listCompletion.push_back( ServerDirectoryControlCompletion{ 12345, ServerDirectoryControlResult::Ok } );
    rig.tick();
    SW_EXPECT_TRUE( rig._sink._listReply.empty() );
}

/** @brief [AdminServiceTest] 첫 관리자: 레코드가 없을 때만 넣고, 잘못된 값은 맡기지 않으며, 저장소 실패는 오류로 남긴다 */
SW_TEST_CASE( AdminServiceTest, SeedRoleOnlyWhenAbsent )
{
    SW_EXPECT_LOG( "AdminService.AdminServiceCouldNotSeed" );
    AdminFixture rig;

    const UnixMilliseconds nowMs{ kNowMs };
    rig._service.seedRole( kPlayer, AdminRole::Viewer, nowMs );
    rig._service.seedRole( kPlayer, AdminRole::Super, nowMs );
    rig._service.seedRole( kPlayer + 1, AdminRole::None, nowMs );
    rig._service.seedRole( kInvalidAccountID, AdminRole::Super, nowMs );
    while ( rig._store.pollCompletions() > 0 )
    {
    }
    AdminRole role    = AdminRole::None;
    uint64    version = 0;
    SW_REQUIRE_TRUE( AdminStoreLogic::readRole( rig._database, kPlayer, role, version ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( role == AdminRole::Viewer );
    SW_REQUIRE_TRUE( AdminStoreLogic::readRole( rig._database, kPlayer + 1, role, version ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( role == AdminRole::None );

    rig._database.armFault( ServiceStoreFault::RejectCommit );
    rig._service.seedRole( kPlayer + 2, AdminRole::Viewer, nowMs );
    while ( rig._store.pollCompletions() > 0 )
    {
    }
}

/** @brief [AdminServiceTest] 종료: 넘긴 명령의 완료를 회수해 응답한 뒤 끝난다 */
SW_TEST_CASE( AdminServiceTest, ShutdownDrainsForwardedCalls )
{
    AdminFixture rig;

    AdminRequest maintenance = makeRequest();
    maintenance._scope       = "game";
    rig.send( kOperator, AdminMethod::SetMaintenance::kID, maintenance );
    SW_REQUIRE_EQUAL( 1, rig._service.getPendingCallCount() );
    rig._directoryControl.finishLast( ServerDirectoryControlResult::Ok );
    rig._service.shutdown();
    SW_EXPECT_TRUE( rig.take()._outcome == AdminResult::Ok );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCallCount() );
}

/** @brief [AdminServiceTest] 다시 시도할 공통 범주(버전 충돌 · 일시 장애)는 요청 층이 기억하지 않아 같은 멱등 키의 지급 재시도가 서비스에 다시 닿고, 끝난 결과는 기억한다 */
SW_TEST_CASE( AdminServiceTest, RetryableReplyReachesTheServiceAgain )
{
    test::ReplyRetentionFixture<AdminProtocol, AdminReply> rig;
    AdminReply                                             finished;
    finished._result = AdminResult::Ok;
    rig.expectFailureReachesService( AdminMethod::AdjustAsset::kID, OnlineError::kBusy, finished );
    rig.expectFailureReachesService( AdminMethod::AdjustAsset::kID, OnlineError::kUnavailable, finished );
}
