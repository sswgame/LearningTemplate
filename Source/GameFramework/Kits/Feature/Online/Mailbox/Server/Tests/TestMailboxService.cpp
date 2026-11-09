// MailboxServiceTest — 우편함 끝단(클라이언트 · 호스트 · 서비스 · 메모리 저장소): 입구(로그인 · 멱등 키 · 깨진 본문 · 모르는 메서드), 목록 · 수령 · 삭제와 클라이언트 캐시, 캠페인 메모리와 재읽기 실패,
// 만료 정리, 공통 범주의 재시도와 응답 기억, 새 우편 알림과 오프라인 푸시, 지표. 저장 규칙은 MailboxStoreLogicTest 가 본다. 프로토콜 서술자로 옮긴 키트의 본보기이다(nogpu).
#include "pch.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxClient.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxService.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Mail/ServiceMailCampaign.h"
#include "GameFramework/Runtime/Online/Ledger/Ledger.h"
#include "GameFramework/Runtime/Online/Mail/ServiceMail.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineRuntimeTestUtil.h"
#include "GameFramework/Runtime/Online/Service/Tests/OnlineHostTestUtil.h"
#include "GameFramework/Runtime/Online/Store/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr AccountID kPlayer = 22;
    constexpr int64     kNowMs  = 1700000000000ll;

    /** @brief 호스트와 클라이언트까지 실제로 도는 묶음입니다. 선언 순서가 수명이다(서비스가 망보다 오래 산다). */
    struct MailboxFixture
    {
        MailboxService          _service{};
        test::OnlineTestFixture _rig{ false };
        MailboxClient           _client{};
        int32                   _clientIndex{ -1 };

        explicit MailboxFixture( MailboxServiceConfig settings = MailboxServiceConfig{}, bool bLogin = true )
        {
            SW_EXPECT_TRUE( _service.initialize( &_rig._server._store, settings ) );
            SW_EXPECT_TRUE( _rig._server._host.registerServerService( &_service ) );
            _rig._server.start();
            _clientIndex = _rig._clients.connect( _rig._server._port, { &_client } );
            _client.initialize( &_rig._clients.getClient( _clientIndex ) );
            tick();
            if ( bLogin )
            {
                _rig._clients.login( _clientIndex, kPlayer );
                tick();
            }
        }

        ~MailboxFixture() { _service.shutdown(); }

        void tick( int64 nowMs = kNowMs ) { test::updateAll( { &_rig._server }, _rig._clients, nowMs ); }

        template <typename TMethod>
        OnlineResult<typename TMethod::Reply> call( const typename TMethod::Request& request, const NetIdempotencyKey& key = NetIdempotencyKey{}, int64 nowMs = kNowMs )
        {
            using Result    = OnlineResult<typename TMethod::Reply>;
            Result result   = {};
            bool   bReplied = false;
            (void)_client.template call<TMethod>( request, OnlineReplyDelegate<TMethod>{ [&result, &bReplied]( const Result& reply )
            {
                result   = reply;
                bReplied = true;
            } },
                                                  key );
            tick( nowMs );
            SW_EXPECT_TRUE( bReplied );
            return result;
        }

        OnlineResult<MailboxListReply> list( int64 nowMs = kNowMs ) { return call<MailboxMethod::List>( MailboxListRequest{}, NetIdempotencyKey{}, nowMs ); }

        /** @brief 클라이언트 창구를 거치지 않고 본문을 그대로 보내 공통 오류를 받습니다(창구는 키 없는 수령 · 깨진 본문을 만들지 않는다). */
        uint16 sendRaw( uint16 method, const BitWriter& body, const NetRequestOptions& options = NetRequestOptions{} )
        {
            uint16 errorCode = OnlineError::kOk;
            bool   bReplied  = false;
            (void)_rig._clients.getClient( _clientIndex ).sendRequest( method, body, options, OnlineResponseDelegate{ [&errorCode, &bReplied]( const OnlineResponse& response )
            {
                errorCode = response._errorCode;
                bReplied  = true;
            } } );
            tick();
            SW_EXPECT_TRUE( bReplied );
            return errorCode;
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
            SW_EXPECT_TRUE( ServiceMail::stageSend( _rig._database, message, transaction, mailKey, bReplayed ) == LedgerResult::Ok );
            SW_EXPECT_TRUE( _rig._database.commit( transaction ) == ServiceStoreResult::Ok );
            return mailKey;
        }

        int64 balanceOf( AccountID accountID )
        {
            LedgerBalance balance;
            SW_EXPECT_TRUE( Ledger::readBalance( _rig._database, LedgerHolder::makeAccount( accountID ), "cur.gold", balance ) == ServiceStoreResult::Ok );
            return balance._amount;
        }
    };

    struct MailboxServiceTestUtil
    {
        /** @brief 새 우편 알림을 돌지 않는 설정입니다(알림을 보는 케이스만 켠다). */
        static MailboxServiceConfig makeQuietSettings()
        {
            MailboxServiceConfig settings;
            settings._noticeIntervalMs = 0;
            return settings;
        }

        static MailboxMailRequest makeMailRequest( string_view mailKey )
        {
            MailboxMailRequest request;
            request._mailKey = string( mailKey );
            return request;
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

/** @brief [MailboxServiceTest] 입구: 로그인 전은 kUnauthenticated, 키 없는 수령 · 깨진 본문 · 모르는 메서드는 kInvalidRequest 이고 저장소에 닿지 않는다 */
SW_TEST_CASE( MailboxServiceTest, EntryRules )
{
    {
        MailboxFixture anonymous( MailboxServiceTestUtil::makeQuietSettings(), false );
        SW_EXPECT_EQUAL( OnlineError::kUnauthenticated, anonymous.list()._errorCode );
    }
    MailboxFixture rig( MailboxServiceTestUtil::makeQuietSettings() );
    const string   mailKey = rig.sendMail( 5 );

    BitWriter claimBody;
    SW_REQUIRE_TRUE( MailboxProtocol::MessageTypes::write( claimBody, MailboxServiceTestUtil::makeMailRequest( mailKey ) ) );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( MailboxMethod::Claim::kID, claimBody ) );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( MailboxMethod::ClaimAll::kID, BitWriter{} ) );
    SW_EXPECT_EQUAL( int64( 0 ), rig.balanceOf( kPlayer ) );

    BitWriter brokenBody;
    brokenBody.writeVarUint( 7 );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( MailboxMethod::List::kID, brokenBody ) );
    SW_EXPECT_EQUAL( OnlineError::kInvalidRequest, rig.sendRaw( static_cast<uint16>( OnlineMethodRange::kMailbox + 0x7F ), BitWriter{} ) );
}

/** @brief [MailboxServiceTest] 흐름: 목록이 캐시를 채우고, 읽음 · 수령 · 지우기가 그 우편을 바로 고치며, 수령하면 잔액이 오르고 답에 잔액이 실린다 */
SW_TEST_CASE( MailboxServiceTest, ListClaimDelete )
{
    MailboxFixture rig( MailboxServiceTestUtil::makeQuietSettings() );
    const string   mailKey = rig.sendMail( 5 );

    const OnlineResult<MailboxListReply> listed = rig.list();
    SW_EXPECT_TRUE( listed.isOk() );
    SW_REQUIRE_EQUAL( size_t( 1 ), listed._reply._listMail.size() );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._client.getMails().size() );
    SW_EXPECT_EQUAL( 1, rig._client.countUnread() );

    SW_EXPECT_TRUE( rig.call<MailboxMethod::MarkRead>( MailboxServiceTestUtil::makeMailRequest( mailKey ) ).isOk() );
    SW_EXPECT_EQUAL( 0, rig._client.countUnread() );

    const OnlineResult<MailboxClaimReply> claimed = rig.call<MailboxMethod::Claim>( MailboxServiceTestUtil::makeMailRequest( mailKey ) );
    SW_EXPECT_TRUE( claimed.isOk() );
    SW_EXPECT_FALSE( claimed._reply._listBalance.empty() );
    SW_EXPECT_TRUE( claimed._idempotencyKey.isValid() ); // 키를 넘기지 않아도 창구가 만들었다
    SW_EXPECT_EQUAL( int64( 5 ), rig.balanceOf( kPlayer ) );
    SW_EXPECT_TRUE( rig._client.getMails()[0]._state == ServiceMailState::Claimed );

    SW_EXPECT_TRUE( rig.call<MailboxMethod::Delete>( MailboxServiceTestUtil::makeMailRequest( mailKey ) ).isOk() );
    SW_EXPECT_TRUE( rig._client.getMails().empty() );
    SW_EXPECT_TRUE( rig.list()._reply._listMail.empty() );
}

/** @brief [MailboxServiceTest] 업무 결과는 공통 범주가 아니라 응답의 결과 칸으로 온다 */
SW_TEST_CASE( MailboxServiceTest, DomainResultTravelsInTheReply )
{
    MailboxFixture rig( MailboxServiceTestUtil::makeQuietSettings() );
    const string   mailKey = rig.sendMail( 5 );

    const OnlineResult<MailboxResultReply> refused = rig.call<MailboxMethod::Delete>( MailboxServiceTestUtil::makeMailRequest( mailKey ) );
    SW_EXPECT_EQUAL( OnlineError::kOk, refused._errorCode );
    SW_EXPECT_TRUE( refused._reply._result == MailboxResult::HasAttachments );
    SW_EXPECT_FALSE( refused.isOk() );
    SW_EXPECT_FALSE( refused.isRetryable() );
}

/** @brief [MailboxServiceTest] 모두 받기: 받은 수를 돌려주고 잔액이 합만큼 오른다 */
SW_TEST_CASE( MailboxServiceTest, ClaimAllCounts )
{
    MailboxFixture rig( MailboxServiceTestUtil::makeQuietSettings() );
    (void)rig.sendMail( 5, 0, "mail.a" );
    (void)rig.sendMail( 7, 0, "mail.b" );
    const OnlineResult<MailboxClaimReply> claimed = rig.call<MailboxMethod::ClaimAll>( OnlineNoBody{} );
    SW_EXPECT_TRUE( claimed.isOk() );
    SW_EXPECT_EQUAL( 2, claimed._reply._claimedCount );
    SW_EXPECT_EQUAL( int64( 12 ), rig.balanceOf( kPlayer ) );
}

/** @brief [MailboxServiceTest] 캠페인: 틱이 읽어 메모리에 두고 첫 쪽 목록 앞에 끼운다. 끝난 캠페인은 빠지고, 읽기가 실패하면 있던 목록을 둔다 */
SW_TEST_CASE( MailboxServiceTest, CampaignsAppearInFirstPage )
{
    SW_EXPECT_LOG( "MailboxService.MailCampaignsCouldNot" );
    MailboxServiceConfig settings = MailboxServiceTestUtil::makeQuietSettings();
    settings._campaignRefreshMs   = 1000;
    MailboxFixture rig( settings );

    ServiceMailCampaign campaign;
    campaign._campaignID = 1;
    campaign._actorID    = 7;
    campaign._titleKey   = "mail.campaign";
    campaign._startMs    = kNowMs - 1;
    campaign._endMs      = kNowMs + 10000;
    campaign._listAttachment.push_back( ServiceMailAttachment{ "cur.gold", 3 } );
    ServiceTransaction transaction;
    SW_REQUIRE_TRUE( ServiceMailCampaign::stageCreate( campaign, transaction ) );
    SW_REQUIRE_TRUE( rig._rig._database.commit( transaction ) == ServiceStoreResult::Ok );

    SW_EXPECT_TRUE( rig.list()._reply._listMail.empty() ); // 아직 다시 읽지 않았다
    rig.tick( kNowMs + 1000 );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.list( kNowMs + 1000 )._reply._listMail.size() );

    rig._rig._database.armFault( ServiceStoreFault::RejectRead );
    rig.tick( kNowMs + 2000 );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.list( kNowMs + 2000 )._reply._listMail.size() );

    rig.tick( kNowMs + 12000 );
    SW_EXPECT_TRUE( rig.list( kNowMs + 12000 )._reply._listMail.empty() );
}

/** @brief [MailboxServiceTest] 정리: 만료된 우편을 치워 목록에서 사라진다 */
SW_TEST_CASE( MailboxServiceTest, SweepRemovesExpiredMail )
{
    MailboxServiceConfig settings = MailboxServiceTestUtil::makeQuietSettings();
    settings._sweepIntervalMs     = 0; // 틱이 스스로 돌지 않는다
    MailboxFixture rig( settings );
    (void)rig.sendMail( 5, kNowMs + 10 );
    rig._service.requestSweep();
    rig.tick( kNowMs + 11 );
    SW_EXPECT_TRUE( rig.list( kNowMs + 11 )._reply._listMail.empty() );
    SW_EXPECT_EQUAL( int64( 0 ), rig.balanceOf( kPlayer ) ); // 발행한 재원은 받지 않으면 사라진다
}

/** @brief [MailboxServiceTest] 시작: 저장소가 없으면 서지 않는다 */
SW_TEST_CASE( MailboxServiceTest, NeedsStore )
{
    SW_EXPECT_LOG( "MailboxService.MailboxServiceNeedsStore" );
    MailboxService service;
    SW_EXPECT_FALSE( service.initialize( nullptr, MailboxServiceTestUtil::makeQuietSettings() ) );
}

/** @brief [MailboxServiceTest] 일시 장애는 공통 범주로 와서 기억되지 않아 같은 멱등 키의 재시도가 서비스에 닿아 끝나고, 끝난 수령은 기억한 답을 주어 원장이 한 번만 움직인다 */
SW_TEST_CASE( MailboxServiceTest, RetryableCategoryReachesTheServiceAgain )
{
    MailboxFixture rig( MailboxServiceTestUtil::makeQuietSettings() );
    const string   mailKey = rig.sendMail( 5 );

    rig._rig._database.armFault( ServiceStoreFault::RejectCommit );
    const OnlineResult<MailboxClaimReply> failed = rig.call<MailboxMethod::Claim>( MailboxServiceTestUtil::makeMailRequest( mailKey ) );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, failed._errorCode );
    SW_EXPECT_TRUE( failed.isRetryable() );
    SW_EXPECT_EQUAL( int64( 0 ), rig.balanceOf( kPlayer ) );

    const OnlineResult<MailboxClaimReply> retried = rig.call<MailboxMethod::Claim>( MailboxServiceTestUtil::makeMailRequest( mailKey ), failed._idempotencyKey );
    SW_EXPECT_TRUE( retried.isOk() );
    SW_EXPECT_EQUAL( 1, retried._reply._claimedCount );
    SW_EXPECT_EQUAL( int64( 5 ), rig.balanceOf( kPlayer ) );

    const OnlineResult<MailboxClaimReply> replayed = rig.call<MailboxMethod::Claim>( MailboxServiceTestUtil::makeMailRequest( mailKey ), failed._idempotencyKey );
    SW_EXPECT_TRUE( replayed.isOk() ); // 다시 처리했다면 AlreadyClaimed 이다
    SW_EXPECT_EQUAL( 1, replayed._reply._claimedCount );
    SW_EXPECT_EQUAL( int64( 5 ), rig.balanceOf( kPlayer ) );
}

/** @brief [MailboxServiceTest] 알림: 접속한 계정의 새 우편은 표에 적은 알림으로 클라이언트에 닿는다 */
SW_TEST_CASE( MailboxServiceTest, NewMailPushReachesTheClient )
{
    MailboxServiceConfig settings;
    settings._noticeIntervalMs = 1;
    MailboxFixture rig( settings );
    const string   mailKey = rig.sendMail( 5 );
    rig.tick( kNowMs + 10 );

    ConsumerLease lease;
    SW_REQUIRE_TRUE( rig._client.claimNewMail( lease ) );
    vector<MailboxNewMail> listNewMail;
    rig._client.pollNewMail( lease, listNewMail );
    SW_REQUIRE_EQUAL( size_t( 1 ), listNewMail.size() );
    SW_EXPECT_EQUAL( mailKey, listNewMail[0]._mailKey );
}

/** @brief [MailboxServiceTest] 알림: 접속하지 않은 계정의 새 우편은 기기 푸시로 간다 */
SW_TEST_CASE( MailboxServiceTest, NewMailGoesToOfflinePushWhenAway )
{
    FakeOfflinePush      offlinePush;
    MailboxServiceConfig settings;
    settings._noticeIntervalMs = 1;
    settings._pOfflinePush     = &offlinePush;
    MailboxFixture rig( settings, false );
    (void)rig.sendMail( 5 );
    rig.tick( kNowMs + 10 );
    SW_REQUIRE_EQUAL( size_t( 1 ), offlinePush._listAccount.size() );
    SW_EXPECT_EQUAL( kPlayer, offlinePush._listAccount[0] );
}

/** @brief [MailboxServiceTest] 지표: 업무 결과와 공통 범주를 메서드마다 한 표에 센다 */
SW_TEST_CASE( MailboxServiceTest, MetricsCountKitResultsAndCommonCategories )
{
    MetricRegistry       registry;
    MailboxServiceConfig settings = MailboxServiceTestUtil::makeQuietSettings();
    settings._pMetricRegistry     = &registry;
    MailboxFixture rig( settings );
    const string   mailKey = rig.sendMail( 5 );

    (void)rig.list();
    BitWriter claimBody;
    SW_REQUIRE_TRUE( MailboxProtocol::MessageTypes::write( claimBody, MailboxServiceTestUtil::makeMailRequest( mailKey ) ) );
    (void)rig.sendRaw( MailboxMethod::Claim::kID, claimBody ); // 키가 없다
    (void)rig.call<MailboxMethod::Delete>( MailboxServiceTestUtil::makeMailRequest( mailKey ) );

    const auto readCount = [&registry]( const utf8* pMethod, const utf8* pResult )
    {
        const vector<MetricLabel> listLabel = {
            {"service", "mailbox"},
            { "method",   pMethod},
            { "result",   pResult}
        };
        const MetricCounter* pCounter = registry.registerCounter( "service_requests_total", "Service requests by method and result", listLabel );
        return pCounter != nullptr ? pCounter->getValue() : uint64( 0 );
    };
    SW_EXPECT_EQUAL( uint64( 1 ), readCount( "list", "ok" ) );
    SW_EXPECT_EQUAL( uint64( 1 ), readCount( "claim", "error_invalid_request" ) );
    SW_EXPECT_EQUAL( uint64( 1 ), readCount( "delete", "has_attachments" ) );
}

/** @brief [MailboxServiceTest] 조립: 설정 글이 고른 키트 · 저장소로 서버가 서고 클라이언트가 끝까지 지난다. 저장소 항목을 고르지 않으면 서지 않는다 */
SW_TEST_CASE( MailboxServiceTest, ServerAssembledFromConfigServesTheClient )
{
    constexpr const utf8* kServerJSON = R"({ "_listStore": [ { "_name": "local", "_driver": "memory" } ], "_listCache": [ { "_name": "local", "_driver": "memory" } ] })";
    constexpr const utf8* kOnlineJSON = R"({ "_storeName": "local", "_cacheName": "local", "_listKit": [ { "_name": "testLogin" }, { "_name": "mailbox" } ] })";

    MailboxClient              client; // 묶음보다 먼저 선언해 나중에 지운다 - 지울 때 스스로 등록을 푼다
    test::OnlineRuntimeFixture rig;
    {
        const auto result = rig.fromConfig( kServerJSON, kOnlineJSON, kNowMs );
        SW_REQUIRE_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
    }
    const int32 clientIndex = rig._clients.connect( test::OnlineRuntimeFixture::kPort, { &client } );
    client.initialize( &rig._clients.getClient( clientIndex ) );
    rig.updateAll( kNowMs );
    rig._clients.login( clientIndex, kPlayer );
    rig.updateAll( kNowMs );

    bool bReplied = false;
    (void)client.call<MailboxMethod::List>( MailboxListRequest{}, OnlineReplyDelegate<MailboxMethod::List>{ [&bReplied]( const OnlineResult<MailboxListReply>& result )
    {
        SW_EXPECT_TRUE( result.isOk() );
        bReplied = true;
    } } );
    rig.updateAll( kNowMs );
    SW_EXPECT_TRUE( bReplied );

    test::OnlineRuntimeFixture unnamed;
    const auto                 failed1 = unnamed.fromConfig( kServerJSON, R"({ "_listKit": [ { "_name": "mailbox" } ] })", kNowMs );
    SW_REQUIRE_FALSE( failed1.has_value() );
    const sw::string error1 = sw::string( failed1.error().message() ) + " " + sw::string( failed1.error().detail() );
    SW_EXPECT_TRUE( error1.find( "_storeName" ) != string::npos );
}
