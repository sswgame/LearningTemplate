// ScopedProgressReport — 간격 안에 끝난 범위는 보고 스레드를 바로 깨워 합류한다(간격만큼 기다리지 않는다). 셰이더 컴파일마다 씌우므로 이것이 깨지면 모든 컴파일이 30 초씩 늘어난다.
#include "pch.h"

#include "Core/Diagnostics/ScopedProgressReport.h"
#include "Core/Testing/TestFramework.h"
#include "Core/Time/MonotonicClock.h"

using namespace sw;

/**
 * @brief [ScopedProgressReportTest] 짧은 범위의 소멸은 간격(30 초)을 기다리지 않고 곧바로 돌아온다
 */
SW_TEST_CASE( ScopedProgressReportTest, ShortScopeEndsWithoutWaitingForTheInterval )
{
    const int64 startNanos = MonotonicClock::nowNanoseconds();
    for ( uint32 index = 0; index < 8u; ++index )
    {
        const ScopedProgressReport progress{ string( "test work" ) };
    }
    const int64 elapsedNanos = MonotonicClock::nowNanoseconds() - startNanos;
    SW_EXPECT_TRUE( elapsedNanos < static_cast<int64>( ScopedProgressReport::kIntervalSeconds ) * constant::time::kNanosecondsPerSecond / 10 );
}
