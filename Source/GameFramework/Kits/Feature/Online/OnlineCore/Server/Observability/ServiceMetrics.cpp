#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Observability/ServiceMetrics.h"

#include "Core/Foundation/Constants.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"

namespace sw
{
    namespace
    {
        struct ServiceMetricsInternal
        {
            struct ErrorLabel
            {
                uint16      _errorCode;
                const utf8* _pName;
            };

            /** @brief 서비스가 낼 수 있는 공통 범주의 라벨입니다. 키트 결과 라벨과 겹치지 않게 앞에 `error_` 를 붙입니다. 맨 끝은 표에 없는 코드가 드는 자리입니다. */
            static constexpr ErrorLabel kArrErrorLabel[] = {
                {OnlineError::kUnauthenticated,  "error_unauthenticated"},
                {      OnlineError::kForbidden,        "error_forbidden"},
                {    OnlineError::kRateLimited,     "error_rate_limited"},
                { OnlineError::kInvalidRequest,  "error_invalid_request"},
                {    OnlineError::kUnavailable,      "error_unavailable"},
                {           OnlineError::kBusy,             "error_busy"},
                { OnlineError::kUpdateRequired,  "error_update_required"},
                {       OnlineError::kNotFound,        "error_not_found"},
                {       OnlineError::kConflict,         "error_conflict"},
                {OnlineError::kFeatureDisabled, "error_feature_disabled"},
                {       OnlineError::kInternal,         "error_internal"},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    ServiceMetrics::ServiceMetrics()
        : _listRequestCounter{}
        , _listLatency{}
        , _pPending{ nullptr }
        , _pPendingPeak{ nullptr }
        , _pendingPeakCount{ 0 }
        , _methodCount{ 0 }
        , _resultCount{ 0 }
        , _errorResultBase{ constant::invalid_index::kInt32 }
    {
    }

    void ServiceMetrics::appendErrorLabelNames( vector<string>& inoutListName )
    {
        for ( const ServiceMetricsInternal::ErrorLabel& label : ServiceMetricsInternal::kArrErrorLabel )
        {
            inoutListName.push_back( label._pName );
        }
    }

    void ServiceMetrics::observeError( int32 methodIndex, uint16 errorCode, int64 receivedNanoseconds )
    {
        const int32 labelCount = static_cast<int32>( SW_COUNT_OF( ServiceMetricsInternal::kArrErrorLabel ) );
        int32       labelIndex = labelCount - 1; // 표에 없는 코드는 `error_internal` 이다
        for ( int32 index = 0; index < labelCount; ++index )
        {
            if ( ServiceMetricsInternal::kArrErrorLabel[index]._errorCode == errorCode )
                labelIndex = index;
        }
        observeRequest( methodIndex, _errorResultBase >= 0 ? _errorResultBase + labelIndex : constant::invalid_index::kInt32, receivedNanoseconds );
    }

    void ServiceMetrics::initializeWithNames( MetricRegistry* pRegistry, const utf8* pServiceName, const vector<string>& listMethodName, const vector<string>& listResultName )
    {
        _listRequestCounter.clear();
        _listLatency.clear();
        _pPending         = nullptr;
        _pPendingPeak     = nullptr;
        _pendingPeakCount = 0;
        _methodCount      = 0;
        _resultCount      = 0;
        _errorResultBase  = constant::invalid_index::kInt32;
        if ( pRegistry == nullptr )
            return;
        _methodCount                    = static_cast<int32>( listMethodName.size() );
        _resultCount                    = static_cast<int32>( listResultName.size() );
        const vector<float64> listBound = MetricRegistry::makeLatencyBounds();
        vector<MetricLabel>   listLabel( 1 );
        listLabel[0]  = MetricLabel{ "service", pServiceName };
        _pPending     = pRegistry->registerGauge( "service_store_pending", "Store works submitted and not yet completed", listLabel );
        _pPendingPeak = pRegistry->registerGauge( "service_store_pending_peak", "Most store works pending at once since start", listLabel );
        for ( const string& methodName : listMethodName )
        {
            listLabel.resize( 2 );
            listLabel[1] = MetricLabel{ "method", methodName };
            _listLatency.push_back( pRegistry->registerHistogram( "service_request_seconds", "Service request latency from receive to reply", listBound, listLabel ) );
            listLabel.resize( 3 );
            for ( const string& resultName : listResultName )
            {
                listLabel[2] = MetricLabel{ "result", resultName };
                _listRequestCounter.push_back( pRegistry->registerCounter( "service_requests_total", "Service requests by method and result", listLabel ) );
            }
        }
    }

    void ServiceMetrics::observeRequest( int32 methodIndex, int32 resultIndex, int64 receivedNanoseconds )
    {
        if ( methodIndex < 0 || methodIndex >= _methodCount )
            return;
        if ( 0 <= resultIndex && resultIndex < _resultCount )
        {
            MetricCounter* pCounter = _listRequestCounter[static_cast<size_t>( methodIndex * _resultCount + resultIndex )];
            if ( pCounter != nullptr )
                pCounter->add();
        }
        MetricHistogram* pLatency = _listLatency[static_cast<size_t>( methodIndex )];
        if ( pLatency != nullptr ) // 예외 — 지연 지표는 실제 처리 시간을 잰다(가짜 시각으로 재면 뜻이 없다, 프로파일러와 같은 자리)
            pLatency->observe( static_cast<float64>( MonotonicClock::nowNanoseconds() - receivedNanoseconds ) * 1e-9 );
    }

    void ServiceMetrics::setPendingCallCount( int32 pendingCount )
    {
        if ( _pPending != nullptr )
            _pPending->set( static_cast<float64>( pendingCount ) );
        if ( pendingCount <= _pendingPeakCount )
            return;
        _pendingPeakCount = pendingCount;
        if ( _pPendingPeak != nullptr )
            _pPendingPeak->set( static_cast<float64>( pendingCount ) );
    }
} // namespace sw
