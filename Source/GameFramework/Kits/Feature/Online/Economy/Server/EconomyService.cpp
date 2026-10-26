#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"

#include "Core/Network/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/Base/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceStoreUtil.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

namespace sw
{
    SW_LOG_CALLER( "EconomyService" );

    namespace
    {
        struct EconomyServiceInternal
        {
            static constexpr int32 kWalletIndex   = EconomyMethod::toIndex( EconomyMethod::kGetWallet );
            static constexpr int32 kHistoryIndex  = EconomyMethod::toIndex( EconomyMethod::kGetHistory );
            static constexpr int32 kPurchaseIndex = EconomyMethod::toIndex( EconomyMethod::kPurchase );
            static constexpr int32 kRedeemIndex   = EconomyMethod::toIndex( EconomyMethod::kRedeemReceipt );
            static constexpr int32 kWithdrawIndex = EconomyMethod::toIndex( EconomyMethod::kWithdrawPurchase );
        };

        /** @brief 경제 요청 하나의 저장소 왕복입니다. 입력은 공개 필드에 값으로 채워 맡기고, 결과는 `complete` 에서 서비스에 넘깁니다. */
        class EconomyRequestWork final : public IServiceStoreWork
        {
        public:
            EconomyPurchaseInput  _purchase;
            EconomyRedeemInput    _redeem;
            EconomyHistoryRequest _history;
            EconomyWithdrawInput  _withdraw;
            AccountID             _accountID;

            EconomyRequestWork( EconomyService* pService, const EconomyServiceConfig& settings, uint64 callID, int32 methodIndex )
                : _purchase{}
                , _redeem{}
                , _history{}
                , _withdraw{}
                , _accountID{ kInvalidAccountID }
                , _reply{}
                , _settings{ settings }
                , _pService{ pService }
                , _callID{ callID }
                , _methodIndex{ methodIndex }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                switch ( _methodIndex )
                {
                    case EconomyServiceInternal::kWalletIndex:
                    {
                        // 결과는 finish 가 _reply._result 에 담는다 — complete 가 그대로 돌려준다
                        (void)EconomyStoreLogic::readWallet( connection, _accountID, _reply );
                        break;
                    }
                    case EconomyServiceInternal::kHistoryIndex:
                    {
                        // 결과는 finish 가 _reply._result 에 담는다 — complete 가 그대로 돌려준다
                        (void)EconomyStoreLogic::readHistory( connection, _accountID, _history, _reply );
                        break;
                    }
                    case EconomyServiceInternal::kPurchaseIndex:
                    {
                        (void)EconomyStoreLogic::purchase( connection, *_settings._pCurrencyCatalog, *_settings._pOfferCatalog, _purchase, _reply );
                        break;
                    }
                    case EconomyServiceInternal::kRedeemIndex:
                    {
                        (void)EconomyStoreLogic::redeemReceipt( connection, *_settings._pCurrencyCatalog, *_settings._pOfferCatalog, _redeem, _reply );
                        break;
                    }
                    case EconomyServiceInternal::kWithdrawIndex:
                    {
                        // 결과는 finish 가 _reply._result 에 담는다 — complete 가 그대로 돌려준다
                        (void)EconomyStoreLogic::withdrawPurchase( connection, *_settings._pCurrencyCatalog, _withdraw, _reply );
                        break;
                    }
                    default:
                    {
                        _reply._result = EconomyResult::InvalidRequest;
                        break;
                    }
                }
            }

            void complete() override { _pService->completeCall( _callID, _reply ); }

        private:
            EconomyReply         _reply;
            EconomyServiceConfig _settings;
            EconomyService*      _pService;
            uint64               _callID;
            int32                _methodIndex;
        };
        /** @brief 원장 보존 검사 한 번입니다. 테이블 전체를 읽는다. */
        class EconomyLedgerAuditWork final : public IServiceStoreWork
        {
        public:
            explicit EconomyLedgerAuditWork( EconomyService* pService )
                : _report{}
                , _pService{ pService }
                , _result{ ServiceStoreResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = LedgerAudit::computeReport( connection, _report ); }
            void complete() override { _pService->completeLedgerAudit( _result, _report ); }

        private:
            LedgerAuditReport  _report;
            EconomyService*    _pService;
            ServiceStoreResult _result;
        };

        /** @brief 환불 알림 하나의 회수입니다. 응답이 없다. */
        class EconomyRefundWork final : public IServiceStoreWork
        {
        public:
            EconomyRefundWork( EconomyService* pService, const CurrencyCatalog* pCurrencies, ReceiptRefundNotice&& notice, int64 nowMs )
                : _notice{ std::move( notice ) }
                , _pService{ pService }
                , _pCurrencies{ pCurrencies }
                , _nowMs{ nowMs }
                , _result{ EconomyResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = EconomyStoreLogic::revokeRefund( connection, *_pCurrencies, _notice, _nowMs, _listChange ); }
            void complete() override { _pService->completeRefund( _result, _listChange ); }

        private:
            ReceiptRefundNotice          _notice;
            vector<EconomyBalanceChange> _listChange;
            EconomyService*              _pService;
            const CurrencyCatalog*       _pCurrencies;
            int64                        _nowMs;
            EconomyResult                _result;
        };
    } // namespace
} // namespace sw

namespace sw
{
    EconomyService::EconomyService()
        : _listPendingCall{}
        , _metrics{}
        , _settings{}
        , _mapPresenceRequestToNotice{}
        , _pHost{ nullptr }
        , _pStore{ nullptr }
        , _pMetricRegistry{ nullptr }
        , _nextCallID{ 1 }
        , _pendingRefundCount{ 0 }
        , _auditRun{}
        , _bAuditPending{ SW_FALSE }
        , _bAuditRunHeld{ SW_FALSE }
        , _bLastAuditBalanced{ SW_FALSE }
        , _nowMs{ 0 }
    {
    }

    EconomyService::~EconomyService()
    {
        SW_ASSERT( _listPendingCall.empty() );
    }

    bool EconomyService::initialize( IServiceStore* pStore, const EconomyServiceConfig& settings )
    {
        const bool bMissingDependencies = pStore == nullptr || settings._pCurrencyCatalog == nullptr || settings._pOfferCatalog == nullptr;
        if ( bMissingDependencies )
        {
            SW_LOG_ERROR( "EconomyService needs a store and both catalogs" );
            return false;
        }
#if defined( SW_SHIPPING )
        if ( settings._pReceiptRegistry != nullptr && settings._pReceiptRegistry->hasDevelopmentValidator() )
        {
            SW_LOG_ERROR( "EconomyService refuses a development-only receipt validator in a shipping build" );
            return false;
        }
#endif
        _pStore          = pStore;
        _settings        = settings;
        _findOwner       = settings._pPresence != nullptr ? settings._pPresence->openFindOwner() : Subscription{};
        _pMetricRegistry = settings._pMetricRegistry;
        if ( settings._pScheduler != nullptr )
        {
            ScheduleDefinition schedule;
            schedule._jobID       = kAuditJobID;
            schedule._kind        = ScheduleKind::Daily;
            schedule._minuteOfDay = settings._auditMinuteOfDay;
            if ( settings._pScheduler->registerJob( schedule, this ) == false )
            {
                SW_LOG_ERROR( "EconomyService: the ledger audit job could not be scheduled (minute of day %#)", settings._auditMinuteOfDay );
                return false;
            }
        }
        _metrics.initialize<EconomyMethodName, EconomyResult>( settings._pMetricRegistry, "economy" );
        return true;
    }

    void EconomyService::shutdown()
    {
        // 영수증 검증 완료도 호출을 끝내므로 단계마다 같이 회수한다.
        ServiceStoreUtil::drainUntilIdle( _pStore, [this]()
        { return _listPendingCall.empty() && _pendingRefundCount <= 0 && _bAuditPending == SW_FALSE; }, [this]()
        { pollReceipts(); } );
        if ( _listPendingCall.empty() == false )
        {
            SW_LOG_ERROR( "EconomyService shut down with %# calls still pending", _listPendingCall.size() );
            _listPendingCall.clear();
        }
        _findOwner.reset(); // 잔액 알림 찾기가 이 객체를 부르지 않게 한다(저장 작업을 회수한 뒤 — 그 완료가 찾기를 낼 수 있다)
        _mapPresenceRequestToNotice.clear();
        _pStore = nullptr;
    }

    void EconomyService::submitCall( const EconomyCall& call, ReplyDelegate onReply )
    {
        PendingCall pending;
        pending._onReply = onReply;
        startCall( call, pending );
    }

    void EconomyService::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        const bool bMutating = context._method == EconomyMethod::kPurchase || context._method == EconomyMethod::kRedeemReceipt ||
                               context._method == EconomyMethod::kWithdrawPurchase;
        const RemoteConfig* pConfig          = host.getRemoteConfig();
        const bool          bFeatureDisabled = bMutating && pConfig != nullptr && pConfig->isFeatureEnabled( kFeatureFlag, context._accountID, true ) == false;
        if ( bFeatureDisabled )
        {
            (void)host.respondError( context._token, OnlineError::kFeatureDisabled );
            return;
        }
        EconomyCall call;
        call._method               = context._method;
        call._accountID            = context._accountID;
        call._idempotencyKey       = context._idempotencyKey;
        call._nowMs                = context._nowMs;
        FixedReadResult readResult = FixedReadResult::Ok;
        switch ( context._method )
        {
            case EconomyMethod::kGetHistory:
            {
                readResult = EconomyProtocol::MessageTypes::read( body, call._history );
                break;
            }
            case EconomyMethod::kPurchase:
            {
                readResult = EconomyProtocol::MessageTypes::read( body, call._purchase );
                break;
            }
            case EconomyMethod::kRedeemReceipt:
            {
                readResult = EconomyProtocol::MessageTypes::read( body, call._redeem );
                break;
            }
            case EconomyMethod::kWithdrawPurchase:
            {
                readResult = EconomyProtocol::MessageTypes::read( body, call._withdraw );
                break;
            }
            default:
            {
                break;
            }
        }
        if ( readResult != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        PendingCall pending;
        pending._pHost = &host;
        pending._token = context._token;
        startCall( call, pending );
    }

    void EconomyService::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        _pHost = &host;
        (void)host;
        update( nowMs );
    }

    void EconomyService::update( int64 nowMs )
    {
        _nowMs = nowMs;
        if ( _pStore == nullptr )
            return;
        pollReceipts();
        pollRefunds();
        _metrics.setPendingStoreWorkCount( static_cast<int32>( _listPendingCall.size() ) );
    }

    void EconomyService::startCall( const EconomyCall& call, PendingCall pending )
    {
        pending._callID              = _nextCallID++;
        pending._receivedNanoseconds = MonotonicClock::nowNanoseconds();
        pending._accountID           = call._accountID;
        pending._methodIndex         = EconomyMethod::toIndex( call._method );
        _listPendingCall.push_back( pending );
        const size_t pendingIndex = _listPendingCall.size() - 1;

        EconomyReply refused;
        refused._result = EconomyResult::Ok;
        if ( pending._methodIndex < 0 || _pStore == nullptr )
            refused._result = EconomyResult::InvalidRequest;
        else if ( call._accountID == kInvalidAccountID )
            refused._result = EconomyResult::NotSignedIn; // 호스트가 이미 막지만 submitCall 경로도 같은 규칙을 따른다
        else if ( call._method == EconomyMethod::kPurchase && call._idempotencyKey.isValid() == false )
            refused._result = EconomyResult::InvalidRequest; // 상태를 바꾸는 요청에는 멱등 키가 필수이다
        if ( refused._result != EconomyResult::Ok )
        {
            finishCall( pendingIndex, refused );
            return;
        }
        if ( call._method == EconomyMethod::kRedeemReceipt )
        {
            const uint64 ticket = _settings._pReceiptRegistry != nullptr
                                    ? _settings._pReceiptRegistry->submitValidation( call._redeem._storeName, call._redeem._payload, call._accountID )
                                    : 0;
            if ( ticket == 0 )
            {
                refused._result = EconomyResult::ReceiptInvalid;
                finishCall( pendingIndex, refused );
                return;
            }
            _listPendingCall[pendingIndex]._receiptTicket = ticket; // 지급 작업은 검증 결과가 오면 맡긴다(pollReceipts)
            return;
        }
        const int64                    nowMs = call._nowMs != 0 ? call._nowMs : _nowMs;
        unique_ptr<EconomyRequestWork> work  = make_unique<EconomyRequestWork>( this, _settings, pending._callID, pending._methodIndex );
        work->_accountID                     = call._accountID;
        work->_history                       = call._history;
        work->_purchase._offerID             = call._purchase._offerID;
        work->_purchase._count               = call._purchase._count;
        work->_purchase._accountID           = call._accountID;
        work->_purchase._nowMs               = nowMs;
        work->_purchase._journalKey          = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( call._accountID ), call._idempotencyKey._high,
                                                                                      call._idempotencyKey._low );
        work->_withdraw._storeName           = call._withdraw._storeName;
        work->_withdraw._transactionID       = call._withdraw._transactionID;
        work->_withdraw._accountID           = call._accountID;
        work->_withdraw._nowMs               = nowMs;
        work->_withdraw._windowMs            = _settings._withdrawalWindowMs;
        _pStore->submit( std::move( work ) );
    }

    void EconomyService::pollReceipts()
    {
        if ( _settings._pReceiptRegistry == nullptr )
            return;
        vector<ReceiptValidationResult> listResult;
        _settings._pReceiptRegistry->pollCompletions( listResult );
        for ( ReceiptValidationResult& result : listResult )
        {
            for ( PendingCall& pending : _listPendingCall )
            {
                if ( pending._receiptTicket != result._ticket )
                    continue;
                pending._receiptTicket              = 0;
                unique_ptr<EconomyRequestWork> work = make_unique<EconomyRequestWork>( this, _settings, pending._callID, EconomyServiceInternal::kRedeemIndex );
                work->_accountID                    = pending._accountID;
                work->_redeem._receipt              = std::move( result );
                work->_redeem._accountID            = pending._accountID;
                work->_redeem._nowMs                = _nowMs;
                work->_redeem._bAcceptSandbox       = _settings._bAcceptSandboxReceipts;
                _pStore->submit( std::move( work ) );
                break;
            }
        }
    }

    void EconomyService::onScheduledRun( const ScheduledRun& run )
    {
        _auditRun      = run;
        _bAuditRunHeld = SW_TRUE;
        requestLedgerAudit();
    }

    void EconomyService::requestLedgerAudit()
    {
        if ( _pStore == nullptr || _bAuditPending == SW_TRUE )
            return;
        _bAuditPending = SW_TRUE;
        _pStore->submit( make_unique<EconomyLedgerAuditWork>( this ) );
    }

    void EconomyService::completeLedgerAudit( ServiceStoreResult result, const LedgerAuditReport& report )
    {
        _bAuditPending      = SW_FALSE;
        const bool bRead    = result == ServiceStoreResult::Ok;
        _bLastAuditBalanced = bRead && report.isBalanced() ? SW_TRUE : SW_FALSE;
        if ( bRead == false )
            SW_LOG_ERROR( "EconomyService: the ledger audit could not read the store" );
        else if ( report.isBalanced() == false )
            SW_LOG_ERROR( "EconomyService: the ledger is not balanced (negative escrows %#, unreadable records %#)", report._negativeEscrowCount, report._unreadableRecordCount );
        if ( bRead && _pMetricRegistry != nullptr )
        {
            for ( const LedgerAssetAudit& asset : report._listAsset )
            {
                const vector<MetricLabel> listLabel = {
                    { "asset", asset._assetID }
                };
                MetricGauge* pIssued    = _pMetricRegistry->registerGauge( "economy_issued_total", "Amount minted per asset (ledger audit)", listLabel );
                MetricGauge* pBurned    = _pMetricRegistry->registerGauge( "economy_burned_total", "Amount burned per asset (ledger audit)", listLabel );
                MetricGauge* pHeld      = _pMetricRegistry->registerGauge( "economy_held_total", "Amount held by accounts and escrows per asset (ledger audit)", listLabel );
                MetricGauge* pImbalance = _pMetricRegistry->registerGauge( "economy_ledger_imbalance", "held - (issued - burned) per asset; must be 0", listLabel );
                if ( pIssued != nullptr )
                    pIssued->set( static_cast<float64>( asset._issued ) );
                if ( pBurned != nullptr )
                    pBurned->set( static_cast<float64>( asset._burned ) );
                if ( pHeld != nullptr )
                    pHeld->set( static_cast<float64>( asset._held ) );
                if ( pImbalance != nullptr )
                    pImbalance->set( static_cast<float64>( asset._held - ( asset._issued - asset._burned ) ) );
            }
        }
        if ( _bAuditRunHeld == SW_TRUE && _settings._pScheduler != nullptr )
            _settings._pScheduler->completeRun( _auditRun, bRead );
        _bAuditRunHeld = SW_FALSE;
    }

    void EconomyService::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _pHost = nullptr;
        _mapPresenceRequestToNotice.clear(); // 접속 상태 답은 호스트가 이미 회수했다
    }

    void EconomyService::notifyBalancesChanged( AccountID accountID, const vector<LedgerBalance>& listBalance )
    {
        const bool bNothingToSend = _pHost == nullptr || accountID == kInvalidAccountID || listBalance.empty();
        if ( bNothingToSend )
            return;
        EconomyBalanceChanged changed;
        changed._listBalance = listBalance;
        BitWriter body;
        if ( EconomyProtocol::MessageTypes::write( body, changed ) == false )
        {
            SW_LOG_WARNING( "EconomyService: a balance notice for account %# is too large to send", accountID );
            return;
        }
        if ( _pHost->sendPush( accountID, EconomyMethod::kPushBalanceChanged, body ) )
            return; // 이 서버에 붙어 있다
        if ( _settings._pPresence == nullptr )
            return; // 서버가 하나이고 붙어 있지 않다. 다음 로그인이 지갑을 읽는다
        const uint64 requestID                 = _settings._pPresence->submitFindByAccount( accountID, AccountPresenceDelegate::create<&EconomyService::onBalancePresenceFound>( this ), _findOwner );
        _mapPresenceRequestToNotice[requestID] = std::move( changed );
    }

    void EconomyService::onBalancePresenceFound( const AccountPresenceResult& result )
    {
        const auto noticeIt = _mapPresenceRequestToNotice.find( result._requestID );
        if ( noticeIt == _mapPresenceRequestToNotice.end() )
            return;
        const EconomyBalanceChanged changed = std::move( noticeIt->second );
        _mapPresenceRequestToNotice.erase( noticeIt );
        if ( result.isOnline() )
            (void)_settings._pPresence->sendRemotePush( result._identity._accountID, EconomyPushName::BalanceChanged, changed ); // 붙어 있지 않으면 보내지 않는다
    }

    void EconomyService::pollRefunds()
    {
        if ( _settings._pReceiptRegistry == nullptr )
            return;
        vector<ReceiptRefundNotice> listNotice;
        _settings._pReceiptRegistry->pollRefunds( listNotice );
        for ( ReceiptRefundNotice& notice : listNotice )
        {
            ++_pendingRefundCount;
            _pStore->submit( make_unique<EconomyRefundWork>( this, _settings._pCurrencyCatalog, std::move( notice ), _nowMs ) );
        }
    }

    void EconomyService::completeRefund( EconomyResult result, const vector<EconomyBalanceChange>& listChange )
    {
        --_pendingRefundCount;
        for ( const EconomyBalanceChange& change : listChange )
        {
            notifyBalancesChanged( change._accountID, vector<LedgerBalance>{ change._balance } );
        }
        if ( result != EconomyResult::Ok && result != EconomyResult::AlreadyRefunded )
            SW_LOG_ERROR( "EconomyService: a store refund could not be revoked (%#) - the store will resend or an operator revokes it", static_cast<int32>( result ) );
        if ( _pMetricRegistry == nullptr )
            return;
        const vector<MetricLabel> listLabel = {
            { "result", result == EconomyResult::Ok ? "revoked" : ( result == EconomyResult::AlreadyRefunded ? "duplicate" : "failed" ) }
        };
        MetricCounter* pCounter = _pMetricRegistry->registerCounter( "economy_refund_total", "Store refunds revoked from accounts", listLabel );
        if ( pCounter != nullptr )
            pCounter->add();
    }

    void EconomyService::completeCall( uint64 callID, const EconomyReply& reply )
    {
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingCall.size(); ++pendingIndex )
        {
            if ( _listPendingCall[pendingIndex]._callID == callID )
            {
                finishCall( pendingIndex, reply );
                return;
            }
        }
        SW_LOG_WARNING( "EconomyService completed an unknown call %#", callID );
    }

    void EconomyService::finishCall( size_t pendingIndex, const EconomyReply& reply )
    {
        const PendingCall pending = _listPendingCall[pendingIndex];
        _listPendingCall.erase( _listPendingCall.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
        if ( pending._pHost != nullptr )
            (void)pending._pHost->respondMessage<EconomyProtocol::MessageTypes>( pending._token, reply );
        else if ( pending._onReply.isBound() )
            pending._onReply( reply );
        _metrics.countRequest( pending._methodIndex, static_cast<int32>( reply._result ) );
        MetricHistogram* pLatency = _metrics.findLatencyHistogram( pending._methodIndex );
        if ( pLatency != nullptr )
            pLatency->observe( static_cast<float64>( MonotonicClock::nowNanoseconds() - pending._receivedNanoseconds ) * 1e-9 );
    }
} // namespace sw

namespace sw
{
    const OnlinePushRelayEntry kEconomyBalanceChangedRelay{ "Economy.BalanceChanged", EconomyMethod{}, EconomyPushName::BalanceChanged, EconomyProtocol::MessageTypes{},
                                                            OnlinePushBody<EconomyBalanceChanged>{} };
} // namespace sw
