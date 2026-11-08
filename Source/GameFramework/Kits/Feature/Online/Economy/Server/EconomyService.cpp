#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"

#include "Core/Network/DevOnlineExtension.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Time/TimePoint.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyDevOptions.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/OnlinePushRelay.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Ledger/LedgerAuditReport.h"
#include "GameFramework/Runtime/Online/Config/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"

namespace sw
{
    namespace
    {
        struct EconomyServiceInternal
        {
            static constexpr int32 kWalletIndex   = EconomyMethod::toIndex( EconomyMethod::GetWallet::kID );
            static constexpr int32 kHistoryIndex  = EconomyMethod::toIndex( EconomyMethod::GetHistory::kID );
            static constexpr int32 kPurchaseIndex = EconomyMethod::toIndex( EconomyMethod::Purchase::kID );
            static constexpr int32 kRedeemIndex   = EconomyMethod::toIndex( EconomyMethod::RedeemReceipt::kID );

            /** @brief 호스트 호출의 공통 칸을 채운 요청입니다. */
            template <typename TMethod>
            static EconomyCall makeCall( const OnlineCallContext& context )
            {
                EconomyCall call{ context._nowMs };
                call._method         = TMethod::kID;
                call._accountID      = context._accountID;
                call._idempotencyKey = context._idempotencyKey;
                return call;
            }
            static constexpr int32 kWithdrawIndex = EconomyMethod::toIndex( EconomyMethod::WithdrawPurchase::kID );
        };

        /** @brief 환불 회수 한 번이 완료로 넘기는 값입니다. */
        struct EconomyRefundResult
        {
            ReceiptRefundNotification    _notice{};
            vector<EconomyBalanceChange> _listChange{};
            EconomyOutcome               _outcome{ EconomyOutcome::fail( OnlineError::kUnavailable ) };
        };

        struct EconomyDevOnlineInternal
        {
            static unique_ptr<IReceiptValidator> createFakeReceiptValidator()
            {
                return make_unique<FakeReceiptValidator>();
            }

            static constexpr EconomyDevOptions kDevOptions{ &createFakeReceiptValidator };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_DEFINE_DEV_ONLINE_EXTENSION( EconomyDevOptions, "_bFakeReceipts" )

    EconomyService::EconomyService()
        : _localCalls{}
        , _mapTicketToCall{}
        , _settings{}
        , _presenceFinds{}
        , _uniqueSetDeliveryInFlight{}
        , _uniqueSetDeliveryAlarmed{}
        , _balanceNotice{ *this }
        , _pHost{ nullptr }
        , _pMetricRegistry{ nullptr }
        , _auditJob{}
        , _refundCursorJob{}
        , _deliveryJob{}
        , _bLastAuditBalanced{ SW_FALSE }
        , _bRefundCursorsRestored{ SW_FALSE }
        , _nowMs{ 0 }
    {
        setReady( false );
    }

    EconomyService::~EconomyService()
    {
        SW_ASSERT( _calls.isEmpty() && _localCalls.isEmpty() );
    }

    bool EconomyService::initialize( IServiceStore* pStore, const EconomyServiceConfig& settings )
    {
        const bool bMissingDependencies = pStore == nullptr || settings._pCurrencyCatalog == nullptr || settings._pOfferCatalog == nullptr;
        if ( bMissingDependencies )
        {
            SW_LOG_ERROR_ID( "EconomyService.EconomyServiceNeedsStoreBoth", "EconomyService needs a store and both catalogs" );
            return false;
        }
        // 서명 없는 개발 전용 제공자는 DevOnline 단위가 있는 구성(개발)에서만 받는다 — 배포는 그 단위를 짓지 않는다.
        const bool bDevValidatorRefused = settings._pReceiptRegistry != nullptr && settings._pReceiptRegistry->hasDevelopmentValidator() && DevOnlineExtension<EconomyDevOptions>::isRegistered() == false;
        if ( bDevValidatorRefused )
        {
            SW_LOG_ERROR_ID( "EconomyService.EconomyServiceRefusesDevelopmentOnly", "EconomyService refuses a development-only receipt validator in a build without the DevOnline unit (Shipping)" );
            return false;
        }
        const bool bBuyerLinksMissing = settings._pReceiptRegistry != nullptr && settings._pReceiptRegistry->requiresExternalLinks() && settings._pExternalLinks == nullptr;
        if ( bBuyerLinksMissing )
        {
            SW_LOG_ERROR_ID( "EconomyService.EconomyServiceNeedsExternalLinks", "EconomyService: a receipt validator binds buyers to linked logins but the account kit's link port is missing" );
            return false;
        }
        _store.bindStore( pStore );
        _settings = settings;
        _refundCursorJob.setPeriod( settings._refundCursorRetryMs ); // 첫 틱에 읽고, 못 읽었으면 이 간격 뒤에 다시 읽는다
        _deliveryJob.setPeriod( settings._deliveryScanIntervalMs );  // 기동 직후 한 번 읽는다
        _presenceFinds.bind( settings._pPresence );
        _pMetricRegistry = settings._pMetricRegistry;
        if ( settings._pScheduler != nullptr )
        {
            ScheduleDef schedule;
            schedule._jobID       = kAuditJobID;
            schedule._kind        = ScheduleKind::Daily;
            schedule._minuteOfDay = settings._auditMinuteOfDay;
            if ( settings._pScheduler->registerJob( schedule, ScheduledJobFunction::create<&EconomyService::onScheduledRun>( this ) ) == false )
            {
                SW_LOG_ERROR_ID( "EconomyService.EconomyServiceLedgerAuditJob", "EconomyService: the ledger audit job could not be scheduled (minute of day %#)", settings._auditMinuteOfDay );
                return false;
            }
        }
        initializeCalls( settings._pMetricRegistry, "economy" );
        setReady( true );
        return true;
    }

    void EconomyService::shutdown()
    {
        // 영수증 검증 완료도 호출을 끝내므로 단계마다 같이 회수한다.
        _store.flushUntilIdle( [this]()
        { return getOpenWorkCount() == 0; }, [this]()
        { pollReceipts(); } );
        _calls.abandonAll();
        _localCalls.clear();
        _mapTicketToCall.clear();
        setReady( false );
        _presenceFinds.unbind(); // 잔액 알림 찾기가 이 객체를 부르지 않게 한다(저장 작업을 회수한 뒤 — 그 완료가 찾기를 낼 수 있다). 기다리던 알림은 버린다
        _store.bindStore( nullptr );
    }

    void EconomyService::submitCall( const EconomyCall& call, ReplyDelegate onReply )
    {
        startCall( call, _localCalls.open( onReply ) );
    }

    uint16 EconomyService::checkRequest( OnlineServiceHost& host, const OnlineCallContext& context )
    {
        const bool          bMutating = context._method == EconomyMethod::Purchase::kID || context._method == EconomyMethod::RedeemReceipt::kID || context._method == EconomyMethod::WithdrawPurchase::kID;
        const RemoteConfig* pConfig   = host.getRemoteConfig();
        return bMutating && pConfig != nullptr && pConfig->isFeatureEnabled( kFeatureFlag, context._accountID, true ) == false ? OnlineError::kFeatureDisabled : OnlineError::kOk;
    }

    void EconomyService::handleCall( OnlineServerCall<EconomyMethod::GetWallet> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        startCall( EconomyServiceInternal::makeCall<EconomyMethod::GetWallet>( context ), call._callID );
    }

    void EconomyService::handleCall( OnlineServerCall<EconomyMethod::GetHistory> call, const OnlineCallContext& context, const EconomyHistoryRequest& request )
    {
        EconomyCall economyCall = EconomyServiceInternal::makeCall<EconomyMethod::GetHistory>( context );
        economyCall._history    = request;
        startCall( economyCall, call._callID );
    }

    void EconomyService::handleCall( OnlineServerCall<EconomyMethod::Purchase> call, const OnlineCallContext& context, const EconomyPurchaseRequest& request )
    {
        EconomyCall economyCall = EconomyServiceInternal::makeCall<EconomyMethod::Purchase>( context );
        economyCall._purchase   = request;
        startCall( economyCall, call._callID );
    }

    void EconomyService::handleCall( OnlineServerCall<EconomyMethod::RedeemReceipt> call, const OnlineCallContext& context, const EconomyRedeemRequest& request )
    {
        EconomyCall economyCall = EconomyServiceInternal::makeCall<EconomyMethod::RedeemReceipt>( context );
        economyCall._redeem     = request;
        startCall( economyCall, call._callID );
    }

    void EconomyService::handleCall( OnlineServerCall<EconomyMethod::WithdrawPurchase> call, const OnlineCallContext& context, const EconomyWithdrawRequest& request )
    {
        EconomyCall economyCall = EconomyServiceInternal::makeCall<EconomyMethod::WithdrawPurchase>( context );
        economyCall._withdraw   = request;
        startCall( economyCall, call._callID );
    }

    void EconomyService::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        _pHost = &host;
        (void)host;
        update( nowMs );
    }

    void EconomyService::update( UnixMilliseconds nowMs )
    {
        _nowMs = nowMs;
        if ( _store.hasStore() == false )
            return;
        if ( _settings._pReceiptRegistry != nullptr )
            _settings._pReceiptRegistry->update( nowMs );
        pollReceipts();
        pollRefunds();
        updateRefundCursors( nowMs );
        updateDeliveries( nowMs );
        _calls.publishPendingCount();
    }

    void EconomyService::confirmDelivery( const ReceiptValidationResult& receipt )
    {
        ReceiptDeliveryTask task;
        if ( EconomyStoreLogic::makeDeliveryTask( receipt, task ) )
            startDelivery( task );
    }

    void EconomyService::startDelivery( const ReceiptDeliveryTask& task )
    {
        if ( _settings._pReceiptRegistry == nullptr || _uniqueSetDeliveryInFlight.insert( task._recordKey ).second == false )
            return; // 이미 날아가는 중이다
        if ( _settings._pReceiptRegistry->confirmDelivery( task, _nowMs ) )
            return;
        _uniqueSetDeliveryInFlight.erase( task._recordKey );
        SW_LOG_WARNING_ID( "EconomyService.DeliveryStoreNotConfigured", "EconomyService: a delivery record belongs to store '%#' which this server does not have - it stays until a server with that store sends it", task._storeName.c_str() );
    }

    void EconomyService::updateDeliveries( UnixMilliseconds nowMs )
    {
        if ( _settings._pReceiptRegistry == nullptr || _settings._pReceiptRegistry->usesDeliveryConfirmation() == false )
            return;
        vector<ReceiptDeliveryResult> listOutcome;
        _settings._pReceiptRegistry->pollDeliveryOutcomes( listOutcome );
        for ( ReceiptDeliveryResult& outcome : listOutcome )
        {
            _uniqueSetDeliveryInFlight.erase( outcome._recordKey );
            if ( outcome._status == ReceiptDeliveryStatus::Retry )
                continue; // 레코드가 남아 다음 훑기가 다시 보낸다
            _uniqueSetDeliveryAlarmed.erase( outcome._recordKey );
            _store.submit( [recordKey = outcome._recordKey]( IServiceStoreConnection& connection )
            { return EconomyStoreLogic::eraseDelivery( connection, recordKey ); },
                           [this, recordKey = outcome._recordKey]( ServiceStoreResult result, ServiceStoreCompletion completion )
            { completeDeliveryErase( recordKey, result, completion ); } );
        }
        if ( _deliveryJob.tryStart( nowMs ) == false )
            return;
        const int32 maxCount = _settings._deliveryScanMaxCount;
        _store.submit( [maxCount]( IServiceStoreConnection& connection )
        {
            ServiceStoreRead<vector<ReceiptDeliveryTask>> read;
            read._result = EconomyStoreLogic::listDeliveries( connection, maxCount, read._value );
            return read;
        }, [this]( const ServiceStoreRead<vector<ReceiptDeliveryTask>>& read, ServiceStoreCompletion completion )
        { completeDeliveryList( read._value, read._result, completion ); } );
    }

    void EconomyService::completeDeliveryList( const vector<ReceiptDeliveryTask>& listTask, ServiceStoreResult result, ServiceStoreCompletion completion )
    {
        _deliveryJob.finish();
        if ( result != ServiceStoreResult::Ok )
        {
            if ( completion.isCancelled() == false )
                SW_LOG_WARNING_ID( "EconomyService.DeliveryScanFailed", "EconomyService: the delivery records could not be read - the next scan reads them again" );
            return;
        }
        for ( const ReceiptDeliveryTask& task : listTask )
        {
            const bool bLate = _nowMs.count() >= task._deadlineMs - _settings._deliveryAlarmMarginMs;
            if ( bLate && _uniqueSetDeliveryAlarmed.insert( task._recordKey ).second )
                SW_LOG_ERROR_ID( "EconomyService.DeliveryNearDeadline", "EconomyService: store '%#' has not taken the delivery confirmation '%#' and its deadline is %# (now %#) - past it the store refunds the purchase and the refund poll takes the grant back", task._storeName.c_str(), task._recordKey.c_str(), task._deadlineMs, _nowMs.count() );
            startDelivery( task );
        }
    }

    void EconomyService::completeDeliveryErase( const string& recordKey, ServiceStoreResult result, ServiceStoreCompletion completion )
    {
        if ( result != ServiceStoreResult::Ok && completion.isCancelled() == false ) // 다음 훑기가 다시 보내고 스토어가 거절하면 그때 지운다
            SW_LOG_WARNING_ID( "EconomyService.DeliveryEraseFailed", "EconomyService: the finished delivery record '%#' could not be erased", recordKey.c_str() );
    }

    void EconomyService::updateRefundCursors( UnixMilliseconds nowMs )
    {
        if ( _settings._pReceiptRegistry == nullptr )
            return;
        if ( _bRefundCursorsRestored == SW_TRUE )
        {
            for ( IReceiptValidator* pValidator : _settings._pReceiptRegistry->getValidators() )
            {
                int64 lastReadMs = 0;
                if ( pValidator->takeRefundCursor( lastReadMs ) == false )
                    continue;
                const EconomyRefundCursor cursor{ pValidator->getStoreName(), lastReadMs };
                _store.submit( [cursor]( IServiceStoreConnection& connection )
                { return EconomyStoreLogic::writeRefundCursor( connection, cursor._storeName, cursor._lastReadMs ); },
                               [this, cursor]( ServiceStoreResult result, ServiceStoreCompletion completion )
                { completeRefundCursorWrite( cursor, result, completion ); } );
            }
            return;
        }
        if ( _refundCursorJob.tryStart( nowMs ) == false )
            return;
        vector<EconomyRefundCursor> listCursor;
        for ( const IReceiptValidator* pValidator : _settings._pReceiptRegistry->getValidators() )
        {
            if ( pValidator->usesRefundCursor() )
                listCursor.push_back( EconomyRefundCursor{ pValidator->getStoreName(), 0 } );
        }
        if ( listCursor.empty() )
        {
            _refundCursorJob.finish();
            _bRefundCursorsRestored = SW_TRUE;
            return;
        }
        _store.submit( [listCursor = std::move( listCursor )]( IServiceStoreConnection& connection ) mutable
        {
            ServiceStoreRead<vector<EconomyRefundCursor>> read;
            read._result = ServiceStoreResult::Ok;
            for ( EconomyRefundCursor& cursor : listCursor )
            {
                read._result = EconomyStoreLogic::readRefundCursor( connection, cursor._storeName, cursor._lastReadMs );
                if ( read._result != ServiceStoreResult::Ok )
                    break; // 하나라도 못 읽으면 다시 읽는다 — 그 스토어를 기준 없이 시작하면 구간을 건너뛴다
            }
            read._value = std::move( listCursor );
            return read;
        }, [this]( const ServiceStoreRead<vector<EconomyRefundCursor>>& read, ServiceStoreCompletion completion )
        { completeRefundCursorRead( read._value, read._result, completion ); } );
    }

    void EconomyService::completeRefundCursorRead( const vector<EconomyRefundCursor>& listCursor, ServiceStoreResult result, ServiceStoreCompletion completion )
    {
        _refundCursorJob.finish(); // 못 읽었으면 다음 주기(`_refundCursorRetryMs`)에 다시 읽는다
        if ( result != ServiceStoreResult::Ok )
        {
            if ( completion.isCancelled() == false )
                SW_LOG_WARNING_ID( "EconomyService.RefundCursorReadFailed", "EconomyService: the refund cursors could not be read - refund polling waits and reads them again" );
            return;
        }
        for ( const EconomyRefundCursor& cursor : listCursor )
        {
            IReceiptValidator* pValidator = _settings._pReceiptRegistry->findValidator( cursor._storeName );
            if ( pValidator != nullptr )
                pValidator->restoreRefundCursor( cursor._lastReadMs );
        }
        _bRefundCursorsRestored = SW_TRUE;
    }

    void EconomyService::completeRefundCursorWrite( const EconomyRefundCursor& cursor, ServiceStoreResult result, ServiceStoreCompletion completion )
    {
        if ( result != ServiceStoreResult::Ok && completion.isCancelled() == false ) // 다음 조회가 다시 쓴다. 그 사이 재시작하면 옛 기준에서 더 겹쳐 읽을 뿐이다
            SW_LOG_WARNING_ID( "EconomyService.RefundCursorWriteFailed", "EconomyService: the refund cursor of store '%#' could not be written", cursor._storeName.c_str() );
    }

    void EconomyService::startCall( const EconomyCall& call, uint64 callID )
    {
        const int32 methodIndex = EconomyMethod::toIndex( call._method );

        EconomyOutcome refused{ EconomyResult::Ok };
        if ( methodIndex < 0 || _store.hasStore() == false )
            refused = EconomyOutcome::fail( OnlineError::kInvalidRequest );
        else if ( call._accountID == kInvalidAccountID )
            refused = EconomyOutcome::fail( OnlineError::kUnauthenticated ); // 호스트가 이미 막지만 submitCall 경로도 같은 규칙을 따른다
        else if ( call._method == EconomyMethod::Purchase::kID && call._idempotencyKey.isValid() == false )
            refused = EconomyOutcome::fail( OnlineError::kInvalidRequest ); // 상태를 바꾸는 요청에는 멱등 키가 필수이다
        if ( refused != EconomyResult::Ok )
        {
            failCallOf( callID, refused._errorCode );
            return;
        }
        if ( call._method == EconomyMethod::RedeemReceipt::kID )
        {
            const uint64 ticket = _settings._pReceiptRegistry != nullptr
                                    ? _settings._pReceiptRegistry->submitValidation( call._redeem._storeName, call._redeem._payload, call._accountID, call._nowMs )
                                    : 0;
            if ( ticket == 0 )
            {
                EconomyRedeemReply invalid;
                invalid._result = EconomyResult::ReceiptInvalid;
                completeCall( callID, EconomyOutcome{ EconomyResult::ReceiptInvalid }, invalid );
                return;
            }
            _mapTicketToCall[ticket] = EconomyReceiptCall{ callID, call._accountID }; // 지급 작업은 검증 결과가 오면 맡긴다(pollReceipts)
            return;
        }
        const AccountID        accountID   = call._accountID;
        const int64            nowMs       = call._nowMs.count();
        const CurrencyCatalog* pCurrencies = _settings._pCurrencyCatalog; // 저장소 스레드는 서비스 멤버를 만지지 않으므로 값을 잡는다
        const OfferCatalog*    pOffers     = _settings._pOfferCatalog;
        switch ( methodIndex )
        {
            case EconomyServiceInternal::kWalletIndex:
            {
                submitStoreCall<EconomyWalletReply>( callID, [accountID]( IServiceStoreConnection& connection, EconomyWalletReply& outReply )
                { return EconomyStoreLogic::readWallet( connection, accountID, outReply ); } );
                return;
            }
            case EconomyServiceInternal::kHistoryIndex:
            {
                submitStoreCall<EconomyHistoryReply>( callID, [accountID, history = call._history]( IServiceStoreConnection& connection, EconomyHistoryReply& outReply )
                { return EconomyStoreLogic::readHistory( connection, accountID, history, outReply ); } );
                return;
            }
            case EconomyServiceInternal::kPurchaseIndex:
            {
                EconomyPurchaseInput purchase;
                purchase._offerID    = call._purchase._offerID;
                purchase._count      = call._purchase._count;
                purchase._accountID  = accountID;
                purchase._nowMs      = nowMs;
                purchase._journalKey = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( accountID ), call._idempotencyKey._high, call._idempotencyKey._low );
                submitStoreCall<EconomyPurchaseReply>( callID, [pCurrencies, pOffers, purchase]( IServiceStoreConnection& connection, EconomyPurchaseReply& outReply )
                { return EconomyStoreLogic::purchase( connection, *pCurrencies, *pOffers, purchase, outReply ); } );
                return;
            }
            case EconomyServiceInternal::kWithdrawIndex:
            {
                EconomyWithdrawInput withdraw;
                withdraw._storeName     = call._withdraw._storeName;
                withdraw._transactionID = call._withdraw._transactionID;
                withdraw._accountID     = accountID;
                withdraw._nowMs         = nowMs;
                withdraw._windowMs      = _settings._withdrawalWindowMs;
                submitStoreCall<EconomyWithdrawReply>( callID, [pCurrencies, withdraw]( IServiceStoreConnection& connection, EconomyWithdrawReply& outReply )
                { return EconomyStoreLogic::withdrawPurchase( connection, *pCurrencies, withdraw, outReply ); } );
                return;
            }
            default:
            {
                failCallOf( callID, OnlineError::kInvalidRequest ); // 지급은 위에서 검증으로 갔다
                return;
            }
        }
    }

    template <typename TReply, typename TRun>
    void EconomyService::submitStoreCall( uint64 callID, TRun&& run )
    {
        _store.submit( [run = std::forward<TRun>( run )]( IServiceStoreConnection& connection )
        {
            ServiceStoreCallResult<EconomyOutcome, TReply> done;
            done._outcome = run( connection, done._reply );
            return done;
        }, [this, callID]( const ServiceStoreCallResult<EconomyOutcome, TReply>& done )
        { completeCall( callID, done._outcome, done._reply ); } );
    }

    void EconomyService::pollReceipts()
    {
        if ( _settings._pReceiptRegistry == nullptr )
            return;
        vector<ReceiptValidationResult> listResult;
        _settings._pReceiptRegistry->pollCompletions( listResult );
        for ( ReceiptValidationResult& result : listResult )
        {
            const auto callIt = _mapTicketToCall.find( result._ticket );
            if ( callIt == _mapTicketToCall.end() )
                continue;
            const EconomyReceiptCall receiptCall = callIt->second;
            _mapTicketToCall.erase( callIt );
            EconomyRedeemInput redeem;
            redeem._receipt                     = std::move( result );
            redeem._pExternalLinks              = _settings._pExternalLinks;
            redeem._accountID                   = receiptCall._accountID;
            redeem._nowMs                       = _nowMs.count();
            redeem._bAcceptSandbox              = _settings._bAcceptSandboxReceipts;
            const CurrencyCatalog*  pCurrencies = _settings._pCurrencyCatalog;
            const OfferCatalog*     pOffers     = _settings._pOfferCatalog;
            ReceiptValidationResult receipt     = redeem._receipt; // 지급이 커밋되면 완료가 스토어에 알린다
            _store.submit( [pCurrencies, pOffers, redeem = std::move( redeem )]( IServiceStoreConnection& connection )
            {
                ServiceStoreCallResult<EconomyOutcome, EconomyRedeemReply> done;
                done._outcome = EconomyStoreLogic::redeemReceipt( connection, *pCurrencies, *pOffers, redeem, done._reply );
                return done;
            }, [this, callID = receiptCall._callID, receipt = std::move( receipt )]( const ServiceStoreCallResult<EconomyOutcome, EconomyRedeemReply>& done )
            {
                if ( done._outcome == EconomyResult::Ok )
                    confirmDelivery( receipt ); // 지급이 커밋된 뒤다
                completeCall( callID, done._outcome, done._reply );
            } );
        }
    }

    void EconomyService::onScheduledRun( const ScheduledRun& run )
    {
        _auditJob.holdRun( run );
        requestLedgerAudit();
    }

    void EconomyService::requestLedgerAudit()
    {
        if ( _store.hasStore() == false || _auditJob.isRunning() )
            return; // 도는 중이면 그 검사가 끝나며 차지한 회차를 돌려준다
        _auditJob.request();
        if ( _auditJob.tryStart( _nowMs ) == false )
            return;
        _store.submit( []( IServiceStoreConnection& connection )
        {
            ServiceStoreRead<LedgerAuditReport> read;
            read._result = LedgerAuditReport::computeReport( connection, read._value );
            return read;
        }, [this]( const ServiceStoreRead<LedgerAuditReport>& read, ServiceStoreCompletion completion )
        { completeLedgerAudit( read._result, read._value, completion ); } );
    }

    void EconomyService::completeLedgerAudit( ServiceStoreResult result, const LedgerAuditReport& report, ServiceStoreCompletion completion )
    {
        _auditJob.finish();
        const bool bRead    = result == ServiceStoreResult::Ok;
        _bLastAuditBalanced = bRead && report.isBalanced() ? SW_TRUE : SW_FALSE;
        if ( bRead == false )
        {
            if ( completion.isCancelled() == false ) // 종료 때문에 취소된 검사는 오류가 아니다
                SW_LOG_ERROR_ID( "EconomyService.EconomyServiceLedgerAuditCould", "EconomyService: the ledger audit could not read the store" );
        }
        else if ( report.isBalanced() == false )
            SW_LOG_ERROR_ID( "EconomyService.EconomyServiceLedgerNotBalanced", "EconomyService: the ledger is not balanced (negative escrows %#, unreadable records %#)", report._negativeEscrowCount, report._unreadableRecordCount );
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
        ScheduledRun run;
        if ( _auditJob.takeHeldRun( run ) && _settings._pScheduler != nullptr )
            _settings._pScheduler->completeRun( run, bRead );
    }

    void EconomyService::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _pHost = nullptr;
        _presenceFinds.unbind(); // 접속 상태 답은 호스트가 이미 회수했다 — 기다리던 알림은 버린다
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
            SW_LOG_WARNING_ID( "EconomyService.EconomyServiceBalanceNoticeAccount", "EconomyService: a balance notice for account %# is too large to send", accountID );
            return;
        }
        if ( _pHost->sendPush( accountID, EconomyMethod::BalanceChanged::kKind, body ) )
            return; // 이 서버에 붙어 있다
        // 서버가 하나면 찾지 않는다 — 붙어 있지 않으니 다음 로그인이 지갑을 읽는다.
        (void)_presenceFinds.findByAccount( accountID, accountID, [this, changed = std::move( changed )]( const AccountPresenceResult& result )
        {
            if ( result.isOnline() && _presenceFinds.hasPresence() )
                (void)_presenceFinds.getPresence()->sendRemotePush( result._identity._accountID, EconomyPushName::BalanceChanged, changed ); // 붙어 있지 않으면 보내지 않는다
        } );
    }

    void EconomyService::pollRefunds()
    {
        if ( _settings._pReceiptRegistry == nullptr )
            return;
        vector<ReceiptRefundNotification> listNotice;
        _settings._pReceiptRegistry->pollRefunds( listNotice );
        for ( ReceiptRefundNotification& notice : listNotice )
        {
            const CurrencyCatalog* pCurrencies = _settings._pCurrencyCatalog;
            const int64            nowMs       = _nowMs.count();
            _store.submit( [pCurrencies, nowMs, notice = std::move( notice )]( IServiceStoreConnection& connection )
            {
                EconomyRefundResult done;
                done._notice  = notice;
                done._outcome = EconomyStoreLogic::revokeRefund( connection, *pCurrencies, notice, nowMs, done._listChange );
                return done;
            }, [this]( const EconomyRefundResult& done, ServiceStoreCompletion completion )
            { completeRefund( done._notice, done._outcome, done._listChange, completion ); } );
        }
    }

    void EconomyService::completeRefund( const ReceiptRefundNotification& notice, EconomyOutcome result, const vector<EconomyBalanceChange>& listChange, ServiceStoreCompletion completion )
    {
        for ( const EconomyBalanceChange& change : listChange )
        {
            notifyBalancesChanged( change._accountID, vector<LedgerBalance>{ change._balance } );
        }
        const bool bFailed = result != EconomyResult::Ok && result != EconomyResult::AlreadyRefunded;
        if ( bFailed && completion.isCancelled() == false ) // 종료 때문에 취소된 회수는 오류가 아니다(다음 기동의 겹친 조회가 다시 읽는다)
        {
            SW_LOG_ERROR_ID( "EconomyService.EconomyServiceStoreRefundCould", "EconomyService: a store refund could not be revoked (result %#, error %#) - the next overlapping poll reports it again",
                             static_cast<int32>( result._result ), static_cast<int32>( result._errorCode ) );
            // 제공자의 겹침 거름이 이 거래를 기억하면 겹친 조회가 다시 알리지 않아 회수할 기회가 없어진다.
            if ( _settings._pReceiptRegistry != nullptr )
                _settings._pReceiptRegistry->removeRefund( notice );
        }
        if ( _pMetricRegistry == nullptr )
            return;
        const vector<MetricLabel> listLabel = {
            { "result", result == EconomyResult::Ok ? "revoked" : ( result == EconomyResult::AlreadyRefunded ? "duplicate" : "failed" ) }
        };
        MetricCounter* pCounter = _pMetricRegistry->registerCounter( "economy_refund_total", "Store refunds revoked from accounts", listLabel );
        if ( pCounter != nullptr )
            pCounter->add();
    }

    void EconomyService::failCallOf( uint64 callID, uint16 errorCode )
    {
        if ( LocalCallTable::isLocal( callID ) )
        {
            OnlineResult<EconomyCallReply> local;
            local._errorCode = errorCode;
            (void)_localCalls.finish( callID, local );
        }
        else if ( failCall( callID, errorCode ) == false )
            _calls.warnUnknownCall( callID );
    }
} // namespace sw

namespace sw
{
    const OnlinePushRelayEntry kEconomyBalanceChangedRelay{ "Economy.BalanceChanged", EconomyMethod{}, EconomyPushName::BalanceChanged, EconomyProtocol::MessageTypes{},
                                                            OnlinePushBody<EconomyBalanceChanged>{} };

    SW_REGISTER_DEV_ONLINE_EXTENSION( EconomyDevOptions, EconomyDevOnlineInternal::kDevOptions );
} // namespace sw
