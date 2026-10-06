#include "pch.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"

#include "TestFramework/TestFramework.h"

// 서비스 표준 지표 — 메서드 × 결과 카운터 · 메서드 지연 · 저장소 대기, 범위 밖 번호 무시, 등록부 없는 조립.

using namespace sw;

namespace
{
    struct ServiceMetricsTestInternal
    {
        static constexpr const utf8* kArrMethod[] = { "get", "buy" };
        static constexpr const utf8* kArrResult[] = { "ok", "refused" };
    };
} // namespace

SW_TEST_CASE( ServiceMetricsTest, CountsByMethodAndResult )
{
    MetricRegistry registry;
    ServiceMetrics metrics;
    metrics.initialize( &registry, "economy", ServiceMetricsTestInternal::kArrMethod, 2, ServiceMetricsTestInternal::kArrResult, 2 );
    SW_EXPECT_EQUAL( registry.getSeriesCount(), 2 * 2 + 2 + 1 );
    metrics.countRequest( 1, 1 );
    metrics.countRequest( 1, 1 );
    metrics.countRequest( 5, 0 ); // 범위 밖은 무시
    metrics.countRequest( 0, -1 );
    metrics.setPendingStoreWorkCount( 3 );
    MetricHistogram* pLatency = metrics.findLatencyHistogram( 1 );
    SW_ASSERT_TRUE( pLatency != nullptr );
    pLatency->observe( 0.002 );
    string text;
    registry.writePrometheusText( text );
    SW_EXPECT_TRUE( text.find( "service_requests_total{service=\"economy\",method=\"buy\",result=\"refused\"} 2\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "service_requests_total{service=\"economy\",method=\"get\",result=\"ok\"} 0\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "service_request_seconds_count{service=\"economy\",method=\"buy\"} 1\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "service_store_pending{service=\"economy\"} 3\n" ) != string::npos );
    SW_EXPECT_TRUE( metrics.findLatencyHistogram( 2 ) == nullptr );
}

SW_TEST_CASE( ServiceMetricsTest, NullRegistryCountsNothing )
{
    ServiceMetrics disabled;
    disabled.initialize( nullptr, "x", ServiceMetricsTestInternal::kArrMethod, 2, ServiceMetricsTestInternal::kArrResult, 2 );
    disabled.countRequest( 0, 0 );
    disabled.setPendingStoreWorkCount( 1 );
    SW_EXPECT_TRUE( disabled.findLatencyHistogram( 0 ) == nullptr );
}

SW_TEST_CASE( ServiceMetricsTest, TwoServicesShareOneFamily )
{
    MetricRegistry registry;
    ServiceMetrics economy;
    ServiceMetrics mailbox;
    economy.initialize( &registry, "economy", ServiceMetricsTestInternal::kArrMethod, 2, ServiceMetricsTestInternal::kArrResult, 2 );
    mailbox.initialize( &registry, "mailbox", ServiceMetricsTestInternal::kArrMethod, 1, ServiceMetricsTestInternal::kArrResult, 2 );
    economy.countRequest( 0, 0 );
    mailbox.countRequest( 0, 0 );
    string text;
    registry.writePrometheusText( text );
    const size_t firstType = text.find( "# TYPE service_requests_total counter\n" );
    SW_ASSERT_TRUE( firstType != string::npos );
    SW_EXPECT_TRUE( text.find( "# TYPE service_requests_total counter\n", firstType + 1 ) == string::npos ); // 이름 하나에 HELP · TYPE 한 번
    SW_EXPECT_TRUE( text.find( "service_requests_total{service=\"mailbox\",method=\"get\",result=\"ok\"} 1\n" ) != string::npos );
}
