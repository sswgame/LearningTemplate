// FileReadDiagnosticsTest — 디스크 흉내: 꺼져 있으면 읽기가 기다리지 않고, 켜면 읽기마다 적어도 그 지연만큼 걸리며 이어진 읽기가 줄을 선다.
// 아래 한계만 재므로 부하와 무관하다. 호스트가 건 관찰자(`DevToolsHost`)를 파일 계층의 알림으로 부른다 — 관찰자를 바꿔 걸면 다른 스레드의 읽기와 수명이 겹친다(nogpu).
#include "pch.h"

#include "Core/File/PlatformFileUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/TestFramework/ScopedGlobalVariableValue.h"
#include "Core/TestFramework/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Foundation/EngineServices.h"

using namespace sw;

/** @brief [FileReadDiagnosticsTest] 지연을 켜면 이어진 두 읽기는 적어도 지연 둘만큼, 대역폭을 켜면 적어도 바이트 / 대역폭만큼 걸리고, 둘 다 끄면 기다리지 않는다 */
SW_TEST_CASE( FileReadDiagnosticsTest, LatencyAndBandwidthAreLowerBounds )
{
    GlobalVariableManager& variables = engine::getSubsystem<GlobalVariableManager>();
    GlobalVariableInfo*    pLatency  = variables.findVariable( "gv_ioEmulationReadLatencyMs" );
    GlobalVariableInfo*    pRate     = variables.findVariable( "gv_ioEmulationReadKilobytesPerSecond" );
    SW_REQUIRE_NOT_NULL( pLatency );
    SW_REQUIRE_NOT_NULL( pRate );
    constexpr int64                 kNanosecondsPerMillisecond = constant::time::kNanosecondsPerSecond / constant::time::kMillisecondsPerSecond;
    test::ScopedGlobalVariableValue latency{ *pLatency, "2" };
    test::ScopedGlobalVariableValue rate{ *pRate, "0" };
    SW_REQUIRE_TRUE( latency.isApplied() );
    SW_REQUIRE_TRUE( rate.isApplied() );
    int64 start = MonotonicClock::nowNanoseconds();
    PlatformFileUtil::recordDeviceRead( 1 );
    PlatformFileUtil::recordDeviceRead( 1 );
    SW_EXPECT_TRUE( MonotonicClock::nowNanoseconds() - start >= 4 * kNanosecondsPerMillisecond );

    SW_REQUIRE_TRUE( latency.set( "0" ) );
    SW_REQUIRE_TRUE( rate.set( "1024" ) ); // 1 MB/s — 4 KB 는 약 3.9 ms
    start = MonotonicClock::nowNanoseconds();
    PlatformFileUtil::recordDeviceRead( size_t{ 4096 } );
    SW_EXPECT_TRUE( MonotonicClock::nowNanoseconds() - start >= 3 * kNanosecondsPerMillisecond );

    SW_REQUIRE_TRUE( rate.set( "0" ) );
    PlatformFileUtil::recordDeviceRead( size_t{ 1 } << 40 ); // 대역폭을 켰다면 몇 날이 걸릴 크기 — 멈추지 않고 돌아온다
}
