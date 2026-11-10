/**
 * @file ProfileScopeID.h
 * @brief 계측 지점 하나의 등록 결과(`ProfileScopeID`)입니다. 구간 값을 칸으로 드는 헤더는 `FrameProfiler.h` 대신 이것만 include 합니다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"

#include "Engine/Profiling/ProfilerBackend.h"

namespace sw
{
    /**
     * @struct ProfileScopeID
     * @brief 계측 지점 하나의 등록 결과입니다 — `FrameProfiler` 표의 슬롯과 외부 출력의 지점. 매크로가 static 으로 한 번 받습니다.
     * @details 기본값은 "어디에도 쌓이지 않는 지점" 입니다 — 0 이 기본이면 등록하지 못한 칸이 슬롯 0 의 구간에 섞인다.
     */
    struct ProfileScopeID
    {
        uint32 _slot{ constant::invalid_index::kUint32 }; ///< `FrameProfiler` 슬롯(없으면 `kInvalidSlot`)
#if SW_PROFILER_BACKEND_COMPILED
        const ProfileZoneSite* _pSite{ nullptr }; ///< 외부 출력 지점(프로세스 수명, 없으면 nullptr)
#endif
    };
} // namespace sw
