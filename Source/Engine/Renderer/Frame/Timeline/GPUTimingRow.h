/**
 * @file GPUTimingRow.h
 * @brief 지난 프레임의 패스별 GPU 시간(프로파일러의 `GPU.<패스>` 구간)을 표 한 장으로 읽습니다 — 개발 명령 `gpu.timings` 의 로그 줄과 감시 값 `GPU.PassMs` · `GPU.FrameMs` 가 쓴다.
 * @details 시간을 쓰는 쪽은 `GPUPassTimeline`(타임스탬프 → 프로파일러)이고, 이 파일은 읽기만 한다. 언리얼 `stat gpu`(패스별 GPU ms 표)와 같은 자리다 — 통합이 측정할 때 같은 값을 로그 · 시나리오로 본다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class FrameProfiler;

    /**
     * @struct GPUTimingRow
     * @brief 패스 하나의 지난 프레임 GPU 시간입니다.
     */
    struct SW_API GPUTimingRow
    {
        /// @brief GPU 구간 이름의 앞머리입니다(`GPUPassTimeline` 이 `GPU.<패스>` 로 등록한다).
        static constexpr const utf8* kScopePrefix = "GPU.";

        string  _name;              ///< 앞머리를 뗀 패스 이름(`Frame` · `Compute` 도 포함)
        float64 _milliseconds{ 0 }; ///< 마지막으로 접힌 프레임의 GPU 시간

        /** @brief @p profiler 의 GPU 구간을 시간이 큰 순서로 모읍니다. 계측이 꺼져 있으면(타임스탬프를 쓰지 않는다) 모두 0 이다. */
        [[nodiscard]] static vector<GPUTimingRow> collect( const FrameProfiler& profiler );
        /** @brief 패스 @p passName(앞머리 없이)의 지난 프레임 GPU 시간입니다. 그 구간이 없으면 false 입니다. */
        [[nodiscard]] static bool findMilliseconds( const FrameProfiler& profiler, string_view passName, float64& outMilliseconds );
    };
} // namespace sw
