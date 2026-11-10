#include "pch.h"

#include "GameFramework/Runtime/Online/Schedule/Server/ServiceRetentionJob.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    namespace
    {
        /** @brief 쓸기 한 회차가 완료로 넘기는 값입니다. 커서가 움직인 규칙 사본을 서비스 스레드로 돌려준다. */
        struct ServiceRetentionSweepResult
        {
            vector<ServiceRetentionRule> _listRule{};
            vector<string>               _listFailedTable{}; ///< 쓸지 못한 테이블이다. 완료가 취소가 아닐 때만 오류로 남긴다
            int64                        _deletedCount{ 0 };
            ServiceStoreResult           _result{ ServiceStoreResult::Ok };
        };

        /** @brief 규칙 사본을 저장소 스레드에서 쓸습니다. 한 테이블이 실패해도 다른 테이블은 이어서 쓴다. */
        ServiceRetentionSweepResult sweepRules( IServiceStoreConnection& connection, vector<ServiceRetentionRule> listRule, int64 nowMs, const ServiceRetentionJobConfig& settings )
        {
            ServiceRetentionSweepResult sweep;
            for ( ServiceRetentionRule& rule : listRule )
            {
                ServiceRetentionReport   report;
                const ServiceStoreResult swept = ServiceRetentionRule::sweep( connection, rule, nowMs, settings._maxScanPerRule, settings._maxDeletePerRule, report );
                sweep._deletedCount += report._deletedCount;
                if ( report._deletedCount > 0 )
                    SW_LOG_INFO_ID( "ServiceRetentionJob.RetentionSweptTableDeleted", "Retention swept table '%#': deleted %# of %# scanned", rule._table.c_str(), report._deletedCount, report._scannedCount );
                if ( swept != ServiceStoreResult::Ok )
                {
                    sweep._listFailedTable.push_back( string( rule._table.c_str() ) );
                    sweep._result = swept;
                }
            }
            sweep._listRule = std::move( listRule );
            return sweep;
        }
    } // namespace

    ServiceRetentionJob::ServiceRetentionJob()
        : _listRule{}
        , _settings{}
        , _pScheduler{ nullptr }
        , _lastDeletedCount{ 0 }
        , _sweepJob{}
        , _store{}
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
                SW_LOG_ERROR_ID( "ServiceRetentionJob.ServiceRetentionJobRuleTableNeeds", "ServiceRetentionJob: the rule of table '%#' needs a time reader and a positive retention", rule._table.c_str() );
                return false;
            }
        }
        _store.bindStore( pStore );
        _pScheduler = pScheduler;
        _settings   = settings;
        _listRule   = std::move( listRule );
        ScheduleDef schedule;
        schedule._jobID       = kJobID;
        schedule._kind        = ScheduleKind::Daily;
        schedule._minuteOfDay = settings._minuteOfDay;
        if ( _pScheduler->registerJob( schedule, ScheduledJobFunction::create<&ServiceRetentionJob::onScheduledRun>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "ServiceRetentionJob.ServiceRetentionJobSweepJobCould", "ServiceRetentionJob: the sweep job could not be scheduled (minute of day %#)", settings._minuteOfDay );
            return false;
        }
        return true;
    }

    void ServiceRetentionJob::shutdown()
    {
        _store.flushUntilIdle();
        _store.bindStore( nullptr );
        _pScheduler = nullptr;
        _listRule.clear();
    }

    void ServiceRetentionJob::onScheduledRun( const ScheduledRun& run )
    {
        if ( _store.hasStore() == false || _sweepJob.isRunning() )
            return;
        _sweepJob.holdRun( run );
        if ( _sweepJob.tryStart( run._occurrenceMs ) == false )
            return;
        _store.submit( [listRule = _listRule, nowMs = run._occurrenceMs.count(), settings = _settings]( IServiceStoreConnection& connection )
        { return sweepRules( connection, listRule, nowMs, settings ); },
                       [this]( ServiceRetentionSweepResult& sweep, ServiceStoreCompletion completion )
        {
            if ( completion.isCancelled() == false ) // 종료 때문에 취소된 일은 오류로 남기지 않는다
            {
                for ( const string& table : sweep._listFailedTable )
                {
                    SW_LOG_ERROR_ID( "ServiceRetentionJob.RetentionSweepTableFailed", "Retention sweep of table '%#' failed", table.c_str() );
                }
            }
            onSweepCompleted( std::move( sweep._listRule ), sweep._result, sweep._deletedCount );
        } );
    }

    void ServiceRetentionJob::onSweepCompleted( vector<ServiceRetentionRule> listRule, ServiceStoreResult result, int64 deletedCount )
    {
        _listRule         = std::move( listRule ); // 커서를 이어받는다
        _lastDeletedCount = deletedCount;
        _sweepJob.finish();
        ScheduledRun run;
        if ( _sweepJob.takeHeldRun( run ) && _pScheduler != nullptr )
            _pScheduler->completeRun( run, result == ServiceStoreResult::Ok );
    }
} // namespace sw
