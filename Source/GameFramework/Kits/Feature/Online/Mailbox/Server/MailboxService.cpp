#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxService.h"

#include "Core/Serialization/BitStream.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Identity/OfflinePush.h"
#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/Base/Online/Ledger/IBalanceNotice.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceStoreUtil.h"

namespace sw
{
    SW_LOG_CALLER( "MailboxService" );

    namespace
    {
        struct MailboxServiceInternal
        {
            static constexpr const utf8* kArrExpiryAction[] = { "discarded", "returned", "removed" };
        };

        /** @brief 새 우편 알림을 가져가며 지우는 작업입니다(`ServiceMail::claimNotices`). */
        class MailboxNoticeWork final : public IServiceStoreWork
        {
        public:
            MailboxNoticeWork( MailboxService* pService, int32 batchCount )
                : _listNotice{}
                , _pService{ pService }
                , _batchCount{ batchCount }
                , _result{ ServiceStoreResult::Unavailable }
            {
            }

            void run( IServiceStoreConnection& connection ) override { _result = ServiceMail::claimNotices( connection, _batchCount, _listNotice ); }
            void complete() override { _pService->completeNotices( _result, _listNotice ); }

        private:
            vector<ServiceMailNotice> _listNotice;
            MailboxService*           _pService;
            int32                     _batchCount;
            ServiceStoreResult        _result;
        };

        /** @brief 우편함 요청 하나의 저장소 왕복입니다. */
        class MailboxRequestWork final : public IServiceStoreWork
        {
        public:
            MailboxRequestWork( MailboxService* pService, const MailboxCall& call, const vector<ServiceMailCampaign>& listCampaign, const ILedgerPolicy* pPolicy, uint64 callID )
                : _call{ call }
                , _listCampaign{ listCampaign }
                , _reply{}
                , _pService{ pService }
                , _pPolicy{ pPolicy }
                , _callID{ callID }
            {
            }

            void run( IServiceStoreConnection& connection ) override
            {
                const uint64 accountID = _call._accountID;
                switch ( _call._method )
                {
                    case MailboxMethod::kList:
                    {
                        (void)MailboxStoreLogic::listMail( connection, accountID, _call._nowMs.count(), _listCampaign, _call._request, _reply );
                        break;
                    }
                    case MailboxMethod::kMarkRead:
                    {
                        _reply._result = MailboxStoreLogic::markRead( connection, accountID, _call._request._mailKey );
                        break;
                    }
                    case MailboxMethod::kClaim:
                    {
                        MailboxClaimInput input;
                        input._mailKey   = _call._request._mailKey;
                        input._accountID = accountID;
                        input._nowMs     = _call._nowMs.count();
                        input._pPolicy   = _pPolicy;
                        _reply._result   = MailboxStoreLogic::claim( connection, input, _reply );
                        break;
                    }
                    case MailboxMethod::kClaimAll:
                    {
                        (void)MailboxStoreLogic::claimAll( connection, accountID, _call._nowMs.count(), _pPolicy, _reply );
                        break;
                    }
                    case MailboxMethod::kDelete:
                    {
                        _reply._result = MailboxStoreLogic::deleteMail( connection, accountID, _call._request._mailKey );
                        break;
                    }
                    default:
                    {
                        _reply._result = MailboxResult::InvalidRequest;
                        break;
                    }
                }
            }

            void complete() override { _pService->completeCall( _callID, _reply ); }

        private:
            MailboxCall                 _call;
            vector<ServiceMailCampaign> _listCampaign; ///< 목록 첫 페이지에 끼울 활성 캠페인이다. 복사한 값이며 서비스 멤버를 건드리지 않는다
            MailboxReply                _reply;
            MailboxService*             _pService;
            const ILedgerPolicy*        _pPolicy;
            uint64                      _callID;
        };

        /** @brief 만료 정리 한 번입니다. */
        class MailboxSweepWork final : public IServiceStoreWork
        {
        public:
            MailboxSweepWork( MailboxService* pService, UnixMilliseconds nowMs, int32 batchCount )
                : _stats{}
                , _pService{ pService }
                , _nowMs{ nowMs }
                , _batchCount{ batchCount }
            {
            }

            void run( IServiceStoreConnection& connection ) override { MailboxStoreLogic::sweepExpired( connection, _nowMs.count(), _batchCount, _stats ); }
            void complete() override { _pService->completeSweep( _stats ); }

        private:
            MailboxSweepStats _stats;
            MailboxService*   _pService;
            UnixMilliseconds  _nowMs;
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
        , _mapPresenceRequestToNotice{}
        , _pStore{ nullptr }
        , _pHost{ nullptr }
        , _arrExpiredCounter{}
        , _callIDAllocator{}
        , _nowMs{ 0 }
        , _nextSweepMs{ 0 }
        , _nextCampaignRefreshMs{ 0 }
        , _nextNoticeMs{ 0 }
        , _bSweepPending{ SW_FALSE }
        , _bSweepRequested{ SW_FALSE }
        , _bCampaignRefreshPending{ SW_FALSE }
        , _bNoticePending{ SW_FALSE }
        , _bSweepRunHeld{ SW_FALSE }
        , _sweepRun{}
    {
    }

    MailboxService::~MailboxService()
    {
        SW_ASSERT( _listPendingCall.empty() );
    }

    bool MailboxService::initialize( IServiceStore* pStore, const MailboxServiceConfig& settings )
    {
        if ( pStore == nullptr )
        {
            SW_LOG_ERROR( "MailboxService needs a store" );
            return false;
        }
        _pStore    = pStore;
        _settings  = settings;
        _findOwner = settings._pPresence != nullptr ? settings._pPresence->openFindOwner() : Subscription{};
        _metrics.initialize<MailboxMethodName, MailboxResult>( settings._pMetricRegistry, "mailbox" );
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
        if ( settings._pScheduler != nullptr )
        {
            ScheduleDefinition schedule;
            schedule._jobID      = kSweepJobID;
            schedule._kind       = ScheduleKind::Interval;
            schedule._intervalMs = settings._sweepIntervalMs;
            if ( settings._pScheduler->registerJob( schedule, this ) == false )
            {
                SW_LOG_ERROR( "MailboxService: the sweep job could not be scheduled (sweepIntervalMs %# must be at least a minute)", settings._sweepIntervalMs );
                return false;
            }
        }
        return true;
    }

    void MailboxService::shutdown()
    {
        ServiceStoreUtil::drainUntilIdle( _pStore, [this]()
        { return isIdle(); } );
        if ( isIdle() == false )
        {
            SW_LOG_ERROR( "MailboxService shut down with %# calls still pending", _listPendingCall.size() );
            _listPendingCall.clear();
        }
        _findOwner.reset(); // 기다리던 찾기의 알림을 잃는다. 우편은 목록에 있다
        _mapPresenceRequestToNotice.clear();
        _pStore = nullptr;
        _pHost  = nullptr;
    }

    void MailboxService::submitCall( const MailboxCall& call, ReplyDelegate onReply )
    {
        PendingCall pending;
        pending._onReply = onReply;
        startCall( call, pending );
    }

    void MailboxService::update( UnixMilliseconds nowMs )
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
        const bool bTickSweep = _settings._pScheduler == nullptr && _settings._sweepIntervalMs > 0 && nowMs >= _nextSweepMs; // 예약 테이블이 있으면 회차가 부른다
        const bool bSweepDue  = bTickSweep || _bSweepRequested == SW_TRUE;
        if ( _bSweepPending == SW_FALSE && bSweepDue )
        {
            _bSweepPending   = SW_TRUE;
            _bSweepRequested = SW_FALSE;
            _nextSweepMs     = nowMs + _settings._sweepIntervalMs;
            _pStore->submit( make_unique<MailboxSweepWork>( this, nowMs, _settings._sweepBatchCount ) );
        }
        const bool bNoticeDue = _settings._noticeIntervalMs > 0 && nowMs >= _nextNoticeMs && _pHost != nullptr;
        if ( _bNoticePending == SW_FALSE && bNoticeDue )
        {
            _bNoticePending = SW_TRUE;
            _nextNoticeMs   = nowMs + _settings._noticeIntervalMs;
            _pStore->submit( make_unique<MailboxNoticeWork>( this, _settings._noticeBatchCount ) );
        }
        _metrics.setPendingStoreWorkCount( static_cast<int32>( _listPendingCall.size() ) );
    }

    void MailboxService::completeNotices( ServiceStoreResult result, vector<ServiceMailNotice>& inoutListNotice )
    {
        _bNoticePending = SW_FALSE;
        if ( result == ServiceStoreResult::Invalid )
            SW_LOG_ERROR( "MailboxService: a new-mail notice record is corrupt - notices stop until it is fixed" );
        if ( result != ServiceStoreResult::Ok || _pHost == nullptr )
            return; // Conflict 는 다른 서버가 가져갔다. 그 밖은 다음 주기에 다시 한다
        if ( static_cast<int32>( inoutListNotice.size() ) == _settings._noticeBatchCount )
            _nextNoticeMs = _nowMs; // 더 있을 수 있으므로 다음 틱에 바로 다시 가져간다
        for ( ServiceMailNotice& notice : inoutListNotice )
        {
            const AccountID accountID = notice._recipientAccountID;
            MailboxNewMail  newMail{ std::move( notice._mailKey ), std::move( notice._titleKey ) };
            BitWriter       body;
            const bool      bWritten = MailboxProtocol::MessageTypes::write( body, newMail );
            if ( bWritten && _pHost->sendPush( accountID, MailboxMethod::kPushNewMail, body ) )
                continue; // 이 서버에 붙어 있다
            if ( _settings._pPresence == nullptr )
            {
                sendOfflinePush( accountID ); // 서버가 하나이고 붙어 있지 않다
                continue;
            }
            const uint64 requestID                 = _settings._pPresence->submitFindByAccount( accountID, AccountPresenceDelegate::create<&MailboxService::onNoticePresenceFound>( this ), _findOwner );
            _mapPresenceRequestToNotice[requestID] = std::move( newMail );
        }
    }

    void MailboxService::onNoticePresenceFound( const AccountPresenceResult& result )
    {
        const auto noticeIt = _mapPresenceRequestToNotice.find( result._requestID );
        if ( noticeIt == _mapPresenceRequestToNotice.end() )
            return;
        const MailboxNewMail newMail = std::move( noticeIt->second );
        _mapPresenceRequestToNotice.erase( noticeIt );
        const AccountID accountID = result._identity._accountID;
        if ( result.isOnline() && _settings._pPresence->sendRemotePush( accountID, MailboxPushName::NewMail, newMail ) )
            return; // 다른 서버에 붙어 있다
        sendOfflinePush( accountID );
    }

    void MailboxService::sendOfflinePush( AccountID accountID )
    {
        if ( _settings._pOfflinePush == nullptr || accountID == kInvalidAccountID )
            return;
        (void)_settings._pOfflinePush->sendOfflinePush( accountID, "push.mail.title", "push.mail.body", "mailbox", _nowMs ); // 도배 제한에 걸리면 보내지 않는다. 우편은 목록에 있다
    }

    void MailboxService::onScheduledRun( const ScheduledRun& run )
    {
        _sweepRun      = run;
        _bSweepRunHeld = SW_TRUE;
        requestSweep();
    }

    void MailboxService::requestSweep()
    {
        _bSweepRequested = SW_TRUE;
    }

    void MailboxService::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        MailboxCall call{ context._nowMs };
        call._method         = context._method;
        call._accountID      = context._accountID;
        call._idempotencyKey = context._idempotencyKey;
        if ( MailboxProtocol::MessageTypes::read( body, call._request ) != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        PendingCall pending;
        pending._pHost = &host;
        pending._token = context._token;
        startCall( call, pending );
    }

    void MailboxService::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        _pHost = &host;
        update( nowMs );
    }

    void MailboxService::startCall( const MailboxCall& call, PendingCall pending )
    {
        pending._callID              = _callIDAllocator.allocate();
        pending._receivedNanoseconds = MonotonicClock::nowNanoseconds(); // 예외 — 지연 지표는 실제 처리 시간을 잰다(가짜 시각으로 재면 뜻이 없다, 프로파일러와 같은 자리)
        pending._methodIndex         = MailboxMethod::toIndex( call._method );
        _listPendingCall.push_back( pending );
        const size_t pendingIndex = _listPendingCall.size() - 1;

        MailboxReply refused;
        refused._result = MailboxResult::Ok;
        if ( pending._methodIndex < 0 || _pStore == nullptr )
            refused._result = MailboxResult::InvalidRequest;
        else if ( call._accountID == kInvalidAccountID )
            refused._result = MailboxResult::NotSignedIn;
        else if ( MailboxMethod::isClaim( call._method ) && call._idempotencyKey.isValid() == false )
            refused._result = MailboxResult::InvalidRequest; // 수령에는 멱등 키가 필수이다
        if ( refused._result != MailboxResult::Ok )
        {
            finishCall( pendingIndex, refused );
            return;
        }
        const bool bFirstPage = call._method == MailboxMethod::kList && call._request._cursor.empty();
        _pStore->submit( sw::make_unique<MailboxRequestWork>( this, call, bFirstPage ? _listCampaign : vector<ServiceMailCampaign>{}, _settings._pPolicy, pending._callID ) );
    }

    void MailboxService::completeCall( uint64 callID, const MailboxReply& reply )
    {
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingCall.size(); ++pendingIndex )
        {
            if ( _listPendingCall[pendingIndex]._callID == callID )
            {
                finishCall( pendingIndex, reply );
                return;
            }
        }
        SW_LOG_WARNING( "MailboxService completed an unknown call %#", callID );
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
        if ( _settings._pBalanceNotice != nullptr )
        {
            for ( const MailboxBalanceChange& change : stats._listChange )
            {
                _settings._pBalanceNotice->notifyBalancesChanged( change._accountID, vector<LedgerBalance>{ change._balance } ); // 돌려받은 계정이 접속해 있으면 지갑이 바로 맞는다
            }
        }
        if ( stats._failedCount > 0 )
            SW_LOG_WARNING( "Mail sweep left %# expired mails for the next pass", stats._failedCount );
        if ( stats._bMore == SW_TRUE )
        {
            _bSweepRequested = SW_TRUE; // 상한까지 채웠으므로 다음 틱에 바로 다시 정리한다
            return;
        }
        if ( _bSweepRunHeld == SW_TRUE && _settings._pScheduler != nullptr )
            _settings._pScheduler->completeRun( _sweepRun, stats._failedCount == 0 ); // 남은 것이 있으면 실패로 적어 다음 회차가 다시 본다
        _bSweepRunHeld = SW_FALSE;
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
            if ( campaign.isActive( _nowMs.count() ) || campaign._startMs > _nowMs.count() )
                _listCampaign.push_back( std::move( campaign ) );
        }
    }

    void MailboxService::finishCall( size_t pendingIndex, const MailboxReply& reply )
    {
        const PendingCall pending = _listPendingCall[pendingIndex];
        _listPendingCall.erase( _listPendingCall.begin() + static_cast<ptrdiff_t>( pendingIndex ) );
        if ( pending._pHost != nullptr )
            (void)pending._pHost->respondMessage<MailboxProtocol::MessageTypes>( pending._token, reply );
        else if ( pending._onReply.isBound() )
            pending._onReply( reply );
        _metrics.countRequest( pending._methodIndex, static_cast<int32>( reply._result ) );
        MetricHistogram* pLatency = _metrics.findLatencyHistogram( pending._methodIndex );
        if ( pLatency != nullptr )
            pLatency->observe( static_cast<float64>( MonotonicClock::nowNanoseconds() - pending._receivedNanoseconds ) * 1e-9 );
    }

    bool MailboxService::isIdle() const
    {
        return _listPendingCall.empty() && _bSweepPending == SW_FALSE && _bCampaignRefreshPending == SW_FALSE && _bNoticePending == SW_FALSE;
    }
} // namespace sw

namespace sw
{
    const OnlinePushRelayEntry kMailboxNewMailRelay{ "Mailbox.NewMail", MailboxMethod{}, MailboxPushName::NewMail, MailboxProtocol::MessageTypes{}, OnlinePushBody<MailboxNewMail>{} };
} // namespace sw
