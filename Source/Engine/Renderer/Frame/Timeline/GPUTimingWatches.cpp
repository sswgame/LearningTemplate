/**
 * @file GPUTimingWatches.cpp
 * @brief GPU 시간의 자동화 감시 값입니다 — `GPU.FrameMs`(프레임의 첫 명령부터 마지막 패스 끝까지), `GPU.PassMs`(key = 패스 이름, 지난 프레임의 그 패스 GPU 시간).
 * @details 시나리오 · 통합 측정만 읽는 값이라 타임라인 TU 가 아니라 이 파일에 둔다(`FrameFogWatches.cpp` 와 같은 모양). 값은 `GPUPassTimeline` 이 프로파일러에 넣은 것이다 —
 *          프로파일러 계측이 꺼져 있으면 타임스탬프를 쓰지 않아 0 이다(`gpu.timings on` 이 켠다).
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Frame/Timeline/GPUTimingRow.h"

namespace sw
{
    namespace
    {
        struct GPUTimingWatchesInternal
        {
            [[nodiscard]] static bool readFrameMilliseconds( const GameObjectManager* pManager, float64& outValue )
            {
                (void)pManager;
                const FrameProfiler* const pProfiler = engine::findSubsystem<FrameProfiler>();
                return pProfiler != nullptr && GPUTimingRow::findMilliseconds( *pProfiler, "Frame", outValue );
            }

            [[nodiscard]] static bool readPassMilliseconds( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const FrameProfiler* const pProfiler = engine::findSubsystem<FrameProfiler>();
                float64                    milliseconds{ 0 };
                if ( pProfiler == nullptr || GPUTimingRow::findMilliseconds( *pProfiler, query._key, milliseconds ) == false )
                    return false;
                outValue.setNumber( milliseconds );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_WATCH( gpuFrameMs, "GPU.FrameMs", "GPU time of the last measured frame in milliseconds (0 while the profiler is off - gpu.timings on)", &GPUTimingWatchesInternal::readFrameMilliseconds );
    SW_AUTOMATION_QUERY_WATCH( gpuPassMs, "GPU.PassMs", "GPU time of the pipeline pass named key in the last measured frame (milliseconds)", &GPUTimingWatchesInternal::readPassMilliseconds, false, true );
} // namespace sw
