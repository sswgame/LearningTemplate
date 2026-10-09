#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Mailbox/MailboxService.h"

#include "Core/Network/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"

namespace sw
{
    SW_LOG_CALLER( "MailboxService" );

    namespace
    {
        struct MailboxServiceInternal
        {
            static constexpr const utf8* kArrMethodName[]   = { "list", "mark_read", "claim", "claim_all", "delete" };
            static constexpr const utf8* kArrResultName[]   = { "ok", "not_signed_in", "invalid_request", "not_found", "already_claimed",
                                                                "expired", "has_attachments", "cap_exceeded", "busy", "unavailable" };
            static constexpr const utf8* kArrExpiryAction[] = { "discarded", "returned", "removed" };
            static_assert( SW_COUNT_OF( kArrMethodName ) == static_cast<size_t>( MailboxMethod::kCount ), "method names must match MailboxMethod" );
            static_assert( SW_COUNT_OF( kArrResultName ) == static_cast<size_t>( MailboxResult::Count ), "result names must match MailboxResult" );
            static constexpr int64 kShutdownWaitMs = 5000;
        };

        /** @brief 우편함 요청 하나의 저장 왕복입니다. */
        class MailboxRequestWork final : public IServiceStoreWork
        {
        public:
            MailboxRequestWork( MailboxService* pService, const MailboxCall& call, const vector<ServiceMailCampaign>& listCampaign, const ILedgerPolicy* pPolicy, uint64 callId )
                : _call{ call }
                , _listCampaign{ listCampaign }
                , _reply{}
                , _pService{ pService }
                , _pPolicy{ pPolicy }
                , _callId{ callId }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                const uint64 accountId = _call._accountId;
                switch ( _call._method )
                {
                    case MailboxMethod::kList:
                    {
                        (void)MailboxStoreLogic::listMail( connection, accountId, _call._nowMs, _listCampaign, _call._request, _reply );
                        break;
                    }
                    case MailboxMethod::kMarkRead:
                    {
                        _reply._result = MailboxStoreLogic::markRead( connection, accountId, _call._request._mailKey );
                        break;
                    }
                    case MailboxMethod::kClaim:
                    {
                        MailboxClaimInput input;
                        input._mailKey   = _call._request._mailKey;
                        input._accountId = accountId;
                        input._nowMs     = _call._nowMs;
                        input._pPolicy   = _pPolicy;
                        _reply._result   = MailboxStoreLogic::claim( connection, input, _reply );
                        break;
                    }
                    case MailboxMethod::kClaimAll:
                    {
                        (void)MailboxStoreLogic::claimAll( connection, accountId, _call._nowMs, _pPolicy, _reply );
                        break;
                    }
                    case MailboxMethod::kDelete:
                    {
                        _reply._result = MailboxStoreLogic::deleteMail( connection, accountId, _call._request._mailKey );
                        break;
                    }
                    default:
                    {
                        _reply._result = MailboxResult::InvalidRequest;
                        break;
                    }
                }
            }

            void complete() override { _pService->completeCall( _callId, _reply ); }

        private:
            MailboxCall                 _call;
            vector<ServiceMailCampaign> _listCampaign; ///< 목록 첫 쪽에 끼울 활성 캠페인(복사 — 서비스 멤버를 만지지 않는다)
            MailboxReply                _reply;
            MailboxService*             _pService;
            const ILedgerPolicy*        _pPolicy;
            uint64                      _callId;
        };

        /** @brief 만료 쓸기 한 번입니다. */
        class MailboxSweepWork final : public IServiceStoreWork
        {
        public:
            MailboxSweepWork( MailboxService* pService, int64 nowMs, int32 batchCount )
                : _stats{}
                , _pService{ pService }
                , _nowMs{ nowMs }
                , _batchCount{ batchCount }
            {
            }

            void run( IServiceStoreConnection& connection ) override { MailboxStoreLogic::sweepExpired( connection, _nowMs, _batchCount, _stats ); }
            void complete() override { _pService->completeSweep( _stats ); }

        private:
            MailboxSweepStats _stats;
            MailboxService*   _pService;
            int64             _nowMs;
            int32             _batchCount;
        };

        /** @brief 캠페인 목록 다시 읽기입니다. */
        class MailboxCampaignWork final : public IServiceStoreWork
        {
        public:
            explicit MailboxCampaignWork( MailboxService* pService )
                : _listCampaign{}
                , _pService{ pService }
                , _result{ ServiceStoreResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = ServiceMailCampaignTable::listCampaigns( connection, _listCampaign ); }
            void complete() override { _pService->completeCampaignRefresh( _result, _listCampaign ); }

        private:
            vector<ServiceMailCampaign> _listCampaign;
            MailboxService*             _pService;
            ServiceStoreResult          _result;
        };
    } // namespace
} // namespace sw

namespace sw
{
    MailboxService::MailboxService()
        : _listPendingCall{}
        , _listCampaign{}
        , _metrics{}
        , _settings{}
        , _pStore{ nullptr }
        , _arrExpiredCounter{}
        , _nextCallId{ 1 }
        , _nowMs{ 0 }
        , _nextSweepMs{ 0 }
        , _nextCampaignRefreshMs{ 0 }
        , _bSweepPending{ SW_FALSE }
        , _bSweepRequested{ SW_FALSE }
        , _bCampaignRefreshPending{ SW_FALSE }
    {
    }

    MailboxService::~MailboxService() { SW_ASSERT( _listPendingCall.empty() ); }

    bool MailboxService::initialize( IServiceStore* pStore, const MailboxServiceSettings& settings )
    {
        if ( pStore == nullptr )
        {
            SW_LOG_ERROR( "MailboxService needs a store" );
            return false;
        }
        _pStore   = pStore;
        _settings = settings;
        _metrics.initialize( settings._pMetricRegistry, "mailbox", MailboxServiceInternal::kArrMethodName, MailboxMethod::kCount, MailboxServiceInternal::kArrResultName,
                             static_cast<int32>( MailboxResult::Count ) );
        if ( settings._pMetricRegistry != nullptr )
        {
            for ( int32 actionIndex = 0; actionIndex < static_cast<int32>( SW_COUNT_OF( _arrExpiredCounter ) ); ++actionIndex )
            {
                const vector<MetricLabel> listLabel{
                    MetricLabel{ "action", MailboxServiceInternal::kArrExpiryAction[actionIndex] }
                };
                _arrExpiredCounter[actionIndex] = settings._pMetricRegistry->registerCounter( "mailbox_expired_total", "Expired mail handled by the sweep", listLabel );
            }
        }
        return true;
    }

    void MailboxService::shutdown()
    {
        const Deadline deadline = Deadline::afterMilliseconds( MailboxServiceInternal::kShutdownWaitMs );
        while ( _pStore != nullptr && isIdle() == false && deadline.isExpired() == false )
        {
            if ( _pStore->pollCompletions() == 0 )
                MonotonicClock::sleepUntilNanoseconds( MonotonicClock::nowNanoseconds() + 1000000 );
        }
        if ( isIdle() == false )
        {
            SW_LOG_ERROR( "MailboxService shut down with %# calls still pending", _listPendingCall.size() );
            _listPendingCall.clear();
        }
        _pStore = nullptr;
    }

    void MailboxService::submitCall( const MailboxCall& call, ReplyDelegate onReply )
    {
        PendingCall pending;
        pending._onReply = onReply;
        startCall( call, pending );
    }

    void MailboxService::tick( int64 nowMs )
    {
        _nowMs = nowMs;
        if ( _pStore == nullptr )
            return;
        if ( _bCampaignRefreshPending == SW_FALSE && nowMs >= _nextCampaignRefreshMs )
        {
            _bCampaignRefreshPending = SW_TRUE;
            _nextCampaignRefreshMs   = nowMs + _settings._campaignRefreshMs;
            _pStore->submit( make_unique<MailboxCampaignWork>( this ) );
        }
        const bool bSweepDue = ( _settings._sweepIntervalMs > 0 && nowMs >= _nextSweepMs ) || _bSweepRequested == SW_TRUE;
        if ( _bSweepPending == SW_FALSE && bSweepDue )
        {
            _bSweepPending   = SW_TRUE;
            _bSweepRequested = SW_FALSE;
            _nextSweepMs     = nowMs + _settings._sweepIntervalMs;
            _pStore->submit( make_unique<MailboxSweepWork>( this, nowMs, _settings._sweepBatchCount ) );
        }
        _metrics.setPendingStoreWorkCount( static_cast<int32>( _listPendingCall.size() ) );
    }

    void MailboxService::requestSweep() { _bSweepRequested = SW_TRUE; }

    void MailboxService::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        MailboxCall call;
        call._method         = context._method;
        call._accountId      = context._accountId;
        call._idempotencyKey = context._idempotencyKey;
        call._nowMs          = context._nowMs;
        if ( MailboxProtocol::readRequest( body, call._request ) == false )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        PendingCall pending;
        pending._pHost = &host;
        pending._token = context._token;
        startCall( call, pending );
    }

    void MailboxService::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        (void)host;
        tick( nowMs );
    }

    void MailboxService::startCall( const MailboxCall& call, PendingCall pending )
    {
        pending._callId              = _nextCallId++;
        pending._receivedNanoseconds = MonotonicClock::nowNanoseconds();
        pending._methodIndex         = MailboxMethod::toIndex( call._method );
        _listPendingCall.push_back( pending );
        const size_t pendingIndex = _listPendingCall.size() - 1;

        MailboxReply refused;
        refused._result = MailboxResult::Ok;
        if ( pending._methodIndex < 0 || _pStore == nullptr )
            refused._result = MailboxResult::InvalidRequest;
        else if ( call._accountId == kInvalidAccountId )
            refused._result = MailboxResult::NotSignedIn;
        else if ( MailboxMethod::isClaim( call._method ) && call._idempotencyKey.isValid() == false )
            refused._result = MailboxResult::InvalidRequest; // 수령은 멱등 키 필수
        if ( refused._result != MailboxResult::Ok )
        {
            finishCall( pendingIndex, refused );
            return;
        }
        MailboxCall work      = call;
        work._nowMs           = call._nowMs != 0 ? call._nowMs : _nowMs;
        const bool bFirstPage = call._method == MailboxMethod::kList && call._request._cursor.empty();
        _pStore->submit( sw::make_unique<MailboxRequestWork>( this, work, bFirstPage ? _listCampaign : vector<ServiceMailCampaign>{}, _settings._pPolicy, pending._callId ) );
    }

    void MailboxService::completeCall( uint64 callId, const MailboxReply& reply )
    {
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingCall.size(); ++pendingIndex )
        {
            if ( _listPendingCall[pendingIndex]._callId == callId )
            {
                finishCall( pendingIndex, reply );
                return;
            }
        }
        SW_LOG_WARNING( "MailboxService completed an unknown call %#", callId );
    }

    void MailboxService::completeSweep( const MailboxSweepStats& stats )
    {
        _bSweepPending         = SW_FALSE;
        const int32 arrCount[] = { stats._discardedCount, stats._returnedCount, stats._removedCount };
        for ( int32 actionIndex = 0; actionIndex < static_cast<int32>( SW_COUNT_OF( arrCount ) ); ++actionIndex )
        {
            if ( _arrExpiredCounter[actionIndex] != nullptr && arrCount[actionIndex] > 0 )
                _arrExpiredCounter[actionIndex]->add( static_cast<uint64>( arrCount[actionIndex] ) );
        }
        if ( stats._failedCount > 0 )
            SW_LOG_WARNING( "Mail sweep left %# expired mails for the next pass", stats._failedCount );
        if ( stats._bMore == SW_TRUE )
            _bSweepRequested = SW_TRUE; // 상한까지 채웠다 — 다음 틱에 바로 또
    }

    void MailboxService::completeCampaignRefresh( ServiceStoreResult result, vector<ServiceMailCampaign>& inoutListCampaign )
    {
        _bCampaignRefreshPending = SW_FALSE;
        if ( result != ServiceStoreResult::Ok )
        {
            SW_LOG_WARNING( "Mail campaigns could not be read - keeping the cached list" );
            return;
        }
        _listCampaign.clear();
        for ( ServiceMailCampaign& campaign : inoutListCampaign )
        {
            if ( campaign.isActive( _nowMs ) || campaign._startMs > _nowMs )
                _listCampaign.push_back( std::move( campaign ) );
        }
    }

    void MailboxService::finishCall( size_t pendingIndex, const MailboxReply& reply )
    {
        const PendingCall pending = _listPendingCall[pendingIndex];
        _listPendingCall.erase( _listPendingCall.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
        if ( pending._pHost != nullptr )
        {
            BitWriter writer;
            MailboxProtocol::writeReply( writer, reply );
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

    bool MailboxService::isIdle() const { return _listPendingCall.empty() && _bSweepPending == SW_FALSE && _bCampaignRefreshPending == SW_FALSE; }
} // namespace sw
