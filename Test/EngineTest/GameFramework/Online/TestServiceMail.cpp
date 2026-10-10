// 우편 넣기 — 발행 재원은 넣을 때 아무것도 안 움직임, 계정 재원은 맡김으로, 같은 멱등 키 한 번, 재원 모자람, 레코드 왕복 · 만료 색인, 기본 만료 처리.
#include "pch.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    constexpr uint64 kSender    = 0x51;
    constexpr uint64 kRecipient = 0x52;

    ServiceMailMessage makeMessage( const utf8* pIdempotencyKey, const LedgerHolder& funding, int64 amount )
    {
        ServiceMailMessage message;
        message._recipientAccountID = kRecipient;
        message._idempotencyKey     = pIdempotencyKey;
        message._fundingHolder      = funding;
        message._titleKey           = "mail.title.reward";
        message._createdMs          = 1000;
        message._expiresMs          = 1000 + ServiceMailConstant::kDefaultRetentionMs;
        message._expiryAction       = ServiceMail::getDefaultExpiryAction( funding );
        message._listAttachment.push_back( ServiceMailAttachment{ "cur.gold", amount } );
        return message;
    }

    LedgerResult send( MemoryServiceDatabase& database, const ServiceMailMessage& message, string& outMailKey, bool& outbReplayed )
    {
        ServiceTransaction transaction;
        const LedgerResult staged = ServiceMail::stageSend( database, message, transaction, outMailKey, outbReplayed );
        if ( staged != LedgerResult::Ok || outbReplayed )
            return staged;
        return database.commit( transaction ) == ServiceStoreResult::Ok ? LedgerResult::Ok : LedgerResult::Unavailable;
    }

    void grant( MemoryServiceDatabase& database, uint64 accountID, int64 amount )
    {
        LedgerTransferRequest request;
        request._journalKey = "test/seed";
        request._reason     = "test.grant";
        request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeMint(), LedgerHolder::makeAccount( accountID ), "cur.gold", amount } );
        LedgerTransferOutcome outcome;
        (void)Ledger::executeTransfer( database, request, outcome );
    }
} // namespace

SW_TEST_CASE( ServiceMailTest, MintFundedMailMovesNothingUntilClaim )
{
    MemoryServiceDatabase database;
    string                mailKey;
    bool                  bReplayed = false;
    SW_ASSERT_TRUE( send( database, makeMessage( "reward.q1", LedgerHolder::makeMint(), 500 ), mailKey, bReplayed ) == LedgerResult::Ok );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 1 );
    SW_EXPECT_EQUAL( database.countRecords( Ledger::getJournalTable() ), 0 );
    uint64 recipient = 0;
    SW_EXPECT_TRUE( ServiceMail::parseRecipient( mailKey, recipient ) );
    SW_EXPECT_EQUAL( recipient, kRecipient );
}

SW_TEST_CASE( ServiceMailTest, AccountFundedMailEscrowsAtSend )
{
    MemoryServiceDatabase database;
    grant( database, kSender, 100 );
    const ServiceMailMessage message = makeMessage( "gift.1", LedgerHolder::makeAccount( kSender ), 40 );
    SW_EXPECT_TRUE( message._expiryAction == ServiceMailExpiryAction::ReturnToSender ); // 플레이어 우편은 만료 때 돌려준다
    string mailKey;
    bool   bReplayed = false;
    SW_ASSERT_TRUE( send( database, message, mailKey, bReplayed ) == LedgerResult::Ok );
    LedgerBalance senderBalance;
    LedgerBalance escrowBalance;
    SW_ASSERT_TRUE( Ledger::readBalance( database, LedgerHolder::makeAccount( kSender ), "cur.gold", senderBalance ) == ServiceStoreResult::Ok );
    SW_ASSERT_TRUE( Ledger::readBalance( database, ServiceMail::makeEscrowHolder( mailKey ), "cur.gold", escrowBalance ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( senderBalance._amount, int64( 60 ) );
    SW_EXPECT_EQUAL( escrowBalance._amount, int64( 40 ) );
    LedgerAuditReport report;
    SW_ASSERT_TRUE( LedgerAudit::computeReport( database, report ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( report.isBalanced() );
}

SW_TEST_CASE( ServiceMailTest, SameIdempotencyKeySendsOnce )
{
    MemoryServiceDatabase database;
    string                firstKey;
    string                secondKey;
    bool                  bReplayed = false;
    SW_ASSERT_TRUE( send( database, makeMessage( "admin.7.1", LedgerHolder::makeMint(), 5 ), firstKey, bReplayed ) == LedgerResult::Ok );
    SW_EXPECT_FALSE( bReplayed );
    ServiceMailMessage retry = makeMessage( "admin.7.1", LedgerHolder::makeMint(), 5 );
    retry._createdMs         = 2000; // 재시도는 다른 시각에 온다 — 그래도 같은 우편
    SW_ASSERT_TRUE( send( database, retry, secondKey, bReplayed ) == LedgerResult::Ok );
    SW_EXPECT_TRUE( bReplayed );
    SW_EXPECT_EQUAL( secondKey, firstKey );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 1 );
}

SW_TEST_CASE( ServiceMailTest, InsufficientFundingSendsNothing )
{
    MemoryServiceDatabase database;
    grant( database, kSender, 10 );
    const uint64 before = database.computeContentHash();
    string       mailKey;
    bool         bReplayed = false;
    SW_EXPECT_TRUE( send( database, makeMessage( "gift.2", LedgerHolder::makeAccount( kSender ), 11 ), mailKey, bReplayed ) == LedgerResult::InsufficientFunds );
    SW_EXPECT_EQUAL( database.computeContentHash(), before );
    const ServiceMailMessage invalid = makeMessage( "Bad Key", LedgerHolder::makeMint(), 1 );
    SW_EXPECT_TRUE( send( database, invalid, mailKey, bReplayed ) == LedgerResult::Invalid );
    const ServiceMailMessage selfFunded = makeMessage( "self", LedgerHolder::makeAccount( kRecipient ), 1 );
    SW_EXPECT_TRUE( send( database, selfFunded, mailKey, bReplayed ) == LedgerResult::Invalid );
    ServiceMailMessage returnMint = makeMessage( "ret", LedgerHolder::makeMint(), 1 );
    returnMint._expiryAction      = ServiceMailExpiryAction::ReturnToSender; // 돌려줄 보낸 계정이 없다
    SW_EXPECT_TRUE( send( database, returnMint, mailKey, bReplayed ) == LedgerResult::Invalid );
    SW_EXPECT_EQUAL( database.computeContentHash(), before );
}

SW_TEST_CASE( ServiceMailTest, RecordRoundTripsAndExpiryIsIndexed )
{
    MemoryServiceDatabase database;
    ServiceMailMessage    message = makeMessage( "reward.q2", LedgerHolder::makeMint(), 3 );
    message._body                 = "본문 — UTF-8";
    message._senderName           = "mail.sender.ops";
    SW_EXPECT_TRUE( message._expiryAction == ServiceMailExpiryAction::Discard ); // 운영 · 보상 우편은 만료 때 첨부가 사라진다
    string mailKey;
    bool   bReplayed = false;
    SW_ASSERT_TRUE( send( database, message, mailKey, bReplayed ) == LedgerResult::Ok );
    ServiceRecord record;
    SW_ASSERT_TRUE( database.readRecord( ServiceMail::getMailTable(), mailKey, record ) == ServiceStoreResult::Ok );
    ServiceMailRecord decoded;
    SW_ASSERT_TRUE( ServiceMail::decodeRecord( record._bytes, decoded ) );
    SW_EXPECT_EQUAL( decoded._message._body, message._body );
    SW_ASSERT_EQUAL( decoded._message._listAttachment.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( decoded._message._listAttachment[0]._amount, int64( 3 ) );
    SW_EXPECT_TRUE( decoded._state == ServiceMailState::Unread );
    SW_EXPECT_TRUE( decoded._message._expiryAction == ServiceMailExpiryAction::Discard );
    ServiceRecord expiry;
    SW_EXPECT_TRUE( database.readRecord( ServiceMail::getExpiryTable(), ServiceMail::makeExpiryKey( message._expiresMs, mailKey ), expiry ) == ServiceStoreResult::Ok );
}
