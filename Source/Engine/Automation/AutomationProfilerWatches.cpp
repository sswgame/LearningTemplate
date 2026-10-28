/**
 * @file AutomationProfilerWatches.cpp
 * @brief 긴 캡처(`ProfilerTraceExport`)의 자동화 감시 값 `Profiler.TraceRecording` 입니다.
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Profiling/ProfilerTraceExport.h"

#if SW_PROFILER_TRACE_ENABLED
namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Profiler.TraceRecording` 입니다. 긴 캡처(`profile.trace record`)가 아직 모으는 중이면 1 입니다. */
        [[nodiscard]] bool readTraceRecording( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            outValue = ProfilerTraceExport::isRecording() ? 1.0 : 0.0;
            return true;
        }
    } // namespace

    // 긴 캡처는 벽시계로 끝난다. 시나리오는 고정 프레임이 아니라 이 감시 값을 `WaitUntil` 로 기다린다(빠른 백엔드는 몇 백 프레임이 0.3 초 안에 돈다).
    SW_AUTOMATION_WATCH( profilerTraceRecording, "Profiler.TraceRecording", "1 while a profile.trace record capture is still collecting", &readTraceRecording );
} // namespace sw
#endif
