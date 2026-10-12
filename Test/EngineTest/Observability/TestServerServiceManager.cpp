#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Observability/MetricRegistry.h"
#include "Engine/Observability/ProcessMetrics.h"
#include "Engine/Observability/ServerServiceManager.h"
#include "Engine/Observability/ServerServiceRegistry.h"

#include "TestFramework/TestFramework.h"

// 서버 서비스 — 레지스트리 순서대로 시작하고 반대로 멈춘다, 실패하면 시작한 것을 되돌린다, 같은 이름은 한 번만 오른다, 프로세스 지표.

using namespace sw;

namespace
{
    /** @brief 테스트 서비스가 남기는 사건 줄입니다. 다른 모듈이 올린 서비스(온라인 서비스 호스트)도 같이 돌므로 `test.` 로 시작하는 줄만 봅니다. */
    struct ServerServiceTestLog
    {
        static vector<string>& get()
        {
            static vector<string> s_listLine;
            return s_listLine;
        }

        static void add( const utf8* pName, const utf8* pEvent )
        {
            string line{ pName };
            line += ':';
            line += pEvent;
            get().push_back( std::move( line ) );
        }

        static void reset() { vector<string>{}.swap( get() ); }
    };

    template <int32 TIndex>
    class RecordingTestService final : public IServerService
    {
    public:
        static constexpr const utf8* kName = TIndex == 0 ? "test.first" : ( TIndex == 1 ? "test.second" : "test.failing" );

        bool initialize( const ServerServiceContext& context, string& outError ) override
        {
            (void)context;
            ServerServiceTestLog::add( kName, "init" );
            if ( TIndex == 2 )
            {
                outError = "test failure";
                return false;
            }
            return true;
        }
        void  shutdown() override { ServerServiceTestLog::add( kName, "shutdown" ); }
        void  tick( int64 nowMs ) override { ServerServiceTestLog::add( kName, nowMs > 0 ? "tick" : "tick0" ); }
        int32 getConnectionCount() const override { return TIndex + 1; }
    };

    using FirstTestService   = RecordingTestService<0>;
    using SecondTestService  = RecordingTestService<1>;
    using FailingTestService = RecordingTestService<2>;
} // namespace

SW_TEST_CASE( ServerServiceManagerTest, StartsInOrderAndStopsInReverse )
{
    ServerServiceTestLog::reset();
    {
        // 올린 순서와 상관없이 시작 순서 값대로 돈다.
        const ServerServiceRegistrar<SecondTestService> second{ SecondTestService::kName, 9001 };
        const ServerServiceRegistrar<FirstTestService>  first{ FirstTestService::kName, 9000 };
        ServerServiceManager                            manager;
        const ServerServiceContext                      context{};
        SW_EXPECT_TRUE( manager.startAll( context ) );
        SW_EXPECT_TRUE( manager.isRunning() );
        SW_EXPECT_TRUE( manager.getConnectionCount() >= 3 ); // 1 + 2(다른 서비스는 연결이 없거나 더한다)
        manager.tick( 5 );
        manager.stopAll();
        SW_EXPECT_FALSE( manager.isRunning() );
        SW_EXPECT_EQUAL( manager.getRunningCount(), 0 );
    }
    const vector<string>& listLine = ServerServiceTestLog::get();
    SW_ASSERT_EQUAL( listLine.size(), size_t{ 6 } );
    SW_EXPECT_EQUAL( listLine[0], string( "test.first:init" ) );
    SW_EXPECT_EQUAL( listLine[1], string( "test.second:init" ) );
    SW_EXPECT_EQUAL( listLine[2], string( "test.first:tick" ) );
    SW_EXPECT_EQUAL( listLine[3], string( "test.second:tick" ) );
    SW_EXPECT_EQUAL( listLine[4], string( "test.second:shutdown" ) );
    SW_EXPECT_EQUAL( listLine[5], string( "test.first:shutdown" ) );
    ServerServiceTestLog::reset();
}

SW_TEST_CASE( ServerServiceManagerTest, FailedStartStopsEveryStartedService )
{
    ServerServiceTestLog::reset();
    {
        const ServerServiceRegistrar<FirstTestService>   first{ FirstTestService::kName, 9000 };
        const ServerServiceRegistrar<FailingTestService> failing{ FailingTestService::kName, 9002 };
        ServerServiceManager                             manager;
        SW_EXPECT_FALSE( manager.startAll( ServerServiceContext{} ) );
        SW_EXPECT_FALSE( manager.isRunning() );
        SW_EXPECT_EQUAL( manager.getRunningCount(), 0 );
    }
    // 실패한 서비스도 shutdown 을 받고, 앞서 시작한 것은 반대 순서로 멈춘다.
    const vector<string>& listLine = ServerServiceTestLog::get();
    SW_ASSERT_EQUAL( listLine.size(), size_t{ 4 } );
    SW_EXPECT_EQUAL( listLine[0], string( "test.first:init" ) );
    SW_EXPECT_EQUAL( listLine[1], string( "test.failing:init" ) );
    SW_EXPECT_EQUAL( listLine[2], string( "test.failing:shutdown" ) );
    SW_EXPECT_EQUAL( listLine[3], string( "test.first:shutdown" ) );
    ServerServiceTestLog::reset();
}

SW_TEST_CASE( ServerServiceManagerTest, RegistryKeepsOneFactoryPerNameAndForgetsUnloadedOnes )
{
    const int32 baseCount = ServerServiceRegistry::getFactoryCount();
    {
        const ServerServiceRegistrar<FirstTestService> first{ FirstTestService::kName, 9000 };
        SW_EXPECT_EQUAL( ServerServiceRegistry::getFactoryCount(), baseCount + 1 );
        // 같은 이름의 두 번째 등록은 거절된다(오류 로그). 두 모듈이 같은 서비스를 올린 실수다.
        const ServerServiceRegistrar<SecondTestService> duplicate{ FirstTestService::kName, 9001 };
        SW_EXPECT_EQUAL( ServerServiceRegistry::getFactoryCount(), baseCount + 1 );
    }
    // 등록기가 사라지면(모듈 언로드) 팩토리도 빠진다.
    SW_EXPECT_EQUAL( ServerServiceRegistry::getFactoryCount(), baseCount );
}

SW_TEST_CASE( ServerServiceManagerTest, HostContextIsVisibleOnlyWhileBound )
{
    // 게임 · 키트 코드가 서버 지표를 받는 길 — 전용 서버가 기동 중에만 묶는다(App · 테스트에서는 null).
    SW_EXPECT_TRUE( ServerServiceRegistry::findHostContext() == nullptr );
    MetricRegistry       registry;
    ServerServiceContext context;
    context._pMetricRegistry = &registry;
    ServerServiceRegistry::bindHostContext( &context );
    const ServerServiceContext* pBound = ServerServiceRegistry::findHostContext();
    SW_ASSERT_NOT_NULL( pBound );
    SW_EXPECT_TRUE( pBound->_pMetricRegistry == &registry );
    ServerServiceRegistry::bindHostContext( nullptr );
    SW_EXPECT_TRUE( ServerServiceRegistry::findHostContext() == nullptr );
}

SW_TEST_CASE( ServerServiceManagerTest, ProcessMetricsReportResidentMemory )
{
    uint64 residentBytes = 0;
    uint64 peakBytes     = 0;
    SW_ASSERT_TRUE( ProcessMetrics::tryGetResidentBytes( residentBytes, peakBytes ) );
    SW_EXPECT_TRUE( residentBytes > 0 );
    SW_EXPECT_TRUE( peakBytes >= residentBytes );

    MetricRegistry registry;
    ProcessMetrics metrics;
    metrics.initialize( &registry, 1700000000123 );
    string text;
    registry.writePrometheusText( text );
    SW_EXPECT_TRUE( text.find( "process_resident_memory_bytes " ) != string::npos );
    SW_EXPECT_TRUE( text.find( "process_resident_memory_max_bytes " ) != string::npos );
    SW_EXPECT_TRUE( text.find( "process_start_time_seconds 1700000000.123" ) != string::npos );
}
