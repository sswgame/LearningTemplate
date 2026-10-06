// GM 도구 — 등급 문지기, 지급 · 회수는 원장 + 감사가 한 트랜잭션, 같은 키 재시도 · 응답 유실, 커밋 거절은 흔적 없음, 환불 회수의 빚, 제재 등급 · 세션 끊기 한 번,
// 조회, 일괄 지급 이어 하기, 캠페인, 자기 등급 금지, 와이어 왕복, 제재 변경 버스 알림 한 번.
#include "pch.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Identity/AccountSessionControl.h"
#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Online/Server/Admin/AdminService.h"
#include "GameFramework/Kits/Online/Server/Admin/AdminStoreLogic.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct AdminFixture
    {
        static constexpr AccountId kViewer   = 0x1;
        static constexpr AccountId kSupport  = 0x2;
        static constexpr AccountId kOperator = 0x3;
        static constexpr AccountId kSuper    = 0x4;
        static constexpr AccountId kPlayer   = 0x100;

        MemoryServiceDatabase _database;
        uint64                _nextKey;

        AdminFixture()
            : _database{}
            , _nextKey{ 1 }
        {
            (void)AdminStoreLogic::seedRole( _database, kViewer, AdminRole::Viewer, 1 );
            (void)AdminStoreLogic::seedRole( _database, kSupport, AdminRole::Support, 1 );
            (void)AdminStoreLogic::seedRole( _database, kOperator, AdminRole::Operator, 1 );
            (void)AdminStoreLogic::seedRole( _database, kSuper, AdminRole::Super, 1 );
        }

        AdminResult run( AccountId adminId, uint16 method, AdminRequest request, AdminReply* pOutReply = nullptr, uint64 keyLow = 0 )
        {
            AdminCommand command;
            command._adminId = adminId;
            command._method  = method;
            command._nowMs   = 5000;
            command._keyHigh = 7;
            command._keyLow  = keyLow != 0 ? keyLow : _nextKey++;
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
            request._accountId = kPlayer;
            request._assetId   = "cur.gold";
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

    class FakeSessionControl final : public IAccountSessionControl
    {
    public:
        vector<AccountId> _listRevoked{};

        void revokeAccountSessions( AccountId accountId, string_view reasonCode, int64 nowMs ) override
        {
            (void)reasonCode;
            (void)nowMs;
            _listRevoked.push_back( accountId );
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
    lookup._accountId = AdminFixture::kPlayer;
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
    command._adminId = AdminFixture::kOperator;
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
    mute._accountId       = AdminFixture::kPlayer;
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
    lookup._accountId = AdminFixture::kPlayer;
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
    AdminProtocol::writeReply( writer, reply );
    BitReader  reader( writer.getBytes().data(), static_cast<int32>( writer.getBytes().size() ) );
    AdminReply decoded;
    SW_ASSERT_TRUE( AdminProtocol::readReply( reader, decoded ) );
    SW_EXPECT_EQUAL( decoded._listAudit.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( decoded._listJournal[0]._listChange[0]._amount, int64( 40 ) );
}

SW_TEST_CASE( AdminServiceTest, BulkMailResumesWithoutDuplicates )
{
    AdminFixture fixture;
    AdminRequest bulk;
    bulk._batchId  = 0xB1;
    bulk._titleKey = "운영 보상";
    bulk._listAttachment.push_back( ServiceMailAttachment{ "cur.gold", 10 } );
    bulk._listAccountId = { 0x201, 0x202, 0x203 };
    AdminReply first;
    SW_ASSERT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kBulkMail, bulk, &first ) == AdminResult::Ok );
    SW_EXPECT_EQUAL( first._processedCount, 3 );
    bulk._listAccountId = { 0x201, 0x202, 0x203, 0x204 }; // 끊긴 뒤 다시 — 새 명령 키, 같은 배치
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
    campaign._batchId  = 0xC1;
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
    demoteSelf._accountId = AdminFixture::kSuper;
    demoteSelf._role      = AdminRole::Viewer;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kSetRole, demoteSelf ) == AdminResult::Forbidden );
    AdminRequest promote;
    promote._accountId = AdminFixture::kSupport;
    promote._role      = AdminRole::Operator;
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSuper, AdminMethod::kSetRole, promote ) == AdminResult::Ok );
    SW_EXPECT_TRUE( fixture.run( AdminFixture::kSupport, AdminMethod::kAdjustAsset, fixture.adjust( 1 ) ) == AdminResult::Ok ); // 다음 명령부터 새 등급
}

SW_TEST_CASE( AdminServiceTest, ServiceRevokesSessionsAfterSuspendOnce )
{
    MemoryServiceDatabase database;
    MemoryServiceStore    store{ &database };
    FakeSessionControl    sessions;
    AdminService          service;
    AdminServiceSettings  settings;
    settings._pSessionControl = &sessions;
    SW_ASSERT_TRUE( service.initialize( &store, settings ) );
    service.seedRole( AdminFixture::kOperator, AdminRole::Operator, 1 );
    (void)store.pollCompletions();
    AdminRequest suspend;
    suspend._accountId    = AdminFixture::kPlayer;
    suspend._sanctionKind = ServiceSanctionKind::Suspend;
    suspend._untilMs      = 99999;
    suspend._memo         = "ticket-9";
    AdminReplyCapture first;
    AdminReplyCapture retry;
    service.submitCall( AdminFixture::kOperator, AdminMethod::kSetSanction, suspend, NetIdempotencyKey{ 1, 2 }, 5000, first.makeDelegate() );
    service.submitCall( AdminFixture::kOperator, AdminMethod::kSetSanction, suspend, NetIdempotencyKey{ 1, 2 }, 5000, retry.makeDelegate() );
    (void)store.pollCompletions();
    service.tick( 5000 );
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
    LocalServerBus        adminBus{ &hub, 1 };
    LocalServerBus        chatBus{ &hub, 2 }; // 다른 서버의 채팅이 듣는다
    chatBus.subscribe( ServiceSanctionBus::kChangedTopic );
    AdminService         service;
    AdminServiceSettings settings;
    settings._pBus = &adminBus;
    SW_ASSERT_TRUE( service.initialize( &store, settings ) );
    service.seedRole( AdminFixture::kSupport, AdminRole::Support, 1 );
    (void)store.pollCompletions();

    AdminRequest mute;
    mute._accountId    = AdminFixture::kPlayer;
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
    BitReader reader( listMessage[0]._bytes.data(), static_cast<int32>( listMessage[0]._bytes.size() ) );
    SW_EXPECT_EQUAL( reader.readVarUint(), AdminFixture::kPlayer );
    service.shutdown();
    store.shutdown();
    (void)store.pollCompletions();
}
