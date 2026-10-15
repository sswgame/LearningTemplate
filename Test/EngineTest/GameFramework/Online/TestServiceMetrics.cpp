#include "pch.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Base/Online/Observability/ServiceMetrics.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"

#include "TestFramework/TestFramework.h"

// 서비스 표준 지표 — 메서드 × 결과 카운터 · 메서드 지연 · 저장소 대기, 범위 밖 번호 무시, 레지스트리 없는 조립.
// 메서드 · 결과 라벨은 열거형의 열거자 이름을 snake_case 로 바꾼 것이다 — 여기서는 ENUM() 이 붙은 아무 열거형(ServiceSanctionKind: chat_mute · suspend · ban)을 둘 다에 쓴다.

using namespace sw;

SW_TEST_CASE( ServiceMetricsTest, CountsByMethodAndResult )
{
    MetricRegistry registry;
    ServiceMetrics metrics;
    metrics.initialize<ServiceSanctionKind, ServiceSanctionKind>( &registry, "economy" );
    SW_EXPECT_EQUAL( registry.getSeriesCount(), 3 * 3 + 3 + 1 );
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
    SW_EXPECT_TRUE( text.find( "service_requests_total{service=\"economy\",method=\"suspend\",result=\"suspend\"} 2\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "service_requests_total{service=\"economy\",method=\"chat_mute\",result=\"chat_mute\"} 0\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "service_request_seconds_count{service=\"economy\",method=\"suspend\"} 1\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "service_store_pending{service=\"economy\"} 3\n" ) != string::npos );
    SW_EXPECT_TRUE( metrics.findLatencyHistogram( 3 ) == nullptr );
}

SW_TEST_CASE( ServiceMetricsTest, NullRegistryCountsNothing )
{
    ServiceMetrics disabled;
    disabled.initialize<ServiceSanctionKind, ServiceSanctionKind>( nullptr, "x" );
    disabled.countRequest( 0, 0 );
    disabled.setPendingStoreWorkCount( 1 );
    SW_EXPECT_TRUE( disabled.findLatencyHistogram( 0 ) == nullptr );
}

SW_TEST_CASE( ServiceMetricsTest, TwoServicesShareOneFamily )
{
    MetricRegistry registry;
    ServiceMetrics economy;
    ServiceMetrics mailbox;
    economy.initialize<ServiceSanctionKind, ServiceSanctionKind>( &registry, "economy" );
    mailbox.initialize<ServiceSanctionKind, ServiceSanctionKind>( &registry, "mailbox" );
    economy.countRequest( 0, 0 );
    mailbox.countRequest( 0, 0 );
    string text;
    registry.writePrometheusText( text );
    const size_t firstType = text.find( "# TYPE service_requests_total counter\n" );
    SW_ASSERT_TRUE( firstType != string::npos );
    SW_EXPECT_TRUE( text.find( "# TYPE service_requests_total counter\n", firstType + 1 ) == string::npos ); // 이름 하나에 HELP · TYPE 한 번
    SW_EXPECT_TRUE( text.find( "service_requests_total{service=\"mailbox\",method=\"chat_mute\",result=\"chat_mute\"} 1\n" ) != string::npos );
}
