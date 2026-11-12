// MailboxStoreLogicTest — 우편함 저장 규칙(메모리 저장소): 수령은 원장을 한 번만 움직이고(두 번째는 AlreadyClaimed), 받지 않은 첨부가 있는 우편은 지울 수 없고,
// 상한을 넘는 수령은 우편을 그대로 두며(CapExceeded), 남의 우편은 NotFound, 모두 받기는 상한에 걸린 우편만 남기고, 받은 우편은 보관 기간 뒤 만료 정리가 지운다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxStoreLogic.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr uint64 kRecipient = 22;
    constexpr uint64 kStranger  = 33;
    constexpr int64  kNowMs     = 1700000000000ll;
    constexpr int64  kClaimedMs = 1000000; ///< 받은 우편을 남기는 기간(테스트용으로 짧다)

    /** @brief 자산마다 같은 상한입니다(0 이하면 상한 없음). */
    class FixedCapPolicy final : public ILedgerPolicy
    {
    public:
        explicit FixedCapPolicy( int64 cap )
            : _cap{ cap }
        {
        }
        int64 getBalanceCap( string_view /*assetID*/ ) const override { return _cap; }
        bool  isTransferable( string_view /*assetID*/ ) const override { return true; }

    private:
        int64 _cap;
    };

    string sendReward( MemoryServiceDatabase& database, string_view idempotencyKey, string_view assetID, int64 amount, int64 expiresMs = 0 )
    {
        ServiceMailMessage message;
        if ( amount > 0 )
            message._listAttachment.push_back( ServiceMailAttachment{ string( assetID ), amount } );
        message._titleKey           = "mail.reward";
        message._idempotencyKey     = string( idempotencyKey );
        message._fundingHolder      = LedgerHolder::makeMint();
        message._recipientAccountID = kRecipient;
        message._createdMs          = kNowMs;
        message._expiresMs          = expiresMs;
        ServiceTransaction transaction;
        string             mailKey;
        bool               bReplayed = false;
        SW_EXPECT_TRUE( ServiceMail::stageSend( database, message, transaction, mailKey, bReplayed ) == LedgerResult::Ok );
        SW_EXPECT_TRUE( database.commit( transaction ) == ServiceStoreResult::Ok );
        return mailKey;
    }

    int64 readBalance( MemoryServiceDatabase& database, uint64 accountID, string_view assetID )
    {
        LedgerBalance balance;
        SW_EXPECT_TRUE( Ledger::readBalance( database, LedgerHolder::makeAccount( accountID ), assetID, balance ) == ServiceStoreResult::Ok );
        return balance._amount;
    }

    MailboxServiceResult claim( MemoryServiceDatabase& database, uint64 accountID, const string& mailKey, const ILedgerPolicy& policy )
    {
        MailboxClaimInput input;
        input._mailKey            = mailKey;
        input._pPolicy            = &policy;
        input._accountID          = accountID;
        input._nowMs              = kNowMs + 1000;
        input._claimedRetentionMs = kClaimedMs;
        MailboxClaimReply reply;
        return MailboxStoreLogic::claim( database, input, reply );
    }
} // namespace

/** @brief [MailboxStoreLogicTest] 수령은 원장을 한 번만 움직인다 — 두 번째 수령은 AlreadyClaimed 이고 잔액이 그대로다(분개 키가 막는다) */
SW_TEST_CASE( MailboxStoreLogicTest, ClaimMovesTheLedgerOnce )
{
    MemoryServiceDatabase database;
    const FixedCapPolicy  policy{ 0 };
    const string          mailKey = sendReward( database, "reward.once", "gold", 50 );
    SW_EXPECT_TRUE( claim( database, kRecipient, mailKey, policy ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( readBalance( database, kRecipient, "gold" ), int64( 50 ) );
    SW_EXPECT_TRUE( claim( database, kRecipient, mailKey, policy ) == MailboxResult::AlreadyClaimed );
    SW_EXPECT_EQUAL( readBalance( database, kRecipient, "gold" ), int64( 50 ) );
}

/** @brief [MailboxStoreLogicTest] 남의 우편은 받을 수도 지울 수도 없다(NotFound — 있는지도 알리지 않는다) */
SW_TEST_CASE( MailboxStoreLogicTest, OtherAccountsSeeNotFound )
{
    MemoryServiceDatabase database;
    const FixedCapPolicy  policy{ 0 };
    const string          mailKey = sendReward( database, "reward.other", "gold", 5 );
    SW_EXPECT_TRUE( claim( database, kStranger, mailKey, policy ) == MailboxResult::NotFound );
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( database, kStranger, mailKey ) == MailboxResult::NotFound );
    SW_EXPECT_EQUAL( readBalance( database, kStranger, "gold" ), int64( 0 ) );
}

/** @brief [MailboxStoreLogicTest] 받지 않은 첨부가 있으면 지울 수 없고(재화가 사라진다), 받은 뒤 · 첨부 없는 우편은 지운다 */
SW_TEST_CASE( MailboxStoreLogicTest, DeleteRefusesUnclaimedAttachments )
{
    MemoryServiceDatabase database;
    const FixedCapPolicy  policy{ 0 };
    const string          withGold = sendReward( database, "reward.gold", "gold", 7 );
    const string          noteOnly = sendReward( database, "notice.only", "gold", 0 );
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( database, kRecipient, withGold ) == MailboxResult::HasAttachments );
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( database, kRecipient, noteOnly ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( claim( database, kRecipient, withGold, policy ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( database, kRecipient, withGold ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 0 );
}

/** @brief [MailboxStoreLogicTest] 상한을 넘는 수령은 CapExceeded 이고 우편 · 잔액이 그대로다. 모두 받기는 상한 안의 우편만 받고 나머지를 남긴다 */
SW_TEST_CASE( MailboxStoreLogicTest, CapExceededLeavesTheMail )
{
    MemoryServiceDatabase database;
    const FixedCapPolicy  policy{ 100 };
    const string          small = sendReward( database, "reward.small", "gold", 60 );
    const string          large = sendReward( database, "reward.large", "gold", 90 );
    SW_EXPECT_TRUE( claim( database, kRecipient, small, policy ) == MailboxResult::Ok );
    const uint64 hashBefore = database.computeContentHash();
    SW_EXPECT_TRUE( claim( database, kRecipient, large, policy ) == MailboxResult::CapExceeded );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashBefore );
    SW_EXPECT_EQUAL( readBalance( database, kRecipient, "gold" ), int64( 60 ) );

    MemoryServiceDatabase batch;
    (void)sendReward( batch, "batch.a", "gold", 60 );
    (void)sendReward( batch, "batch.b", "gold", 90 );
    (void)sendReward( batch, "batch.c", "gems", 3 );
    MailboxClaimReply reply;
    SW_EXPECT_TRUE( MailboxStoreLogic::claimAll( batch, kRecipient, kNowMs + 1000, kClaimedMs, &policy, reply ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( reply._claimedCount, 2 ); // 금 하나 + 보석, 남은 금 우편은 상한에 걸린다
    SW_EXPECT_TRUE( readBalance( batch, kRecipient, "gold" ) <= 100 );
    SW_EXPECT_EQUAL( readBalance( batch, kRecipient, "gems" ), int64( 3 ) );
}

/** @brief [MailboxStoreLogicTest] 받은 우편(만료 있음 · 없음)은 받은 뒤 보관 기간이 지나면 만료 정리가 지운다. 그 전에는 남고, 지운 뒤 잔액은 그대로이며 다시 받으면 NotFound 이다 */
SW_TEST_CASE( MailboxStoreLogicTest, SweepRemovesClaimedMailAfterRetention )
{
    MemoryServiceDatabase database;
    const FixedCapPolicy  policy{ 0 };
    const string          expiring = sendReward( database, "reward.expiring", "gold", 5, kNowMs + 100000 );
    const string          forever  = sendReward( database, "reward.forever", "gold", 7 );
    SW_EXPECT_TRUE( claim( database, kRecipient, expiring, policy ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( claim( database, kRecipient, forever, policy ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getExpiryTable() ), 2 ); // 둘 다 "받은 뒤 보관 기간" 색인 하나씩이다

    const int64       purgeMs = kNowMs + 1000 + kClaimedMs;
    MailboxSweepStats stats;
    MailboxStoreLogic::sweepExpired( database, purgeMs - 1, 16, stats );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 2 ); // 원래 만료(+100 초)가 지났어도 받은 우편은 보관 기간까지 남는다

    MailboxStoreLogic::sweepExpired( database, purgeMs, 16, stats );
    SW_EXPECT_EQUAL( stats._removedCount, 2 );
    SW_EXPECT_EQUAL( stats._failedCount, 0 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 0 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getExpiryTable() ), 0 );
    SW_EXPECT_EQUAL( readBalance( database, kRecipient, "gold" ), int64( 12 ) );
    SW_EXPECT_TRUE( claim( database, kRecipient, expiring, policy ) == MailboxResult::NotFound );
    SW_EXPECT_EQUAL( readBalance( database, kRecipient, "gold" ), int64( 12 ) );
}

/** @brief [MailboxStoreLogicTest] 받은 우편을 사용자가 지우면 보관 기간 색인도 함께 지워진다. 보관 기간이 없는 수령은 거절된다(설정 필수 값) */
SW_TEST_CASE( MailboxStoreLogicTest, ClaimedMailDeleteAndMissingRetention )
{
    MemoryServiceDatabase database;
    const FixedCapPolicy  policy{ 0 };
    const string          mailKey = sendReward( database, "reward.delete", "gold", 3 );
    MailboxClaimInput     input;
    input._mailKey   = mailKey;
    input._pPolicy   = &policy;
    input._accountID = kRecipient;
    input._nowMs     = kNowMs + 1000;
    MailboxClaimReply reply;
    SW_EXPECT_TRUE( MailboxStoreLogic::claim( database, input, reply ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( readBalance( database, kRecipient, "gold" ), int64( 0 ) );

    SW_EXPECT_TRUE( claim( database, kRecipient, mailKey, policy ) == MailboxResult::Ok );
    SW_EXPECT_TRUE( MailboxStoreLogic::deleteMail( database, kRecipient, mailKey ) == MailboxResult::Ok );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getMailTable() ), 0 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceMail::getExpiryTable() ), 0 );
}
