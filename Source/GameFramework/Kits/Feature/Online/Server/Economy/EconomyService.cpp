#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Economy/EconomyService.h"

#include "Core/Network/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Kits/Feature/Online/Economy/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Catalog/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Server/Economy/EconomyStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Server/Economy/Receipt/ReceiptValidator.h"

namespace sw
{
    SW_LOG_CALLER( "EconomyService" );

    namespace
    {
        struct EconomyServiceInternal
        {
            static constexpr const utf8* kArrMethodName[] = { "get_wallet", "get_history", "purchase", "redeem_receipt" };
            static constexpr const utf8* kArrResultName[] = { "ok",
                                                              "not_signed_in",
                                                              "invalid_request",
                                                              "unknown_offer",
                                                              "not_on_sale",
                                                              "not_purchasable",
                                                              "limit_reached",
                                                              "insufficient_funds",
                                                              "cap_exceeded",
                                                              "unknown_product",
                                                              "receipt_invalid",
                                                              "receipt_refunded",
                                                              "receipt_pending",
                                                              "already_redeemed",
                                                              "busy",
                                                              "unavailable" };
            static_assert( SW_COUNT_OF( kArrMethodName ) == static_cast<size_t>( EconomyMethod::kCount ), "method names must match EconomyMethod" );
            static_assert( SW_COUNT_OF( kArrResultName ) == static_cast<size_t>( EconomyResult::Count ), "result names must match EconomyResult" );
            static constexpr int64 kShutdownWaitMs = 5000;
            static constexpr int32 kWalletIndex    = EconomyMethod::toIndex( EconomyMethod::kGetWallet );
            static constexpr int32 kHistoryIndex   = EconomyMethod::toIndex( EconomyMethod::kGetHistory );
            static constexpr int32 kPurchaseIndex  = EconomyMethod::toIndex( EconomyMethod::kPurchase );
            static constexpr int32 kRedeemIndex    = EconomyMethod::toIndex( EconomyMethod::kRedeemReceipt );
        };

        /** @brief 경제 요청 하나의 저장 왕복입니다. 입력은 공개 칸에 값으로 채워 맡기고, 결과는 `complete` 에서 서비스에 넘긴다. */
        class EconomyRequestWork final : public IServiceStoreWork
        {
        public:
            EconomyPurchaseInput  _purchase;
            EconomyRedeemInput    _redeem;
            EconomyHistoryRequest _history;
            AccountId             _accountId;

            EconomyRequestWork( EconomyService* pService, const EconomyServiceSettings& settings, uint64 callId, int32 methodIndex )
                : _purchase{}
                , _redeem{}
                , _history{}
                , _accountId{ kInvalidAccountId }
                , _reply{}
                , _settings{ settings }
                , _pService{ pService }
                , _callId{ callId }
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
                        (void)EconomyStoreLogic::readWallet( connection, _accountId, _reply );
                        break;
                    }
                    case EconomyServiceInternal::kHistoryIndex:
                    {
                        // 결과는 finish 가 _reply._result 에 담는다 — complete 가 그대로 돌려준다
                        (void)EconomyStoreLogic::readHistory( connection, _accountId, _history, _reply );
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
                    default:
                    {
                        _reply._result = EconomyResult::InvalidRequest;
                        break;
                    }
                }
            }

            void complete() override { _pService->completeCall( _callId, _reply ); }

        private:
            EconomyReply           _reply;
            EconomyServiceSettings _settings;
            EconomyService*        _pService;
            uint64                 _callId;
            int32                  _methodIndex;
        };
    } // namespace
} // namespace sw

namespace sw
{
    EconomyService::EconomyService()
        : _listPendingCall{}
        , _metrics{}
        , _settings{}
        , _pStore{ nullptr }
        , _nextCallId{ 1 }
        , _nowMs{ 0 }
    {
    }

    EconomyService::~EconomyService() { SW_ASSERT( _listPendingCall.empty() ); }

    bool EconomyService::initialize( IServiceStore* pStore, const EconomyServiceSettings& settings )
    {
        if ( pStore == nullptr || settings._pCurrencyCatalog == nullptr || settings._pOfferCatalog == nullptr )
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
        _pStore   = pStore;
        _settings = settings;
        _metrics.initialize( settings._pMetricRegistry, "economy", EconomyServiceInternal::kArrMethodName, EconomyMethod::kCount, EconomyServiceInternal::kArrResultName,
                             static_cast<int32>( EconomyResult::Count ) );
        return true;
    }

    void EconomyService::shutdown()
    {
        const Deadline deadline = Deadline::afterMilliseconds( EconomyServiceInternal::kShutdownWaitMs );
        while ( _pStore != nullptr && _listPendingCall.empty() == false && deadline.isExpired() == false )
        {
            pollReceipts();
            if ( _pStore->pollCompletions() == 0 )
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 1000000 );
        }
        if ( _listPendingCall.empty() == false )
        {
            SW_LOG_ERROR( "EconomyService shut down with %# calls still pending", _listPendingCall.size() );
            _listPendingCall.clear();
        }
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
        const bool          bMutating = context._method == EconomyMethod::kPurchase || context._method == EconomyMethod::kRedeemReceipt;
        const RemoteConfig* pConfig   = host.getRemoteConfig();
        if ( bMutating && pConfig != nullptr && pConfig->isFeatureEnabled( kFeatureFlag, context._accountId, true ) == false )
        {
            (void)host.respondError( context._token, OnlineError::kFeatureDisabled );
            return;
        }
        EconomyCall call;
        call._method         = context._method;
        call._accountId      = context._accountId;
        call._idempotencyKey = context._idempotencyKey;
        call._nowMs          = context._nowMs;
        bool bDecoded        = true;
        switch ( context._method )
        {
            case EconomyMethod::kGetHistory:
            {
                bDecoded = EconomyProtocol::readHistoryRequest( body, call._history );
                break;
            }
            case EconomyMethod::kPurchase:
            {
                bDecoded = EconomyProtocol::readPurchaseRequest( body, call._purchase );
                break;
            }
            case EconomyMethod::kRedeemReceipt:
            {
                bDecoded = EconomyProtocol::readRedeemRequest( body, call._redeem );
                break;
            }
            default:
            {
                break;
            }
        }
        if ( bDecoded == false )
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
        (void)host;
        tick( nowMs );
    }

    void EconomyService::tick( int64 nowMs )
    {
        _nowMs = nowMs;
        if ( _pStore == nullptr )
            return;
        pollReceipts();
        _metrics.setPendingStoreWorkCount( static_cast<int32>( _listPendingCall.size() ) );
    }

    void EconomyService::startCall( const EconomyCall& call, PendingCall pending )
    {
        pending._callId              = _nextCallId++;
        pending._receivedNanoseconds = MonotonicClock::nowNanoseconds();
        pending._accountId           = call._accountId;
        pending._methodIndex         = EconomyMethod::toIndex( call._method );
        _listPendingCall.push_back( pending );
        const size_t pendingIndex = _listPendingCall.size() - 1;

        EconomyReply refused;
        refused._result = EconomyResult::Ok;
        if ( pending._methodIndex < 0 || _pStore == nullptr )
            refused._result = EconomyResult::InvalidRequest;
        else if ( call._accountId == kInvalidAccountId )
            refused._result = EconomyResult::NotSignedIn; // 호스트가 이미 막지만 submitCall 경로도 같은 규칙
        else if ( call._method == EconomyMethod::kPurchase && call._idempotencyKey.isValid() == false )
            refused._result = EconomyResult::InvalidRequest; // 상태를 바꾸는 요청은 멱등 키 필수
        if ( refused._result != EconomyResult::Ok )
        {
            finishCall( pendingIndex, refused );
            return;
        }
        if ( call._method == EconomyMethod::kRedeemReceipt )
        {
            const uint64 ticket = _settings._pReceiptRegistry != nullptr
                                    ? _settings._pReceiptRegistry->submitValidation( call._redeem._storeName, call._redeem._payload, call._accountId )
                                    : 0;
            if ( ticket == 0 )
            {
                refused._result = EconomyResult::ReceiptInvalid;
                finishCall( pendingIndex, refused );
                return;
            }
            _listPendingCall[pendingIndex]._receiptTicket = ticket; // 지급 일은 검증 결과가 오면(pollReceipts)
            return;
        }
        const int64                    nowMs = call._nowMs != 0 ? call._nowMs : _nowMs;
        unique_ptr<EconomyRequestWork> work  = make_unique<EconomyRequestWork>( this, _settings, pending._callId, pending._methodIndex );
        work->_accountId                     = call._accountId;
        work->_history                       = call._history;
        work->_purchase._offerId             = call._purchase._offerId;
        work->_purchase._count               = call._purchase._count;
        work->_purchase._accountId           = call._accountId;
        work->_purchase._nowMs               = nowMs;
        work->_purchase._journalKey          = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( call._accountId ), call._idempotencyKey._high,
                                                                                      call._idempotencyKey._low );
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
                unique_ptr<EconomyRequestWork> work = make_unique<EconomyRequestWork>( this, _settings, pending._callId, EconomyServiceInternal::kRedeemIndex );
                work->_accountId                    = pending._accountId;
                work->_redeem._receipt              = std::move( result );
                work->_redeem._accountId            = pending._accountId;
                work->_redeem._nowMs                = _nowMs;
                work->_redeem._bAcceptSandbox       = _settings._bAcceptSandboxReceipts;
                _pStore->submit( std::move( work ) );
                break;
            }
        }
    }

    void EconomyService::completeCall( uint64 callId, const EconomyReply& reply )
    {
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingCall.size(); ++pendingIndex )
        {
            if ( _listPendingCall[pendingIndex]._callId == callId )
            {
                finishCall( pendingIndex, reply );
                return;
            }
        }
        SW_LOG_WARNING( "EconomyService completed an unknown call %#", callId );
    }

    void EconomyService::finishCall( size_t pendingIndex, const EconomyReply& reply )
    {
        const PendingCall pending = _listPendingCall[pendingIndex];
        _listPendingCall.erase( _listPendingCall.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
        if ( pending._pHost != nullptr )
        {
            BitWriter writer;
            EconomyProtocol::writeReply( writer, reply );
            (void)pending._pHost->respondOk( pending._token, writer );
        }
        else if ( pending._onReply.isBound() )
        {
            pending._onReply( reply );
        }
        _metrics.countRequest( pending._methodIndex, static_cast<int32>( reply._result ) );
        MetricHistogram* pLatency = _metrics.findLatencyHistogram( pending._methodIndex );
        if ( pLatency != nullptr )
            pLatency->observe( static_cast<float64>( MonotonicClock::nowNanoseconds() - pending._receivedNanoseconds ) * 1e-9 );
    }
} // namespace sw
