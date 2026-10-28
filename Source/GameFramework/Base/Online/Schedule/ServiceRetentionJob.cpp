#include "pch.h"

#include "GameFramework/Base/Online/Schedule/ServiceRetentionJob.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/Base/Online/Store/ServiceStoreUtil.h"

namespace sw
{
    SW_LOG_CALLER( "ServiceRetentionJob" );

    /** @brief 규칙 사본을 저장소 스레드에서 쓸고, 커서가 움직인 사본을 서비스 스레드로 돌려줍니다. */
    class ServiceRetentionJob::SweepWork final : public IServiceStoreWork
    {
    public:
        SweepWork( ServiceRetentionJob* pJob, vector<ServiceRetentionRule> listRule, int64 nowMs )
            : _listRule{ std::move( listRule ) }
            , _pJob{ pJob }
            , _nowMs{ nowMs }
            , _deletedCount{ 0 }
            , _result{ ServiceStoreResult::Ok }
        {
        }

        void run( IServiceStoreConnection& connection ) override
        {
            for ( ServiceRetentionRule& rule : _listRule )
            {
                ServiceRetentionReport   report;
                const ServiceStoreResult swept = ServiceRetention::sweep( connection, rule, _nowMs, _pJob->_settings._maxScanPerRule, _pJob->_settings._maxDeletePerRule, report );
                _deletedCount += report._deletedCount;
                if ( report._deletedCount > 0 )
                    SW_LOG_INFO( "Retention swept table '%#': deleted %# of %# scanned", rule._table.c_str(), report._deletedCount, report._scannedCount );
                if ( swept != ServiceStoreResult::Ok )
                {
                    if ( isCancelled() == false ) // 종료 때문에 취소된 일은 오류로 남기지 않는다(취소 표시는 run 앞에 선다)
                        SW_LOG_ERROR( "Retention sweep of table '%#' failed", rule._table.c_str() );
                    _result = swept; // 다른 테이블은 이어서 쓴다
                }
            }
        }

        void complete() override { _pJob->onSweepCompleted( std::move( _listRule ), _result, _deletedCount ); }

    private:
        vector<ServiceRetentionRule> _listRule;
        ServiceRetentionJob*         _pJob;
        int64                        _nowMs;
        int64                        _deletedCount;
        ServiceStoreResult           _result;
    };

    ServiceRetentionJob::ServiceRetentionJob()
        : _listRule{}
        , _settings{}
        , _run{}
        , _pStore{ nullptr }
        , _pScheduler{ nullptr }
        , _lastDeletedCount{ 0 }
        , _bPending{ SW_FALSE }
        , _bRunHeld{ SW_FALSE }
    {
    }

    ServiceRetentionJob::~ServiceRetentionJob()
    {
        shutdown();
    }

    bool ServiceRetentionJob::initialize( IServiceStore* pStore, ServiceScheduler* pScheduler, vector<ServiceRetentionRule> listRule, const ServiceRetentionJobConfig& settings )
    {
        if ( listRule.empty() )
            return true;
        for ( const ServiceRetentionRule& rule : listRule )
        {
            if ( rule._pfnReadTime == nullptr || rule._retentionMs <= 0 )
            {
                SW_LOG_ERROR( "ServiceRetentionJob: the rule of table '%#' needs a time reader and a positive retention", rule._table.c_str() );
                return false;
            }
        }
        _pStore     = pStore;
        _pScheduler = pScheduler;
        _settings   = settings;
        _listRule   = std::move( listRule );
        ScheduleDefinition schedule;
        schedule._jobID       = kJobID;
        schedule._kind        = ScheduleKind::Daily;
        schedule._minuteOfDay = settings._minuteOfDay;
        if ( _pScheduler->registerJob( schedule, ScheduledJobFunction::create<&ServiceRetentionJob::onScheduledRun>( this ) ) == false )
        {
            SW_LOG_ERROR( "ServiceRetentionJob: the sweep job could not be scheduled (minute of day %#)", settings._minuteOfDay );
            return false;
        }
        return true;
    }

    void ServiceRetentionJob::shutdown()
    {
        if ( _pStore != nullptr )
            ServiceStoreUtil::drainUntilIdle( _pStore, [this]()
            { return _bPending == SW_FALSE; } );
        _pStore     = nullptr;
        _pScheduler = nullptr;
        _listRule.clear();
    }

    void ServiceRetentionJob::onScheduledRun( const ScheduledRun& run )
    {
        if ( _pStore == nullptr || _bPending == SW_TRUE )
            return;
        _run      = run;
        _bRunHeld = SW_TRUE;
        _bPending = SW_TRUE;
        _pStore->submit( make_unique<SweepWork>( this, _listRule, run._occurrenceMs.count() ) );
    }

    void ServiceRetentionJob::onSweepCompleted( vector<ServiceRetentionRule> listRule, ServiceStoreResult result, int64 deletedCount )
    {
        _listRule         = std::move( listRule ); // 커서를 이어받는다
        _lastDeletedCount = deletedCount;
        _bPending         = SW_FALSE;
        if ( _bRunHeld == SW_TRUE && _pScheduler != nullptr )
            _pScheduler->completeRun( _run, result == ServiceStoreResult::Ok );
        _bRunHeld = SW_FALSE;
    }
} // namespace sw
