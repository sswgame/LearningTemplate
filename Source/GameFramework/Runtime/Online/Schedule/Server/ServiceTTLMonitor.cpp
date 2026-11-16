#include "pch.h"

#include "GameFramework/Runtime/Online/Schedule/Server/ServiceTTLMonitor.h"

#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    namespace
    {
        /** @brief 쓸기 한 회차가 완료로 넘기는 값입니다. 커서가 움직인 규칙 사본을 서비스 스레드로 돌려준다. */
        struct ServiceTTLSweepResult
        {
            vector<ServiceTTLRule> _listRule{};
            vector<string>         _listFailedTable{}; ///< 쓸지 못한 테이블이다. 완료가 취소가 아닐 때만 오류로 남긴다
            int64                  _deletedCount{ 0 };
            ServiceStoreResult     _result{ ServiceStoreResult::Ok };
        };

        /** @brief 규칙 사본을 저장소 스레드에서 쓸습니다. 한 테이블이 실패해도 다른 테이블은 이어서 쓴다. */
        ServiceTTLSweepResult sweepRules( IServiceStoreConnection& connection, vector<ServiceTTLRule> listRule, int64 nowMs )
        {
            ServiceTTLSweepResult sweep;
            for ( ServiceTTLRule& rule : listRule )
            {
                ServiceTTLSweepReport    report;
                const ServiceStoreResult swept = ServiceTTLRule::sweep( connection, rule, nowMs, rule._maxScanPerSweep, rule._maxDeletePerSweep, report );
                sweep._deletedCount += report._deletedCount;
                if ( report._deletedCount > 0 )
                    SW_LOG_INFO_ID( "ServiceTTLMonitor.TTLSweptTableDeleted", "TTL swept table '%#': deleted %# of %# scanned", rule._table.c_str(), report._deletedCount, report._scannedCount );
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

    ServiceTTLMonitor::ServiceTTLMonitor()
        : _listRule{}
        , _pScheduler{ nullptr }
        , _lastDeletedCount{ 0 }
        , _sweepJob{}
        , _store{}
    {
    }

    ServiceTTLMonitor::~ServiceTTLMonitor()
    {
        shutdown();
    }

    bool ServiceTTLMonitor::initialize( IServiceStore* pStore, ServiceScheduler* pScheduler, vector<ServiceTTLRule> listRule, int32 minuteOfDay )
    {
        if ( listRule.empty() )
            return true;
        for ( const ServiceTTLRule& rule : listRule )
        {
            if ( rule.isSchedulable() == false )
            {
                SW_LOG_ERROR_ID( "ServiceTTLMonitor.ServiceTTLMonitorRuleTableNeeds", "ServiceTTLMonitor: the rule of table '%#' needs a positive expire-after, a time reader or a TTL index, and positive sweep limits", rule._table.c_str() );
                return false;
            }
        }
        _store.bindStore( pStore );
        _pScheduler = pScheduler;
        _listRule   = std::move( listRule );
        ScheduleDef schedule;
        schedule._jobID       = kJobID;
        schedule._kind        = ScheduleKind::Daily;
        schedule._minuteOfDay = minuteOfDay;
        if ( _pScheduler->registerJob( schedule, ScheduledJobFunction::create<&ServiceTTLMonitor::onScheduledRun>( this ) ) == false )
        {
            SW_LOG_ERROR_ID( "ServiceTTLMonitor.ServiceTTLMonitorSweepJobCould", "ServiceTTLMonitor: the sweep job could not be scheduled (minute of day %#)", minuteOfDay );
            return false;
        }
        return true;
    }

    void ServiceTTLMonitor::shutdown()
    {
        _store.flushUntilIdle();
        _store.bindStore( nullptr );
        _pScheduler = nullptr;
        _listRule.clear();
    }

    void ServiceTTLMonitor::onScheduledRun( const ScheduledRun& run )
    {
        if ( _store.hasStore() == false || _sweepJob.isRunning() )
            return;
        _sweepJob.holdRun( run );
        if ( _sweepJob.tryStart( run._occurrenceMs ) == false )
            return;
        _store.submit( [listRule = _listRule, nowMs = run._occurrenceMs.count()]( IServiceStoreConnection& connection )
        { return sweepRules( connection, listRule, nowMs ); },
                       [this]( ServiceTTLSweepResult& sweep, ServiceStoreCompletion completion )
        {
            if ( completion.isCancelled() == false ) // 종료 때문에 취소된 일은 오류로 남기지 않는다
            {
                for ( const string& table : sweep._listFailedTable )
                {
                    SW_LOG_ERROR_ID( "ServiceTTLMonitor.TTLSweepTableFailed", "TTL sweep of table '%#' failed", table.c_str() );
                }
            }
            onSweepCompleted( std::move( sweep._listRule ), sweep._result, sweep._deletedCount );
        } );
    }

    void ServiceTTLMonitor::onSweepCompleted( vector<ServiceTTLRule> listRule, ServiceStoreResult result, int64 deletedCount )
    {
        _listRule         = std::move( listRule ); // 커서를 이어받는다
        _lastDeletedCount = deletedCount;
        _sweepJob.finish();
        ScheduledRun run;
        if ( _sweepJob.takeHeldRun( run ) && _pScheduler != nullptr )
            _pScheduler->completeRun( run, result == ServiceStoreResult::Ok );
    }
} // namespace sw
