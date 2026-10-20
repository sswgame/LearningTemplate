// GM 도구 — 등급 문지기, 지급 · 회수는 원장 + 감사가 한 트랜잭션, 같은 키 재시도 · 응답 유실, 커밋 거절은 흔적 없음, 환불 회수의 빚, 제재 등급 · 세션 끊기 한 번,
// 조회, 일괄 지급 이어 하기, 캠페인, 자기 등급 금지, 와이어 왕복, 제재 변경 버스 알림 한 번, 신고 목록과 처리, 점검과 공지를 서버 디렉터리 키트에 넘기기.
#include "pch.h"

#include "Core/Network/BitStream.h"

#include "EngineTest/GameFramework/Online/OnlineMessageTestUtil.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Directory/ServerDirectoryControl.h"
#include "GameFramework/Base/Online/Identity/AccountNameIndex.h"
#include "GameFramework/Base/Online/Identity/AccountSessionControl.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Base/Online/Report/ServiceReport.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Admin/Shared/AdminProtocol.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct AdminFixture
    {
        static constexpr AccountID kViewer   = 0x1;
        static constexpr AccountID kSupport  = 0x2;
        static constexpr AccountID kOperator = 0x3;
        static constexpr AccountID kSuper    = 0x4;
        static constexpr AccountID kPlayer   = 0x100;

        MemoryServiceDatabase    _database;
        const IAccountNameIndex* _pNameIndex;
        uint64                   _nextKey;

        AdminFixture()
            : _database{}
            , _pNameIndex{ nullptr }
            , _nextKey{ 1 }
        {
            (void)AdminStoreLogic::seedRole( _database, kViewer, AdminRole::Viewer, 1 );
            (void)AdminStoreLogic::seedRole( _database, kSupport, AdminRole::Support, 1 );
            (void)AdminStoreLogic::seedRole( _database, kOperator, AdminRole::Operator, 1 );
            (void)AdminStoreLogic::seedRole( _database, kSuper, AdminRole::Super, 1 );
        }

        AdminResult run( AccountID adminID, uint16 method, AdminRequest request, AdminReply* pOutReply = nullptr, uint64 keyLow = 0 )
        {
            AdminCommand command;
            command._adminID    = adminID;
            command._method     = method;
            command._pNameIndex = _pNameIndex;
            command._nowMs      = 5000;
            command._keyHigh    = 7;
            command._keyLow     = keyLow != 0 ? keyLow : _nextKey++;
            if ( request._memo.empty() )
                request._memo = "ticket-1";
            command._request = std::move( request );
            AdminReply        reply;
            const AdminResult result = AdminStoreLogic::execute( _database, command, reply );
            if ( pOutReply != nullptr )
                *pOutReply = reply;
            return result;
        }

        AdminRequest adjust( int64 amount ) const
        {
            AdminRequest request;
            request._accountID = kPlayer;
            request._assetID   = "cur.gold";
            request._amount    = amount;
            return request;
        }

        int64 gold()
        {
            LedgerBalance balance;
            // 실패면 balance 가 0 으로 남아 호출한 단언이 틀린 값으로 잡는다
            (void)Ledger::readBalance( _database, LedgerHolder::makeAccount( kPlayer ), "cur.gold", balance );
            return balance._amount;
        }

        int32 countAudit( string_view subject )
        {
            vector<ServiceAuditEntry> listEntry;
            string                    cursor;
            (void)ServiceAuditLog::listEntries( _database, subject, "", 100, listEntry, cursor );
            return static_cast<int32>( listEntry.size() );
        }
    };

    /** @brief 접속하지 않은 정식 계정 하나("OfflineHero" = kPlayer)만 아는 이름 색인입니다. */
    class FakeNameIndex final : public IAccountNameIndex
    {
    public:
        ServiceStoreResult readIdentityByDisplayName( IServiceStoreConnection& connection, string_view displayName, AccountIdentity& outIdentity ) const override
        {
            (void)connection;
            if ( displayName != "OfflineHero" )
                return ServiceStoreResult::NotFound;
            outIdentity._accountID   = AdminFixture::kPlayer;
            outIdentity._displayName = "OfflineHero";
            return ServiceStoreResult::Ok;
        }

        ServiceStoreResult readIdentity( IServiceStoreConnection& connection, AccountID accountID, AccountIdentity& outIdentity ) const override
        {
            if ( accountID != AdminFixture::kPlayer )
                return ServiceStoreResult::NotFound;
            return readIdentityByDisplayName( connection, "OfflineHero", outIdentity );
        }
    };

    class FakeSessionControl final : public IAccountSessionControl
    {
    public:
        vector<AccountID> _listRevoked{};

        void revokeAccountSessions( AccountID accountID, string_view reasonCode, int64 nowMs ) override
        {
            (void)reasonCode;
            (void)nowMs;
            _listRevoked.push_back( accountID );
        }
    };

    /** @brief 받은 변경을 적고 다음 `drainControlCompletions` 에 정한 결과로 끝낸다. */
    class FakeDirectoryControl final : public IServerDirectoryControl
    {
    public:
        vector<ServerDirectoryControlCompletion> _listPending{};
        vector<string>                           _listCall{};
        ServerDirectoryControlResult             _nextResult{ ServerDirectoryControlResult::Ok };

        void setMaintenance( const ServerDirectoryMaintenanceChange& change, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag ) override
        {
            (void)actorID;
            (void)memo;
            (void)nowMs;
            _listCall.push_back( "set:" + change._scope );
            _listPending.push_back( ServerDirectoryControlCompletion{ requestTag, _nextResult } );
        }

        void clearMaintenance( string_view scope, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag ) override
        {
            (void)actorID;
            (void)memo;
            (void)nowMs;
            _listCall.push_back( "clear:" + string( scope ) );
            _listPending.push_back( ServerDirectoryControlCompletion{ requestTag, _nextResult } );
        }

        void postNotice( const ServerDirectoryNoticeChange& change, AccountID actorID, int64 nowMs, uint64 requestTag ) override
        {
            (void)actorID;
            (void)nowMs;
            _listCall.push_back( "post:" + change._text );
            _listPending.push_back( ServerDirectoryControlCompletion{ requestTag, _nextResult } );
        }

        void removeNotice( uint64 noticeID, AccountID actorID, int64 nowMs, uint64 requestTag ) override
        {
            (void)noticeID;
            (void)actorID;
            (void)nowMs;
            _listCall.push_back( "remove" );
            _listPending.push_back( ServerDirectoryControlCompletion{ requestTag, _nextResult } );
        }

        void drainControlCompletions( vector<ServerDirectoryControlCompletion>& outListCompletion ) override
        {
            outListCompletion.insert( outListCompletion.end(), _listPending.begin(), _listPending.end() );
            _listPending.clear();
        }
    };

    struct AdminReplyCapture
    {
        AdminReply _reply{};
        int32      _count{ 0 };

        void onReply( const AdminReply& reply )
        {
            _reply = reply;
            ++_count;
        }

        AdminService::ReplyDelegate makeDelegate() { return AdminService::ReplyDelegate::create<&AdminReplyCapture::onReply>( this ); }
    };
} // namespace

SW_TEST_CASE( AdminServiceTest, RoleGatesEveryCommand )
{
    AdminFixture fixture;
    AdminRequest lookup;
    lookup._accountID = AdminFixture::kPlayer;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kLookupAccount, lookup ) == AdminResult::Ok );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kAdjustAsset, fixture.adjust( 10 ) ) == AdminResult::Forbidden );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kPlayer, AdminMethod::kLookupAccount, lookup ) == AdminResult::Forbidden ); // 등급 없음
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 10 ) ) == AdminResult::Ok );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSuper, static_cast<uint16>( OnlineMethodRange::kAdmin + 0x70 ), lookup ) == AdminResult::Forbidden );
}

SW_TEST_CASE( AdminServiceTest, GrantAndRevokeAreAuditedInTheSameTransaction )
{
    AdminFixture fixture;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 100 ) ) == AdminResult::Ok );
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( -30 ) ) == AdminResult::Ok );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( -100 ) ) == AdminResult::InsufficientFunds ); // 음수 금지
    SW_EXPECT_EQUAL( fixture.gold(), int64( 70 ) );
    SW_EXPECT_EQUAL( fixture.countAudit( "acct.0000000000000100/" ), 2 ); // 거절된 회수는 감사 줄도 없다
    AdminCommand command;
    command._adminID = AdminFixture::kOperator;
    command._method  = AdminMethod::kAdjustAsset;
    command._keyLow  = 999;
    command._request = fixture.adjust( 1 ); // 메모 없음
    AdminReply reply;
    SW_EXPECT_TRUE( AdminStoreLogic::execute( fixture._database, command, reply ) == AdminResult::InvalidRequest ); // 메모 필수
}

SW_TEST_CASE( AdminServiceTest, RefundRevokeMayLeaveDebt )
{
    AdminFixture fixture;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 20 ) ) == AdminResult::Ok );
    AdminRequest refund = fixture.adjust( -50 );
    refund._bRefund     = SW_TRUE;
    AdminReply reply;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, refund, &reply ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( fixture.gold(), int64( -30 ) ); // 이미 쓴 몫은 빚으로
    vector<ServiceAuditEntry> listEntry;
    string                    cursor;
    SW_ASSERT_TRUE( ServiceAuditLog::listEntries( fixture._database, "acct.0000000000000100/", "", 1, listEntry, cursor ) == ServiceStoreResult::Ok );
    SW_ASSERT_EQUAL( listEntry.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listEntry[0]._action, string( "refund.revoke" ) );
    SW_EXPECT_EQUAL( listEntry[0]._after, string( "cur.gold=-30" ) );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( -1 ) ) == AdminResult::InsufficientFunds ); // 빚이 있는 동안 회수 · 사용 불가
}

SW_TEST_CASE( AdminServiceTest, SameKeyIsReplayedAndRejectedCommitLeavesNothing )
{
    AdminFixture fixture;
    AdminReply   first;
    AdminReply   second;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 50 ), &first, 42 ) == AdminResult::Ok );
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 50 ), &second, 42 ) == AdminResult::Ok );
    SW_EXPECT_TRUE( second._bReplayed == SW_TRUE );
    SW_EXPECT_EQUAL( fixture.gold(), int64( 50 ) );
    const uint64 before = fixture._database.computeContentHash();
    fixture._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 5 ), nullptr, 43 ) == AdminResult::Unavailable );
    SW_EXPECT_EQUAL( fixture._database.computeContentHash(), before );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 5 ), nullptr, 43 ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( fixture.gold(), int64( 55 ) );
    fixture._database.armFault( ServiceStoreFault::LoseCommitReply ); // 적용됐지만 응답을 잃었다 — 멱등 기록으로 가린다
    AdminReply lost;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 5 ), &lost, 44 ) == AdminResult::Ok );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 5 ), nullptr, 44 ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( fixture.gold(), int64( 60 ) );
    SW_EXPECT_EQUAL( fixture.countAudit( "acct.0000000000000100/" ), 3 ); // 효과 셋 · 감사 줄 셋
}

SW_TEST_CASE( AdminServiceTest, SanctionNeedsTheRightRole )
{
    AdminFixture fixture;
    AdminRequest mute;
    mute._accountID       = AdminFixture::kPlayer;
    mute._sanctionKind    = ServiceSanctionKind::ChatMute;
    mute._untilMs         = 9000;
    AdminRequest suspend  = mute;
    suspend._sanctionKind = ServiceSanctionKind::Suspend;
    AdminRequest ban      = mute;
    ban._sanctionKind     = ServiceSanctionKind::Ban;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSupport, AdminMethod::kSetSanction, mute ) == AdminResult::Ok );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSupport, AdminMethod::kSetSanction, suspend ) == AdminResult::Forbidden );
    AdminReply reply;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kSetSanction, suspend, &reply ) == AdminResult::Ok );
    SW_EXPECT_TRUE( reply._bRevokeSessions == SW_TRUE );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kSetSanction, ban ) == AdminResult::Forbidden );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kSetSanction, ban ) == AdminResult::Ok );
    ServiceSanctionState state;
    SW_ASSERT_TRUE( ServiceSanction::readState( fixture._database, AdminFixture::kPlayer, state ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( ServiceSanction::isActive( state, ServiceSanctionKind::ChatMute, 8999 ) );
    SW_EXPECT_TRUE( ServiceSanction::isActive( state, ServiceSanctionKind::Ban, 0x7FFFFFFF00000000ll ) ); // 0 이 아니면 영구
}

SW_TEST_CASE( AdminServiceTest, LookupShowsBalancesSanctionHistoryAndAudit )
{
    AdminFixture fixture;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kAdjustAsset, fixture.adjust( 40 ) ) == AdminResult::Ok );
    AdminRequest lookup;
    lookup._accountID = AdminFixture::kPlayer;
    AdminReply reply;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kLookupAccount, lookup, &reply ) == AdminResult::Ok );
    SW_ASSERT_EQUAL( reply._listBalance.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( reply._listBalance[0]._amount, int64( 40 ) );
    SW_ASSERT_EQUAL( reply._listJournal.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( reply._listJournal[0]._reason, string( "admin.grant" ) );
    SW_EXPECT_TRUE( reply._listJournal[0]._actorKind == LedgerActorKind::Admin );
    SW_ASSERT_EQUAL( reply._listAudit.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( reply._listAudit[0]._actor, string( "gm.0000000000000003" ) );
    SW_EXPECT_EQUAL( reply._listAudit[0]._after, string( "cur.gold=40" ) );

    BitWriter writer; // 와이어 왕복 — 운영 도구가 받는 그대로
    SW_ASSERT_TRUE( AdminProtocol::MessageTypes::write( writer, reply ) );
    BitReader  reader( writer.getBytes().data(), writer.getByteCount() );
    AdminReply decoded;
    SW_ASSERT_TRUE( AdminProtocol::MessageTypes::read( reader, decoded ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( FixedLayout::equals( decoded, reply ) );
    SW_EXPECT_EQUAL( decoded._listAudit.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( decoded._listJournal[0]._listChange[0]._amount, int64( 40 ) );
}

SW_TEST_CASE( AdminServiceTest, BulkMailResumesWithoutDuplicates )
{
    AdminFixture fixture;
    AdminRequest bulk;
    bulk._batchID  = 0xB1;
    bulk._titleKey = "운영 보상";
    bulk._listAttachment.push_back( ServiceMailAttachment{ "cur.gold", 10 } );
    bulk._listAccountID = { 0x201, 0x202, 0x203 };
    AdminReply first;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kBulkMail, bulk, &first ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( first._processedCount, 3 );
    bulk._listAccountID = { 0x201, 0x202, 0x203, 0x204 }; // 끊긴 뒤 다시 — 새 명령 키, 같은 배치
    AdminReply second;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kBulkMail, bulk, &second ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( second._processedCount, 1 );
    SW_EXPECT_EQUAL( fixture._database.countRecords( ServiceMail::getMailTable() ), 4 );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kOperator, AdminMethod::kBulkMail, bulk ) == AdminResult::Forbidden );
}

SW_TEST_CASE( AdminServiceTest, CampaignCreationIsAudited )
{
    AdminFixture fixture;
    AdminRequest campaign;
    campaign._batchID  = 0xC1;
    campaign._startMs  = 1000;
    campaign._endMs    = 100000;
    campaign._titleKey = "mail.title.anniversary";
    campaign._listAttachment.push_back( ServiceMailAttachment{ "cur.gem_free", 300 } );
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kCreateCampaign, campaign ) == AdminResult::Ok );
    vector<ServiceMailCampaign> listCampaign;
    SW_ASSERT_TRUE( ServiceMailCampaignTable::listCampaigns( fixture._database, listCampaign ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( listCampaign.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( fixture.countAudit( "campaign.00000000000000c1/" ), 1 );
}

SW_TEST_CASE( AdminServiceTest, AdminCannotChangeOwnRole )
{
    AdminFixture fixture;
    AdminRequest demoteSelf;
    demoteSelf._accountID = AdminFixture::kSuper;
    demoteSelf._role      = AdminRole::Viewer;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kSetRole, demoteSelf ) == AdminResult::Forbidden );
    AdminRequest promote;
    promote._accountID = AdminFixture::kSupport;
    promote._role      = AdminRole::Operator;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kSetRole, promote ) == AdminResult::Ok );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSupport, AdminMethod::kAdjustAsset, fixture.adjust( 1 ) ) == AdminResult::Ok ); // 다음 명령부터 새 등급
}

SW_TEST_CASE( AdminServiceTest, LookupFindsOfflineAccountsByNameThroughTheNameIndex )
{
    AdminFixture  fixture;
    FakeNameIndex nameIndex;
    AdminRequest  byName;
    byName._displayName = "OfflineHero";
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kLookupAccount, byName ) == AdminResult::UnknownAccount ); // 색인이 없으면 이름으로 찾지 않는다
    fixture._pNameIndex = &nameIndex;
    AdminReply reply;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kLookupAccount, byName, &reply ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( reply._identity._accountID, AdminFixture::kPlayer );
    SW_EXPECT_EQUAL( reply._identity._displayName, string( "OfflineHero" ) );
    AdminRequest unknown;
    unknown._displayName = "Nobody";
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kLookupAccount, unknown ) == AdminResult::UnknownAccount );
    AdminRequest byID;
    byID._accountID = 0x999; // 색인에 없는 계정 id
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kLookupAccount, byID ) == AdminResult::UnknownAccount );
}

SW_TEST_CASE( AdminServiceTest, ReportsAreListedAndResolvedOnceByTheSupportRole )
{
    AdminFixture  fixture;
    ServiceReport report;
    report._category   = "chat";
    report._context    = "world.kr";
    report._reason     = "abuse.insult";
    report._reporterID = 0x200;
    report._targetID   = AdminFixture::kPlayer;
    report._createdMs  = 4000;
    report._listLine.push_back( ServiceReportLine{ "bad guy", "insult", AdminFixture::kPlayer, 3900 } );
    ServiceTransaction open;
    SW_ASSERT_TRUE( ServiceReportStore::stageOpen( open, report, 1 ) );
    SW_ASSERT_TRUE( fixture._database.commit( open ) == ServiceStoreResult::Ok );

    AdminRequest list;
    list._reportStatus = ServiceReportStatus::Open;
    AdminReply listed;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kListReports, list, &listed ) == AdminResult::Ok );
    SW_ASSERT_EQUAL( listed._listReport.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( listed._listReport[0]._listLine[0]._text, string( "insult" ) );

    AdminRequest resolve;
    resolve._reportKey    = listed._listReport[0]._reportKey;
    resolve._reportStatus = ServiceReportStatus::Resolved;
    resolve._memo         = "muted for a day";
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kResolveReport, resolve ) == AdminResult::Forbidden ); // 처리는 Support 부터
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSupport, AdminMethod::kResolveReport, resolve ) == AdminResult::Ok );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSupport, AdminMethod::kResolveReport, resolve ) == AdminResult::NotFound ); // 새 멱등 키 — 이미 닫혔다
    AdminRequest reopen  = resolve;
    reopen._reportStatus = ServiceReportStatus::Open;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSupport, AdminMethod::kResolveReport, reopen ) == AdminResult::InvalidRequest );
    SW_EXPECT_EQUAL( fixture.countAudit( "acct.0000000000000100/" ), 1 ); // 처리 하나에 감사 기록 하나

    AdminReply closed;
    list._reportStatus = ServiceReportStatus::Resolved;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kViewer, AdminMethod::kListReports, list, &closed ) == AdminResult::Ok );
    SW_ASSERT_EQUAL( closed._listReport.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( closed._listReport[0]._status == ServiceReportStatus::Resolved );
    SW_EXPECT_EQUAL( closed._listReport[0]._resolverID, AdminFixture::kSupport );
}

SW_TEST_CASE( AdminServiceTest, MaintenanceAndNoticeCommandsAreForwardedAfterTheRoleCheck )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    store{ &database };
    FakeDirectoryControl  control;
    AdminService          service;
    AdminServiceConfig    settings;
    settings._pDirectoryControl = &control;
    SW_ASSERT_TRUE( service.initialize( &store, settings ) );
    service.seedRole( AdminFixture::kOperator, AdminRole::Operator, 1 );
    service.seedRole( AdminFixture::kSupport, AdminRole::Support, 1 );
    (void)store.pollCompletions();

    AdminRequest maintenance;
    maintenance._scope      = "game";
    maintenance._messageKey = "maintenance.weekly";
    maintenance._startMs    = 1000;
    maintenance._endMs      = 9000;
    maintenance._memo       = "weekly";
    AdminRequest notice;
    notice._noticeID = 7;
    notice._body     = "notice.event";
    notice._memo     = "event";
    AdminReplyCapture set;
    AdminReplyCapture denied;
    AdminReplyCapture posted;
    service.submitCall( AdminFixture::kOperator, AdminMethod::kSetMaintenance, maintenance, NetIdempotencyKey{ 1, 1 }, 5000, set.makeDelegate() );
    service.submitCall( AdminFixture::kSupport, AdminMethod::kSetMaintenance, maintenance, NetIdempotencyKey{ 1, 2 }, 5000, denied.makeDelegate() );
    control._nextResult = ServerDirectoryControlResult::Ok;
    service.submitCall( AdminFixture::kOperator, AdminMethod::kPostNotice, notice, NetIdempotencyKey{ 1, 3 }, 5000, posted.makeDelegate() );
    (void)store.pollCompletions();    // 등급 확인이 끝나 넘긴다
    SW_EXPECT_EQUAL( set._count, 0 ); // 아직 서버 디렉터리 키트의 완료 전이다
    SW_EXPECT_EQUAL( denied._count, 1 );
    SW_EXPECT_TRUE( denied._reply._result == AdminResult::Forbidden );
    SW_ASSERT_EQUAL( control._listCall.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( control._listCall[0], string( "set:game" ) );
    SW_EXPECT_EQUAL( control._listCall[1], string( "post:notice.event" ) );
    service.update( 5000 );
    SW_EXPECT_EQUAL( set._count, 1 );
    SW_EXPECT_TRUE( set._reply._result == AdminResult::Ok );
    SW_EXPECT_TRUE( posted._reply._result == AdminResult::Ok );

    control._nextResult = ServerDirectoryControlResult::NotFound;
    AdminReplyCapture removed;
    service.submitCall( AdminFixture::kOperator, AdminMethod::kRemoveNotice, notice, NetIdempotencyKey{ 1, 4 }, 5000, removed.makeDelegate() );
    (void)store.pollCompletions();
    service.update( 5000 );
    SW_EXPECT_TRUE( removed._reply._result == AdminResult::NotFound );
    service.shutdown();
    store.shutdown();
    (void)store.pollCompletions();
}

SW_TEST_CASE( AdminServiceTest, ServiceRevokesSessionsAfterSuspendOnce )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    store{ &database };
    FakeSessionControl    sessions;
    AdminService          service;
    AdminServiceConfig    settings;
    settings._pSessionControl = &sessions;
    SW_ASSERT_TRUE( service.initialize( &store, settings ) );
    service.seedRole( AdminFixture::kOperator, AdminRole::Operator, 1 );
    (void)store.pollCompletions();
    AdminRequest suspend;
    suspend._accountID    = AdminFixture::kPlayer;
    suspend._sanctionKind = ServiceSanctionKind::Suspend;
    suspend._untilMs      = 99999;
    suspend._memo         = "ticket-9";
    AdminReplyCapture first;
    AdminReplyCapture retry;
    service.submitCall( AdminFixture::kOperator, AdminMethod::kSetSanction, suspend, NetIdempotencyKey{ 1, 2 }, 5000, first.makeDelegate() );
    service.submitCall( AdminFixture::kOperator, AdminMethod::kSetSanction, suspend, NetIdempotencyKey{ 1, 2 }, 5000, retry.makeDelegate() );
    (void)store.pollCompletions();
    service.update( 5000 );
    SW_EXPECT_TRUE( first._reply._result == AdminResult::Ok );
    SW_EXPECT_TRUE( retry._reply._bReplayed == SW_TRUE );
    SW_ASSERT_EQUAL( sessions._listRevoked.size(), size_t( 1 ) ); // 재시도 응답에서 다시 끊지 않는다
    SW_EXPECT_EQUAL( sessions._listRevoked[0], AdminFixture::kPlayer );
    service.shutdown();
    store.shutdown();
    (void)store.pollCompletions();
}

SW_TEST_CASE( AdminServiceTest, SanctionChangeIsPublishedOnceOnTheBus )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    store{ &database };
    LocalServerBusHub     hub;
    LocalServerBus        adminTransport{ &hub, 1 };
    ServerBus             adminBus{ adminTransport };
    LocalServerBus        chatBus{ &hub, 2 }; // 다른 서버의 채팅이 듣는다
    chatBus.subscribe( ServiceSanctionBus::kChangedTopic.getName() );
    AdminService       service;
    AdminServiceConfig settings;
    settings._pBus = &adminBus;
    SW_ASSERT_TRUE( service.initialize( &store, settings ) );
    service.seedRole( AdminFixture::kSupport, AdminRole::Support, 1 );
    (void)store.pollCompletions();

    AdminRequest mute;
    mute._accountID    = AdminFixture::kPlayer;
    mute._sanctionKind = ServiceSanctionKind::ChatMute;
    mute._untilMs      = 99999;
    mute._memo         = "ticket-10";
    AdminReplyCapture first;
    AdminReplyCapture retry;
    AdminReplyCapture lookup;
    service.submitCall( AdminFixture::kSupport, AdminMethod::kSetSanction, mute, NetIdempotencyKey{ 3, 4 }, 5000, first.makeDelegate() );
    service.submitCall( AdminFixture::kSupport, AdminMethod::kSetSanction, mute, NetIdempotencyKey{ 3, 4 }, 5000, retry.makeDelegate() );
    service.submitCall( AdminFixture::kSupport, AdminMethod::kLookupAccount, mute, NetIdempotencyKey{ 3, 5 }, 5000, lookup.makeDelegate() );
    (void)store.pollCompletions();
    SW_EXPECT_TRUE( first._reply._result == AdminResult::Ok );
    SW_EXPECT_TRUE( retry._reply._bReplayed == SW_TRUE );

    vector<ServerBusMessage> listMessage;
    (void)chatBus.pollMessages( listMessage );
    SW_ASSERT_EQUAL( listMessage.size(), size_t( 1 ) ); // 재시도 · 조회는 내지 않는다
    ServiceSanctionChanged changed;
    SW_ASSERT_TRUE( ServiceSanctionBus::kChangedTopic.read( listMessage[0], changed ) == ServerBusReadResult::Read );
    SW_EXPECT_EQUAL( changed._accountID, AdminFixture::kPlayer );
    service.shutdown();
    store.shutdown();
    (void)store.pollCompletions();
}

/**
 * @brief [AdminProtocolTest] 애노테이션은 정수 리터럴만 받으므로, 같은 상한을 적은 `PROPERTY( MaxCount )` 와 상수가 어긋나지 않는지 생성된 코덱의 정규 문자열로 본다
 */
SW_TEST_CASE( AdminProtocolTest, DeclaredLimitsMatchTheConstants )
{
    using Util = test::OnlineMessageTestUtil;
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminRequest>( "_listAttachment" ), uint32( ServiceMailConstant::kMaxAttachmentCount ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminRequest>( "_listAccountID" ), uint32( AdminProtocol::kMaxBulkCount ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminRequest>( "_assetID" ), uint32( LedgerConstant::kMaxAssetIDSize ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminRequest>( "_memo" ), uint32( AdminProtocol::kMaxMemoSize ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminRequest>( "_titleKey" ), uint32( ServiceMailConstant::kMaxTitleSize ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminRequest>( "_body" ), uint32( ServiceMailConstant::kMaxBodySize ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminJournalLine>( "_reason" ), uint32( LedgerConstant::kMaxReasonSize ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminJournalLine>( "_memo" ), uint32( LedgerConstant::kMaxMemoSize ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminReply>( "_listBalance" ), uint32( OnlineLimit::kMaxBalanceCount ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminReply>( "_listJournal" ), uint32( AdminProtocol::kMaxListCount ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminReply>( "_listAudit" ), uint32( AdminProtocol::kMaxListCount ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminReply>( "_listReport" ), uint32( AdminProtocol::kMaxReportListCount ) );
    SW_EXPECT_EQUAL( Util::declaredMaxCount<AdminRequest>( "_reportKey" ), uint32( ServiceReportConstant::kMaxReportKeySize ) );
}

/**
 * @brief [AdminProtocolTest] GM 메시지는 상한까지 왕복하고, 목록 개수 범위와 누구도 가질 수 없는 등급은 쓰지 않는다
 */
SW_TEST_CASE( AdminProtocolTest, MessagesRoundTripAndRefuseOutOfRange )
{
    using Messages = AdminProtocol::MessageTypes;
    using Util     = test::OnlineMessageTestUtil;
    AdminRequest request;
    request._listAttachment.assign( 8, ServiceMailAttachment{ "cur.gold", 10 } );
    request._listAccountID.assign( AdminProtocol::kMaxBulkCount, AccountID( 0x201 ) );
    request._memo         = string( 1024, 'm' );
    request._body         = string( 2048, 'b' );
    request._maxCount     = 100;
    request._sanctionKind = ServiceSanctionKind::Ban;
    request._role         = AdminRole::Super;
    request._bRefund      = SW_TRUE;
    SW_EXPECT_TRUE( Util::roundTrips<Messages>( request ) );
    AdminReply reply;
    reply._sanction._arrUntilMs[2] = ServiceSanctionState::kPermanentMs;
    reply._sanction._reasonCode    = "sanction.suspended";
    reply._identity                = AccountIdentity{ string( 32, 'i' ), 5, SW_TRUE };
    reply._listAudit.assign( 100, ServiceAuditEntry{ "gm.1", "admin.grant", "acct.2/", string( 2048, 'b' ), "", "", 7 } );
    reply._bRevokeSessions = SW_TRUE; // 서버 안에서만 쓰는 값은 와이어에 싣지 않는다
    BitWriter writer;
    SW_ASSERT_TRUE( Messages::write( writer, reply ) );
    BitReader  reader( writer.getBytes().data(), writer.getByteCount() );
    AdminReply decoded;
    SW_ASSERT_TRUE( Messages::read( reader, decoded ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( FixedLayout::equals( decoded, reply ) );
    SW_EXPECT_TRUE( decoded._bRevokeSessions == SW_FALSE );
    SW_EXPECT_TRUE( decoded._sanction._arrUntilMs[2] == ServiceSanctionState::kPermanentMs );
    SW_EXPECT_TRUE( Util::refusesEveryPrefix<Messages>( request ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "out-of-range values are not written" );
        AdminRequest zeroCount;
        zeroCount._maxCount = 0;
        SW_EXPECT_TRUE( Util::refusesToWrite<Messages>( zeroCount ) );
        AdminRequest nobodyRole;
        nobodyRole._role = AdminRole::Count;
        SW_EXPECT_TRUE( Util::refusesToWrite<Messages>( nobodyRole ) );
    }
}
