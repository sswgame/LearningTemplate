#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/Ledger/Ledger.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"

// ServiceMailTest — 우편 넣기(보상 · 플레이어 첨부 맡김), 멱등 키 재전송, 새 우편 알림이 한 번만 나가는지. (nogpu)

namespace
{
    constexpr uint64 kSender    = 11;
    constexpr uint64 kRecipient = 22;
    constexpr int64  kNowMs     = 1700000000000ll;

    sw::ServiceMailMessage makeReward( sw::string_view idempotencyKey, int64 amount )
    {
        sw::ServiceMailMessage message;
        message._listAttachment.push_back( sw::ServiceMailAttachment{ "gold", amount } );
        message._titleKey           = "mail.reward";
        message._idempotencyKey     = sw::string( idempotencyKey );
        message._fundingHolder      = sw::LedgerHolder::makeMint();
        message._recipientAccountID = kRecipient;
        message._createdMs          = kNowMs;
        return message;
    }

    sw::LedgerResult send( sw::MemoryServiceDatabase& database, const sw::ServiceMailMessage& message, sw::string& outMailKey, bool& outbReplayed )
    {
        sw::ServiceTransaction transaction;
        const sw::LedgerResult staged = sw::ServiceMail::stageSend( database, message, transaction, outMailKey, outbReplayed );
        if ( staged != sw::LedgerResult::Ok || outbReplayed )
            return staged;
        return database.commit( transaction ) == sw::ServiceStoreResult::Ok ? sw::LedgerResult::Ok : sw::LedgerResult::Unavailable;
    }

    void grantGold( sw::MemoryServiceDatabase& database, uint64 accountID, int64 amount )
    {
        sw::LedgerTransferRequest request;
        request._listPosting.push_back( sw::LedgerPosting{ sw::LedgerHolder::makeMint(), sw::LedgerHolder::makeAccount( accountID ), "gold", amount } );
        request._journalKey = "test/grant";
        request._reason     = "test.grant";
        sw::LedgerTransferOutcome outcome;
        SW_EXPECT_TRUE( sw::Ledger::executeTransfer( database, request, outcome ) == sw::LedgerResult::Ok );
    }
} // namespace

/**
 * @brief [ServiceMailTest] 보상 우편은 레코드 · 알림을 남기고, 같은 멱등 키로 다시 보내면 새 우편 없이 재생이다. 알림은 한 번만 가져간다
 */
SW_TEST_CASE( ServiceMailTest, RewardMailIsSentOnce )
{
    sw::MemoryServiceDatabase    database;
    const sw::ServiceMailMessage reward = makeReward( "reward.quest1.22", 50 );
    sw::string                   mailKey;
    bool                         bReplayed = false;
    SW_REQUIRE_TRUE( send( database, reward, mailKey, bReplayed ) == sw::LedgerResult::Ok );
    SW_EXPECT_FALSE( bReplayed );
    const int32 mailCount = database.countRecords( sw::ServiceMail::getMailTable() );
    SW_EXPECT_EQUAL( 1, mailCount );

    uint64 recipient = 0;
    SW_EXPECT_TRUE( sw::ServiceMail::parseRecipient( mailKey, recipient ) );
    SW_EXPECT_EQUAL( kRecipient, recipient );
    sw::ServiceRecord stored;
    SW_REQUIRE_TRUE( database.readRecord( sw::ServiceMail::getMailTable(), mailKey, stored ) == sw::ServiceStoreResult::Ok );
    sw::ServiceMailRecord record;
    SW_REQUIRE_TRUE( sw::ServiceMail::decodeRecord( stored._bytes, record ) );
    SW_EXPECT_EQUAL( reward._titleKey, record._message._titleKey );
    SW_REQUIRE_EQUAL( size_t( 1 ), record._message._listAttachment.size() );
    SW_EXPECT_EQUAL( int64( 50 ), record._message._listAttachment[0]._amount );

    sw::string again;
    SW_REQUIRE_TRUE( send( database, reward, again, bReplayed ) == sw::LedgerResult::Ok );
    SW_EXPECT_TRUE( bReplayed );
    SW_EXPECT_EQUAL( mailCount, database.countRecords( sw::ServiceMail::getMailTable() ) );

    sw::vector<sw::ServiceMailNotice> listNotice;
    SW_REQUIRE_TRUE( sw::ServiceMail::claimNotices( database, 10, listNotice ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( size_t( 1 ), listNotice.size() );
    SW_REQUIRE_TRUE( sw::ServiceMail::claimNotices( database, 10, listNotice ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( listNotice.empty() );
}

/**
 * @brief [ServiceMailTest] 플레이어가 보낸 첨부는 보낸 계정에서 우편 맡김으로 옮겨지고, 모자라면 아무것도 붙이지 않는다. 감사는 균형이다
 */
SW_TEST_CASE( ServiceMailTest, PlayerAttachmentsGoToEscrow )
{
    sw::MemoryServiceDatabase database;
    grantGold( database, kSender, 30 );

    sw::ServiceMailMessage gift = makeReward( "gift.1", 20 );
    gift._fundingHolder         = sw::LedgerHolder::makeAccount( kSender );
    gift._actorKind             = sw::LedgerActorKind::Player;
    gift._expiryAction          = sw::ServiceMailExpiryAction::ReturnToSender;
    sw::string mailKey;
    bool       bReplayed = false;
    SW_REQUIRE_TRUE( send( database, gift, mailKey, bReplayed ) == sw::LedgerResult::Ok );

    sw::LedgerBalance balance;
    SW_REQUIRE_TRUE( sw::Ledger::readBalance( database, sw::LedgerHolder::makeAccount( kSender ), "gold", balance ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( int64( 10 ), balance._amount );
    SW_REQUIRE_TRUE( sw::Ledger::readBalance( database, sw::ServiceMail::makeEscrowHolder( mailKey ), "gold", balance ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( int64( 20 ), balance._amount );

    sw::ServiceMailMessage tooMuch = makeReward( "gift.2", 20 );
    tooMuch._fundingHolder         = sw::LedgerHolder::makeAccount( kSender );
    tooMuch._actorKind             = sw::LedgerActorKind::Player;
    sw::ServiceTransaction transaction;
    SW_EXPECT_TRUE( sw::ServiceMail::stageSend( database, tooMuch, transaction, mailKey, bReplayed ) == sw::LedgerResult::InsufficientFunds );
    SW_EXPECT_TRUE( transaction.isEmpty() );

    sw::LedgerAuditReport report;
    SW_REQUIRE_TRUE( sw::LedgerAudit::computeReport( database, report ) == sw::ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( report.isBalanced() );
}

/**
 * @brief [ServiceMailTest] 멱등 키 없음 · 첨부 상한 초과 · 0 이하 첨부는 보낼 수 없는 우편이다
 */
SW_TEST_CASE( ServiceMailTest, RejectsInvalidMessages )
{
    SW_EXPECT_TRUE( sw::ServiceMail::isValidMessage( makeReward( "ok.1", 1 ) ) );
    SW_EXPECT_FALSE( sw::ServiceMail::isValidMessage( makeReward( "", 1 ) ) );
    SW_EXPECT_FALSE( sw::ServiceMail::isValidMessage( makeReward( "ok.2", 0 ) ) );
    sw::ServiceMailMessage crowded = makeReward( "ok.3", 1 );
    crowded._listAttachment.resize( static_cast<size_t>( sw::ServiceMailConstant::kMaxAttachmentCount + 1 ), sw::ServiceMailAttachment{ "gold", 1 } );
    SW_EXPECT_FALSE( sw::ServiceMail::isValidMessage( crowded ) );
}
