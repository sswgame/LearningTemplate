// EconomyReceiptsTest — 결제 저장 규칙과 서비스(메모리 저장소 + 가짜 스토어): 영수증 지급은 거래당 한 번(다른 계정이면 AlreadyRedeemed), 샌드박스 · 모르는 상품 거절, 환불 회수는 빚을 허용하고 한 번만,
// 지급 전에 온 환불이 지급을 막음, 구매자 대조(외부 로그인 연동 · 계정 토큰 · 묶지 않음), 청약철회는 쓰지 않은 만큼 · 기한 안 · 구매자만 · 환불과 겹치지 않음, 지갑 · 내역, 서비스의 멱등 키 요구 · 원장 보존 검사.
// 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Protocol/ReceiptBuyerToken.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Steam/SteamReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Runtime/Online/Ledger/Server/Ledger.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr AccountID kBuyer    = 42;
    constexpr AccountID kOther    = 43;
    constexpr int64     kNowMs    = 1700000000000ll;
    constexpr int64     kWindowMs = 1000;

    constexpr int64 kFarDeadlineMs = kNowMs + 100000000; ///< 지급 알림의 기한 — 기한 경고를 보는 케이스만 가깝게 둔다

    const utf8* const kCurrencyXML = R"(<CurrencyCatalog>
  <Currency id="cur.gold"/>
  <Currency id="cur.gem_paid" paid="true"/>
</CurrencyCatalog>)";

    const utf8* const kOfferXML = R"(<OfferCatalog>
  <Offer id="gem_100">
    <Product store="fake" id="com.x.gem100"/>
    <Grant asset="cur.gem_paid" amount="100"/>
    <Grant asset="cur.gold" amount="10"/>
  </Offer>
</OfferCatalog>)";

    /** @brief 계정 @p _linkedAccount 만 제공자 "steam" 의 주체 "S1" 에 연동된 가짜 연동 조회입니다. */
    class FakeExternalLinks final : public IAccountExternalLinks
    {
    public:
        AccountID          _linkedAccount{ kBuyer };
        ServiceStoreResult _read{ ServiceStoreResult::Ok };

        ServiceStoreResult readIsLinked( IServiceStoreConnection& connection, AccountID accountID, string_view provider, string_view subject, bool& outbLinked ) const override
        {
            (void)connection;
            outbLinked = accountID == _linkedAccount && provider == "steam" && subject == "S1";
            return _read;
        }
    };

    /** @brief 저장 규칙을 바로 부르는 묶음입니다. */
    struct ReceiptFixture
    {
        MemoryServiceDatabase _database;
        CurrencyCatalog       _currencies;
        OfferCatalog          _offers;
        EconomyServiceResult  _lastServiceResult{}; ///< 마지막 `redeem` · `withdraw` 의 결과이다. 공통 범주면 응답은 뜻이 없다

        ReceiptFixture()
            : _database{}
            , _currencies{}
            , _offers{}
            , _lastServiceResult{}
        {
            SW_EXPECT_TRUE( _currencies.loadFromXMLText( kCurrencyXML, "test" ) );
            SW_EXPECT_TRUE( _offers.loadFromXMLText( kOfferXML, "test" ) );
        }

        EconomyRedeemReply redeem( AccountID accountID, string_view transactionID = "tx1", ReceiptStatus status = ReceiptStatus::Valid, string_view productID = "com.x.gem100", bool bSandbox = false, bool bAcceptSandbox = false )
        {
            EconomyRedeemInput input;
            input._receipt._storeName     = "fake";
            input._receipt._transactionID = string( transactionID );
            input._receipt._productID     = string( productID );
            input._receipt._status        = status;
            input._receipt._bSandbox      = bSandbox ? SW_TRUE : SW_FALSE;
            input._accountID              = accountID;
            input._nowMs                  = kNowMs;
            input._bAcceptSandbox         = bAcceptSandbox ? SW_TRUE : SW_FALSE;
            EconomyRedeemReply reply;
            _lastServiceResult = EconomyStoreLogic::redeemReceipt( _database, _currencies, _offers, input, reply );
            return reply;
        }

        /** @brief 규칙 @p binding 과 답의 구매자 @p storeBuyerID 로 지급합니다. */
        EconomyServiceResult redeemBound( AccountID accountID, ReceiptBuyerBinding binding, string_view storeBuyerID, const IAccountExternalLinks* pLinks, string_view transactionID = "tx1" )
        {
            EconomyRedeemInput input;
            input._receipt._storeName                = "fake";
            input._receipt._transactionID            = string( transactionID );
            input._receipt._productID                = "com.x.gem100";
            input._receipt._status                   = ReceiptStatus::Valid;
            input._receipt._storeBuyerID             = string( storeBuyerID );
            input._receipt._buyerRule._binding       = binding;
            input._receipt._buyerRule._loginProvider = "steam";
            input._pExternalLinks                    = pLinks;
            input._accountID                         = accountID;
            input._nowMs                             = kNowMs;
            EconomyRedeemReply reply;
            return EconomyStoreLogic::redeemReceipt( _database, _currencies, _offers, input, reply );
        }

        EconomyServiceResult refund( string_view transactionID = "tx1", vector<EconomyBalanceChange>* pOutListChange = nullptr )
        {
            ReceiptRefundNotification notice;
            notice._storeName     = "fake";
            notice._transactionID = string( transactionID );
            vector<EconomyBalanceChange> listChange;
            const EconomyServiceResult   result = EconomyStoreLogic::revokeRefund( _database, _currencies, notice, kNowMs, listChange );
            if ( pOutListChange != nullptr )
                *pOutListChange = listChange;
            return result;
        }

        EconomyWithdrawReply withdraw( AccountID accountID, int64 nowMs = kNowMs, string_view transactionID = "tx1" )
        {
            EconomyWithdrawInput input;
            input._storeName     = "fake";
            input._transactionID = string( transactionID );
            input._accountID     = accountID;
            input._nowMs         = nowMs;
            input._windowMs      = kWindowMs;
            EconomyWithdrawReply reply;
            _lastServiceResult = EconomyStoreLogic::withdrawPurchase( _database, _currencies, input, reply );
            return reply;
        }

        int64 getBalanceOf( AccountID accountID, string_view assetID = "cur.gem_paid" )
        {
            LedgerBalance balance;
            SW_EXPECT_TRUE( Ledger::readBalance( _database, LedgerHolder::makeAccount( accountID ), assetID, balance ) == ServiceStoreResult::Ok );
            return balance._amount;
        }

        /** @brief 계정의 재화를 태웁니다(게임에서 쓴 것). */
        void spend( AccountID accountID, int64 amount, string_view assetID = "cur.gem_paid" )
        {
            LedgerTransferRequest request;
            request._listPosting.push_back( LedgerPosting{ LedgerHolder::makeAccount( accountID ), LedgerHolder::makeSink(), string( assetID ), amount } );
            request._journalKey = "test/spend/" + to_string( accountID ) + "/" + to_string( amount );
            request._reason     = "test.spend";
            LedgerTransferResult serviceResult;
            SW_EXPECT_TRUE( Ledger::executeTransfer( _database, request, serviceResult ) == LedgerResult::Ok );
        }
    };

    /** @brief 환불 조회 기준 시각을 쓰는 가짜 제공자입니다. 서비스와 주고받은 것을 기록하고, 판단은 `StoreReceiptValidator` 쪽 테스트가 본다. */
    class CursorRecordingValidator final : public IReceiptValidator
    {
    public:
        vector<int64>    _listRestored{};
        ReceiptBuyerRule _buyerRule{};
        int64            _movedMs{ 0 }; ///< 0 이 아니면 다음 `takeRefundCursor` 가 내준다

        const utf8*             getStoreName() const override { return "cursor"; }
        const ReceiptBuyerRule& getBuyerRule() const override { return _buyerRule; }
        void                    submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now ) override
        {
            (void)request;
            (void)now;
        }
        void update( UnixMilliseconds now ) override { (void)now; }
        void pollCompletions( vector<ReceiptValidationResult>& outListResult ) override { (void)outListResult; }
        void pollRefunds( vector<ReceiptRefundNotification>& outListNotice ) override { (void)outListNotice; }
        bool usesRefundCursor() const override { return true; }
        void restoreRefundCursor( int64 lastReadMs ) override { _listRestored.push_back( lastReadMs ); }
        bool takeRefundCursor( int64& outLastReadMs ) override
        {
            if ( _movedMs == 0 )
                return false;
            outLastReadMs = _movedMs;
            _movedMs      = 0;
            return true;
        }
    };

    /** @brief 영수증마다 "지급 뒤에 알려라" 고 답하는 가짜 스토어입니다. 스토어 이름은 카탈로그에 있는 "fake" 이고, 지급 알림은 기록만 하고 결과는 테스트가 냅니다. */
    class DeliveryRecordingValidator final : public IReceiptValidator
    {
    public:
        vector<ReceiptDeliveryTask>     _listAsked{};
        vector<ReceiptDeliveryResult>   _listServiceResult{};
        vector<ReceiptValidationResult> _listResult{};
        ReceiptBuyerRule                _buyerRule{};
        string                          _storeBuyerID{};
        int64                           _deadlineMs{ kFarDeadlineMs };

        const utf8*             getStoreName() const override { return "fake"; }
        const ReceiptBuyerRule& getBuyerRule() const override { return _buyerRule; }
        void                    submitValidation( const ReceiptValidationRequest& request, UnixMilliseconds now ) override
        {
            (void)now;
            ReceiptValidationResult& result = _listResult.emplace_back();
            result._storeName               = "fake";
            result._transactionID           = request._payload;
            result._productID               = "com.x.gem100";
            result._deliveryKey             = "key." + request._payload;
            result._deliveryDeadlineMs      = _deadlineMs;
            result._storeBuyerID            = _storeBuyerID;
            result._buyerRule               = _buyerRule;
            result._ticket                  = request._ticket;
            result._status                  = ReceiptStatus::Valid;
        }
        void update( UnixMilliseconds now ) override { (void)now; }
        void pollCompletions( vector<ReceiptValidationResult>& outListResult ) override
        {
            outListResult.insert( outListResult.end(), _listResult.begin(), _listResult.end() );
            _listResult.clear();
        }
        void pollRefunds( vector<ReceiptRefundNotification>& outListNotice ) override { (void)outListNotice; }
        bool usesDeliveryConfirmation() const override { return true; }
        void confirmDelivery( const ReceiptDeliveryTask& task, UnixMilliseconds now ) override
        {
            (void)now;
            _listAsked.push_back( task );
        }
        void pollDeliveryOutcomes( vector<ReceiptDeliveryResult>& outListServiceResult ) override
        {
            outListServiceResult.insert( outListServiceResult.end(), _listServiceResult.begin(), _listServiceResult.end() );
            _listServiceResult.clear();
        }

        /** @brief 맡은 알림 @p index 의 결과를 다음 회수에 냅니다. */
        void answer( size_t index, ReceiptDeliveryStatus status ) { _listServiceResult.push_back( ReceiptDeliveryResult{ "fake", _listAsked[index]._recordKey, status } ); }
    };

    /** @brief 서비스 답을 받습니다. 원격 클라이언트와 같은 모양이다(공통 범주는 `_errorCode`). */
    struct EconomyReplySink
    {
        vector<OnlineResult<EconomyCallReply>> _listReply{};

        void onReply( const OnlineResult<EconomyCallReply>& reply ) { _listReply.push_back( reply ); }

        /** @brief 받은 답의 결과입니다. 공통 범주면 그 오류입니다. */
        static EconomyServiceResult toResult( const OnlineResult<EconomyCallReply>& reply )
        {
            return reply._errorCode != OnlineError::kOk ? EconomyServiceResult::fail( reply._errorCode ) : EconomyServiceResult{ reply._reply._result };
        }
    };

    /** @brief 서비스 + 가짜 스토어입니다. */
    struct EconomyServiceFixture
    {
        ReceiptFixture           _data;
        MemoryServiceStore       _store;
        FakeReceiptValidator     _validator;
        ReceiptValidatorRegistry _registry;
        EconomyService           _service;
        EconomyReplySink         _sink;
        EconomyServiceResult     _lastServiceResult; ///< 마지막 `call` 의 결과이다

        explicit EconomyServiceFixture( bool bAcceptSandbox = false )
            : _data{}
            , _store{ &_data._database }
            , _validator{}
            , _registry{}
            , _service{}
            , _sink{}
            , _lastServiceResult{}
        {
            SW_EXPECT_TRUE( _registry.registerValidator( &_validator ) );
            EconomyServiceConfig settings;
            settings._pCurrencyCatalog       = &_data._currencies;
            settings._pOfferCatalog          = &_data._offers;
            settings._pReceiptRegistry       = &_registry;
            settings._bAcceptSandboxReceipts = bAcceptSandbox ? SW_TRUE : SW_FALSE;
            SW_EXPECT_TRUE( _service.initialize( &_store, settings ) );
        }

        ~EconomyServiceFixture()
        {
            _service.shutdown();
            _registry.shutdown();
        }

        void pump()
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                _service.update( UnixMilliseconds{ kNowMs } );
                while ( _store.pollCompletions() > 0 )
                {
                }
            }
        }

        /** @brief 같은 프로세스 호출 하나를 끝까지 돌립니다. 결과는 `_lastOutcome` 에 남는다(공통 범주면 응답은 빈 값이다). */
        EconomyCallReply call( uint16 method, AccountID accountID, const EconomyCall* pCall = nullptr )
        {
            EconomyCall call = pCall != nullptr ? *pCall : EconomyCall{ UnixMilliseconds{ kNowMs } };
            call._method     = method;
            call._accountID  = accountID;
            _service.submitCall( call, EconomyService::ReplyDelegate::create<&EconomyReplySink::onReply>( &_sink ) );
            pump();
            SW_EXPECT_EQUAL( size_t( 1 ), _sink._listReply.size() );
            const OnlineResult<EconomyCallReply> result = _sink._listReply.empty() ? OnlineResult<EconomyCallReply>{} : _sink._listReply.back();
            _sink._listReply.clear();
            _lastServiceResult = EconomyReplySink::toResult( result );
            return result._reply;
        }

        EconomyCallReply redeem( AccountID accountID, string_view payload )
        {
            EconomyCall call{ UnixMilliseconds{ kNowMs } };
            call._redeem._storeName = "fake";
            call._redeem._payload   = string( payload );
            return this->call( EconomyMethod::RedeemReceipt::kID, accountID, &call );
        }
    };

    /** @brief 지급 뒤 알림을 쓰는 서비스 하나입니다. 저장소를 넘겨받아 재시작을 흉내 냅니다. */
    struct DeliveryServiceFixture
    {
        DeliveryRecordingValidator _validator;
        ReceiptValidatorRegistry   _registry;
        EconomyService             _service;
        EconomyReplySink           _sink;
        MemoryServiceStore&        _store;
        int64                      _nowMs;

        DeliveryServiceFixture( ReceiptFixture& data, MemoryServiceStore& store, int64 nowMs = kNowMs )
            : _validator{}
            , _registry{}
            , _service{}
            , _sink{}
            , _store{ store }
            , _nowMs{ nowMs }
        {
            SW_EXPECT_TRUE( _registry.registerValidator( &_validator ) );
            EconomyServiceConfig settings;
            settings._pCurrencyCatalog       = &data._currencies;
            settings._pOfferCatalog          = &data._offers;
            settings._pReceiptRegistry       = &_registry;
            settings._deliveryScanIntervalMs = 1000;
            settings._deliveryAlarmMarginMs  = 100;
            SW_EXPECT_TRUE( _service.initialize( &store, settings ) );
        }

        ~DeliveryServiceFixture()
        {
            _service.shutdown();
            _registry.shutdown();
        }

        void pump()
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                _service.update( UnixMilliseconds{ _nowMs } );
                while ( _store.pollCompletions() > 0 )
                {
                }
            }
        }

        EconomyServiceResult redeem( AccountID accountID, string_view payload )
        {
            EconomyCall call{ UnixMilliseconds{ _nowMs } };
            call._method            = EconomyMethod::RedeemReceipt::kID;
            call._accountID         = accountID;
            call._redeem._storeName = "fake";
            call._redeem._payload   = string( payload );
            _service.submitCall( call, EconomyService::ReplyDelegate::create<&EconomyReplySink::onReply>( &_sink ) );
            pump();
            SW_EXPECT_EQUAL( size_t( 1 ), _sink._listReply.size() );
            const EconomyServiceResult result = _sink._listReply.empty() ? EconomyServiceResult::fail( OnlineError::kUnavailable ) : EconomyReplySink::toResult( _sink._listReply.back() );
            _sink._listReply.clear();
            return result;
        }
    };

    struct DeliveryTestUtil
    {
        static size_t countRecords( ReceiptFixture& data )
        {
            vector<ReceiptDeliveryTask> listTask;
            SW_EXPECT_TRUE( EconomyStoreLogic::listDeliveries( data._database, 100, listTask ) == ServiceStoreResult::Ok );
            return listTask.size();
        }
    };
} // namespace

/** @brief [EconomyReceiptsTest] 지급: 카탈로그의 지급대로 받고, 같은 거래를 다시 내면 재생으로 한 번만 받으며, 다른 계정은 AlreadyRedeemed 다 */
SW_TEST_CASE( EconomyReceiptsTest, RedeemGrantsOncePerTransaction )
{
    ReceiptFixture           rig;
    const EconomyRedeemReply first = rig.redeem( kBuyer );
    SW_EXPECT_TRUE( first._result == EconomyResult::Ok );
    SW_EXPECT_TRUE( first._bReplayed == SW_FALSE );
    SW_EXPECT_EQUAL( string( "com.x.gem100" ), first._productID );
    SW_EXPECT_EQUAL( size_t( 2 ), first._listBalance.size() );
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kBuyer ) );
    SW_EXPECT_EQUAL( int64( 10 ), rig.getBalanceOf( kBuyer, "cur.gold" ) );

    const EconomyRedeemReply again = rig.redeem( kBuyer );
    SW_EXPECT_TRUE( again._result == EconomyResult::Ok );
    SW_EXPECT_TRUE( again._bReplayed == SW_TRUE );
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kBuyer ) );

    SW_EXPECT_TRUE( rig.redeem( kOther )._result == EconomyResult::AlreadyRedeemed );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kOther ) );
}

/** @brief [EconomyReceiptsTest] 외부 로그인 묶기: 답의 스토어 신원이 그 계정에 연동된 주체여야 받는다 — 남의 주문 번호를 먼저 낸 계정이 받던 결함이다. 진 뒤에도 진짜 구매자는 받는다 */
SW_TEST_CASE( EconomyReceiptsTest, LinkedLoginBuyerMustMatchTheAccount )
{
    ReceiptFixture    rig;
    FakeExternalLinks links;
    SW_EXPECT_TRUE( rig.redeemBound( kOther, ReceiptBuyerBinding::LinkedLogin, "S1", &links ) == EconomyResult::NotPurchaser );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kOther ) );
    SW_EXPECT_TRUE( rig.redeemBound( kBuyer, ReceiptBuyerBinding::LinkedLogin, "S2", &links ) == EconomyResult::NotPurchaser );
    SW_EXPECT_TRUE( rig.redeemBound( kBuyer, ReceiptBuyerBinding::LinkedLogin, "", &links ) == EconomyResult::NotPurchaser ); // 답이 구매자를 말하지 않으면 통과시키지 않는다
    SW_EXPECT_TRUE( rig.redeemBound( kBuyer, ReceiptBuyerBinding::LinkedLogin, "S1", nullptr ).hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kBuyer ) );

    links._read = ServiceStoreResult::Unavailable;
    SW_EXPECT_TRUE( rig.redeemBound( kBuyer, ReceiptBuyerBinding::LinkedLogin, "S1", &links ).hasError( OnlineError::kUnavailable ) );
    links._read = ServiceStoreResult::Ok;
    SW_EXPECT_TRUE( rig.redeemBound( kBuyer, ReceiptBuyerBinding::LinkedLogin, "S1", &links ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kBuyer ) );
}

/** @brief [EconomyReceiptsTest] 계정 토큰 묶기: 답의 토큰이 그 계정의 것이어야 받는다(대소문자 무시). 토큰이 없으면 까닭을 말하며 거절한다. 묶지 않는 규칙은 먼저 낸 계정이 받는다 */
SW_TEST_CASE( EconomyReceiptsTest, AccountTokenBuyerMustMatchTheAccount )
{
    SW_EXPECT_LOG( "EconomyStoreLogic.BuyerTokenMissing" );
    ReceiptFixture rig;
    const string   token = ReceiptBuyerToken::make( kBuyer );
    SW_EXPECT_TRUE( rig.redeemBound( kOther, ReceiptBuyerBinding::AccountToken, token, nullptr ) == EconomyResult::NotPurchaser );
    SW_EXPECT_TRUE( rig.redeemBound( kBuyer, ReceiptBuyerBinding::AccountToken, "", nullptr ) == EconomyResult::BuyerTokenMissing );
    string upper = token;
    for ( utf8& character : upper )
    {
        character = 'a' <= character && character <= 'f' ? static_cast<utf8>( character - 'a' + 'A' ) : character;
    }
    SW_EXPECT_TRUE( rig.redeemBound( kBuyer, ReceiptBuyerBinding::AccountToken, upper, nullptr ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kBuyer ) );

    SW_EXPECT_TRUE( rig.redeemBound( kOther, ReceiptBuyerBinding::None, "", nullptr, "tx2" ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kOther ) );
}

/** @brief [EconomyReceiptsTest] 계정 토큰: 애플이 받는 UUID 모양(36 자)이고 구글 상한(64 자) 안이며 계정의 모든 자리가 값을 바꾼다 */
SW_TEST_CASE( EconomyReceiptsTest, BuyerTokenIsAUUIDPerAccount )
{
    const string token = ReceiptBuyerToken::make( 0x0123456789ABCDEFull );
    SW_EXPECT_EQUAL( string( "01234567-89ab-4000-8000-00000000cdef" ), token );
    SW_EXPECT_EQUAL( size_t( 36 ), ReceiptBuyerToken::make( 1 ).size() );
    for ( int32 shift = 0; shift < 64; shift += 4 )
    {
        SW_EXPECT_NOT_EQUAL( ReceiptBuyerToken::make( 0 ), ReceiptBuyerToken::make( uint64( 1 ) << shift ) );
    }
}

/** @brief [EconomyReceiptsTest] 구매자를 외부 로그인으로 묶는 제공자가 있는데 연동 포트가 없으면 서비스가 기동을 거절한다 — 묶지 않은 채로 뜨지 않는다 */
SW_TEST_CASE( EconomyReceiptsTest, ServiceRefusesLinkedBindingWithoutThePort )
{
    SW_EXPECT_LOG( "EconomyService.EconomyServiceNeedsExternalLinks" );
    ReceiptFixture       data;
    MemoryServiceStore   store{ &data._database };
    SteamReceiptSettings steam;
    steam._buyerRule._binding       = ReceiptBuyerBinding::LinkedLogin;
    steam._buyerRule._loginProvider = "steam";
    SteamReceiptValidator    validator( steam, nullptr ); // 묻지 않으므로 클라이언트를 쓰지 않는다
    ReceiptValidatorRegistry registry;
    SW_EXPECT_TRUE( registry.registerValidator( &validator ) );
    SW_EXPECT_TRUE( registry.requiresExternalLinks() );

    EconomyServiceConfig settings;
    settings._pCurrencyCatalog = &data._currencies;
    settings._pOfferCatalog    = &data._offers;
    settings._pReceiptRegistry = &registry;
    EconomyService refused;
    SW_EXPECT_FALSE( refused.initialize( &store, settings ) );

    FakeExternalLinks links;
    settings._pExternalLinks = &links;
    EconomyService accepted;
    SW_EXPECT_TRUE( accepted.initialize( &store, settings ) );
    accepted.shutdown();
}

/** @brief [EconomyReceiptsTest] 지급 거절: 검증 결과 · 샌드박스 · 모르는 상품 · 계정 0 은 아무것도 주지 않는다 */
SW_TEST_CASE( EconomyReceiptsTest, RedeemRefusalsGrantNothing )
{
    ReceiptFixture rig;
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1", ReceiptStatus::Invalid )._result == EconomyResult::ReceiptInvalid );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1", ReceiptStatus::Refunded )._result == EconomyResult::ReceiptRefunded );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1", ReceiptStatus::Retry )._result == EconomyResult::ReceiptPending );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1", ReceiptStatus::Count )._result == EconomyResult::ReceiptInvalid );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1", ReceiptStatus::Valid, "com.x.unknown" )._result == EconomyResult::UnknownProduct );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1", ReceiptStatus::Valid, "com.x.gem100", true, false )._result == EconomyResult::ReceiptInvalid );
    (void)rig.redeem( kInvalidAccountID );
    SW_EXPECT_TRUE( rig._lastServiceResult.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kBuyer ) );

    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1", ReceiptStatus::Valid, "com.x.gem100", true, true )._result == EconomyResult::Ok ); // 개발 서버는 샌드박스를 받는다
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kBuyer ) );
}

/** @brief [EconomyReceiptsTest] 지급 응답을 잃어도 같은 거래를 다시 내면 한 번만 받는다 */
SW_TEST_CASE( EconomyReceiptsTest, RedeemSurvivesLostCommitReply )
{
    ReceiptFixture rig;
    rig._database.armFault( ServiceStoreFault::LoseCommitReply );
    (void)rig.redeem( kBuyer );
    const EconomyRedeemReply retried = rig.redeem( kBuyer );
    SW_EXPECT_TRUE( retried._result == EconomyResult::Ok );
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kBuyer ) );
}

/** @brief [EconomyReceiptsTest] 환불: 지급한 만큼 회수하고 이미 쓴 부분은 빚이 된다. 두 번 와도 한 번만 회수하고, 환불된 거래는 다시 받을 수 없다 */
SW_TEST_CASE( EconomyReceiptsTest, RefundRevokesOnceIntoDebt )
{
    ReceiptFixture rig;
    SW_EXPECT_TRUE( rig.redeem( kBuyer )._result == EconomyResult::Ok );
    rig.spend( kBuyer, 70 );

    vector<EconomyBalanceChange> listChange;
    SW_EXPECT_TRUE( rig.refund( "tx1", &listChange ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( size_t( 2 ), listChange.size() );
    SW_EXPECT_EQUAL( kBuyer, listChange[0]._accountID );
    SW_EXPECT_EQUAL( int64( -70 ), rig.getBalanceOf( kBuyer ) );

    SW_EXPECT_TRUE( rig.refund() == EconomyResult::AlreadyRefunded );
    SW_EXPECT_EQUAL( int64( -70 ), rig.getBalanceOf( kBuyer ) );
    SW_EXPECT_TRUE( rig.redeem( kBuyer )._result == EconomyResult::ReceiptRefunded );
    SW_EXPECT_EQUAL( int64( -70 ), rig.getBalanceOf( kBuyer ) );
}

/** @brief [EconomyReceiptsTest] 지급보다 먼저 온 환불은 표시만 남기고, 뒤에 온 지급을 막는다 */
SW_TEST_CASE( EconomyReceiptsTest, RefundBeforeRedeemBlocksIt )
{
    ReceiptFixture rig;
    SW_EXPECT_TRUE( rig.refund() == EconomyResult::Ok );
    SW_EXPECT_TRUE( rig.redeem( kBuyer )._result == EconomyResult::ReceiptRefunded );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kBuyer ) );
}

/** @brief [EconomyReceiptsTest] 환불 저장소 실패는 Unavailable 이고 스토어가 다시 보내면 한 번만 회수한다 */
SW_TEST_CASE( EconomyReceiptsTest, RefundRetriesAfterStoreFailure )
{
    ReceiptFixture rig;
    SW_EXPECT_TRUE( rig.redeem( kBuyer )._result == EconomyResult::Ok );
    rig._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.refund().hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kBuyer ) );
    SW_EXPECT_TRUE( rig.refund() == EconomyResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kBuyer ) );

    rig._database.armFault( ServiceStoreFault::RejectRead );
    SW_EXPECT_TRUE( rig.refund( "tx2" ).hasError( OnlineError::kUnavailable ) );
}

/** @brief [EconomyReceiptsTest] 청약철회: 쓰지 않은 만큼만 돌려받고 내역을 알린다. 다시 하면 AlreadyRefunded, 그 뒤 온 스토어 환불은 또 회수하지 않는다 */
SW_TEST_CASE( EconomyReceiptsTest, WithdrawTakesBackOnlyUnused )
{
    ReceiptFixture rig;
    SW_EXPECT_TRUE( rig.redeem( kBuyer )._result == EconomyResult::Ok );
    rig.spend( kBuyer, 70 );

    const EconomyWithdrawReply withdrawn = rig.withdraw( kBuyer );
    SW_EXPECT_TRUE( withdrawn._result == EconomyResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 2 ), withdrawn._listWithdrawn.size() );
    SW_EXPECT_EQUAL( int64( 30 ), withdrawn._listWithdrawn[0]._amount + withdrawn._listWithdrawn[1]._amount - 10 );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kBuyer ) );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kBuyer, "cur.gold" ) );

    SW_EXPECT_TRUE( rig.withdraw( kBuyer )._result == EconomyResult::AlreadyRefunded );
    SW_EXPECT_TRUE( rig.refund() == EconomyResult::AlreadyRefunded );
    SW_EXPECT_EQUAL( int64( 0 ), rig.getBalanceOf( kBuyer ) );
}

/** @brief [EconomyReceiptsTest] 청약철회 거절: 다른 계정 · 없는 거래는 NotPurchaser, 기한이 지나면 WithdrawalExpired, 다 썼으면 NothingToWithdraw 다 */
SW_TEST_CASE( EconomyReceiptsTest, WithdrawRefusals )
{
    ReceiptFixture rig;
    SW_EXPECT_TRUE( rig.redeem( kBuyer )._result == EconomyResult::Ok );
    SW_EXPECT_TRUE( rig.withdraw( kOther )._result == EconomyResult::NotPurchaser );
    SW_EXPECT_TRUE( rig.withdraw( kBuyer, kNowMs, "tx-none" )._result == EconomyResult::NotPurchaser );
    (void)rig.withdraw( kInvalidAccountID );
    SW_EXPECT_TRUE( rig._lastServiceResult.hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_TRUE( rig.withdraw( kBuyer, kNowMs + kWindowMs + 1 )._result == EconomyResult::WithdrawalExpired );
    SW_EXPECT_EQUAL( int64( 100 ), rig.getBalanceOf( kBuyer ) );

    rig.spend( kBuyer, 100 );
    rig.spend( kBuyer, 10, "cur.gold" );
    SW_EXPECT_TRUE( rig.withdraw( kBuyer, kNowMs + kWindowMs )._result == EconomyResult::NothingToWithdraw ); // 기한 끝 시각은 안이다
}

/** @brief [EconomyReceiptsTest] 지갑과 내역: 지급 · 사용이 부호와 함께 최근 내역에 나온다 */
SW_TEST_CASE( EconomyReceiptsTest, WalletAndHistoryReflectMoves )
{
    ReceiptFixture rig;
    SW_EXPECT_TRUE( rig.redeem( kBuyer )._result == EconomyResult::Ok );
    rig.spend( kBuyer, 30 );

    EconomyWalletReply wallet;
    SW_EXPECT_TRUE( EconomyStoreLogic::readWallet( rig._database, kBuyer, wallet ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( size_t( 2 ), wallet._listBalance.size() );

    EconomyHistoryRequest request;
    request._maxCount = 10;
    EconomyHistoryReply history;
    SW_EXPECT_TRUE( EconomyStoreLogic::readHistory( rig._database, kBuyer, request, history ) == EconomyResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 2 ), history._listHistory.size() );
    int64 total = 0;
    for ( const EconomyHistoryEntry& entry : history._listHistory )
    {
        for ( const LedgerBalance& change : entry._listChange )
        {
            if ( change._assetID == "cur.gem_paid" )
                total += change._amount;
        }
    }
    SW_EXPECT_EQUAL( int64( 70 ), total );
}

/** @brief [EconomyReceiptsTest] 서비스: 영수증은 스토어 검증 뒤에 지급한다. 등록 안 된 스토어 · 깨진 영수증 · 샌드박스는 거절, 멱등 키 없는 구매 · 로그인 전 요청도 거절한다 */
SW_TEST_CASE( EconomyReceiptsTest, ServiceRedeemsThroughTheValidator )
{
    EconomyServiceFixture  rig;
    const EconomyCallReply redeemed = rig.redeem( kBuyer, "fake|com.x.gem100|tx1|ok" );
    SW_EXPECT_TRUE( redeemed._result == EconomyResult::Ok );
    SW_EXPECT_EQUAL( int64( 100 ), rig._data.getBalanceOf( kBuyer ) );

    SW_EXPECT_TRUE( rig.redeem( kBuyer, "not a receipt" )._result == EconomyResult::ReceiptInvalid );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "fake|com.x.gem100|tx2|sandbox" )._result == EconomyResult::ReceiptInvalid );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "fake|com.x.gem100|tx3|refunded" )._result == EconomyResult::ReceiptRefunded );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "fake|com.x.gem100|tx4|retry" )._result == EconomyResult::ReceiptPending );

    EconomyCall other{ UnixMilliseconds{ kNowMs } };
    other._redeem._storeName = "apple";
    other._redeem._payload   = "x";
    SW_EXPECT_TRUE( rig.call( EconomyMethod::RedeemReceipt::kID, kBuyer, &other )._result == EconomyResult::ReceiptInvalid );

    (void)rig.call( EconomyMethod::Purchase::kID, kBuyer );
    SW_EXPECT_TRUE( rig._lastServiceResult.hasError( OnlineError::kInvalidRequest ) );
    (void)rig.call( EconomyMethod::GetWallet::kID, kInvalidAccountID );
    SW_EXPECT_TRUE( rig._lastServiceResult.hasError( OnlineError::kUnauthenticated ) );
    SW_EXPECT_EQUAL( size_t( 2 ), rig.call( EconomyMethod::GetWallet::kID, kBuyer )._listBalance.size() );
}

/** @brief [EconomyReceiptsTest] 서비스: 스토어가 보낸 환불 알림을 회수한다 */
SW_TEST_CASE( EconomyReceiptsTest, ServiceRevokesPushedRefunds )
{
    EconomyServiceFixture rig;
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "fake|com.x.gem100|tx1|ok" )._result == EconomyResult::Ok );
    rig._validator.pushRefund( "tx1", kNowMs );
    rig._validator.pushRefund( "tx1", kNowMs ); // 스토어는 다시 보낼 수 있다
    rig.pump();
    SW_EXPECT_EQUAL( int64( 0 ), rig._data.getBalanceOf( kBuyer ) );
}

/** @brief [EconomyReceiptsTest] 원장 보존 검사: 지급 · 사용 · 환불 뒤에도 잔액 합이 발행 - 소각과 맞는다(환불의 빚도 포함) */
SW_TEST_CASE( EconomyReceiptsTest, LedgerAuditStaysBalanced )
{
    EconomyServiceFixture rig;
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "fake|com.x.gem100|tx1|ok" )._result == EconomyResult::Ok );
    rig._data.spend( kBuyer, 70 );
    rig._validator.pushRefund( "tx1", kNowMs );
    rig.pump();
    rig._service.requestLedgerAudit();
    rig.pump();
    SW_EXPECT_TRUE( rig._service.wasLastAuditBalanced() );
}

/** @brief [EconomyReceiptsTest] 지급 알림: 지급이 커밋된 뒤에만 스토어에 맡기고 레코드가 함께 남으며, 받았다는 답에 지운다. 지급 분개와 레코드는 한 트랜잭션이다 */
SW_TEST_CASE( EconomyReceiptsTest, DeliveryIsConfirmedAfterTheGrant )
{
    ReceiptFixture         data;
    MemoryServiceStore     store{ &data._database };
    DeliveryServiceFixture rig( data, store );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1" ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( int64( 100 ), data.getBalanceOf( kBuyer ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._validator._listAsked.size() );
    SW_EXPECT_EQUAL( string( "key.tx1" ), rig._validator._listAsked[0]._deliveryKey );
    SW_EXPECT_EQUAL( string( "fake" ), rig._validator._listAsked[0]._storeName );
    SW_EXPECT_EQUAL( size_t( 1 ), DeliveryTestUtil::countRecords( data ) );

    rig._validator.answer( 0, ReceiptDeliveryStatus::Confirmed );
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 0 ), DeliveryTestUtil::countRecords( data ) );
}

/** @brief [EconomyReceiptsTest] 지급이 지면 알리지 않는다 — 스토어가 환불하게 둔다. 저장소 실패 · 다른 구매자 · 이미 다른 계정이 받은 거래 모두 레코드도 알림도 없다. 저장소가 돌아와 다시 내면 그때 알린다 */
SW_TEST_CASE( EconomyReceiptsTest, FailedGrantIsNotConfirmed )
{
    ReceiptFixture         data;
    MemoryServiceStore     store{ &data._database };
    DeliveryServiceFixture rig( data, store );
    rig.pump(); // 기동 직후의 훑기를 먼저 끝낸다

    data._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1" ).hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( rig._validator._listAsked.empty() );
    SW_EXPECT_EQUAL( size_t( 0 ), DeliveryTestUtil::countRecords( data ) );
    SW_EXPECT_EQUAL( int64( 0 ), data.getBalanceOf( kBuyer ) );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1" ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._validator._listAsked.size() );

    SW_EXPECT_TRUE( rig.redeem( kOther, "tx1" ) == EconomyResult::AlreadyRedeemed );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._validator._listAsked.size() );

    rig._validator._buyerRule._binding = ReceiptBuyerBinding::AccountToken;
    rig._validator._storeBuyerID       = ReceiptBuyerToken::make( kBuyer );
    SW_EXPECT_TRUE( rig.redeem( kOther, "tx2" ) == EconomyResult::NotPurchaser );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._validator._listAsked.size() );
    SW_EXPECT_EQUAL( size_t( 1 ), DeliveryTestUtil::countRecords( data ) );
}

/** @brief [EconomyReceiptsTest] 지급 알림의 재시도: 닿지 못하면 레코드가 남아 훑기 간격마다 다시 보내고, 날아가는 동안은 겹쳐 보내지 않으며, 거절은 지운다 */
SW_TEST_CASE( EconomyReceiptsTest, DeliveryRetriesUntilAnswered )
{
    ReceiptFixture         data;
    MemoryServiceStore     store{ &data._database };
    DeliveryServiceFixture rig( data, store );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1" ) == EconomyResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._validator._listAsked.size() );

    rig._nowMs += 1000;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), rig._validator._listAsked.size() ); // 아직 날아가는 중이다

    rig._validator.answer( 0, ReceiptDeliveryStatus::Retry );
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 1 ), DeliveryTestUtil::countRecords( data ) );
    SW_EXPECT_EQUAL( size_t( 1 ), rig._validator._listAsked.size() ); // 간격 전이다

    rig._nowMs += 1000;
    rig.pump();
    SW_REQUIRE_EQUAL( size_t( 2 ), rig._validator._listAsked.size() );
    rig._validator.answer( 1, ReceiptDeliveryStatus::Refused );
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 0 ), DeliveryTestUtil::countRecords( data ) );
}

/** @brief [EconomyReceiptsTest] 재시작: 지급 뒤 알림을 끝내지 못하고 내려가도 다음 기동이 레코드를 읽어 다시 보낸다 — 메모리에만 있으면 3 일 뒤 스토어가 환불해 지급이 회수된다 */
SW_TEST_CASE( EconomyReceiptsTest, DeliverySurvivesARestart )
{
    ReceiptFixture     data;
    MemoryServiceStore store{ &data._database };
    {
        DeliveryServiceFixture before( data, store );
        SW_EXPECT_TRUE( before.redeem( kBuyer, "tx1" ) == EconomyResult::Ok );
    }
    DeliveryServiceFixture after( data, store );
    after.pump();
    SW_REQUIRE_EQUAL( size_t( 1 ), after._validator._listAsked.size() );
    SW_EXPECT_EQUAL( string( "key.tx1" ), after._validator._listAsked[0]._deliveryKey );
    SW_EXPECT_EQUAL( kFarDeadlineMs, after._validator._listAsked[0]._deadlineMs );
}

/** @brief [EconomyReceiptsTest] 같은 거래를 다시 내면 재생이지만 스토어가 아직 확인 전이라 했으니 다시 알린다 */
SW_TEST_CASE( EconomyReceiptsTest, ReplayedGrantIsConfirmedAgain )
{
    ReceiptFixture         data;
    MemoryServiceStore     store{ &data._database };
    DeliveryServiceFixture rig( data, store );
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1" ) == EconomyResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), rig._validator._listAsked.size() );
    rig._validator.answer( 0, ReceiptDeliveryStatus::Confirmed );
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 0 ), DeliveryTestUtil::countRecords( data ) );

    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1" ) == EconomyResult::Ok );
    SW_EXPECT_EQUAL( int64( 100 ), data.getBalanceOf( kBuyer ) );
    SW_EXPECT_EQUAL( size_t( 2 ), rig._validator._listAsked.size() );
    SW_EXPECT_EQUAL( size_t( 1 ), DeliveryTestUtil::countRecords( data ) );
}

/** @brief [EconomyReceiptsTest] 기한이 가까운데 끝나지 않은 지급 알림은 오류 로그를 한 번 남긴다(사람이 볼 시간) */
SW_TEST_CASE( EconomyReceiptsTest, DeliveryNearTheDeadlineAlarmsOnce )
{
    SW_EXPECT_LOG( "EconomyService.DeliveryNearDeadline" );
    ReceiptFixture         data;
    MemoryServiceStore     store{ &data._database };
    DeliveryServiceFixture rig( data, store );
    rig._validator._deadlineMs = kNowMs + 1000;
    SW_EXPECT_TRUE( rig.redeem( kBuyer, "tx1" ) == EconomyResult::Ok );
    rig._validator.answer( 0, ReceiptDeliveryStatus::Retry );
    rig._nowMs += 1000; // 기한이 경고 폭(100) 안으로 왔다
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 2 ), rig._validator._listAsked.size() );
    rig._validator.answer( 1, ReceiptDeliveryStatus::Retry );
    rig._nowMs += 1000;
    rig.pump();
    SW_EXPECT_EQUAL( size_t( 3 ), rig._validator._listAsked.size() ); // 기한이 지나도 스토어가 거절할 때까지 보낸다
}

/** @brief [EconomyReceiptsTest] 지급 알림 레코드: 판 번호가 다르거나 깨진 레코드는 오류와 함께 건너뛰고 나머지를 읽는다. 알림 키가 없는 결과는 일감을 만들지 않는다 */
SW_TEST_CASE( EconomyReceiptsTest, DeliveryRecordRules )
{
    SW_EXPECT_LOG( "EconomyStoreLogic.DeliveryRecordUnreadable" );
    ReceiptFixture          rig;
    ReceiptValidationResult receipt;
    receipt._storeName     = "fake";
    receipt._transactionID = "tx1";
    ReceiptDeliveryTask task;
    SW_EXPECT_FALSE( EconomyStoreLogic::makeDeliveryTask( receipt, task ) );
    receipt._deliveryKey = "k";
    SW_EXPECT_TRUE( EconomyStoreLogic::makeDeliveryTask( receipt, task ) );
    SW_EXPECT_FALSE( task._recordKey.empty() );

    ServiceTransaction broken;
    broken.put( EconomyStoreLogic::getDeliveryTable(), "a", vector<uint8>{ 0xFF } );
    SW_EXPECT_TRUE( rig._database.commit( broken ) == ServiceStoreResult::Ok );
    vector<ReceiptDeliveryTask> listTask;
    SW_EXPECT_TRUE( EconomyStoreLogic::listDeliveries( rig._database, 10, listTask ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( listTask.empty() );
    SW_EXPECT_TRUE( EconomyStoreLogic::eraseDelivery( rig._database, "a" ) == ServiceStoreResult::Ok );
}

/** @brief [EconomyReceiptsTest] 환불 조회 기준: 기동 때 저장소에서 읽어 넘기고, 옮겨진 기준을 쓰며, 다시 켠 서비스가 그 값을 받는다 — 전에는 메모리에만 있어 재시작이 구간을 비웠다 */
SW_TEST_CASE( EconomyReceiptsTest, RefundCursorSurvivesARestart )
{
    ReceiptFixture     data;
    MemoryServiceStore store{ &data._database };
    const auto         makeSettings = [&data]( ReceiptValidatorRegistry& registry )
    {
        EconomyServiceConfig settings;
        settings._pCurrencyCatalog = &data._currencies;
        settings._pOfferCatalog    = &data._offers;
        settings._pReceiptRegistry = &registry;
        return settings;
    };
    const auto pump = [&store]( EconomyService& service, int64 nowMs )
    {
        for ( int32 round = 0; round < 4; ++round )
        {
            service.update( UnixMilliseconds{ nowMs } );
            while ( store.pollCompletions() > 0 )
            {
            }
        }
    };
    {
        CursorRecordingValidator validator;
        ReceiptValidatorRegistry registry;
        SW_EXPECT_TRUE( registry.registerValidator( &validator ) );
        EconomyService service;
        SW_EXPECT_TRUE( service.initialize( &store, makeSettings( registry ) ) );
        pump( service, kNowMs );
        SW_REQUIRE_EQUAL( size_t( 1 ), validator._listRestored.size() );
        SW_EXPECT_EQUAL( int64( 0 ), validator._listRestored[0] ); // 첫 기동이라 기준이 없다
        validator._movedMs = kNowMs + 5;
        pump( service, kNowMs );
        service.shutdown();
    }
    CursorRecordingValidator validator;
    ReceiptValidatorRegistry registry;
    SW_EXPECT_TRUE( registry.registerValidator( &validator ) );
    EconomyService service;
    SW_EXPECT_TRUE( service.initialize( &store, makeSettings( registry ) ) );
    pump( service, kNowMs + 100 );
    SW_REQUIRE_EQUAL( size_t( 1 ), validator._listRestored.size() ); // 한 번만 넘긴다
    SW_EXPECT_EQUAL( kNowMs + 5, validator._listRestored[0] );
    service.shutdown();
}

/** @brief [EconomyReceiptsTest] 환불 조회 기준을 읽지 못하면 넘기지 않아 조회가 시작되지 않고, 다시 읽어 넘긴다 — 기준 없이 시작하면 구간을 건너뛴다 */
SW_TEST_CASE( EconomyReceiptsTest, UnreadableRefundCursorHoldsThePolling )
{
    SW_EXPECT_LOG( "EconomyService.RefundCursorReadFailed" );
    ReceiptFixture           data;
    MemoryServiceStore       store{ &data._database };
    CursorRecordingValidator validator;
    ReceiptValidatorRegistry registry;
    SW_EXPECT_TRUE( registry.registerValidator( &validator ) );
    EconomyServiceConfig settings;
    settings._pCurrencyCatalog    = &data._currencies;
    settings._pOfferCatalog       = &data._offers;
    settings._pReceiptRegistry    = &registry;
    settings._refundCursorRetryMs = 1000;
    EconomyService service;
    SW_EXPECT_TRUE( service.initialize( &store, settings ) );

    data._database.armFault( ServiceStoreFault::RejectRead );
    service.update( UnixMilliseconds{ kNowMs } );
    while ( store.pollCompletions() > 0 )
    {
    }
    service.update( UnixMilliseconds{ kNowMs + 999 } ); // 간격 전에는 다시 읽지 않는다
    while ( store.pollCompletions() > 0 )
    {
    }
    SW_EXPECT_TRUE( validator._listRestored.empty() );

    service.update( UnixMilliseconds{ kNowMs + 1000 } );
    while ( store.pollCompletions() > 0 )
    {
    }
    SW_EXPECT_EQUAL( size_t( 1 ), validator._listRestored.size() );
    service.shutdown();
}

/** @brief [EconomyReceiptsTest] 기준 시각의 저장 규칙: 없으면 0, 쓴 값을 읽고, 깨진 값은 오류와 Invalid 다 */
SW_TEST_CASE( EconomyReceiptsTest, RefundCursorRecord )
{
    SW_EXPECT_LOG( "EconomyStoreLogic.RefundCursorUnreadable" );
    ReceiptFixture rig;
    int64          lastReadMs = 7;
    SW_EXPECT_TRUE( EconomyStoreLogic::readRefundCursor( rig._database, "google", lastReadMs ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), lastReadMs );
    SW_EXPECT_TRUE( EconomyStoreLogic::writeRefundCursor( rig._database, "google", kNowMs ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( EconomyStoreLogic::readRefundCursor( rig._database, "google", lastReadMs ) == ServiceStoreResult::Ok );
    SW_EXPECT_EQUAL( kNowMs, lastReadMs );

    ServiceTransaction broken;
    broken.put( EconomyStoreLogic::getRefundCursorTable(), "apple", vector<uint8>{} );
    SW_EXPECT_TRUE( rig._database.commit( broken ) == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( EconomyStoreLogic::readRefundCursor( rig._database, "apple", lastReadMs ) == ServiceStoreResult::Invalid );
}

/** @brief [EconomyReceiptsTest] 시작: 저장소 · 카탈로그가 없으면 서지 않는다 */
SW_TEST_CASE( EconomyReceiptsTest, ServiceNeedsDependencies )
{
    SW_EXPECT_LOG( "EconomyService.EconomyServiceNeedsStoreBoth" );
    EconomyService service;
    SW_EXPECT_FALSE( service.initialize( nullptr, EconomyServiceConfig{} ) );
}
