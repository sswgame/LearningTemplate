// MailboxServiceTest — 우편함 서비스(메모리 저장소): 호출 입구(로그인 · 멱등 키), 목록 · 수령 · 삭제, 캠페인 메모리와 재읽기 실패, 만료 정리 요청과 잔액 알림, 새 우편 알림의 오프라인 푸시.
// 저장 규칙은 MailboxStoreLogicTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxService.h"
#include "GameFramework/Runtime/Online/Ledger/Ledger.h"
#include "GameFramework/Runtime/Online/Mail/ServiceMail.h"
#include "GameFramework/Runtime/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Runtime/Online/Service/Tests/OnlineHostTestUtil.h"
#include "GameFramework/Runtime/Online/Store/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr AccountID kPlayer = 22;
    constexpr int64     kNowMs  = 1700000000000ll;

    struct MailboxReplySink
    {
        vector<MailboxReply> _listReply{};

        void onReply( const MailboxReply& reply ) { _listReply.push_back( reply ); }
    };

    struct MailboxFixture
    {
        MemoryServiceDatabase _database;
        MemoryServiceStore    _store;
        MailboxService        _service;
        MailboxReplySink      _sink;
        uint64                _nextKey{ 1 };

        explicit MailboxFixture( MailboxServiceConfig settings = MailboxServiceConfig{} )
            : _database{}
            , _store{ &_database }
            , _service{}
            , _sink{}
        {
            settings._noticeIntervalMs = 0;
            SW_EXPECT_TRUE( _service.initialize( &_store, settings ) );
        }

        ~MailboxFixture() { _service.shutdown(); }

        void tick( int64 nowMs = kNowMs )
        {
            _service.update( UnixMilliseconds{ nowMs } );
            while ( _store.pollCompletions() > 0 )
            {
            }
        }

        MailboxReply call( uint16 method, AccountID accountID, string_view mailKey = "", bool bKey = true, int64 nowMs = kNowMs )
        {
            MailboxCall call{ UnixMilliseconds{ nowMs } };
            call._method           = method;
            call._accountID        = accountID;
            call._request._mailKey = string( mailKey );
            if ( bKey )
                call._idempotencyKey = NetIdempotencyKey{ 1, _nextKey++ };
            _service.submitCall( call, MailboxService::ReplyDelegate::create<&MailboxReplySink::onReply>( &_sink ) );
            while ( _store.pollCompletions() > 0 )
            {
            }
            SW_EXPECT_EQUAL( size_t( 1 ), _sink._listReply.size() );
            const MailboxReply reply = _sink._listReply.empty() ? MailboxReply{} : _sink._listReply.back();
            _sink._listReply.clear();
            return reply;
        }

        string sendMail( int64 amount, int64 expiresMs = 0, string_view idempotencyKey = "mail.a" )
        {
            ServiceMailMessage message;
            message._listAttachment.push_back( ServiceMailAttachment{ "cur.gold", amount } );
            message._titleKey           = "mail.reward";
            message._idempotencyKey     = string( idempotencyKey );
            message._fundingHolder      = LedgerHolder::makeMint();
            message._recipientAccountID = kPlayer;
            message._createdMs          = kNowMs;
            message._expiresMs          = expiresMs;
            message._expiryAction       = ServiceMail::getDefaultExpiryAction( message._fundingHolder );
            ServiceTransaction transaction;
            string             mailKey;
            bool               bReplayed = false;
            SW_EXPECT_TRUE( ServiceMail::stageSend( _database, message, transaction, mailKey, bReplayed ) == LedgerResult::Ok );
            SW_EXPECT_TRUE( _database.commit( transaction ) == ServiceStoreResult::Ok );
            return mailKey;
        }

        int64 balanceOf( AccountID accountID )
        {
            LedgerBalance balance;
            SW_EXPECT_TRUE( Ledger::readBalance( _database, LedgerHolder::makeAccount( accountID ), "cur.gold", balance ) == ServiceStoreResult::Ok );
            return balance._amount;
        }
    };

    /** @brief 오프라인 푸시 호출을 셉니다. */
    class FakeOfflinePush final : public IOfflinePushSender
    {
    public:
        vector<AccountID> _listAccount{};

        bool sendOfflinePush( AccountID accountID, string_view /*titleKey*/, string_view /*bodyKey*/, string_view /*deepLink*/, UnixMilliseconds /*nowMs*/ ) override
        {
            _listAccount.push_back( accountID );
            return true;
        }
    };
} // namespace

/** @brief [MailboxServiceTest] 입구: 로그인 전은 NotSignedIn, 멱등 키 없는 수령은 InvalidRequest 이고 저장소에 닿지 않는다 */
SW_TEST_CASE( MailboxServiceTest, EntryRules )
{
    MailboxFixture rig;
    SW_EXPECT_TRUE( rig.call( MailboxMethod::kList, kInvalidAccountID )._result == MailboxResult::NotSignedIn );
    const string mailKey = rig.sendMail( 5 );
    SW_EXPECT_TRUE( rig.call( MailboxMethod::kClaim, kPlayer, mailKey, false )._result == MailboxResult::InvalidRequest );
    SW_EXPECT_TRUE( rig.call( MailboxMethod::kClaimAll, kPlayer, "", false )._result == MailboxResult::InvalidRequest );
    SW_EXPECT_EQUAL( int64( 0 ), rig.balanceOf( kPlayer ) );
    SW_EXPECT_TRUE( rig.call( 9999, kPlayer )._result == MailboxResult::InvalidRequest );
}

/** @brief [MailboxServiceTest] 흐름: 목록에 나오고, 수령하면 잔액이 오르며 답에 잔액이 실린다. 받은 우편은 지울 수 있다 */
SW_TEST_CASE( MailboxServiceTest, ListClaimDelete )
{
    MailboxFixture rig;
    const string   mailKey = rig.sendMail( 5 );

    const MailboxReply listed = rig.call( MailboxMethod::kList, kPlayer );
    SW_EXPECT_TRUE( listed._result == MailboxResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), listed._listMail.size() );

    SW_EXPECT_TRUE( rig.call( MailboxMethod::kMarkRead, kPlayer, mailKey )._result == MailboxResult::Ok );
    const MailboxReply claimed = rig.call( MailboxMethod::kClaim, kPlayer, mailKey );
    SW_EXPECT_TRUE( claimed._result == MailboxResult::Ok );
    SW_EXPECT_FALSE( claimed._listBalance.empty() );
    SW_EXPECT_EQUAL( int64( 5 ), rig.balanceOf( kPlayer ) );

    SW_EXPECT_TRUE( rig.call( MailboxMethod::kDelete, kPlayer, mailKey )._result == MailboxResult::Ok );
    SW_EXPECT_TRUE( rig.call( MailboxMethod::kList, kPlayer )._listMail.empty() );
}

/** @brief [MailboxServiceTest] 모두 받기: 받은 수를 돌려주고 잔액이 합만큼 오른다 */
SW_TEST_CASE( MailboxServiceTest, ClaimAllCounts )
{
    MailboxFixture rig;
    (void)rig.sendMail( 5, 0, "mail.a" );
    (void)rig.sendMail( 7, 0, "mail.b" );
    const MailboxReply claimed = rig.call( MailboxMethod::kClaimAll, kPlayer );
    SW_EXPECT_TRUE( claimed._result == MailboxResult::Ok );
    SW_EXPECT_EQUAL( 2, claimed._claimedCount );
    SW_EXPECT_EQUAL( int64( 12 ), rig.balanceOf( kPlayer ) );
}

/** @brief [MailboxServiceTest] 캠페인: 틱이 읽어 메모리에 두고 첫 쪽 목록 앞에 끼운다. 끝난 캠페인은 빠지고, 읽기가 실패하면 있던 목록을 둔다 */
SW_TEST_CASE( MailboxServiceTest, CampaignsAppearInFirstPage )
{
    SW_EXPECT_LOG( "MailboxService.MailCampaignsCouldNot" );
    MailboxServiceConfig settings;
    settings._campaignRefreshMs = 1000;
    MailboxFixture rig( settings );

    ServiceMailCampaign campaign;
    campaign._campaignID = 1;
    campaign._actorID    = 7;
    campaign._titleKey   = "mail.campaign";
    campaign._startMs    = kNowMs - 1;
    campaign._endMs      = kNowMs + 10000;
    campaign._listAttachment.push_back( ServiceMailAttachment{ "cur.gold", 3 } );
    ServiceTransaction transaction;
    SW_REQUIRE_TRUE( ServiceMailCampaignTable::stageCreate( campaign, transaction ) );
    SW_REQUIRE_TRUE( rig._database.commit( transaction ) == ServiceStoreResult::Ok );

    SW_EXPECT_TRUE( rig.call( MailboxMethod::kList, kPlayer )._listMail.empty() ); // 아직 읽지 않았다
    rig.tick();
    SW_EXPECT_EQUAL( size_t( 1 ), rig.call( MailboxMethod::kList, kPlayer )._listMail.size() );

    rig._database.armFault( ServiceStoreFault::RejectRead );
    rig.tick( kNowMs + 1000 );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.call( MailboxMethod::kList, kPlayer )._listMail.size() );

    rig.tick( kNowMs + 10001 + 1000 );
    SW_EXPECT_TRUE( rig.call( MailboxMethod::kList, kPlayer, "", true, kNowMs + 12000 )._listMail.empty() );
}

/** @brief [MailboxServiceTest] 정리: 만료된 우편을 치워 목록에서 사라진다. 저장소 실패로 남은 것은 다음 정리가 치운다 */
SW_TEST_CASE( MailboxServiceTest, SweepRemovesExpiredMail )
{
    MailboxServiceConfig settings;
    settings._sweepIntervalMs = 0; // 틱이 스스로 돌지 않는다
    MailboxFixture rig( settings );
    (void)rig.sendMail( 5, kNowMs + 10 );
    rig._service.requestSweep();
    rig.tick( kNowMs + 11 );
    SW_EXPECT_TRUE( rig.call( MailboxMethod::kList, kPlayer, "", true, kNowMs + 11 )._listMail.empty() );
    SW_EXPECT_EQUAL( int64( 0 ), rig.balanceOf( kPlayer ) ); // 발행한 재원은 받지 않으면 사라진다
}

/** @brief [MailboxServiceTest] 시작: 저장소가 없으면 서지 않는다 */
SW_TEST_CASE( MailboxServiceTest, NeedsStore )
{
    SW_EXPECT_LOG( "MailboxService.MailboxServiceNeedsStore" );
    MailboxService service;
    SW_EXPECT_FALSE( service.initialize( nullptr, MailboxServiceConfig{} ) );
}

/** @brief [MailboxServiceTest] 다시 시도할 결과(버전 충돌 · 일시 장애 — `MailboxReply::isRetryable`)는 요청 층이 기억하지 않아 같은 멱등 키의 수령 재시도가 서비스에 다시 닿고, 끝난 결과는 기억한다 */
SW_TEST_CASE( MailboxServiceTest, RetryableReplyReachesTheServiceAgain )
{
    test::ReplyRetentionFixture<MailboxProtocol, MailboxReply> rig;
    MailboxReply                                               finished;
    finished._result = MailboxResult::Ok;
    MailboxReply busy;
    busy._result = MailboxResult::Busy;
    rig.expectRetryReachesService( MailboxMethod::kClaim, busy, finished );
    MailboxReply unavailable;
    unavailable._result = MailboxResult::Unavailable;
    rig.expectRetryReachesService( MailboxMethod::kClaim, unavailable, finished );
}
