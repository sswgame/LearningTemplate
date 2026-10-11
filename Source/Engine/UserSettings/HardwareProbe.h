/**
 * @file HardwareProbe.h
 * @brief 첫 실행에 그래픽 품질 프리셋을 고르려고 읽는 하드웨어 사양입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief 프리셋 자동 선택(`ScalabilityAutoDetectRule`)이 보는 사양입니다.
     * @details 지금은 CPU 논리 코어 수와 시스템 메모리만 본다. GPU 를 묻는 함수(어댑터 이름, 전용 메모리)는 RHI 에 없어서 빠져 있다.
     *          규칙은 "이 사양이면 이 프리셋에서 시작" 하는 출발점일 뿐이고, 플레이어가 메뉴에서 바꾼다.
     */
    struct HardwareProbeResult
    {
        uint32 _logicalCoreCount{ 0 }; ///< 논리 코어 수(하이퍼스레딩 포함)
        uint32 _systemMemoryMb{ 0 };   ///< 물리 메모리(MB). 못 읽으면 0 입니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 지금 기계의 사양을 읽습니다. */
    struct SW_API HardwareProbe
    {
        /** @brief 코어 수와 메모리를 읽습니다. 읽지 못한 필드는 0 입니다. 수 마이크로초가 걸립니다. */
        static HardwareProbeResult probe();
    };
} // namespace sw
