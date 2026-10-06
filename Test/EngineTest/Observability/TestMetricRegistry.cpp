#include "pch.h"

#include "Engine/Observability/MetricRegistry.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// 지표 등록부 — 같은 시리즈 한 포인터, 모양이 다른 재등록 거절, 텍스트 형식(HELP · TYPE · 라벨 이스케이프 · 누적 칸 · +Inf), 동시 증가, 구간 타이머.

using namespace sw;

namespace
{
    vector<MetricLabel> makeOneLabel( const utf8* pName, const utf8* pValue )
    {
        vector<MetricLabel> listLabel( 1 );
        listLabel[0] = MetricLabel{ pName, pValue };
        return listLabel;
    }

    void addMany( MetricCounter* pCounter, MetricGauge* pGauge, MetricHistogram* pHistogram )
    {
        for ( int32 index = 0; index < 10000; ++index )
        {
            pCounter->add();
            pGauge->add( 1.0 );
            pHistogram->observe( 0.5 );
        }
    }
} // namespace

SW_TEST_CASE( MetricRegistryTest, SameSeriesReturnsSamePointer )
{
    MetricRegistry registry;
    MetricCounter* pFirst  = registry.registerCounter( "requests_total", "Requests", makeOneLabel( "result", "ok" ) );
    MetricCounter* pSecond = registry.registerCounter( "requests_total", "Requests", makeOneLabel( "result", "ok" ) );
    MetricCounter* pOther  = registry.registerCounter( "requests_total", "Requests", makeOneLabel( "result", "fail" ) );
    SW_ASSERT_TRUE( pFirst != nullptr );
    SW_EXPECT_TRUE( pFirst == pSecond );
    SW_EXPECT_TRUE( pOther != nullptr && pOther != pFirst );
    SW_EXPECT_EQUAL( registry.getSeriesCount(), 2 );
}

SW_TEST_CASE( MetricRegistryTest, MismatchedRegistrationsAreRefused )
{
    MetricRegistry registry;
    SW_ASSERT_TRUE( registry.registerCounter( "a_total", "A" ) != nullptr );
    SW_EXPECT_TRUE( registry.registerGauge( "a_total", "A" ) == nullptr );                             // 종류가 다르다
    SW_EXPECT_TRUE( registry.registerCounter( "a_total", "A", makeOneLabel( "x", "1" ) ) == nullptr ); // 라벨 이름이 다르다
    SW_EXPECT_TRUE( registry.registerCounter( "9bad", "B" ) == nullptr );                              // 이름 규칙
    SW_EXPECT_TRUE( registry.registerCounter( "c_total", "C", makeOneLabel( "__reserved", "1" ) ) == nullptr );
    SW_EXPECT_TRUE( registry.registerCounter( "d_total", "D", makeOneLabel( "le", "1" ) ) == nullptr ); // 히스토그램 칸 라벨
    SW_EXPECT_TRUE( registry.registerHistogram( "h", "H", { 1.0, 0.5 } ) == nullptr );                  // 내림차순
    SW_EXPECT_TRUE( registry.registerHistogram( "h", "H", { 0.5, 0.5 } ) == nullptr );                  // 같은 경계 둘
    SW_ASSERT_TRUE( registry.registerHistogram( "h", "H", { 0.5, 1.0 } ) != nullptr );
    SW_EXPECT_TRUE( registry.registerHistogram( "h", "H", { 0.5, 2.0 } ) == nullptr ); // 같은 이름 · 다른 경계
    SW_EXPECT_EQUAL( registry.getSeriesCount(), 2 );
}

SW_TEST_CASE( MetricRegistryTest, WritesPrometheusTextFormat )
{
    MetricRegistry registry;
    registry.registerCounter( "economy_purchases_total", "Purchases", makeOneLabel( "result", "ok" ) )->add( 3 );
    registry.registerGauge( "connections_open", "Open \"connections\"\nnow" )->set( 2.5 );
    registry.registerCounter( "odd_total", "Odd", makeOneLabel( "value", "a\"b\\c\nd" ) )->add();
    string text;
    registry.writePrometheusText( text );
    SW_EXPECT_TRUE( text.find( "# HELP economy_purchases_total Purchases\n# TYPE economy_purchases_total counter\neconomy_purchases_total{result=\"ok\"} 3\n" ) !=
                    string::npos );
    SW_EXPECT_TRUE( text.find( "# HELP connections_open Open \"connections\"\\nnow\n" ) != string::npos ); // HELP 는 따옴표를 이스케이프하지 않는다
    SW_EXPECT_TRUE( text.find( "# TYPE connections_open gauge\nconnections_open 2.5\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "odd_total{value=\"a\\\"b\\\\c\\nd\"} 1\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "connections_open" ) < text.find( "economy_purchases_total" ) ); // 이름 순
}

SW_TEST_CASE( MetricRegistryTest, HistogramBucketsAreCumulativeWithInf )
{
    MetricRegistry   registry;
    MetricHistogram* pHistogram = registry.registerHistogram( "latency_seconds", "Latency", { 0.25, 1.0 }, makeOneLabel( "method", "buy" ) );
    SW_ASSERT_TRUE( pHistogram != nullptr );
    pHistogram->observe( 0.125 ); // 2 진으로 정확한 값만 — 합 문자열을 그대로 본다
    pHistogram->observe( 0.25 );  // 경계와 같으면 그 칸(le)
    pHistogram->observe( 0.5 );
    pHistogram->observe( 8.0 );
    string text;
    registry.writePrometheusText( text );
    SW_EXPECT_TRUE( text.find( "# TYPE latency_seconds histogram\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "latency_seconds_bucket{method=\"buy\",le=\"0.25\"} 2\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "latency_seconds_bucket{method=\"buy\",le=\"1\"} 3\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "latency_seconds_bucket{method=\"buy\",le=\"+Inf\"} 4\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "latency_seconds_sum{method=\"buy\"} 8.875\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "latency_seconds_count{method=\"buy\"} 4\n" ) != string::npos );
    SW_EXPECT_EQUAL( pHistogram->getCount(), uint64( 4 ) );
}

SW_TEST_CASE( MetricRegistryTest, ConcurrentUpdatesAreNotLost )
{
    MetricRegistry   registry;
    MetricCounter*   pCounter   = registry.registerCounter( "hits_total", "Hits" );
    MetricGauge*     pGauge     = registry.registerGauge( "level", "Level" );
    MetricHistogram* pHistogram = registry.registerHistogram( "spread", "Spread", { 1.0 } );
    std::thread      arrThread[4];
    for ( std::thread& thread : arrThread )
    {
        thread = std::thread( &addMany, pCounter, pGauge, pHistogram );
    }
    for ( std::thread& thread : arrThread )
    {
        thread.join();
    }
    SW_EXPECT_EQUAL( pCounter->getValue(), uint64( 40000 ) );
    SW_EXPECT_EQUAL( pGauge->getValue(), 40000.0 );
    SW_EXPECT_EQUAL( pHistogram->getCount(), uint64( 40000 ) );
    SW_EXPECT_EQUAL( pHistogram->getSum(), 20000.0 );
}

SW_TEST_CASE( MetricRegistryTest, ScopedTimerObservesOnce )
{
    MetricRegistry   registry;
    MetricHistogram* pHistogram = registry.registerHistogram( "scope_seconds", "Scope", MetricRegistry::makeLatencyBounds() );
    SW_ASSERT_TRUE( pHistogram != nullptr );
    {
        ScopedMetricTimer timer( pHistogram );
    }
    {
        ScopedMetricTimer nothing( nullptr ); // 등록부 없는 조립 — 아무것도 하지 않는다
    }
    SW_EXPECT_EQUAL( pHistogram->getCount(), uint64( 1 ) );
    SW_EXPECT_TRUE( pHistogram->getSum() >= 0.0 );
}
