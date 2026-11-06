#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Time/TimePoint.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyDevOptions.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Runtime/Online/Config/DevOnlineExtension.h"
#include "GameFramework/Runtime/Online/Config/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Identity/AccountPresence.h"
#include "GameFramework/Runtime/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerAudit.h"
#include "GameFramework/Runtime/Online/Store/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/ServiceStoreUtil.h"

namespace sw
{
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

            void complete() override
            {
                if ( _methodIndex == EconomyServiceInternal::kRedeemIndex && _reply._result == EconomyResult::Ok )
                    _pService->confirmDelivery( _redeem._receipt ); // 지급이 커밋된 뒤다
                _pService->completeCall( _callID, _reply );
            }

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
            void complete() override { _pService->completeLedgerAudit( _result, _report, getCompletion() ); }

        private:
            LedgerAuditReport  _report;
            EconomyService*    _pService;
            ServiceStoreResult _result;
        };

        /** @brief 환불 알림 하나의 회수입니다. 응답이 없다. */
        class EconomyRefundWork final : public IServiceStoreWork
        {
        public:
            EconomyRefundWork( EconomyService* pService, const CurrencyCatalog* pCurrencies, ReceiptRefundNotice&& notice, UnixMilliseconds nowMs )
                : _notice{ std::move( notice ) }
                , _pService{ pService }
                , _pCurrencies{ pCurrencies }
                , _nowMs{ nowMs }
                , _result{ EconomyResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = EconomyStoreLogic::revokeRefund( connection, *_pCurrencies, _notice, _nowMs.count(), _listChange ); }
            void complete() override { _pService->completeRefund( _notice, _result, _listChange, getCompletion() ); }

        private:
            ReceiptRefundNotice          _notice;
            vector<EconomyBalanceChange> _listChange;
            EconomyService*              _pService;
            const CurrencyCatalog*       _pCurrencies;
            UnixMilliseconds             _nowMs;
            EconomyResult                _result;
        };

        /** @brief 저장소에 남은 환불 조회 기준 시각을 스토어마다 읽습니다. */
        class EconomyRefundCursorReadWork final : public IServiceStoreWork
        {
        public:
            EconomyRefundCursorReadWork( EconomyService* pService, vector<EconomyRefundCursor>&& listCursor )
                : _listCursor{ std::move( listCursor ) }
                , _pService{ pService }
                , _result{ ServiceStoreResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                for ( EconomyRefundCursor& cursor : _listCursor )
                {
                    _result = EconomyStoreLogic::readRefundCursor( connection, cursor._storeName, cursor._lastReadMs );
                    if ( _result != ServiceStoreResult::Ok )
                        return; // 하나라도 못 읽으면 다시 읽는다 — 그 스토어를 기준 없이 시작하면 구간을 건너뛴다
                }
            }

            void complete() override { _pService->completeRefundCursorRead( _listCursor, _result, getCompletion() ); }

        private:
            vector<EconomyRefundCursor> _listCursor;
            EconomyService*             _pService;
            ServiceStoreResult          _result;
        };

        /** @brief 환불 조회 기준 시각 하나를 씁니다. */
        class EconomyRefundCursorWriteWork final : public IServiceStoreWork
        {
        public:
            EconomyRefundCursorWriteWork( EconomyService* pService, EconomyRefundCursor cursor )
                : _cursor{ std::move( cursor ) }
                , _pService{ pService }
                , _result{ ServiceStoreResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = EconomyStoreLogic::writeRefundCursor( connection, _cursor._storeName, _cursor._lastReadMs ); }
            void complete() override { _pService->completeRefundCursorWrite( _cursor, _result, getCompletion() ); }

        private:
            EconomyRefundCursor _cursor;
            EconomyService*     _pService;
            ServiceStoreResult  _result;
        };

        /** @brief 남은 지급 알림을 읽습니다. */
        class EconomyDeliveryListWork final : public IServiceStoreWork
        {
        public:
            EconomyDeliveryListWork( EconomyService* pService, int32 maxCount )
                : _listTask{}
                , _pService{ pService }
                , _maxCount{ maxCount }
                , _result{ ServiceStoreResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = EconomyStoreLogic::listDeliveries( connection, _maxCount, _listTask ); }
            void complete() override { _pService->completeDeliveryList( _listTask, _result, getCompletion() ); }

        private:
            vector<ReceiptDeliveryTask> _listTask;
            EconomyService*             _pService;
            int32                       _maxCount;
            ServiceStoreResult          _result;
        };

        /** @brief 끝난 지급 알림의 레코드를 지웁니다. */
        class EconomyDeliveryEraseWork final : public IServiceStoreWork
        {
        public:
            EconomyDeliveryEraseWork( EconomyService* pService, string recordKey )
                : _recordKey{ std::move( recordKey ) }
                , _pService{ pService }
                , _result{ ServiceStoreResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = EconomyStoreLogic::eraseDelivery( connection, _recordKey ); }
            void complete() override { _pService->completeDeliveryErase( _recordKey, _result, getCompletion() ); }

        private:
            string             _recordKey;
            EconomyService*    _pService;
            ServiceStoreResult _result;
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
    SW_DEFINE_DEV_ONLINE_EXTENSION( EconomyDevOptions, "fakeReceipts" );

    EconomyService::EconomyService()
        : _calls{}
        , _settings{}
        , _mapPresenceRequestToNotice{}
        , _uniqueSetDeliveryInFlight{}
        , _uniqueSetDeliveryAlarmed{}
        , _balanceNotice{ *this }
        , _pHost{ nullptr }
        , _pStore{ nullptr }
        , _pMetricRegistry{ nullptr }
        , _pendingRefundCount{ 0 }
        , _pendingUpkeepCount{ 0 }
        , _nextRefundCursorReadMs{ 0 }
        , _nextDeliveryScanMs{ 0 }
        , _auditRun{}
        , _bAuditPending{ SW_FALSE }
        , _bAuditRunHeld{ SW_FALSE }
        , _bLastAuditBalanced{ SW_FALSE }
        , _bRefundCursorsRestored{ SW_FALSE }
        , _bRefundCursorReadPending{ SW_FALSE }
        , _bDeliveryScanPending{ SW_FALSE }
        , _nowMs{ 0 }
    {
    }

    EconomyService::~EconomyService()
    {
        SW_ASSERT( _calls.isEmpty() );
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
        const bool bDevValidatorRefused = settings._pReceiptRegistry != nullptr && settings._pReceiptRegistry->hasDevelopmentValidator() && EconomyDevOptions::s_devOnline.isRegistered() == false;
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
        _pStore          = pStore;
        _settings        = settings;
        _findOwner       = settings._pPresence != nullptr ? settings._pPresence->openFindOwner() : Subscription{};
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
        _calls.initialize<EconomyMethodName, EconomyResult>( settings._pMetricRegistry, "economy" );
        return true;
    }

    void EconomyService::shutdown()
    {
        // 영수증 검증 완료도 호출을 끝내므로 단계마다 같이 회수한다.
        ServiceStoreUtil::drainUntilIdle( _pStore, [this]()
        { return _calls.isEmpty() && _pendingRefundCount <= 0 && _pendingUpkeepCount <= 0 && _bAuditPending == SW_FALSE; }, [this]()
        { pollReceipts(); } );
        _calls.abandonAll();
        _findOwner.reset(); // 잔액 알림 찾기가 이 객체를 부르지 않게 한다(저장 작업을 회수한 뒤 — 그 완료가 찾기를 낼 수 있다)
        _mapPresenceRequestToNotice.clear();
        _pStore = nullptr;
    }

    void EconomyService::submitCall( const EconomyCall& call, ReplyDelegate onReply )
    {
        startCall( call, CallTable::Origin::fromDelegate( onReply ) );
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
        EconomyCall call{ context._nowMs };
        call._method               = context._method;
        call._accountID            = context._accountID;
        call._idempotencyKey       = context._idempotencyKey;
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
        startCall( call, CallTable::Origin::fromHost( host, context._token ) );
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
        if ( _pStore == nullptr )
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
        vector<ReceiptDeliveryOutcome> listOutcome;
        _settings._pReceiptRegistry->pollDeliveryOutcomes( listOutcome );
        for ( ReceiptDeliveryOutcome& outcome : listOutcome )
        {
            _uniqueSetDeliveryInFlight.erase( outcome._recordKey );
            if ( outcome._status == ReceiptDeliveryStatus::Retry )
                continue; // 레코드가 남아 다음 훑기가 다시 보낸다
            _uniqueSetDeliveryAlarmed.erase( outcome._recordKey );
            ++_pendingUpkeepCount;
            _pStore->submit( make_unique<EconomyDeliveryEraseWork>( this, std::move( outcome._recordKey ) ) );
        }
        if ( _bDeliveryScanPending == SW_TRUE || nowMs.count() < _nextDeliveryScanMs )
            return;
        _nextDeliveryScanMs   = nowMs.count() + _settings._deliveryScanIntervalMs;
        _bDeliveryScanPending = SW_TRUE;
        ++_pendingUpkeepCount;
        _pStore->submit( make_unique<EconomyDeliveryListWork>( this, _settings._deliveryScanMaxCount ) );
    }

    void EconomyService::completeDeliveryList( const vector<ReceiptDeliveryTask>& listTask, ServiceStoreResult result, ServiceStoreCompletion completion )
    {
        --_pendingUpkeepCount;
        _bDeliveryScanPending = SW_FALSE;
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
        --_pendingUpkeepCount;
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
                ++_pendingUpkeepCount;
                _pStore->submit( make_unique<EconomyRefundCursorWriteWork>( this, EconomyRefundCursor{ pValidator->getStoreName(), lastReadMs } ) );
            }
            return;
        }
        if ( _bRefundCursorReadPending == SW_TRUE || nowMs.count() < _nextRefundCursorReadMs )
            return;
        vector<EconomyRefundCursor> listCursor;
        for ( const IReceiptValidator* pValidator : _settings._pReceiptRegistry->getValidators() )
        {
            if ( pValidator->usesRefundCursor() )
                listCursor.push_back( EconomyRefundCursor{ pValidator->getStoreName(), 0 } );
        }
        if ( listCursor.empty() )
        {
            _bRefundCursorsRestored = SW_TRUE;
            return;
        }
        _bRefundCursorReadPending = SW_TRUE;
        ++_pendingUpkeepCount;
        _pStore->submit( make_unique<EconomyRefundCursorReadWork>( this, std::move( listCursor ) ) );
    }

    void EconomyService::completeRefundCursorRead( const vector<EconomyRefundCursor>& listCursor, ServiceStoreResult result, ServiceStoreCompletion completion )
    {
        --_pendingUpkeepCount;
        _bRefundCursorReadPending = SW_FALSE;
        if ( result != ServiceStoreResult::Ok )
        {
            _nextRefundCursorReadMs = _nowMs.count() + _settings._refundCursorRetryMs;
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
        --_pendingUpkeepCount;
        if ( result != ServiceStoreResult::Ok && completion.isCancelled() == false ) // 다음 조회가 다시 쓴다. 그 사이 재시작하면 옛 기준에서 더 겹쳐 읽을 뿐이다
            SW_LOG_WARNING_ID( "EconomyService.RefundCursorWriteFailed", "EconomyService: the refund cursor of store '%#' could not be written", cursor._storeName.c_str() );
    }

    void EconomyService::startCall( const EconomyCall& call, const CallTable::Origin& origin )
    {
        EconomyCallExtra extra;
        extra._accountID         = call._accountID;
        const int32  methodIndex = EconomyMethod::toIndex( call._method );
        const uint64 callID      = _calls.open( origin, methodIndex, extra );

        EconomyReply refused;
        refused._result = EconomyResult::Ok;
        if ( methodIndex < 0 || _pStore == nullptr )
            refused._result = EconomyResult::InvalidRequest;
        else if ( call._accountID == kInvalidAccountID )
            refused._result = EconomyResult::NotSignedIn; // 호스트가 이미 막지만 submitCall 경로도 같은 규칙을 따른다
        else if ( call._method == EconomyMethod::kPurchase && call._idempotencyKey.isValid() == false )
            refused._result = EconomyResult::InvalidRequest; // 상태를 바꾸는 요청에는 멱등 키가 필수이다
        if ( refused._result != EconomyResult::Ok )
        {
            (void)_calls.finish( callID, refused );
            return;
        }
        if ( call._method == EconomyMethod::kRedeemReceipt )
        {
            const uint64 ticket = _settings._pReceiptRegistry != nullptr
                                    ? _settings._pReceiptRegistry->submitValidation( call._redeem._storeName, call._redeem._payload, call._accountID, call._nowMs )
                                    : 0;
            if ( ticket == 0 )
            {
                refused._result = EconomyResult::ReceiptInvalid;
                (void)_calls.finish( callID, refused );
                return;
            }
            CallTable::Call* pCall = _calls.find( callID ); // 방금 올렸다 — 검증 제출은 표를 건드리지 않는다
            if ( pCall != nullptr )
                pCall->_extra._receiptTicket = ticket; // 지급 작업은 검증 결과가 오면 맡긴다(pollReceipts)
            return;
        }
        const UnixMilliseconds         nowMs = call._nowMs;
        unique_ptr<EconomyRequestWork> work  = make_unique<EconomyRequestWork>( this, _settings, callID, methodIndex );
        work->_accountID                     = call._accountID;
        work->_history                       = call._history;
        work->_purchase._offerID             = call._purchase._offerID;
        work->_purchase._count               = call._purchase._count;
        work->_purchase._accountID           = call._accountID;
        work->_purchase._nowMs               = nowMs.count();
        work->_purchase._journalKey          = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( call._accountID ), call._idempotencyKey._high,
                                                                                      call._idempotencyKey._low );
        work->_withdraw._storeName           = call._withdraw._storeName;
        work->_withdraw._transactionID       = call._withdraw._transactionID;
        work->_withdraw._accountID           = call._accountID;
        work->_withdraw._nowMs               = nowMs.count();
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
            const uint64     ticket = result._ticket;
            CallTable::Call* pCall  = _calls.findIf( [ticket]( const CallTable::Call& call )
            { return call._extra._receiptTicket == ticket; } );
            if ( pCall == nullptr )
                continue;
            pCall->_extra._receiptTicket             = 0;
            const AccountID                accountID = pCall->_extra._accountID;
            unique_ptr<EconomyRequestWork> work      = make_unique<EconomyRequestWork>( this, _settings, pCall->_callID, EconomyServiceInternal::kRedeemIndex );
            work->_accountID                         = accountID;
            work->_redeem._receipt                   = std::move( result );
            work->_redeem._pExternalLinks            = _settings._pExternalLinks;
            work->_redeem._accountID                 = accountID;
            work->_redeem._nowMs                     = _nowMs.count();
            work->_redeem._bAcceptSandbox            = _settings._bAcceptSandboxReceipts;
            _pStore->submit( std::move( work ) );
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

    void EconomyService::completeLedgerAudit( ServiceStoreResult result, const LedgerAuditReport& report, ServiceStoreCompletion completion )
    {
        _bAuditPending      = SW_FALSE;
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
            SW_LOG_WARNING_ID( "EconomyService.EconomyServiceBalanceNoticeAccount", "EconomyService: a balance notice for account %# is too large to send", accountID );
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

    void EconomyService::completeRefund( const ReceiptRefundNotice& notice, EconomyResult result, const vector<EconomyBalanceChange>& listChange, ServiceStoreCompletion completion )
    {
        --_pendingRefundCount;
        for ( const EconomyBalanceChange& change : listChange )
        {
            notifyBalancesChanged( change._accountID, vector<LedgerBalance>{ change._balance } );
        }
        const bool bFailed = result != EconomyResult::Ok && result != EconomyResult::AlreadyRefunded;
        if ( bFailed && completion.isCancelled() == false ) // 종료 때문에 취소된 회수는 오류가 아니다(다음 기동의 겹친 조회가 다시 읽는다)
        {
            SW_LOG_ERROR_ID( "EconomyService.EconomyServiceStoreRefundCould", "EconomyService: a store refund could not be revoked (%#) - the next overlapping poll reports it again", static_cast<int32>( result ) );
            // 제공자의 겹침 거름이 이 거래를 기억하면 겹친 조회가 다시 알리지 않아 회수할 기회가 없어진다.
            if ( _settings._pReceiptRegistry != nullptr )
                _settings._pReceiptRegistry->forgetRefund( notice );
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

    void EconomyService::completeCall( uint64 callID, const EconomyReply& reply )
    {
        // 다시 시도할 결과는 요청 층이 기억하지 않는다 — 기억하면 같은 키의 재시도가 서비스에 닿지 못하고 같은 대기를 받는다. 모르는 id 는 표가 경고한다.
        (void)_calls.finish( callID, reply, reply.isRetryable() ? NetResponseRetention::Forget : NetResponseRetention::Remember );
    }
} // namespace sw

namespace sw
{
    const OnlinePushRelayEntry kEconomyBalanceChangedRelay{ "Economy.BalanceChanged", EconomyMethod{}, EconomyPushName::BalanceChanged, EconomyProtocol::MessageTypes{},
                                                            OnlinePushBody<EconomyBalanceChanged>{} };

    SW_REGISTER_DEV_ONLINE_EXTENSION( EconomyDevOptions, EconomyDevOnlineInternal::kDevOptions );
} // namespace sw
