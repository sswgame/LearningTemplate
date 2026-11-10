// EconomyClientTest · EconomyMirrorTest · StorePurchaseFlowTest — 경제 클라이언트(잔액 캐시 · 멱등 키 · 알림), 원장 → 지갑 · 인벤토리 거울, 스토어 결제 흐름(토큰 싣기 · 지급 뒤 마치기 · 재시도).
// 서버 쪽은 같은 프로토콜 항목의 가짜(`OnlineHostTestUtil` 루프백), 스토어 쪽은 가짜 다리다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/EconomyClient.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/EconomyMirror.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/API/StorePurchaseFlow.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/ReceiptBuyerToken.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"

using namespace sw;

namespace
{
    constexpr int64  kNowMs   = 1700000000000ll;
    constexpr uint64 kAccount = 0x0123456789ABCDEFull;

    /** @brief 가짜 서버가 낼 답입니다. 메서드마다 그 응답 형식으로 옮겨 보낸다(`FakeEconomyService::respond`). */
    struct EconomyTestReply
    {
        vector<LedgerBalance> _listBalance{};
        EconomyResult         _result{ EconomyResult::Ok };
    };

    /** @brief 서버 쪽 가짜 — 정해 둔 답으로 응답하고 받은 것을 기록합니다. */
    class FakeEconomyService final : public IOnlineServerService
    {
    public:
        vector<EconomyRedeemRequest> _listRedeem{};
        vector<NetIdempotencyKey>    _listKey{};
        vector<uint16>               _listMethod{};
        EconomyTestReply             _reply{};

        const OnlineProtocolEntry& getProtocol() const override { return EconomyProtocol::kOnlineEntry; }
        bool                       isAnonymousMethod( uint16 method ) const override
        {
            (void)method;
            return true;
        }

        void onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            _listMethod.push_back( context._method );
            _listKey.push_back( context._idempotencyKey );
            if ( context._method == EconomyMethod::RedeemReceipt::kID )
            {
                EconomyRedeemRequest request;
                SW_EXPECT_TRUE( EconomyProtocol::MessageTypes::read( body, request ) == FixedReadResult::Ok );
                _listRedeem.push_back( request );
            }
            respond( host, context );
        }

    private:
        /** @brief 메서드의 응답 형식으로 답합니다. 진짜 서비스와 같은 규칙(`respondReply` — 스토어 대기는 기억하지 않는다)이다. */
        void respond( OnlineServiceHost& host, const OnlineCallContext& context ) const
        {
            if ( context._method == EconomyMethod::GetWallet::kID )
            {
                EconomyWalletReply reply;
                reply._listBalance = _reply._listBalance;
                (void)host.respondReply<EconomyProtocol::MessageTypes>( context._token, reply );
            }
            else if ( context._method == EconomyMethod::Purchase::kID )
            {
                EconomyPurchaseReply reply;
                reply._listBalance = _reply._listBalance;
                reply._result      = _reply._result;
                (void)host.respondReply<EconomyProtocol::MessageTypes>( context._token, reply );
            }
            else if ( context._method == EconomyMethod::RedeemReceipt::kID )
            {
                EconomyRedeemReply reply;
                reply._listBalance = _reply._listBalance;
                reply._result      = _reply._result;
                (void)host.respondReply<EconomyProtocol::MessageTypes>( context._token, reply );
            }
            else if ( context._method == EconomyMethod::WithdrawPurchase::kID )
            {
                EconomyWithdrawReply reply;
                reply._listBalance = _reply._listBalance;
                reply._result      = _reply._result;
                (void)host.respondReply<EconomyProtocol::MessageTypes>( context._token, reply );
            }
            else
            {
                (void)host.respondReply<EconomyProtocol::MessageTypes>( context._token, EconomyHistoryReply{} );
            }
        }
    };

    /** @brief 가짜 서버 + 연결 하나의 경제 클라이언트입니다. 선언 순서가 수명이다. */
    struct EconomyClientFixture
    {
        FakeEconomyService      _service{};
        EconomyClient           _economy{};
        test::OnlineTestFixture _rig{ false };
        int32                   _clientIndex{ -1 };

        EconomyClientFixture()
        {
            SW_EXPECT_TRUE( _rig._server._host.registerServerService( &_service ) );
            _rig._server.start();
            _clientIndex = _rig._clients.connect( _rig._server._port, { &_economy } );
            _economy.initialize( &_rig._clients.getClient( _clientIndex ) );
            pump();
            SW_EXPECT_TRUE( _rig._clients.getClient( _clientIndex ).isReady() );
        }

        void pump() { test::updateAll( { &_rig._server }, _rig._clients, kNowMs ); }
    };

    /** @brief 받은 답 하나입니다. 메서드마다 응답 형식이 달라 공통 칸만 옮겨 적는다. */
    struct EconomyRecordedReply
    {
        NetIdempotencyKey _idempotencyKey{};
        uint64            _requestID{ 0 };
        uint16            _errorCode{ OnlineError::kOk };
        EconomyResult     _result{ EconomyResult::Ok }; ///< 결과 칸이 없는 응답(지갑 · 내역)은 Ok 이다
    };

    /** @brief 경제 클라이언트의 답을 받습니다. */
    struct EconomyReplyRecorder
    {
        vector<EconomyRecordedReply> _listReply{};

        template <typename TReply>
        void onReply( const OnlineResult<TReply>& reply )
        {
            EconomyRecordedReply& recorded = _listReply.emplace_back();
            recorded._idempotencyKey       = reply._idempotencyKey;
            recorded._requestID            = reply._requestID;
            recorded._errorCode            = reply._errorCode;
            if constexpr ( OnlineReplyHasResult<TReply>::value )
                recorded._result = reply._reply._result;
        }

        template <typename TMethod>
        OnlineReplyDelegate<TMethod> makeDelegate()
        {
            return OnlineReplyDelegate<TMethod>::template create<&EconomyReplyRecorder::onReply<typename TMethod::Reply>>( this );
        }
    };

    /** @brief 가짜 결제 다리입니다. 받은 요청과 마친 영수증을 기록하고, 낼 결과는 테스트가 넣습니다. */
    class FakeBillingBridge final : public IStoreBillingBridge
    {
    public:
        vector<StoreBillingRequest> _listRequest{};
        vector<StoreBillingResult>  _listResult{};
        vector<string>              _listFinished{};
        uint8                       _bAvailable{ SW_TRUE };

        const utf8* getStoreName() const override { return "google"; }
        bool        beginPurchase( const StoreBillingRequest& request ) override
        {
            _listRequest.push_back( request );
            return _bAvailable == SW_TRUE;
        }
        void pollResults( vector<StoreBillingResult>& outListResult ) override
        {
            outListResult.insert( outListResult.end(), _listResult.begin(), _listResult.end() );
            _listResult.clear();
        }
        void finishPurchase( string_view receiptPayload ) override { _listFinished.emplace_back( receiptPayload ); }

        void pushPurchased( string_view payload )
        {
            StoreBillingResult& result = _listResult.emplace_back();
            result._status             = StoreBillingStatus::Purchased;
            result._receiptPayload     = string( payload );
        }
    };

    /** @brief 흐름 + 가짜 서버 + 가짜 다리입니다. */
    struct PurchaseFlowFixture
    {
        EconomyClientFixture        _client{};
        FakeBillingBridge           _bridge{};
        vector<StorePurchaseReport> _listReport{};
        StorePurchaseFlow           _flow;

        PurchaseFlowFixture()
            : _flow{ _bridge, _client._economy, StorePurchaseFlow::ReportDelegate::create<&PurchaseFlowFixture::onReport>( this ) }
        {
        }

        void onReport( const StorePurchaseReport& report ) { _listReport.push_back( report ); }

        void pump()
        {
            _flow.update();
            _client.pump();
        }
    };

    struct MirrorTestUtil
    {
        static constexpr const utf8* kCatalogXML = R"(<ItemCatalog>
  <Item id="stone" category="Material" maxStack="10" weight="1"/>
  <Item id="arrow" category="Ammo" maxStack="20" weight="1"/>
</ItemCatalog>)";
    };
} // namespace

/** @brief [EconomyClientTest] 지갑 응답은 캐시를 통째로 바꾸고, 구매 응답은 바뀐 것만 덮는다. 고칠 때마다 판이 오른다 */
SW_TEST_CASE( EconomyClientTest, RepliesUpdateTheCache )
{
    EconomyClientFixture rig;
    EconomyReplyRecorder recorder;
    rig._service._reply._listBalance = {
        LedgerBalance{"cur.gold", 10},
        LedgerBalance{ "cur.gem",  5}
    };
    (void)rig._economy.call<EconomyMethod::GetWallet>( recorder.makeDelegate<EconomyMethod::GetWallet>() );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), recorder._listReply.size() );
    SW_EXPECT_EQUAL( int64( 10 ), rig._economy.getBalance( "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 5 ), rig._economy.getBalance( "cur.gem" ) );
    const uint32 revision = rig._economy.getRevision();

    rig._service._reply._listBalance = {
        LedgerBalance{ "cur.gold", 3 }
    };
    EconomyPurchaseRequest purchase;
    purchase._offerID = "offer";
    (void)rig._economy.call<EconomyMethod::Purchase>( purchase, recorder.makeDelegate<EconomyMethod::Purchase>(), NetIdempotencyKey{} );
    rig.pump();
    SW_EXPECT_EQUAL( int64( 3 ), rig._economy.getBalance( "cur.gold" ) );
    SW_EXPECT_EQUAL( int64( 5 ), rig._economy.getBalance( "cur.gem" ) ); // 답에 없는 자산은 그대로다
    SW_EXPECT_NOT_EQUAL( revision, rig._economy.getRevision() );

    rig._service._reply._listBalance = {
        LedgerBalance{ "cur.gold", 1 }
    };
    (void)rig._economy.call<EconomyMethod::GetWallet>( recorder.makeDelegate<EconomyMethod::GetWallet>() );
    rig.pump();
    SW_EXPECT_EQUAL( int64( 0 ), rig._economy.getBalance( "cur.gem" ) ); // 스냅샷에 없으면 없다
}

/** @brief [EconomyClientTest] 실패한 답은 캐시를 바꾸지 않고 결과를 그대로 넘긴다 */
SW_TEST_CASE( EconomyClientTest, FailedReplyChangesNothing )
{
    EconomyClientFixture rig;
    EconomyReplyRecorder recorder;
    rig._service._reply._result      = EconomyResult::InsufficientFunds;
    rig._service._reply._listBalance = {
        LedgerBalance{ "cur.gold", 99 }
    };
    EconomyPurchaseRequest purchase;
    purchase._offerID = "offer";
    (void)rig._economy.call<EconomyMethod::Purchase>( purchase, recorder.makeDelegate<EconomyMethod::Purchase>(), NetIdempotencyKey{} );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), recorder._listReply.size() );
    SW_EXPECT_TRUE( recorder._listReply[0]._result == EconomyResult::InsufficientFunds );
    SW_EXPECT_EQUAL( int64( 0 ), rig._economy.getBalance( "cur.gold" ) );
}

/** @brief [EconomyClientTest] 구매 · 지급의 멱등 키: 비어 있으면 새로 만들고 답이 돌려주며, 그 키를 넘기면 서버가 같은 키로 알아 기억한 답을 준다(서비스는 다시 불리지 않는다). 키는 호출마다 다르다 */
SW_TEST_CASE( EconomyClientTest, IdempotencyKeyIsKeptForRetries )
{
    EconomyClientFixture rig;
    EconomyReplyRecorder recorder;
    EconomyRedeemRequest redeem;
    redeem._storeName = "google";
    redeem._payload   = "p|t";
    (void)rig._economy.call<EconomyMethod::RedeemReceipt>( redeem, recorder.makeDelegate<EconomyMethod::RedeemReceipt>(), NetIdempotencyKey{} );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), recorder._listReply.size() );
    const NetIdempotencyKey first = recorder._listReply[0]._idempotencyKey;
    SW_EXPECT_TRUE( first.isValid() );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._service._listKey.size() );
    SW_EXPECT_TRUE( rig._service._listKey[0] == first );

    (void)rig._economy.call<EconomyMethod::RedeemReceipt>( redeem, recorder.makeDelegate<EconomyMethod::RedeemReceipt>(), first );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 2 ), recorder._listReply.size() );
    SW_EXPECT_TRUE( recorder._listReply[1]._idempotencyKey == first );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._service._listKey.size() ); // 다른 키를 보냈다면 서비스가 다시 불렸다

    (void)rig._economy.call<EconomyMethod::RedeemReceipt>( redeem, recorder.makeDelegate<EconomyMethod::RedeemReceipt>(), NetIdempotencyKey{} );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._service._listKey.size() );
    SW_EXPECT_FALSE( rig._service._listKey[1] == first );
}

/** @brief [EconomyClientTest] 연결 전 요청은 보내지 않고 요청 함수 안에서 공통 범주 `kUnavailable` 로 끝난다 */
SW_TEST_CASE( EconomyClientTest, RequestBeforeInitializeFailsAtOnce )
{
    EconomyClient        client;
    EconomyReplyRecorder recorder;
    const uint64         requestID = client.call<EconomyMethod::GetWallet>( recorder.makeDelegate<EconomyMethod::GetWallet>() );
    SW_REQUIRE_EQUAL( size_t( 1 ), recorder._listReply.size() );
    SW_EXPECT_EQUAL( requestID, recorder._listReply[0]._requestID );
    SW_EXPECT_EQUAL( OnlineError::kUnavailable, recorder._listReply[0]._errorCode );
}

/** @brief [EconomyClientTest] 잔액 변경 알림은 캐시를 덮고, 다른 알림은 무시한다. 캐시는 지갑으로 비친다 */
SW_TEST_CASE( EconomyClientTest, BalancePushOverwritesTheCache )
{
    EconomyClient client;
    client.applyBalances( {
                              LedgerBalance{ "cur.gold", 10 }
    },
                          true );

    EconomyBalanceChanged changed;
    changed._listBalance = {
        LedgerBalance{ "cur.gold", -5 }
    };
    BitWriter writer;
    SW_REQUIRE_TRUE( EconomyProtocol::MessageTypes::write( writer, changed ) );
    {
        BitReader reader( writer.getBytes().data(), writer.getByteCount() );
        client.onServicePush( static_cast<uint16>( EconomyMethod::BalanceChanged::kKind + 1 ), reader );
    }
    SW_EXPECT_EQUAL( int64( 10 ), client.getBalance( "cur.gold" ) );
    {
        BitReader reader( writer.getBytes().data(), writer.getByteCount() );
        client.onServicePush( EconomyMethod::BalanceChanged::kKind, reader );
    }
    SW_EXPECT_EQUAL( int64( -5 ), client.getBalance( "cur.gold" ) );

    Wallet wallet;
    client.applyLedgerBalances( wallet );
    SW_EXPECT_EQUAL( int64( -5 ), wallet.getBalance( hashed_string{ "gold" } ) );
}

/** @brief [EconomyMirrorTest] 지갑: `cur.` 자산만 차이만큼 맞추고(빚 포함) 다른 접두어는 건너뛴다. 스냅샷이 아니면 목록에 없는 통화를 건드리지 않는다 */
SW_TEST_CASE( EconomyMirrorTest, WalletFollowsTheLedger )
{
    Wallet wallet;
    wallet.add( hashed_string{ "gold" }, 50 );
    wallet.add( hashed_string{ "gem" }, 7 );
    EconomyMirror::applyToWallet( {
                                      LedgerBalance{    "cur.gold", 80},
                                      LedgerBalance{  "item.stone",  3},
                                      LedgerBalance{"custom.thing",  9},
                                      LedgerBalance{        "cur.",  1}
    },
                                  false, wallet );
    SW_EXPECT_EQUAL( int64( 80 ), wallet.getBalance( hashed_string{ "gold" } ) );
    SW_EXPECT_EQUAL( int64( 7 ), wallet.getBalance( hashed_string{ "gem" } ) );
    SW_EXPECT_EQUAL( size_t( 2 ), wallet.getBalances().size() );

    EconomyMirror::applyToWallet( {
                                      LedgerBalance{ "cur.gold", -20 }
    },
                                  false, wallet );
    SW_EXPECT_EQUAL( int64( -20 ), wallet.getBalance( hashed_string{ "gold" } ) );

    EconomyMirror::applyToWallet( {
                                      LedgerBalance{ "cur.gold", 1 }
    },
                                  true, wallet );
    SW_EXPECT_EQUAL( int64( 1 ), wallet.getBalance( hashed_string{ "gold" } ) );
    SW_EXPECT_EQUAL( int64( 0 ), wallet.getBalance( hashed_string{ "gem" } ) ); // 스냅샷에 없으면 0 이다
}

/** @brief [EconomyMirrorTest] 인벤토리: `item.` 자산을 맞추고 빚은 0 으로 비친다. 넣지 못한 개수를 돌려주고, 스냅샷은 없는 아이템을 먼저 빼 슬롯을 비운다 */
SW_TEST_CASE( EconomyMirrorTest, InventoryFollowsTheLedger )
{
    ItemCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( MirrorTestUtil::kCatalogXML, "EconomyMirrorTest" ) );
    Inventory inventory;
    SW_REQUIRE_TRUE( inventory.initialize( &catalog, 1 ) );
    const hashed_string stone{ "stone" };
    const hashed_string arrow{ "arrow" };

    SW_EXPECT_EQUAL( 5, EconomyMirror::applyToInventory( {
                                                             LedgerBalance{ "item.stone", 15 }
    },
                                                         false, inventory ) ); // 한 슬롯에 열 개까지다
    SW_EXPECT_EQUAL( 10, inventory.getItemCount( stone ) );
    SW_EXPECT_EQUAL( 0, EconomyMirror::applyToInventory( {
                                                             LedgerBalance{ "item.stone", 4 }
    },
                                                         false, inventory ) );
    SW_EXPECT_EQUAL( 4, inventory.getItemCount( stone ) );
    SW_EXPECT_EQUAL( 0, EconomyMirror::applyToInventory( {
                                                             LedgerBalance{ "item.stone", -3 }
    },
                                                         false, inventory ) );
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( stone ) );

    SW_EXPECT_EQUAL( 0, EconomyMirror::applyToInventory( {
                                                             LedgerBalance{ "item.stone", 2 }
    },
                                                         false, inventory ) );
    SW_EXPECT_EQUAL( 0, EconomyMirror::applyToInventory( {
                                                             LedgerBalance{ "item.arrow", 3 }
    },
                                                         true, inventory ) ); // 돌을 먼저 빼야 슬롯이 빈다
    SW_EXPECT_EQUAL( 0, inventory.getItemCount( stone ) );
    SW_EXPECT_EQUAL( 3, inventory.getItemCount( arrow ) );
}

/** @brief [StorePurchaseFlowTest] 구매 요청에 그 계정의 토큰이 실린다 — UUID 모양(애플)이고 64 자 이하(구글)다. 로그인 전 · 빈 상품은 스토어를 열지 않는다 */
SW_TEST_CASE( StorePurchaseFlowTest, BeginCarriesTheBuyerToken )
{
    SW_EXPECT_LOG( "StorePurchaseFlow.CannotBegin" );
    PurchaseFlowFixture rig;
    SW_EXPECT_TRUE( rig._flow.begin( kAccount, "gem100" ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._bridge._listRequest.size() );
    SW_EXPECT_EQUAL( string( "gem100" ), rig._bridge._listRequest[0]._productID );
    SW_EXPECT_EQUAL( string( "01234567-89ab-4000-8000-00000000cdef" ), rig._bridge._listRequest[0]._buyerToken );
    SW_EXPECT_EQUAL( ReceiptBuyerToken::make( kAccount ), rig._bridge._listRequest[0]._buyerToken );

    SW_EXPECT_FALSE( rig._flow.begin( 0, "gem100" ) );
    SW_EXPECT_FALSE( rig._flow.begin( kAccount, "" ) );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._bridge._listRequest.size() );

    rig._bridge._bAvailable = SW_FALSE;
    SW_EXPECT_FALSE( rig._flow.begin( kAccount, "gem100" ) ); // 스토어를 쓸 수 없다
}

/** @brief [StorePurchaseFlowTest] 결제된 영수증을 스토어 이름과 함께 서버에 내고, 지급되면 거래를 마친 뒤 알린다 */
SW_TEST_CASE( StorePurchaseFlowTest, PurchasedIsRedeemedThenFinished )
{
    PurchaseFlowFixture rig;
    rig._bridge.pushPurchased( "gem100|TOKEN" );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._client._service._listRedeem.size() );
    SW_EXPECT_EQUAL( string( "google" ), rig._client._service._listRedeem[0]._storeName );
    SW_EXPECT_EQUAL( string( "gem100|TOKEN" ), rig._client._service._listRedeem[0]._payload );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listReport.size() );
    SW_EXPECT_TRUE( rig._listReport[0]._result == EconomyResult::Ok );
    SW_EXPECT_TRUE( rig._listReport[0]._bRetryable == SW_FALSE );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._bridge._listFinished.size() );
    SW_EXPECT_EQUAL( string( "gem100|TOKEN" ), rig._bridge._listFinished[0] );
}

/** @brief [StorePurchaseFlowTest] 스토어에 닿지 못한 서버 답은 거래를 마치지 않고 다시 낼 수 있다. 다시 내면 같은 멱등 키이고, 지급되면 마친다 */
SW_TEST_CASE( StorePurchaseFlowTest, PendingRedeemRetriesWithTheSameKey )
{
    PurchaseFlowFixture rig;
    rig._client._service._reply._result = EconomyResult::ReceiptPending;
    rig._bridge.pushPurchased( "gem100|TOKEN" );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._listReport.size() );
    SW_EXPECT_TRUE( rig._listReport[0]._bRetryable == SW_TRUE );
    SW_EXPECT_TRUE( rig._bridge._listFinished.empty() );

    rig._client._service._reply._result = EconomyResult::Ok;
    const StorePurchaseReport pending   = rig._listReport[0];
    rig._flow.retryRedeem( pending );
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._client._service._listKey.size() );
    SW_EXPECT_TRUE( rig._client._service._listKey[0] == rig._client._service._listKey[1] );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._bridge._listFinished.size() );

    rig._flow.retryRedeem( rig._listReport[1] ); // 끝난 결과는 다시 내지 않는다
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 2 ), rig._client._service._listKey.size() );
}

/** @brief [StorePurchaseFlowTest] 거절된 영수증은 마치지 않는다 — 다음 기동에 스토어가 다시 내 맞는 계정이 받을 수 있다. 토큰이 빠졌으면 원인을 로그로 말한다. 환불된 거래는 마친다 */
SW_TEST_CASE( StorePurchaseFlowTest, RefusedPurchaseStaysOpen )
{
    SW_EXPECT_LOG( "StorePurchaseFlow.BuyerTokenMissing" );
    PurchaseFlowFixture rig;
    for ( EconomyResult result : { EconomyResult::NotPurchaser, EconomyResult::BuyerTokenMissing, EconomyResult::AlreadyRedeemed, EconomyResult::ReceiptInvalid } )
    {
        rig._client._service._reply._result = result;
        rig._bridge.pushPurchased( "gem100|TOKEN" );
        rig.pump();
        SW_EXPECT_TRUE( rig._bridge._listFinished.empty() );
        SW_EXPECT_TRUE( rig._listReport.back()._result == result );
        SW_EXPECT_TRUE( rig._listReport.back()._bRetryable == SW_FALSE );
    }
    rig._client._service._reply._result = EconomyResult::ReceiptRefunded;
    rig._bridge.pushPurchased( "gem100|TOKEN" );
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._bridge._listFinished.size() );
}

/** @brief [StorePurchaseFlowTest] 취소 · 실패 · 보류는 서버에 내지 않고 그대로 알린다 */
SW_TEST_CASE( StorePurchaseFlowTest, UnpaidResultsNeverReachTheServer )
{
    PurchaseFlowFixture rig;
    for ( StoreBillingStatus status : { StoreBillingStatus::Cancelled, StoreBillingStatus::Failed, StoreBillingStatus::Pending } )
    {
        StoreBillingResult& result = rig._bridge._listResult.emplace_back();
        result._status             = status;
        result._failureText        = "text";
    }
    rig.pump();
    SW_EXPECT_TRUE( rig._client._service._listMethod.empty() );
    SW_REQUIRE_EQUAL( size_t( 3 ), rig._listReport.size() );
    SW_EXPECT_TRUE( rig._listReport[0]._billing == StoreBillingStatus::Cancelled );
    SW_EXPECT_EQUAL( string( "text" ), rig._listReport[1]._failureText );
    SW_EXPECT_TRUE( rig._listReport[2]._billing == StoreBillingStatus::Pending );
    SW_EXPECT_TRUE( rig._bridge._listFinished.empty() );
}
