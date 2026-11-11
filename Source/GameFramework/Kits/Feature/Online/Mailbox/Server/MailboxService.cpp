#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxService.h"

#include "Core/Serialization/BitStream.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/OnlinePushRelay.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Ledger/IBalanceChangedListener.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Mail/Server/ServiceMail.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    namespace
    {
        struct MailboxServiceInternal
        {
            static constexpr const utf8* kArrExpiryAction[] = { "discarded", "returned", "removed" };
        };

        /** @brief 저장소 스레드가 쓰는 요청 쪽 값입니다. 복사한 값이며 서비스 멤버를 건드리지 않습니다. */
        struct MailboxCallInput
        {
            vector<ServiceMailCampaign> _listCampaign{}; ///< 목록 첫 페이지에 끼울 활성 캠페인이다
            const ILedgerPolicy*        _pPolicy{ nullptr };
            AccountID                   _accountID{ kInvalidAccountID };
            int64                       _nowMs{ 0 };
        };

        /** @brief 메서드마다의 저장 함수입니다. 오버로드라 표에 메서드를 더하고 줄을 빠뜨리면 컴파일되지 않습니다. */
        struct MailboxStoreCall
        {
            static MailboxServiceResult run( MailboxMethod::List, IServiceStoreConnection& connection, const MailboxListRequest& request, const MailboxCallInput& input, MailboxListReply& outReply )
            {
                return MailboxStoreLogic::listMail( connection, input._accountID, input._nowMs, input._listCampaign, request, outReply );
            }

            static MailboxServiceResult run( MailboxMethod::MarkRead, IServiceStoreConnection& connection, const MailboxMailRequest& request, const MailboxCallInput& input, MailboxResultReply& outReply )
            {
                const MailboxServiceResult serviceResult = MailboxStoreLogic::markRead( connection, input._accountID, request._mailKey );
                outReply._result                         = serviceResult._result;
                return serviceResult;
            }

            static MailboxServiceResult run( MailboxMethod::Claim, IServiceStoreConnection& connection, const MailboxMailRequest& request, const MailboxCallInput& input, MailboxClaimReply& outReply )
            {
                MailboxClaimInput claimInput;
                claimInput._mailKey                      = request._mailKey;
                claimInput._accountID                    = input._accountID;
                claimInput._nowMs                        = input._nowMs;
                claimInput._pPolicy                      = input._pPolicy;
                const MailboxServiceResult serviceResult = MailboxStoreLogic::claim( connection, claimInput, outReply );
                outReply._result                         = serviceResult._result;
                return serviceResult;
            }

            static MailboxServiceResult run( MailboxMethod::ClaimAll, IServiceStoreConnection& connection, const OnlineNoBody& request, const MailboxCallInput& input, MailboxClaimReply& outReply )
            {
                (void)request;
                return MailboxStoreLogic::claimAll( connection, input._accountID, input._nowMs, input._pPolicy, outReply );
            }

            static MailboxServiceResult run( MailboxMethod::Delete, IServiceStoreConnection& connection, const MailboxMailRequest& request, const MailboxCallInput& input, MailboxResultReply& outReply )
            {
                const MailboxServiceResult serviceResult = MailboxStoreLogic::deleteMail( connection, input._accountID, request._mailKey );
                outReply._result                         = serviceResult._result;
                return serviceResult;
            }
        };

    } // namespace
} // namespace sw

namespace sw
{
    MailboxService::MailboxService()
        : _listCampaign{}
        , _settings{}
        , _presenceFinds{}
        , _pHost{ nullptr }
        , _arrExpiredCounter{}
        , _nowMs{ 0 }
        , _sweepJob{}
        , _campaignJob{}
        , _noticeJob{}
    {
    }

    MailboxService::~MailboxService()
    {
        SW_ASSERT( _calls.isEmpty() );
    }

    bool MailboxService::initialize( IServiceStore* pStore, const MailboxServiceConfig& settings )
    {
        if ( pStore == nullptr )
        {
            SW_LOG_ERROR_ID( "MailboxService.MailboxServiceNeedsStore", "MailboxService needs a store" );
            return false;
        }
        _store.bindStore( pStore );
        _settings = settings;
        _sweepJob.setPeriod( settings._pScheduler == nullptr ? settings._sweepIntervalMs : 0 ); // 예약 테이블이 있으면 회차가 부른다
        _campaignJob.setPeriod( settings._campaignRefreshMs );
        _noticeJob.setPeriod( settings._noticeIntervalMs ); // 0 이면 알림을 보내지 않는다
        _presenceFinds.bind( settings._pPresence );
        initializeCalls( settings._pMetricRegistry, "mailbox" );
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
            ScheduleDef schedule;
            schedule._jobID      = kSweepJobID;
            schedule._kind       = ScheduleKind::Interval;
            schedule._intervalMs = settings._sweepIntervalMs;
            if ( settings._pScheduler->registerJob( schedule, ScheduledJobFunction::create<&MailboxService::onScheduledRun>( this ) ) == false )
            {
                SW_LOG_ERROR_ID( "MailboxService.MailboxServiceSweepJobCould", "MailboxService: the sweep job could not be scheduled (sweepIntervalMs %# must be at least a minute)", settings._sweepIntervalMs );
                return false;
            }
        }
        return true;
    }

    void MailboxService::shutdown()
    {
        _store.flushUntilIdle( [this]()
        { return isIdle(); } );
        _calls.abandonAll();
        _presenceFinds.unbind(); // 기다리던 찾기의 알림을 잃는다. 우편은 목록에 있다
        _store.bindStore( nullptr );
        _pHost = nullptr;
    }

    void MailboxService::update( UnixMilliseconds nowMs )
    {
        _nowMs = nowMs;
        if ( _store.hasStore() == false )
            return;
        if ( _campaignJob.tryStart( nowMs ) )
        {
            _store.submit( []( IServiceStoreConnection& connection )
            {
                ServiceStoreRead<vector<ServiceMailCampaign>> read;
                read._result = ServiceMailCampaign::listCampaigns( connection, read._value );
                return read;
            }, [this]( ServiceStoreRead<vector<ServiceMailCampaign>>& read, ServiceStoreCompletion completion )
            { completeCampaignRefresh( read._result, read._value, completion ); } );
        }
        if ( _sweepJob.tryStart( nowMs ) )
        {
            const int32 batchCount = _settings._sweepBatchCount;
            _store.submit( [nowMs, batchCount]( IServiceStoreConnection& connection )
            {
                MailboxSweepStats stats;
                MailboxStoreLogic::sweepExpired( connection, nowMs.count(), batchCount, stats );
                return stats;
            }, [this]( const MailboxSweepStats& stats, ServiceStoreCompletion completion )
            { completeSweep( stats, completion ); } );
        }
        if ( _pHost != nullptr && _noticeJob.tryStart( nowMs ) )
        {
            const int32 batchCount = _settings._noticeBatchCount;
            _store.submit( [batchCount]( IServiceStoreConnection& connection )
            {
                ServiceStoreRead<vector<ServiceMailNotification>> read;
                read._result = ServiceMail::claimNotices( connection, batchCount, read._value );
                return read;
            }, [this]( ServiceStoreRead<vector<ServiceMailNotification>>& read )
            { completeNotices( read._result, read._value ); } );
        }
        _calls.publishPendingCount();
    }

    void MailboxService::completeNotices( ServiceStoreResult result, vector<ServiceMailNotification>& inoutListNotice )
    {
        _noticeJob.finish();
        if ( result == ServiceStoreResult::Invalid )
            SW_LOG_ERROR_ID( "MailboxService.MailboxServiceNewMailNotice", "MailboxService: a new-mail notice record is corrupt - notices stop until it is fixed" );
        if ( result != ServiceStoreResult::Ok || _pHost == nullptr )
            return; // Conflict 는 다른 서버가 가져갔다. 그 밖은 다음 주기에 다시 한다
        if ( static_cast<int32>( inoutListNotice.size() ) == _settings._noticeBatchCount )
            _noticeJob.request(); // 더 있을 수 있으므로 다음 틱에 바로 다시 가져간다
        for ( ServiceMailNotification& notice : inoutListNotice )
        {
            const AccountID accountID = notice._recipientAccountID;
            MailboxNewMail  newMail{ std::move( notice._mailKey ), std::move( notice._titleKey ) };
            BitWriter       body;
            const bool      bWritten = MailboxProtocol::MessageTypes::write( body, newMail );
            if ( bWritten && _pHost->sendPush( accountID, MailboxMethod::NewMail::kKind, body ) )
                continue; // 이 서버에 붙어 있다
            const bool bFinding = _presenceFinds.findByAccount( accountID, accountID, [this, newMail = std::move( newMail )]( const AccountPresenceResult& result )
            { handleMailPresenceResult( result, newMail ); } );
            if ( bFinding == false )
                sendOfflinePush( accountID ); // 서버가 하나이고 붙어 있지 않다
        }
    }

    void MailboxService::notifyClaimedBalances( AccountID accountID, const MailboxServiceResult& serviceResult, const MailboxClaimReply& reply )
    {
        const bool bNothingChanged = serviceResult.isFailed() || reply._listBalance.empty() || _settings._pBalanceNotice == nullptr;
        if ( bNothingChanged )
            return;
        _settings._pBalanceNotice->notifyBalancesChanged( accountID, reply._listBalance ); // 지갑은 서버의 잔액 변경 알림으로 맞는다 — 게임이 응답의 잔액을 옮기지 않는다
    }

    void MailboxService::handleMailPresenceResult( const AccountPresenceResult& result, const MailboxNewMail& newMail )
    {
        const AccountID accountID = result._identity._accountID;
        if ( result.isOnline() && _presenceFinds.hasPresence() && _presenceFinds.getPresence()->sendRemotePush( accountID, MailboxPushName::NewMail, newMail ) )
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
        _sweepJob.holdRun( run );
    }

    void MailboxService::requestSweep()
    {
        _sweepJob.request();
    }

    void MailboxService::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        _pHost = &host;
        update( nowMs );
    }

    template <typename TMethod>
    void MailboxService::submitCall( OnlineServerCall<TMethod> call, const OnlineCallContext& context, const typename TMethod::Request& request, bool bWithCampaigns )
    {
        if ( _store.hasStore() == false )
        {
            (void)fail( call, OnlineError::kUnavailable ); // 내려가는 중이다
            return;
        }
        MailboxCallInput input;
        input._pPolicy   = _settings._pPolicy;
        input._accountID = context._accountID;
        input._nowMs     = context._nowMs.count();
        if ( bWithCampaigns )
            input._listCampaign = _listCampaign;
        _store.submit( [request, input = std::move( input )]( IServiceStoreConnection& connection )
        {
            ServiceStoreCallResult<MailboxServiceResult, typename TMethod::Reply> done;
            done._serviceResult = MailboxStoreCall::run( TMethod{}, connection, request, input, done._reply );
            return done;
        }, [this, call, accountID = context._accountID]( const ServiceStoreCallResult<MailboxServiceResult, typename TMethod::Reply>& done )
        {
            if constexpr ( std::is_same_v<typename TMethod::Reply, MailboxClaimReply> )
                notifyClaimedBalances( accountID, done._serviceResult, done._reply );
            else
                (void)accountID;                                    // 잔액 알림이 있는 메서드만 쓴다
            (void)finish( call, done._serviceResult, done._reply ); // 모르는 표지는 호출 표가 경고한다
        } );
    }

    void MailboxService::handleCall( OnlineServerCall<MailboxMethod::List> call, const OnlineCallContext& context, const MailboxListRequest& request )
    {
        submitCall( call, context, request, request._cursor.empty() ); // 캠페인은 첫 페이지 앞에 끼운다
    }

    void MailboxService::handleCall( OnlineServerCall<MailboxMethod::MarkRead> call, const OnlineCallContext& context, const MailboxMailRequest& request )
    {
        submitCall( call, context, request, false );
    }

    void MailboxService::handleCall( OnlineServerCall<MailboxMethod::Claim> call, const OnlineCallContext& context, const MailboxMailRequest& request )
    {
        submitCall( call, context, request, false );
    }

    void MailboxService::handleCall( OnlineServerCall<MailboxMethod::ClaimAll> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        submitCall( call, context, request, false );
    }

    void MailboxService::handleCall( OnlineServerCall<MailboxMethod::Delete> call, const OnlineCallContext& context, const MailboxMailRequest& request )
    {
        submitCall( call, context, request, false );
    }

    void MailboxService::completeSweep( const MailboxSweepStats& stats, ServiceStoreCompletion completion )
    {
        _sweepJob.finish();
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
        if ( stats._failedCount > 0 && completion.isCancelled() == false ) // 종료 때문에 취소된 정리는 경고하지 않는다
            SW_LOG_WARNING_ID( "MailboxService.MailSweepLeftExpired", "Mail sweep left %# expired mails for the next pass", stats._failedCount );
        if ( stats._bMore == SW_TRUE )
        {
            _sweepJob.request(); // 상한까지 채웠으므로 다음 틱에 바로 다시 정리한다
            return;
        }
        ScheduledRun run;
        if ( _sweepJob.takeHeldRun( run ) && _settings._pScheduler != nullptr )
            _settings._pScheduler->completeRun( run, stats._failedCount == 0 ); // 남은 것이 있으면 실패로 적어 다음 회차가 다시 본다
    }

    void MailboxService::completeCampaignRefresh( ServiceStoreResult result, vector<ServiceMailCampaign>& inoutListCampaign, ServiceStoreCompletion completion )
    {
        _campaignJob.finish();
        if ( result != ServiceStoreResult::Ok )
        {
            if ( completion.isCancelled() == false ) // 종료 때문에 취소된 읽기는 경고하지 않고 캐시를 그대로 둔다
                SW_LOG_WARNING_ID( "MailboxService.MailCampaignsCouldNot", "Mail campaigns could not be read - keeping the cached list" );
            return;
        }
        _listCampaign.clear();
        for ( ServiceMailCampaign& campaign : inoutListCampaign )
        {
            if ( campaign.isActive( _nowMs.count() ) || campaign._startMs > _nowMs.count() )
                _listCampaign.push_back( std::move( campaign ) );
        }
    }

} // namespace sw

namespace sw
{
    const OnlinePushRelayEntry kMailboxNewMailRelay{ "Mailbox.NewMail", MailboxMethod::NewMail{}, MailboxProtocol::MessageTypes{} };
} // namespace sw
