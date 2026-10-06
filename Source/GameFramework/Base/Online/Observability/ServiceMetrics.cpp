#include "pch.h"

#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"

#include "Engine/Observability/MetricRegistry.h"

namespace sw
{
    ServiceMetrics::ServiceMetrics()
        : _listRequestCounter{}
        , _listLatency{}
        , _pPending{ nullptr }
        , _methodCount{ 0 }
        , _resultCount{ 0 }
    {
    }

    void ServiceMetrics::initialize( MetricRegistry* pRegistry, const utf8* pServiceName, const utf8* const* arrMethodName, int32 methodCount,
                                     const utf8* const* arrResultName, int32 resultCount )
    {
        _listRequestCounter.clear();
        _listLatency.clear();
        _pPending    = nullptr;
        _methodCount = 0;
        _resultCount = 0;
        if ( pRegistry == nullptr )
            return;
        _methodCount                    = methodCount;
        _resultCount                    = resultCount;
        const vector<float64> listBound = MetricRegistry::makeLatencyBounds();
        vector<MetricLabel>   listLabel( 1 );
        listLabel[0] = MetricLabel{ "service", pServiceName };
        _pPending    = pRegistry->registerGauge( "service_store_pending", "Store works submitted and not yet completed", listLabel );
        for ( int32 methodIndex = 0; methodIndex < methodCount; ++methodIndex )
        {
            listLabel.resize( 2 );
            listLabel[1] = MetricLabel{ "method", arrMethodName[methodIndex] };
            _listLatency.push_back( pRegistry->registerHistogram( "service_request_seconds", "Service request latency from receive to reply", listBound, listLabel ) );
            listLabel.resize( 3 );
            for ( int32 resultIndex = 0; resultIndex < resultCount; ++resultIndex )
            {
                listLabel[2] = MetricLabel{ "result", arrResultName[resultIndex] };
                _listRequestCounter.push_back( pRegistry->registerCounter( "service_requests_total", "Service requests by method and result", listLabel ) );
            }
        }
    }

    void ServiceMetrics::countRequest( int32 methodIndex, int32 resultIndex )
    {
        const bool bInRange = 0 <= methodIndex && methodIndex < _methodCount && 0 <= resultIndex && resultIndex < _resultCount;
        if ( bInRange == false )
            return;
        MetricCounter* pCounter = _listRequestCounter[static_cast<size_t>( methodIndex * _resultCount + resultIndex )];
        if ( pCounter != nullptr )
            pCounter->add();
    }

    MetricHistogram* ServiceMetrics::findLatencyHistogram( int32 methodIndex ) const
    {
        if ( methodIndex < 0 || methodIndex >= _methodCount )
            return nullptr;
        return _listLatency[static_cast<size_t>( methodIndex )];
    }

    void ServiceMetrics::setPendingStoreWorkCount( int32 pendingCount )
    {
        if ( _pPending != nullptr )
            _pPending->set( static_cast<float64>( pendingCount ) );
    }
} // namespace sw
