// 우편함 — 목록(최근 것 · 만료 숨김 · 캠페인), 수령(발행 · 맡김), 두 번 · 동시 수령은 원장 한 번, 응답 유실, 상한, 지우기 규칙, 만료 쓸기(버림 · 돌려줌),
// 캠페인 한 계정 한 번, 모두 받기, 거래 반환 우편, 서비스(submitCall · 주기 쓸기 · 멱등 키 필수).
#include "pch.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Server/Mailbox/MailboxService.h"
#include "GameFramework/Kits/Feature/Online/Server/Mailbox/MailboxStoreLogic.h"

#include "TestFramework/TestFramework.h"

#include <thread>

using namespace sw;

namespace
{
    struct MailFixture
    {
        static constexpr uint64 kOwner  = 0x10;
        static constexpr uint64 kSender = 0x20;

        MemoryServiceDatabase _database;

        string send( const utf8* pIdempotencyKey, const LedgerHolder& funding, int64 amount, int64 createdMs = 1000, int64 expiresMs = 100000,
                     ServiceMailExpiryAction action = ServiceMailExpiryAction::Discard )
        {
            ServiceMailMessage message;
            message._recipientAccountId = kOwner;
            message._idempotencyKey     = pIdempotencyKey;
            message._fundingHolder      = funding;
            message._createdMs          = createdMs;
            message._expiresMs          = expiresMs;
            message._expiryAction       = action;
            message._titleKey           = "mail.title";
            if ( amount > 0 )
                message._listAttachment.push_back( ServiceMailAttachment{ "cur.gold", amount } );
            ServiceTransaction transaction;
            string             mailKey;
            bool               bReplayed = false;
            if ( ServiceMail::stageSend( _database, message, transaction, mailKey, bReplayed ) != LedgerResult::Ok )
                return string();
            if ( bReplayed == false && _database.commit( transaction ) != ServiceStoreResult::Ok )
                return string();
            return mailKey;
        }

        void grant( uint64 accountId, int64 amount, const utf8* pKey )
        {
            LedgerTransferRequest request;
            request._journalKey = string( "test/" ) + pKey;
            request._reason     = "test.grant";
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountId ), "cur.gold", amount } );
            LedgerTransferOutcome outcome;
            (void)Ledger::executeTransfer( _database, request, outcome );
        }

        int64 gold( uint64 accountId )
        {
            LedgerBalance balance;
            // 실패면 balance 가 0 으로 남아 호출한 단언이 틀린 값으로 잡는다
            (void)Ledger::readBalance( _database, LedgerHolder::makeAccount( accountId ), "cur.gold", balance );
            return balance._amount;
        }

        MailboxResult claim( const string& mailKey, int64 nowMs = 2000, const ILedgerPolicy* pPolicy = nullptr )
        {
            MailboxClaimInput input;
            input._mailKey   = mailKey;
            input._accountId = kOwner;
            input._nowMs     = nowMs;
            input._pPolicy   = pPolicy;
            MailboxReply reply;
            return MailboxStoreLogic::claim( _database, input, reply );
        }

        bool isBalanced()
        {
            LedgerAuditReport report;
            return LedgerAudit::computeReport( _database, report ) == ServiceStoreResult::Ok && report.isBalanced();
        }
    };

    class MailboxSmallCap final : public ILedgerPolicy
    {
    public:
        int64 getBalanceCap( string_view assetId ) const override { return assetId == "cur.gold" ? 50 : 0; }
    };

    struct TestMailboxInternal
    {
        static void claimOnce( MailFixture* pFixture, const string* pMailKey, MailboxResult* pOutResult ) { *pOutResult = pFixture->claim( *pMailKey ); }
    };

    struct MailboxReplyCapture
    {
        MailboxReply _reply{};
        int32        _count{ 0 };

        void onReply( const MailboxReply& reply )
        {
            _reply = reply;
            ++_count;
        }

        MailboxService::ReplyDelegate makeDelegate() { return MailboxService::ReplyDelegate::create<&MailboxReplyCapture::onReply>( this ); }
    };
} // namespace

SW_TEST_CASE( MailboxTest, ListShowsNewestFirstAndHidesExpired )
{
    MailFixture    fixture;
    const string   older   = fixture.send( "a", LedgerHolder::makeMint(), 1, 1000, 100000 );
    const string   newer   = fixture.send( "b", LedgerHolder::makeMint(), 1, 2000, 100000 );
    const string   expired = fixture.send( "c", LedgerHolder::makeMint(), 1, 1500, 3000 );
    MailboxRequest request;
    MailboxReply   reply;
    SW_ASSERT_TRUE( MailboxStoreLogic::listMail( fixture._database, MailFixture::kOwner, 5000, {}, request, reply ) == MailboxResult::Ok );
    SW_ASSERT_EQUAL( reply._listMail.size(), size_t( 2 ) );
    SW_EXPECT_EQUAL( reply._listMail[0]._mailKey, newer );
    SW_EXPECT_EQUAL( reply._listMail[1]._mailKey, older );
    SW_EXPECT_TRUE( fixture.claim( expired, 5000 ) == MailboxResult::Expired );
}

SW_TEST_CASE( MailboxTest, ClaimFromMintAndFromEscrow )
{
    MailFixture fixture;
    fixture.grant( MailFixture::kSender, 100, "s" );
    const string fromMint   = fixture.send( "reward", LedgerHolder::makeMint(), 30 );
    const string fromSender = fixture.send( "gift", LedgerHolder::makeAccount( MailFixture::kSender ), 40 );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kSender ), int64( 60 ) ); // 보낼 때 맡김으로
    SW_EXPECT_TRUE( fixture.claim( fromMint ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( fixture.claim( fromSender ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 70 ) );
    SW_EXPECT_TRUE( fixture.claim( fromSender ) == MailboxResult::AlreadyClaimed );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 70 ) );
    SW_EXPECT_TRUE( fixture.isBalanced() );
}

SW_TEST_CASE( MailboxTest, ConcurrentClaimsMoveTheLedgerOnce )
{
    MailFixture   fixture;
    const string  mailKey = fixture.send( "race", LedgerHolder::makeMint(), 25 );
    MailboxResult arrResult[6]{};
    std::thread   arrThread[6];
    for ( int32 threadIndex = 0; threadIndex < 6; ++threadIndex )
    {
        arrThread[threadIndex] = std::thread( &TestMailboxInternal::claimOnce, &fixture, &mailKey, &arrResult[threadIndex] );
    }
    for ( std::thread& thread : arrThread )
    {
        thread.join();
    }
    int32 okCount = 0;
    for ( const MailboxResult result : arrResult )
    {
        SW_EXPECT_TRUE( result == MailboxResult::Ok || result == MailboxResult::AlreadyClaimed );
        okCount += result == MailboxResult::Ok ? 1 : 0;
    }
    SW_EXPECT_TRUE( okCount >= 1 ); // 겹친 수령은 같은 분개를 보고 Ok 를 받을 수 있다 — 원장은 한 번
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 25 ) );
    SW_EXPECT_TRUE( fixture.isBalanced() );
}

SW_TEST_CASE( MailboxTest, LostCommitReplyClaimIsResolvedByTheJournal )
{
    MailFixture  fixture;
    const string mailKey = fixture.send( "lost", LedgerHolder::makeMint(), 10 );
    fixture._database.armFault( ServiceStoreFault::LoseCommitReply );
    SW_EXPECT_TRUE( fixture.claim( mailKey ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( fixture.claim( mailKey ) == MailboxResult::AlreadyClaimed );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 10 ) );
    fixture._database.armFault( ServiceStoreFault::RejectCommit );
    const string second = fixture.send( "rejected", LedgerHolder::makeMint(), 5 ); // 넣기가 거절됐다 — 빈 키
    SW_EXPECT_TRUE( second.empty() );
}

SW_TEST_CASE( MailboxTest, CapExceededKeepsTheMail )
{
    MailFixture     fixture;
    MailboxSmallCap cap;
    const string    mailKey = fixture.send( "big", LedgerHolder::makeMint(), 60 );
    SW_EXPECT_TRUE( fixture.claim( mailKey, 2000, &cap ) == MailboxResult::CapExceeded );
    MailboxRequest request;
    MailboxReply   reply;
    SW_ASSERT_TRUE( MailboxStoreLogic::listMail( fixture._database, MailFixture::kOwner, 2000, {}, request, reply ) == MailboxResult::Ok );
    SW_ASSERT_EQUAL( reply._listMail.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( reply._listMail[0]._state == ServiceMailState::Unread );
}

SW_TEST_CASE( MailboxTest, DeleteRefusesUnclaimedAttachments )
{
    MailFixture  fixture;
    const string withGold = fixture.send( "g", LedgerHolder::makeMint(), 5 );
    const string notice   = fixture.send( "n", LedgerHolder::makeMint(), 0 );
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( fixture._database, MailFixture::kOwner, withGold ) == MailboxResult::HasAttachments );
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( fixture._database, MailFixture::kOwner, notice ) == MailboxResult::Ok );
    SW_ASSERT_TRUE( fixture.claim( withGold ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( fixture._database, MailFixture::kSender, withGold ) == MailboxResult::NotFound ); // 남의 우편
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( fixture._database, MailFixture::kOwner, withGold ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( fixture._database.countRecords( ServiceMail::getExpiryTable() ), 0 );
    SW_EXPECT_EQUAL( fixture._database.countRecords( ServiceMail::getMailTable() ), 0 );
}

SW_TEST_CASE( MailboxTest, SweepDiscardsOrReturnsEscrowedAttachments )
{
    MailFixture fixture;
    fixture.grant( MailFixture::kSender, 100, "s" );
    (void)fixture.send( "discard", LedgerHolder::makeAccount( MailFixture::kSender ), 10, 1000, 3000, ServiceMailExpiryAction::Discard );
    (void)fixture.send( "return", LedgerHolder::makeAccount( MailFixture::kSender ), 20, 1000, 3000, ServiceMailExpiryAction::ReturnToSender );
    (void)fixture.send( "mint", LedgerHolder::makeMint(), 5, 1000, 3000 );
    (void)fixture.send( "later", LedgerHolder::makeMint(), 5, 1000, 900000 );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kSender ), int64( 70 ) );
    MailboxSweepStats stats;
    MailboxStoreLogic::sweepExpired( fixture._database, 5000, 16, stats );
    SW_EXPECT_EQUAL( stats._discardedCount, 1 );
    SW_EXPECT_EQUAL( stats._returnedCount, 1 );
    SW_EXPECT_EQUAL( stats._removedCount, 1 );
    SW_EXPECT_EQUAL( stats._failedCount, 0 );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kSender ), int64( 90 ) );
    SW_EXPECT_EQUAL( fixture._database.countRecords( ServiceMail::getMailTable() ), 1 ); // "later" 만 남는다
    SW_EXPECT_TRUE( fixture.isBalanced() );
}

SW_TEST_CASE( MailboxTest, CampaignIsClaimedOncePerAccount )
{
    MailFixture         fixture;
    ServiceMailCampaign campaign;
    campaign._campaignId = 77;
    campaign._startMs    = 1000;
    campaign._endMs      = 9000;
    campaign._titleKey   = "mail.title.event";
    campaign._listAttachment.push_back( ServiceMailAttachment{ "cur.gold", 15 } );
    ServiceTransaction create;
    SW_ASSERT_TRUE( ServiceMailCampaignTable::stageCreate( campaign, create ) );
    SW_ASSERT_TRUE( fixture._database.commit( create ) == ServiceStoreResult::Ok );
    vector<ServiceMailCampaign> listCampaign;
    SW_ASSERT_TRUE( ServiceMailCampaignTable::listCampaigns( fixture._database, listCampaign ) == ServiceStoreResult::Ok );
    MailboxRequest request;
    MailboxReply   reply;
    SW_ASSERT_TRUE( MailboxStoreLogic::listMail( fixture._database, MailFixture::kOwner, 2000, listCampaign, request, reply ) == MailboxResult::Ok );
    SW_ASSERT_EQUAL( reply._listMail.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( reply._listMail[0]._bCampaign == SW_TRUE );
    SW_EXPECT_TRUE( fixture.claim( reply._listMail[0]._mailKey ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( fixture.claim( reply._listMail[0]._mailKey ) == MailboxResult::AlreadyClaimed );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 15 ) );
    MailboxReply after;
    SW_ASSERT_TRUE( MailboxStoreLogic::listMail( fixture._database, MailFixture::kOwner, 2000, listCampaign, request, after ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( after._listMail.empty() );
    SW_EXPECT_TRUE( fixture.claim( reply._listMail[0]._mailKey, 9000 ) == MailboxResult::Expired );
}

SW_TEST_CASE( MailboxTest, ClaimAllTakesEveryAttachmentAndSkipsCapped )
{
    MailFixture     fixture;
    MailboxSmallCap cap;
    (void)fixture.send( "a", LedgerHolder::makeMint(), 20, 1000 );
    (void)fixture.send( "b", LedgerHolder::makeMint(), 20, 1001 );
    (void)fixture.send( "c", LedgerHolder::makeMint(), 20, 1002 ); // 셋째는 상한 50 을 넘는다
    MailboxReply reply;
    SW_EXPECT_TRUE( MailboxStoreLogic::claimAll( fixture._database, MailFixture::kOwner, 2000, &cap, reply ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( reply._claimedCount, 2 );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 40 ) );
    SW_ASSERT_EQUAL( reply._listBalance.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( reply._listBalance[0]._amount, int64( 40 ) );
}

SW_TEST_CASE( MailboxTest, TradeReturnMailClaimsFromEscrow )
{
    MailFixture fixture;
    // 거래 실패 반환 흉내: 계정 → 맡김, 그 맡김을 재원으로 우편(넣을 때 맡김 → 우편 맡김) → 수령
    fixture.grant( MailFixture::kOwner, 30, "o" );
    const LedgerHolder    auctionEscrow = LedgerHolder::makeEscrow( "auction", "00000000000000aa" );
    LedgerTransferRequest lock;
    lock._journalKey = "auction/lock-aa";
    lock._reason     = "auction.lock";
    lock._listPosting.push_back( LedgerPosting{ LedgerHolder::makeAccount( MailFixture::kOwner ), auctionEscrow, "cur.gold", 30 } );
    LedgerTransferOutcome outcome;
    SW_ASSERT_TRUE( Ledger::executeTransfer( fixture._database, lock, outcome ) == LedgerResult::Ok );
    const string mailKey = fixture.send( "auction.aa.return", auctionEscrow, 30 );
    SW_ASSERT_FALSE( mailKey.empty() );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 0 ) );
    SW_EXPECT_TRUE( fixture.claim( mailKey ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 30 ) );
    SW_EXPECT_TRUE( fixture.isBalanced() );
}

SW_TEST_CASE( MailboxTest, ServiceClaimNeedsAKeyAndSweepsOnItsPeriod )
{
    MailFixture            fixture;
    MemoryServiceStore     store{ &fixture._database };
    MailboxService         service;
    MailboxServiceSettings settings;
    settings._sweepIntervalMs = 1000;
    SW_ASSERT_TRUE( service.initialize( &store, settings ) );
    const string mailKey = fixture.send( "svc", LedgerHolder::makeMint(), 7, 1000, 4000 );

    MailboxCall claimCall;
    claimCall._method           = MailboxMethod::kClaim;
    claimCall._accountId        = MailFixture::kOwner;
    claimCall._nowMs            = 2000;
    claimCall._request._mailKey = mailKey;
    MailboxReplyCapture noKey;
    service.submitCall( claimCall, noKey.makeDelegate() );
    SW_EXPECT_TRUE( noKey._reply._result == MailboxResult::InvalidRequest ); // 수령은 멱등 키 필수 — 그 자리에서 거절

    claimCall._idempotencyKey = NetIdempotencyKey{ 1, 2 };
    MailboxReplyCapture claimed;
    service.submitCall( claimCall, claimed.makeDelegate() );
    (void)store.pollCompletions();
    service.tick( 2000 );
    SW_ASSERT_EQUAL( claimed._count, 1 );
    SW_EXPECT_TRUE( claimed._reply._result == MailboxResult::Ok );
    SW_EXPECT_EQUAL( fixture.gold( MailFixture::kOwner ), int64( 7 ) );

    (void)fixture.send( "expire", LedgerHolder::makeMint(), 3, 1000, 4000 );
    (void)store.pollCompletions(); // 앞 틱의 쓸기 · 캠페인 일이 끝났다(호스트가 서비스 틱 전에 하는 일)
    service.tick( 5000 );          // 주기 — 쓸기 일을 맡긴다
    (void)store.pollCompletions();
    SW_EXPECT_EQUAL( fixture._database.countRecords( ServiceMail::getExpiryTable() ), 0 );
    SW_EXPECT_EQUAL( fixture._database.countRecords( ServiceMail::getMailTable() ), 1 ); // 받은 "svc" 만 남는다(받은 우편은 쓸기 대상이 아니다)
    service.shutdown();
    store.shutdown();
    (void)store.pollCompletions();
}
